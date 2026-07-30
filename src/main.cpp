#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <cocos2d.h>
#include <fmod.hpp>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <fstream>
#include <memory>
#include <algorithm>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

using namespace geode::prelude;

struct Mp3StreamState {
    drmp3 mp3;
    std::vector<uint8_t> fileBuffer;
    std::vector<drmp3_seek_point> seekPoints;
    bool isSeekTableBuilt = false;
    uintmax_t fileSize = 0;
    bool isInitialized = false;

    ~Mp3StreamState() {
        if (isInitialized) {
            drmp3_uninit(&mp3);
        }
    }
};

static std::unordered_map<FMOD::Sound*, std::shared_ptr<Mp3StreamState>> g_activeStreams;
static std::unordered_map<std::string, FMOD::Sound*> g_soundCache;
static std::mutex g_streamMutex;

FMOD_RESULT F_API soundRelease_detour(FMOD::Sound* self) {
    if (self) {
        std::lock_guard<std::mutex> lock(g_streamMutex);
        auto it = g_activeStreams.find(self);
        if (it != g_activeStreams.end()) {
            g_activeStreams.erase(it);
        }

        for (auto cacheIt = g_soundCache.begin(); cacheIt != g_soundCache.end(); ) {
            if (cacheIt->second == self) {
                cacheIt = g_soundCache.erase(cacheIt);
            } else {
                ++cacheIt;
            }
        }
    }

    return self->release();
}

FMOD_RESULT F_CALLBACK pcmreadcallback(FMOD_SOUND* sound, void* data, unsigned int datalen) {
    auto soundObj = reinterpret_cast<FMOD::Sound*>(sound);
    Mp3StreamState* state = nullptr;
    soundObj->getUserData(reinterpret_cast<void**>(&state));

    if (state && state->isInitialized) {
        drmp3_uint64 framesToRead = datalen / (state->mp3.channels * sizeof(int16_t));
        drmp3_uint64 framesRead = drmp3_read_pcm_frames_s16(&state->mp3, framesToRead, reinterpret_cast<int16_t*>(data));

        if (framesRead < framesToRead) {
            size_t bytesRead = static_cast<size_t>(framesRead * state->mp3.channels * sizeof(int16_t));
            std::memset(reinterpret_cast<char*>(data) + bytesRead, 0, datalen - bytesRead);
        }

        return FMOD_OK;
    }
    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_CALLBACK pcmsetposcallback(FMOD_SOUND* sound, int subsound, unsigned int position, FMOD_TIMEUNIT postype) {
    auto soundObj = reinterpret_cast<FMOD::Sound*>(sound);
    Mp3StreamState* state = nullptr;
    soundObj->getUserData(reinterpret_cast<void**>(&state));

    if (state && state->isInitialized) {
        drmp3_uint64 targetFrame = position;
        if (postype & FMOD_TIMEUNIT_MS) {
            targetFrame = static_cast<drmp3_uint64>((static_cast<double>(position) / 1000.0) * state->mp3.sampleRate);
        }

        if (targetFrame > 44100 && !state->isSeekTableBuilt) {
            uintmax_t calculatedCount = (state->fileSize / (1024 * 1024)) * 15;
            drmp3_uint32 seekCount = static_cast<drmp3_uint32>(std::clamp<uintmax_t>(calculatedCount, 30, 1000));
            
            state->seekPoints.resize(seekCount);
            if (drmp3_calculate_seek_points(&state->mp3, &seekCount, state->seekPoints.data())) {
                state->seekPoints.resize(seekCount);
                drmp3_bind_seek_table(&state->mp3, seekCount, state->seekPoints.data());
            }
            state->isSeekTableBuilt = true;
        }

        drmp3_seek_to_pcm_frame(&state->mp3, targetFrame);
        return FMOD_OK;
    }
    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT createMp3StreamOnDemand(FMOD::System* self, const std::string& fullPath, FMOD::Sound** sound) {
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(fullPath, ec);

    if (fileSize < 400 * 1024) {
        return FMOD_ERR_FORMAT;
    }

    auto state = std::make_shared<Mp3StreamState>();
    state->fileSize = fileSize;

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return FMOD_ERR_FILE_NOTFOUND;

    file.seekg(0, std::ios::beg);
    state->fileBuffer.resize(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(state->fileBuffer.data()), fileSize);
    file.close();

    if (drmp3_init_memory(&state->mp3, state->fileBuffer.data(), state->fileBuffer.size(), NULL)) {
        state->isInitialized = true;

        unsigned int estimatedPcmBytes = static_cast<unsigned int>(fileSize * 10);

        FMOD_CREATESOUNDEXINFO exinfo;
        std::memset(&exinfo, 0, sizeof(exinfo));
        exinfo.cbsize = sizeof(exinfo);
        exinfo.defaultfrequency = state->mp3.sampleRate;
        exinfo.numchannels = state->mp3.channels;
        exinfo.format = FMOD_SOUND_FORMAT_PCM16;
        exinfo.length = estimatedPcmBytes;
        
        exinfo.decodebuffersize = 8192; 
        
        exinfo.pcmreadcallback = pcmreadcallback;
        exinfo.pcmsetposcallback = pcmsetposcallback;
        exinfo.userdata = state.get();

        FMOD_RESULT res = self->createStream(
            NULL,
            FMOD_2D | FMOD_OPENUSER | FMOD_CREATESTREAM,
            &exinfo,
            sound
        );

        if (res == FMOD_OK && sound && *sound) {
            (*sound)->setUserData(state.get());

            std::lock_guard<std::mutex> lock(g_streamMutex);
            g_activeStreams[*sound] = state;

            return FMOD_OK;
        }
    }

    return FMOD_ERR_FILE_BAD;
}

FMOD_RESULT F_API createStream_detour(
    FMOD::System* self,
    const char* name_or_data,
    FMOD_MODE mode,
    FMOD_CREATESOUNDEXINFO* exinfo,
    FMOD::Sound** sound
) {
    if (name_or_data && !((uintptr_t)name_or_data < 0x10000)) {
        std::string path = name_or_data;

        if (path.find(".mp3") != std::string::npos || path.find(".MP3") != std::string::npos) {
            std::string fullPath = path;
            if (!std::filesystem::exists(fullPath)) {
                fullPath = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(path.c_str(), false).c_str();
            }

            if (std::filesystem::exists(fullPath)) {
                {
                    std::lock_guard<std::mutex> lock(g_streamMutex);
                    auto it = g_soundCache.find(fullPath);
                    if (it != g_soundCache.end() && it->second) {
                        *sound = it->second;
                        return FMOD_OK;
                    }
                }

                FMOD_RESULT res = createMp3StreamOnDemand(self, fullPath, sound);
                if (res == FMOD_OK && sound && *sound) {
                    std::lock_guard<std::mutex> lock(g_streamMutex);
                    g_soundCache[fullPath] = *sound;
                    return FMOD_OK;
                }
            }
        }
    }

    return self->createStream(name_or_data, mode, exinfo, sound);
}

FMOD_RESULT F_API createSound_detour(
    FMOD::System* self,
    const char* name_or_data,
    FMOD_MODE mode,
    FMOD_CREATESOUNDEXINFO* exinfo,
    FMOD::Sound** sound
) {
    if (name_or_data && !((uintptr_t)name_or_data < 0x10000)) {
        std::string path = name_or_data;

        if (path.find(".mp3") != std::string::npos || path.find(".MP3") != std::string::npos) {
            std::string fullPath = path;
            if (!std::filesystem::exists(fullPath)) {
                fullPath = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(path.c_str(), false).c_str();
            }

            if (std::filesystem::exists(fullPath)) {
                {
                    std::lock_guard<std::mutex> lock(g_streamMutex);
                    auto it = g_soundCache.find(fullPath);
                    if (it != g_soundCache.end() && it->second) {
                        *sound = it->second;
                        return FMOD_OK;
                    }
                }

                FMOD_RESULT res = createMp3StreamOnDemand(self, fullPath, sound);
                if (res == FMOD_OK && sound && *sound) {
                    std::lock_guard<std::mutex> lock(g_streamMutex);
                    g_soundCache[fullPath] = *sound;
                    return FMOD_OK;
                }
            }
        }
    }

    return self->createSound(name_or_data, mode, exinfo, sound);
}

$on_mod(Loaded) {
    log::info("FMODAudioFix loaded");

    (void)Mod::get()->hook(
        reinterpret_cast<void*>(geode::addresser::getNonVirtual(&FMOD::System::createStream)),
        &createStream_detour,
        "FMOD::System::createStream",
        tulip::hook::TulipConvention::Default
    );

    (void)Mod::get()->hook(
        reinterpret_cast<void*>(geode::addresser::getNonVirtual(&FMOD::System::createSound)),
        &createSound_detour,
        "FMOD::System::createSound",
        tulip::hook::TulipConvention::Default
    );

    (void)Mod::get()->hook(
        reinterpret_cast<void*>(geode::addresser::getNonVirtual(&FMOD::Sound::release)),
        &soundRelease_detour,
        "FMOD::Sound::release",
        tulip::hook::TulipConvention::Default
    );
}