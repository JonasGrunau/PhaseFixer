# PhaseFucker

Automatic phase alignment for two tracks that recorded the same sound: kick
in/out, snare top/bottom, close mics against overheads, DI against amp. Put it
on the track you want to fix, feed the reference into its sidechain, press
**ALIGN**.

It works like Voxengo's PHA-979: every frequency of the track is turned by the
same angle with a linear-phase filter, plus an optional polarity flip. Nothing
is moved in time, so the track keeps its transients where they were; only how
it sums with the reference changes. The cost is a fixed latency of about
48 ms, which the host compensates.

VST3 and Audio Unit for macOS (universal), VST3 for Windows, plus a standalone
app for trying it without a DAW. JUCE 8.

## Using it

1. Insert PhaseFucker on the track to fix.
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
  share, the uncorrected track dotted, and the area between them hatched: that
  is what cancels. Drag sideways to turn the phase (Shift for fine), wheel to
  zoom, *HOLD* to freeze the hit.
- **B — Phase / frequency**: the track's phase against the reference per
  1/6 octave, as it arrives (grey) and corrected (vermilion). Dots grow with
  level and fade where a band doesn't add up coherently. Beyond ±90° the two
  cancel. Top right: how much louder the low end sums now than uncorrected.
- **C — Correction**: PHASE (−180…+180°), polarity, A/B, and the time anchor.
- **Time anchor**: how far apart the two tracks arrive. A rotation can't move
  a track in time; when they are far apart, one angle only fits part of the
  spectrum, and this tells you how much more you would get by moving the
  track in the arrangement.
- **Footer**: what happened and what to do next, with the confidence of the
  last analysis.

## How the detection works

`src/core/align/Analyzer.cpp`, on the last 10 s of both signals (mono):

1. **Frames** (Hann, 75 % overlap) only where both play. Frames with a shared
   hit weigh up to 3× more than sustained bleed; levels are evened out so one
   loud bar doesn't decide alone.
2. **Cross-spectrum and coherence** per bin, corrected for the bias a finite
   number of frames gives the coherence.
3. **Rotation**: the summed energy with the track turned by φ is
   Σ(S_aa + S_bb) + 2·Re(e^{jφ}·Σ W·S_ab), largest at φ = −arg Σ W·S_ab. The
   weights favour the coherent part below about 1 kHz (PHA-979's manual asks
   for correlation near 1 there and lets the highs drift) and count each
   octave equally, not each FFT bin. Polarity takes whatever is beyond ±90°.
4. **Confidence** from how well one angle fits (weighted phase spread), the
   coherence, whether ~1 s segments land on the same angle, and how much
   material there was.
5. **Time anchor**: maximum-likelihood GCC (Knapp & Carter) finds how far
   apart the two arrive (to a fraction of a sample), and what moving the track
   would add on top of the rotation.

The audio path (`Rotator`) computes `cos φ · x − sin φ · H{x}` with `H` a
linear-phase FIR Hilbert transformer (Kaiser-windowed, ~86 ms long, flat
within 0.1 dB from 20 Hz to Nyquist − 20 Hz). It runs as a partitioned FFT
convolution in which a stereo pair shares one complex FFT. Angle and polarity
glide over 20 ms, so nothing clicks.

## Building

Requirements: macOS 11+, CMake 3.24+, Ninja, Xcode command line tools.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This installs the VST3 and AU into `~/Library/Audio/Plug-Ins` and builds the
standalone app. JUCE 8 is
downloaded automatically; pass `-DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE`
to use a local checkout.

### Core only

Analysis and DSP don't depend on JUCE and build in seconds:

```sh
cmake -B build-core -G Ninja -DCMAKE_BUILD_TYPE=Release -DPHASEFUCKER_BUILD_PLUGIN=OFF
cmake --build build-core
./build-core/phasefucker_tests                      # rotation, Hilbert filter, detection, capture
./build-core/phasefucker_align ref.wav track.wav    # the rotation the plugin would apply to two files
```

### UI snapshots

`build/phasefucker_snapshot_artefacts/Release/phasefucker_snapshot <dir>`
renders the editor to PNGs (empty, idle, listening, aligned, offset, compare,
no sidechain) with synthetic drums, no DAW needed. `... <dir> bench` times
scope and editor paints instead.

`./start.sh` builds and opens the standalone app (inputs 1-2 track, 3-4 reference).

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
src/core/dsp       FFT, Hilbert FIR design
src/core/align     capture, cross-spectrum, analyzer, real-time rotator, live spectrum
src/plugin         JUCE processor and editor
src/ui             theme, controls, scope, phase strip
tests/             Catch2 tests for the core
tools/             WAV aligner, offscreen UI snapshot tool
packaging/         macOS pkg and Windows installer scripts
```

## License

The plugin links JUCE 8 under the AGPLv3. IBM Plex fonts under the SIL Open
Font License (`resources/fonts/IBMPlex-OFL.txt`).
