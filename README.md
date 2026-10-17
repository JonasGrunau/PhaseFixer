# PhaseFucker

Automatic phase alignment for two tracks that recorded the same sound: kick
in/out, snare top/bottom, close mics against overheads, DI against amp. Put it
on the track you want to move, feed the reference into its sidechain, press
**ALIGN**. It shifts the track in time (to a fraction of a sample), flips the
polarity if that sums better, and adds an allpass when part of the spectrum is
turned against the rest. Everything it decides lands on normal knobs, so you
can check it on the scope and change it.

VST3 and Audio Unit for macOS (universal), VST3 for Windows. JUCE 8.

## Using it

1. Insert PhaseFucker on the track that should move (usually the later or the
   less important mic).
2. Route the reference track into the plugin's sidechain:
   - **Logic**: *Side Chain* menu at the top of the plugin window.
   - **Ableton Live**: open the device's *Sidechain* section and pick the track.
   - **Reaper**: route the reference to channels 3/4 of this track.
   - **Cubase / Nuendo**: activate side-chain on the plugin, then send the reference to it.
3. Play a part where both sound and press **ALIGN**. If it has already heard
   about 2 s of both together in the last 10 s, it analyses at once; otherwise
   it listens until it has (press again to use what it has).
4. Check the scope and the phase strip. **A/B** plays the track uncorrected at
   the same latency, so the comparison is fair.

### What you see

- **A — Scope**: reference (ink) and track (vermilion) around the last hit they
  share, the uncorrected track dotted, and the area between reference and track
  hatched: that is what cancels. Drag sideways to nudge the shift (Shift for
  fine), wheel to zoom, *HOLD* to freeze the hit.
- **B — Phase / frequency**: the track's phase against the reference per
  1/6 octave, as it arrives (grey) and corrected (vermilion). Dots grow with
  level and fade where a band doesn't add up coherently. Beyond ±90° the two
  cancel. Top right: how much louder the low end sums now than uncorrected.
- **C — Correction**: shift (±50 ms), polarity, allpass (frequency, Q), A/B,
  lookahead.
- **Footer**: what happened and what to do next, with the confidence of the
  last analysis.

### Lookahead and latency

Moving a track *earlier* needs lookahead. The plugin reports it as latency and
the host compensates. *Auto* (default) picks the smallest of 0 / 5 / 10 / 20 /
50 ms that covers the current shift, 300 ms after the shift stops moving. Fix it
to a value if your host dislikes latency changes. A 32-sample interpolator
latency is always there.

## How the detection works

`src/core/align/Analyzer.cpp`, on the last 10 s of both signals (mono):

1. **Frames** (Hann, 4× the search range long, 75 % overlap) only where both
   play. Frames with a shared hit weigh up to 3× more than sustained bleed;
   levels are evened out so one loud bar doesn't decide alone.
2. **Maximum-likelihood GCC** (Knapp & Carter): cross-spectrum weighted by
   γ² / (1 − γ²) / |S_ab| with bias-corrected coherence γ², 25 Hz–16 kHz. Gives
   sharp peaks even when the two mics sound very different.
3. **Candidates**: the five strongest peaks within ±50 ms, each refined to a
   fraction of a sample, then scored by how loud reference + shifted track
   actually sum, weighted towards 60 Hz–2 kHz where cancellation is heard.
   This picks the arrival that sums best instead of a reflection, and sets
   the polarity.
4. **Second pass** with the track frames pre-shifted by the winner (long delays
   don't lose coherence to the window) for the final position (~0.01 sample),
   coherence and confidence (peak sharpness, coherence, how well the corrected
   pair adds up, and whether ~1 s segments agree on their own).
5. **Allpass fit**: the correlation through a grid of 2nd-order allpasses
   (40 frequencies × 5 Qs, one inverse FFT each), the best polished by
   coordinate search with the shift re-fitted. Kept only if it raises the
   summed level by at least 0.3 dB.
6. Plain-language failures: not enough shared signal, silent sidechain,
   unrelated signals, or a delay beyond the range.

The audio path (`Aligner`) is a 64-tap Kaiser-windowed sinc delay (exact at
whole samples), polarity and an RBJ allpass. Changes crossfade over 20 ms, so
nothing clicks.

## Building

Requirements: macOS 11+, CMake 3.24+, Ninja, Xcode command line tools.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This installs the VST3 and AU into `~/Library/Audio/Plug-Ins`. JUCE 8 is
downloaded automatically; pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`
to use a local checkout.

### Core only

Analysis and DSP don't depend on JUCE and build in seconds:

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPHASEFUCKER_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/phasefucker_tests                      # detection, delay line, allpass, capture
./build-core/phasefucker_align ref.wav track.wav    # what the plugin would do with two files
```

### UI snapshots

`build/phasefucker_snapshot_artefacts/Release/phasefucker_snapshot <dir>`
renders the editor to PNGs (empty, idle, listening, aligned, rotate, compare,
no sidechain) with synthetic drums, no DAW needed.

### Releases

Push a version tag and GitHub Actions builds, tests and validates the plugins
and attaches the installers to a new release:

```sh
git tag v1.2.0 && git push origin v1.2.0
```

You get a macOS `.pkg` (universal, VST3 + AU), a Windows setup `.exe` (VST3)
and a zip of the bare plug-ins for each. A suffix such as `v1.2.0-beta.1` makes
it a pre-release. The installer scripts are in `packaging/`.

The macOS build is signed with a Developer ID and notarized when these
repository secrets are set (Settings > Secrets and variables > Actions):

| Secret | Value |
|--------|-------|
| `MACOS_CERTIFICATES_P12` | base64 of a `.p12` holding both the *Developer ID Application* and the *Developer ID Installer* certificate with their keys |
| `MACOS_CERTIFICATES_PASSWORD` | the `.p12` password |
| `NOTARY_APPLE_ID` | Apple ID of the developer account |
| `NOTARY_PASSWORD` | an app-specific password for that Apple ID |

Without them the macOS build is unsigned and macOS blocks the `.pkg` the first
time (System Settings > Privacy & Security > Open Anyway). The Windows
installer is not signed, so SmartScreen asks once (More info > Run anyway).

## Project layout

```
src/core/dsp       FFT, sinc delay line, allpass
src/core/align     capture, cross-spectrum, analyzer, real-time aligner, live spectrum
src/plugin         JUCE processor and editor
src/ui             theme, controls, scope, phase strip
tests/             Catch2 tests for the core
tools/             WAV aligner, offscreen UI snapshot tool
packaging/         macOS pkg and Windows installer scripts
```

## License

The plugin links JUCE 8 under the AGPLv3. IBM Plex fonts under the SIL Open
Font License (`resources/fonts/IBMPlex-OFL.txt`).
