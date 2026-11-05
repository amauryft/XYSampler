# XYSampler

One-shot sample player (AU / VST3) by Mau Audio. One XY pad shapes every hit:

- **X: Decay.** An exponential fade over the sample, from 20 ms up to 2 s. The far right edge
  lets the sample play to its natural end.
- **Y: Pitch.** The centre is 0 st. The pad edges are ±Y SENS semitones (2 to 24).
- **X SENS / Y SENS.** How much the pad changes per movement. X narrows the decay span around
  the middle, and Y sets the pitch range.
- **Drive.** tanh saturation with anti-aliasing, gain-matched so it gets denser rather than louder.
- **Motion.** Moves the point the next hit gets around your set point. Patterns: Circle,
  Figure-8, Decay only, Pitch only, Corners, Drift. **Depth** sets how far it moves, and **Rate**
  sets one cycle per 1 bar, 1/2, 1/4 or 1/8, synced to the host tempo. With the **Time** clock it
  moves with time and is locked to the song position while playing, so loops repeat exactly. With
  the **Step** clock each hit advances it 1/8 of a cycle, and it restarts when the song starts.

Values are taken once per hit, so a ringing hit keeps its pitch and decay. Signal flow per hit:
`sample (resampled for pitch) → drive → decay → sum → level`.

## Using it

- **Load a sample:** click the waveform strip, or drop a file on the window. The audio is stored
  inside the session, so moving the file later doesn't break it.
- **Trigger:** any MIDI note (velocity sets the level), the HIT button, or a click on the pad.
- **Pad:** drag to set. Hold Shift for fine moves. Double-click to reset.
- **Knobs:** drag up or down. Hold Shift for fine moves. Double-click or Option-click to reset.
- **Themes:** the three dots, top right (Dark, Violet, Light). The theme and window size are saved with the session.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

Every push to `main` builds a universal, ad-hoc-signed installer on GitHub Actions. The build
runs Apple's `auval` and publishes the installer as a pre-release on the Releases page.

## Layout

| Path | What |
|---|---|
| `Source/HitCore.h` | DSP engine, motion patterns, parameter mappings (no JUCE) |
| `Source/PluginProcessor.*` | Parameters, MIDI, transport, sample loading, state |
| `Source/PluginEditor.*` | WebView host and native functions for the UI |
| `ui/index.html` | The whole UI. Open it in a browser for a preview mode with no audio |
| `tests/core_test.cpp` | Engine tests: `c++ -std=c++17 -O2 -I Source tests/core_test.cpp -o t && ./t` |
