import miniaudio
import soundcard as sc
import numpy as np
import matplotlib.pyplot as plt
import os
import ctypes
import time

FILE_PATH = os.path.expandvars(r"%LOCALAPPDATA%\GeometryDash\905108.mp3")
SONG_NAME = "Creo - Crazy"
DURATION = 90
FS = 44100

if not os.path.exists(FILE_PATH):
    print(f"File not found: {FILE_PATH}")
    exit(1)

def wait_for_r_key(prompt_message):
    print("--------------------------------------------------")
    print(prompt_message)
    print(" Press 'R' inside Geometry Dash to start recording...")
    print("--------------------------------------------------")
    # Clear key buffer state
    ctypes.windll.user32.GetAsyncKeyState(0x52)
    while True:
        if ctypes.windll.user32.GetAsyncKeyState(0x52) & 0x8000:
            print("\n>>> 'R' KEY PRESSED! Recording 90 seconds of audio...")
            break
        time.sleep(0.02)
    time.sleep(0.5)  # Prevent accidental double triggering

# Bit-Perfect 0 dBFS normalization
def normalize_to_0db(signal):
    peak = np.percentile(np.abs(signal), 99.99)
    if peak > 0:
        signal = np.clip(signal, -peak, peak)
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal = signal / max_val
    return signal

print(f"1. Decoding original MP3 file ({SONG_NAME})...")
decoded = miniaudio.decode_file(FILE_PATH)
file_samples = np.frombuffer(decoded.samples, dtype=np.int16).astype(np.float32) / 32768.0
if decoded.nchannels == 2:
    file_samples = file_samples[::2]
file_samples = file_samples[:FS * DURATION]

speaker = sc.default_speaker()
mic = sc.get_microphone(id=str(speaker.name), include_loopback=True)

wait_for_r_key("STEP 1: Launch GD WITH MOD ENABLED, enter the level.")
rec_data_mod = mic.record(numframes=FS * DURATION, samplerate=FS)
gd_mod_samples = rec_data_mod[:, 0]
print(">>> Recording 1 (WITH MOD) completed!\n")

wait_for_r_key("STEP 2: Launch GD WITHOUT MOD (or disable mod), enter the level.")
rec_data_nomod = mic.record(numframes=FS * DURATION, samplerate=FS)
gd_nomod_samples = rec_data_nomod[:, 0]
print(">>> Recording 2 (WITHOUT MOD) completed!\n")

print("Processing audio signals and generating 3-way spectrogram...")

np.seterr(divide='ignore')

file_samples = normalize_to_0db(file_samples)
gd_mod_samples = normalize_to_0db(gd_mod_samples)
gd_nomod_samples = normalize_to_0db(gd_nomod_samples)

fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 12), sharex=True, sharey=True)

VMIN = -80
VMAX = 0

im1 = ax1.specgram(file_samples, Fs=FS, NFFT=2048, noverlap=1024, cmap='magma', vmin=VMIN, vmax=VMAX)[3]
ax1.axhline(y=16000, color='cyan', linestyle='--', linewidth=1.5, label='16 kHz Line')
ax1.set_title(f"1. ORIGINAL FILE ({SONG_NAME})", fontsize=11, fontweight='bold')
ax1.set_ylabel("Frequency (Hz)", fontsize=10)
ax1.set_ylim(0, 22050)
ax1.legend(loc='upper right')

im2 = ax2.specgram(gd_mod_samples, Fs=FS, NFFT=2048, noverlap=1024, cmap='magma', vmin=VMIN, vmax=VMAX)[3]
ax2.axhline(y=16000, color='cyan', linestyle='--', linewidth=1.5, label='16 kHz Line')
ax2.set_title(f"2. GEOMETRY DASH WITH MOD ({SONG_NAME})", fontsize=11, fontweight='bold')
ax2.set_ylabel("Frequency (Hz)", fontsize=10)
ax2.legend(loc='upper right')

im3 = ax3.specgram(gd_nomod_samples, Fs=FS, NFFT=2048, noverlap=1024, cmap='magma', vmin=VMIN, vmax=VMAX)[3]
ax3.axhline(y=16000, color='cyan', linestyle='--', linewidth=1.5, label='16 kHz Line')
ax3.set_title(f"3. GEOMETRY DASH WITHOUT MOD ({SONG_NAME})", fontsize=11, fontweight='bold')
ax3.set_xlabel("Time (seconds)", fontsize=10)
ax3.set_ylabel("Frequency (Hz)", fontsize=10)
ax3.legend(loc='upper right')

cbar = fig.colorbar(im1, ax=[ax1, ax2, ax3], orientation='vertical', pad=0.02)
cbar.set_label('Volume (dB)', fontsize=11)

output_file = "3_way_spectrogram_comparison.png"
plt.savefig(output_file, dpi=300, bbox_inches='tight')

print("--------------------------------------------------")
print(f" DONE! Saved 3-way comparison to: {output_file}")
print("--------------------------------------------------")