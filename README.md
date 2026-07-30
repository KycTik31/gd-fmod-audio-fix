# GD FMOD Audio Fix

A Geode mod for Geometry Dash 2.2 that bypasses FMOD's ~16kHz audio cutoff on MP3 tracks.

## Disclaimer
I don't write C++ and vibe-coded this mod using AI. However, I tested it a little bit (spectral analysis, memory leaks, song triggers, and long levels like Eon) to make sure it works as intended (and it does as far as I can tell).

Mod currently available only for Windows

## Installation
1. [Download mod](https://github.com/KycTik31/gd-fmod-audio-fix/releases/latest/download/kyctik.fmod-audio-fix.geode)
2. Open Geometry Dash
3. Click the Geode button
4. In the bottom left, click the settings button
5. Click `Install From File`
6. You are good to go!

## What it does
FMOD's default MP3 decoder in Geometry Dash applies a lowpass synthesis filter around 16kHz to save CPU, muffling high frequencies compared to standard media players.

This mod hooks `FMOD::System::createStream`. When GD loads an MP3, the mod decodes the frames on-the-fly into raw 16-bit PCM using `dr_mp3` and passes the PCM stream back to FMOD. Because FMOD receives raw PCM instead of an MP3 stream, its internal MP3 lowpass filter is bypassed, restoring full audio up to 20kHz.

## Screenshot
![Spectrogram Comparison](images/3_way_spectrogram_comparison.png)
*(The white lines on the last spectrogram are due to my laggy PC)*

You can reproduce this image using `analyze_gd.py` in the `tools` folder. First, install the required libraries via:
```
pip install miniaudio soundcard numpy matplotlib
```

## Credits
- dr_mp3 by David Reid (Mackron).
- Original 16kHz bug research on r/geometrydash (/u/IOMAN_IM).
