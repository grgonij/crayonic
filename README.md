# crayonic

A playful one-shot instrument plugin (VST3 / AU / Standalone) with a hand-drawn, crayon-on-paper look. Load up to four sounds; every note picks one of them at random, and each sound has its own pitch, envelope, loop, arpeggiator and effects.

## Features

- **4 sound slots**: drag in any WAV, AIFF, FLAC or OGG; each note picks a sound at random (never the same one twice in a row)
- **Per sound**: volume, pitch, hi/lo pass filters, pan and stereo separation, start position with random start, time-stretch, ADSR envelope, glide and legato, looping, and an arpeggiator
- **Volume shaping**: draw a volume curve straight onto the waveform, like an FL Studio automation clip
- **Layers**: link sounds so picking one also plays another
- **Effects per sound**: distortion, granular, delay (tempo-synced) and reverb
- **Drag to copy**: drag a knob's name, a tab or an effect card onto another sound to copy its settings
- **Presets and kits**: save presets, roll the dice for a random one, and share kits (presets plus all their sounds) as a single zip

## Download

Builds for Windows and macOS (Apple Silicon + Intel) are produced automatically by GitHub Actions on every push: open the **Actions** tab, pick the latest run, and download **crayonic-Windows** or **crayonic-macOS**.

- **Windows**: copy `crayonic.vst3` to `C:\Program Files\Common Files\VST3`
- **macOS**: unzip, open Terminal, type `bash ` and drag `install.command` into the window, then press Enter (the plugin isn't signed by Apple yet, so this step is needed)

Then rescan plugins in your DAW. In FL Studio, crayonic appears under **Generators**. The included *crayonic Quick Start Guide.pdf* explains every feature.

## Building from source

Requirements: CMake 3.22+, and Visual Studio 2026 (Windows) or Xcode (macOS).

```bash
git clone --recurse-submodules <this repository>
cd crayonic
cmake -B build
cmake --build build --config Release
```

The plugin ends up in `build/MyPlugin_artefacts/Release/`.

## Built with

- [JUCE](https://juce.com) (see JUCE's license for distribution terms)
- [Patrick Hand](https://fonts.google.com/specimen/Patrick+Hand) font, SIL Open Font License (`Assets/OFL.txt`)
