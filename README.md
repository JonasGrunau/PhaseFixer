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

### Per hit

When one angle can't fit every hit, like a bass that changes notes under the
kick, switch **TURN** to **PER HIT** (plugin on the bass, kick on the
sidechain). Loop the part, press play, then **ALIGN**: every hit of the
reference gets its own turn, placed on the DAW's timeline (the *phase map*,
saved with the session). The turn holds while a hit rings and glides to the
next one in the second half of the gap (20–250 ms, landing 5 ms before the
next hit), so a sustained note hardly bends. A hit that would gain less than
0.3 dB by a turn of its own keeps the one before: no move for nothing.

Section B then shows the map: one point per hit at its turn, the turn over time
as a line, bars numbered as in the DAW. Drag a point up or down to change it
(Shift for fine), double-click to go back to what ALIGN found; edited points
are hollow and stay when you ALIGN again. ALIGN in another part of the song
adds that part; *CLEAR* empties the map. The PHASE dial becomes an *OFFSET*
added to the whole map. A loop heard more than once within the 10 s is merged
hit by hit.

### Export

**REC PASS** (section C, top right) records the corrected track for one pass:
it starts when playback starts or the loop comes round, and stops when the loop
comes round again or playback stops (90 s at most). The pass is rendered
offline through the same filter, so the file is exactly what you heard, minus
the latency; a loop pass is rendered as a loop, its end running into its
start. Then drag **DRAG WAV** into the arrangement at the bar the footer names.
The 24-bit WAVs are kept in `~/Music/PhaseFucker`.

### What you see

- **A — Scope**: reference (ink) and track (vermilion) over each other, the
  uncorrected track dotted, and the area between them hatched: that is what
  cancels. *TRIGGER* sets where the window starts: **BAR** (default) or
  **BEAT** of the DAW's timeline, so the picture stands still while the song
  plays, or **HIT**, the strongest transient both tracks share. Without a
  running transport BAR/BEAT show hits and say so. *BARS* sets how much it
  shows, ¼ to 4 bars at the song's tempo; the default ½ bar shows two kicks and
  what the bass does between them. Several bars step by that many from bar 1
  (1–2, 3–4, …). The wheel zooms freely (2 ms – 8 s), double-click goes back to
  the bar length. The height is set by the bar length and stays put while
  you zoom or turn the phase. Drag sideways over the plot to turn the phase (Shift for
  fine), *HOLD* freezes the picture. Trigger, bars and zoom are saved with the
  session.
- **B — Phase / frequency**: the track's phase against the reference per
  1/6 octave, as it arrives (grey) and corrected (vermilion). Dots grow with
  level and fade where a band doesn't add up coherently. Beyond ±90° the two
  cancel. Top right: how much louder the low end sums now than uncorrected.
- **B — Phase map** (per hit, instead of the phase strip): see above.
- **C — Correction**: TURN (one for all, or per hit), PHASE (−180…+180°; per
  hit the offset on top of the map), polarity, A/B, the time anchor, and
  export.
- **Time anchor**: how far apart the two tracks arrive. A rotation can't move
  a track in time; when they are far apart, one angle only fits part of the
  spectrum, and this tells you how much more you would get by moving the
  track in the arrangement.
- **Footer**: what happened and what to do next, with the confidence of the
  last analysis.

## How the detection works

`src/core/align/Analyzer.cpp`, on the last 10 s of both signals (mono):

1. **Hits**: onsets of the reference below ~400 Hz (a kick, a bass note, a
   snare's body). Each hit is judged from just before it to the next one (at
   most 0.6 s), through the same Hilbert filter the plugin plays with, so a
   turn found here is the turn you hear, in 10 ms pieces: the level of the
   sum's envelope in dB, each piece weighted by how much the two play
   together there. A psytrance kick and its K-B-B-B bass hardly overlap; this
   way only where they meet (the kick's tail into the first bass note)
   decides, and the kick alone costs nothing. What is listened to: the low
   end below ~150 Hz (weight), 150–500 Hz over the first 45 ms (punch, half
   as much), and a little of 500 Hz–2 kHz. That gives a turn per hit (the
   phase map) and one turn for all.
2. **Material without hits** (pads, sustained parts): frames (Hann, 75 %
   overlap) only where both play, the cross-spectrum S_ab, and the angle
   φ = −arg Σ W·S_ab that maximises the summed energy, weighted towards the
   coherent part below about 1 kHz (PHA-979's manual asks for correlation near
   1 there and lets the highs drift). Polarity takes whatever is beyond ±90°.
3. **Coherence** per bin from the same frames, corrected for the bias a
   finite number of frames gives it.
4. **Confidence** from how well one angle fits, the coherence (or, for hits of
   different instruments like kick and bass, how well each hit fits a turn of
   its own), whether the hits want the same turn, and how much material there
   was. When they don't, the footer suggests PER HIT.
5. **Time anchor**: maximum-likelihood GCC (Knapp & Carter) finds how far
   apart the two arrive (to a fraction of a sample), and what moving the track
   would add on top of the rotation.

The audio path (`Rotator`) computes `cos φ · x − sin φ · H{x}` with `H` a
linear-phase FIR Hilbert transformer (Kaiser-windowed, ~86 ms long, flat
within 0.1 dB from 20 Hz to Nyquist − 20 Hz). It runs as a partitioned FFT
convolution in which a stereo pair shares one complex FFT. Every change of
angle or polarity glides along the shorter arc over 20 ms: no click, and no
dip in level however far it turns (a flip is a turn by 180°).

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
no sidechain, the PER HIT hint, per hit, empty map, export) with the demo's
scenarios (the `PF_DEMO_KIT` samples if set), no DAW needed; `roundtrip` checks the map survives a
saved session. `... <dir> bench` times scope and editor paints
instead.

### Trying it without a DAW

```sh
./start.sh                       # built-in kick + rolling bass scenarios, pick one in the window
./start.sh ref.wav track.wav     # your files: the reference, then the track to fix
```

`start.sh` builds and opens the demo host: the plugin in a window with audio
looping through it, the reference on its sidechain, out of your speakers. The
bar on top plays/stops (space), picks the material and what you hear. The
built-in material is a psytrance kick and bass, four bars: the kick on every
beat and the bass played like a sampler, either offbeat (one note on the "and"
of every beat, ringing on under the next kick until the next note cuts it) or
rolling (the three 16ths after the kick, K-B-B-B, gated). Scenarios: the bass changing notes under the kick (PER
HIT), the same every beat (one turn fits), the kick's sub layer 2.9 ms late
and flipped, or the whole loop through a chain that turned it 115 degrees; or
your files.

Real samples come from a kit file: copy `demo-kit.example.txt` to
`demo-kit.txt` (ignored by git, sample packs aren't ours to share) and list a
kick, the bass shots (root first, up to three more notes of the same patch),
their tempo and the pattern; `start.sh` picks it up (or set `PF_DEMO_KIT`). Without a kit
the loop is synthesized (`tools/PsyLoop.h`). Monitors: **SUM** (reference + corrected track, the reference
delayed by the plugin's latency like a DAW does; this is where the alignment
is heard), **TRACK** or **REFERENCE** alone. The plugin gets a matching
looping transport, so the scope's BAR and BEAT triggers, PER HIT and export
work as in a DAW (Stop keeps processing with the transport stopped, as hosts
do). Right-click DRAG WAV to show the file in Finder.
For a configure option on first run: `PF_CMAKE_ARGS=-D... ./start.sh`.

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
src/core/align     capture, cross-spectrum, analyzer, phase map, real-time rotator, live spectrum
src/plugin         JUCE processor and editor
src/ui             theme, controls, scope, phase strip, map lane, export key
tests/             Catch2 tests for the core
tools/             WAV aligner, offscreen UI snapshot tool
packaging/         macOS pkg and Windows installer scripts
```

## License

The plugin links JUCE 8 under the AGPLv3. IBM Plex fonts under the SIL Open
Font License (`resources/fonts/IBMPlex-OFL.txt`).
