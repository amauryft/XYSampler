# HitPad

One-shot sample player (AU / VST3 / Standalone) with a single XY pad:

- **X: Decay.** Exponential "VCA" decay laid over the sample. Far left is 20 ms to -60 dB,
  log-spaced up to 2 s at 97% of the way across. The last 3% opens up to *no decay*,
  so the sample plays to its natural end.
- **Y: Saturation.** tanh drive from -18 dB (clean) up to +24 dB (crushed). The output is
  normalised by `1/tanh(drive)`, so turning it up makes the sound denser without making it
  much louder. Uses antiderivative anti-aliasing (ADAA), so heavy drive doesn't fold
  harsh aliasing back down.

Signal flow per hit: `sample → saturation → decay VCA → sum → level`.
Because saturation sits *before* the decay, the tail stays as saturated as the attack and
the decay time stays exactly what the readout says. To flip the order, swap the
saturator and VCA lines in `Engine::render` in `Source/HitCore.h`.

## Using it

- **Load:** drop an audio file anywhere on the window, or click the waveform strip.
  WAV/AIFF/FLAC/OGG work everywhere; MP3/M4A also work on macOS. Samples are capped at 30 s.
- **Trigger:** any MIDI note (velocity sets the level; note-offs are ignored, drum-machine
  style), the **HIT** button, or clicking the pad.
- **Pad:** drag to set decay and saturation. Hold Shift for fine moves. Double-click resets.
- **Waveform strip:** grey is the raw sample; orange is what you'll hear with the current decay.
- Up to 6 overlapping hits. A seventh hit steals the oldest voice with a 3 ms fade.
- Session recall stores the sample's **file path**. If you move the file, reload it.

## Build

Needs CMake ≥ 3.22 and Xcode command-line tools. JUCE 8.0.9 is fetched automatically.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

With the default `COPY_PLUGIN_AFTER_BUILD=ON`, the AU and VST3 are copied straight into
`~/Library/Audio/Plug-Ins/`.

**GitHub route** (same as SuperSpread): push this folder to a private repo. The
`build-mac` Action builds a universal (Apple Silicon + Intel), ad-hoc-signed
`HitPad-mac.zip` as a downloadable artifact. Install steps are in `INSTALL-mac.txt`.

## Tests

The DSP lives in `Source/HitCore.h` with no JUCE dependency:

```sh
c++ -std=c++17 -O2 -I Source tests/core_test.cpp -o core_test && ./core_test
```

The tests check decay timing against the -60 dB target, the open end, that full scale is
never exceeded at any drive, that Y=0 is transparent, harmonic growth with Y, sample-rate
conversion, voice stealing and velocity.

## Files

| File | What |
|---|---|
| `Source/HitCore.h` | Engine, voices, saturator, parameter mappings (shared with the UI) |
| `Source/PluginProcessor.*` | JUCE wrapper: params, MIDI, sample loading, state |
| `Source/XYPad.h` | The XY pad component |
| `Source/PluginEditor.*` | Window, waveform strip, readouts, level, HIT |

## Natural next steps

- Multiple pads (drum-machine grid), each with its own sample and XY position
- Choke groups, pitch, and a start-offset per pad
- Embed the sample in the session instead of storing its path
