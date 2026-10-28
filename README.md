# Cutbench Notes

An original sample-mapping tool for hip-hop, boom bap, lo-fi and experimental producers.
It listens to the audio that passes through the plugin, shows which pitches are in it, and
turns the notes you select into a MIDI pattern you can drag into your DAW.

It is inspired by the *workflow* of tools like noteGRABBER. It contains no code, branding or
assets from any other product.

Formats: **VST3**, **Audio Unit (`.component`)** and a **standalone app** (loads WAV/AIFF/FLAC).
Built with C++17, JUCE 8.0.4 and CMake.

---

## Status - please read this first

| Part | State |
| --- | --- |
| Analysis core (FFT, pitch analysis, candidates/QuickGrab, note editor with undo/redo and quantise, MIDI file writer, audio FIFO / live-analysis / recording threads, preview synth) | **Built and tested**: 57 unit tests pass (g++ 13, `-Wall -Wextra`), also clean under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer. This was done on Linux, not on a Mac. |
| JUCE plugin layer (`plugin/src`: processor, editor, analysis view, piano roll, drag-and-drop) | **Written but not compiled yet.** No JUCE and no macOS toolchain were available when it was written. The first run of the GitHub workflow is its first real compile - expect that it may need a round of small fixes. Send the compiler output and it can be fixed quickly. |
| CMake files, GitHub workflow | Written, **not executed yet** (the source lists were cross-checked by script). |
| Any DAW (Ableton Live etc.) | **Not tested.** Host compatibility, especially MIDI drag-and-drop out of the plugin window, is unverified. |
| Code signing / notarisation | **Not done.** Downloaded builds are unsigned (ad-hoc signed in CI); see "Gatekeeper" below. |

---

## What works (implemented)

* **Live analysis** of the audio on the host track (mono or stereo, down-mixed to mono for analysis).
  The audio callback only copies samples into a lock-free FIFO; no allocation, locks or file I/O there.
  The host audio passes through **bit-exact** unless you press *Play* (preview).
* **Recording** of up to 120 s of the plugin input, then automatic detailed analysis. *Re-analyse* repeats it.
* **Loading audio files** (WAV/AIFF/FLAC up to 5 minutes) - button or drag-and-drop onto the window. Works in the plugin and the standalone app.
* **STFT pitch analysis**: Hann window, FFT 8192 / hop 512 for recordings and files (4096 / 1024 for the live view),
  log-compressed spectrum, noise-floor subtraction, harmonic-sum salience for every semitone C0-C8,
  then iterative pick-and-cancel so the harmonics of an already explained note do not become new notes.
* **Display**: piano keys with note names (C0-C8), time ruler, heat map of the real analysis data,
  temporal smoothing, sensitivity threshold, adjustable pitch range, zoom, scrolling (scroll bar, mouse wheel,
  Cmd+wheel zooms), hover/click readout of the strongest picks at that time.
  Toggle *Harmonic filter* to see either what the engine believes (picked notes) or the raw salience.
* **Candidates and QuickGrab**: detections are grouped into notes and given a heuristic score and an
  "uncertain" flag (marked `?`). One QuickGrab slider decides how many candidates are selected. Nothing is
  added to the pattern until you click *Apply QuickGrab* or click a candidate - review first.
* **Pattern editor** (piano roll): select, move, resize, add (double-click), delete, per-note velocity lane,
  undo/redo, quantise (1/4 - 1/32 at the BPM you set; *Host* takes the tempo from the DAW), grid snapping (hold Alt to bypass).
* **Preview**: built-in synth plays the pattern; compare with the original passage using
  *Original / MIDI / Original + MIDI*. While a captured passage is previewed it replaces the host input.
* **MIDI export**: standard MIDI file (format 0, 480 PPQ, tempo + 4/4 time signature, note-on/off pairs,
  velocity 1-127, overlapping same-pitch notes trimmed). Time 0 = start of the analysed passage.
  Drag the "Drag MIDI to your DAW" box, or use *Export .mid...* / *Open folder* (fallback). Files go to
  `~/Documents/Cutbench Notes/Exports/`.
* The MIDI pattern and settings are saved with the DAW project (the audio passage is not).

## Known limitations (honest list)

* **Pitch detection is a heuristic.** Expect errors: octave mistakes, ghost notes from shared partials in chords,
  missed quiet notes, noisy results on drums and heavily distorted material. A lone sine is flagged as octave-ambiguous
  on purpose. Always check the result by ear with the preview.
* Weak notes below 25% of the strongest note in the same frame are not picked; semitone neighbours that are much
  weaker than a strong note are treated as spectral leakage and removed (a real tight cluster can lose its weak member).
* Below roughly C2 neighbouring semitones are not cleanly resolved by an 8192-point FFT; they are inferred from harmonics.
* **Timing accuracy is limited by the analysis window**: about +-60 ms on clean notes (measured in the tests),
  worse for blurred onsets. Quantise afterwards if needed. No pitch-bend / vibrato / glide tracking.
* The confidence value is a ranking score, **not** a probability.
* Preview is mono. No host-transport sync: preview always starts at the beginning of the passage.
* Octave naming: this tool uses **C4 = MIDI 60**. **Ableton Live shows MIDI 60 as C3.** The notes are identical, only the label differs.
* Live view is for watching only; editing works on a recorded passage or loaded file.
* Windows/Linux are not set up (CMake would need small additions for VST3 on those systems).
* JUCE 8 is dual-licensed (AGPLv3 / commercial). Check JUCE's licence terms before distributing builds.

---

## Get the `.component` (and VST3) with GitHub - no local build needed

1. Create an empty repository on github.com (private is fine).
   Note: macOS runners are free for public repositories; private repositories use your plan's included minutes, and macOS minutes count extra.
2. In Terminal, in the folder with this project:

   ```bash
   cd CutbenchNotes
   git init -b main
   git add .
   git commit -m "Cutbench Notes"
   git remote add origin https://github.com/<your-user>/<your-repo>.git
   git push -u origin main
   ```

   (`git` comes with the Xcode Command Line Tools: run `xcode-select --install` if macOS asks for it.)
3. Open the repository on github.com -> **Actions** -> **Build macOS (Intel)**. It starts automatically on push
   (or press **Run workflow**). The first run downloads JUCE and takes a while.
4. When it is green, open the run and download the artifact **CutbenchNotes-macOS-Intel** (a zip containing
   `Cutbench_Notes.component.zip`, `Cutbench_Notes.vst3.zip`, `Cutbench_Notes.app.zip`).
5. Unzip the downloads, then install (see below) and remove the quarantine flag.
6. If the run is **red**, open the failing step, copy the error text and send it over. Because the plugin layer
   has not been compiled yet, a first-run fix round is realistic.

The workflow first runs the core unit tests on the Intel runner, then builds all three formats, ad-hoc signs them
and runs `auval` on the Audio Unit (informational, it does not fail the build).

## Build on your 2017 Intel MacBook Pro

Requirements: macOS with the **Command Line Tools** (Apple clang + git), **CMake 3.22 or newer**, and internet
for the first configure (it downloads JUCE 8.0.4 once). No Homebrew needed.

```bash
xcode-select --install            # Command Line Tools (skip if already installed)
```

CMake is not part of the Command Line Tools. Download the macOS `.dmg` from <https://cmake.org/download/>
(a 3.28-3.31 release is recommended), drag **CMake** into `/Applications`, then:

```bash
export PATH="/Applications/CMake.app/Contents/bin:$PATH"
cmake --version                   # should print 3.22 or newer

cd CutbenchNotes
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure     # runs the core unit tests
```

The build copies the plugins to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`
(switch off with `-DCUTBENCH_COPY_PLUGIN=OFF`). Build outputs are in `build/CutbenchNotes_artefacts/`.

Useful options:

| Option | Meaning |
| --- | --- |
| `-DCUTBENCH_JUCE_DIR=/path/to/JUCE` | Use a local JUCE 8.0.4 checkout (fully offline builds). |
| `-DCUTBENCH_BUILD_PLUGIN=OFF` | Build and test only the analysis core (no JUCE needed). |
| `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` | Universal binary (default is Intel `x86_64`). |
| `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0` | Raise the minimum macOS version (default 10.15). |

Core-only build without CMake (just a compiler), useful to run the tests:

```bash
clang++ -std=c++17 -O2 -pthread -Icore/include -Itests core/src/*.cpp tests/test_*.cpp -o cutbench_tests && ./cutbench_tests
```

## Installing and using it in Ableton Live

1. **Remove the quarantine flag** from downloaded builds (macOS Gatekeeper blocks unsigned downloaded plug-ins):

   ```bash
   xattr -dr com.apple.quarantine "Cutbench Notes.component" "Cutbench Notes.vst3"
   ```
2. Copy the files:
   * VST3 -> `~/Library/Audio/Plug-Ins/VST3/Cutbench Notes.vst3`
   * Audio Unit -> `~/Library/Audio/Plug-Ins/Components/Cutbench Notes.component`
3. In Live: **Settings/Preferences -> Plug-Ins**. Turn on **Use VST3 Plug-In System Folders** and/or **Use Audio Units**
   (Live 11 and newer load VST3; Live 10 only loads AU, so use the `.component` there), then press **Rescan**.
4. The plugin is an **audio effect**: put it on the **audio track** that plays your sample
   (Plug-Ins -> Cutbench Notes). It analyses the audio flowing through the track while it plays.
5. Workflow: press **Record** in the plugin, play the sample/loop in Live, press **Stop rec**. Analysis starts automatically.
   Raise **QuickGrab**, press **Apply QuickGrab**, fix notes in the piano roll, set **BPM** (or **Host**) and **Quantise**.
   Check with **Preview: Original + MIDI**.
6. Drag **Drag MIDI to your DAW** onto a MIDI track. If Live does not accept the drop from a plugin window, use
   **Export .mid...** or **Open folder** and drag the file from Finder into Live.
   The pattern starts at time 0 = beginning of the recorded passage, so drop it where the recording started.

## Troubleshooting

* **Plugin does not show up in Live.** Check the Plug-Ins preferences toggles (step 3), press Rescan, restart Live.
  Confirm the file really is in the folder above. For AU, open Terminal and run
  `auval -a | grep -i cutbench` - the plugin should be listed; `auval -v aufx Cbnn Cbnc` validates it in detail.
  If it still does not appear, a deep rescan (hold **Option** while pressing Rescan on many Live versions) or removing a stale
  copy of the plugin and rescanning helps.
* **"... can't be opened because Apple cannot check it for malicious software" / plugin silently ignored.**
  Run the `xattr -dr com.apple.quarantine ...` command from step 1 on the plugin files, then rescan.
* **Live marks the plugin as unusable / it crashed during scan.** Live lists such plug-ins in the Plug-Ins browser
  as unusable. Remove the plugin file, rescan, put the newest build back, rescan again. Please send the Console.app
  crash report (Console -> Crash Reports) if it persists.
* **The display stays empty.** The live view only moves while audio reaches the plugin (usually while the transport plays
  a clip on that track). Make sure the plugin sits on the track that makes the sound (not on a muted or empty track) and is
  after any effect that silences it.
* **Record produces nothing / "no audio to analyse".** Nothing reached the plugin while recording. In the standalone app,
  choose an audio input in the settings dialog (macOS asks for microphone permission) and un-mute the input. Better:
  load a file instead.
* **Standalone: loud feedback.** The standalone app starts with the input muted for this reason; unmute only with headphones.
* **Notes are off by an octave in Live.** Live shows MIDI 60 as C3, this plugin as C4. The MIDI data is identical.
* **Detected notes are wrong.** See "Known limitations". Lower the pitch range to the part you care about, lower Sensitivity to
  remove weak ghosts, use the pattern editor, and always compare with *Preview*.
* **MIDI drag does nothing.** Not every host accepts file drags from plugin windows. Use **Export .mid...** or **Open folder**.
* **CMake error "JUCE ... CMake 3.22 required".** Install a newer CMake from cmake.org (see build section).
* **Configure step fails to download JUCE.** Needs internet once. Alternatively download the JUCE 8.0.4 source release,
  unpack it and pass `-DCUTBENCH_JUCE_DIR=/path/to/JUCE`.
* **GitHub workflow cannot find the `macos-15-intel` runner.** GitHub retires old runner images over time. Change
  `runs-on` to `macos-15` in `.github/workflows/build-macos.yml`; the CMake files still build for Intel (`x86_64`).

## How the analysis works

1. Samples are windowed (Hann) and transformed with an FFT; magnitudes are converted to amplitude and log-compressed.
2. A moving average over the spectrum (about +-170 Hz) is subtracted together with a small margin, which removes broadband noise and window sidelobes.
3. For every semitone C0-C8 the **salience** is a weighted sum of the spectrum at its first 8 harmonics
   (weights 0.8^(h-1), each looked up within +-35 cents).
4. **Iterative estimation**: take the best note, scale the spectrum down around its harmonics, repeat (max 6 notes per frame,
   later picks must reach 25% of the first). This is what stops harmonics from becoming extra notes.
5. Over time, picks of one semitone are joined into segments (gaps up to 50 ms bridged, segments below 60 ms dropped).
   Weak leading/trailing frames are trimmed to sharpen timing, and leakage into semitone neighbours is removed.
6. **Score** = clamp(strength / 0.2) x (1 - 0.4 x octave ambiguity) x duration factor. The octave ambiguity compares the salience of the note
   one octave lower with the note itself. Anything with a low score or high ambiguity is drawn with a `?`.

Measured on a Linux VM (not on a 2017 MacBook): live frames cost about 0.23 ms each at 43 frames/s (about 1% of a core),
and a 60 s passage is analysed in about 2 s in the background.

## Project layout

```
CMakeLists.txt                 build (core library, tests, JUCE plugin)
core/include/cutbench/*.h      JUCE-free analysis core (public headers)
core/src/*.cpp
plugin/src/*                   JUCE plugin: processor, editor, analysis view, piano roll, drag source
tests/*                        unit tests (own tiny test harness, no downloads)
.github/workflows/build-macos.yml   Intel macOS build + artifacts
```

Core modules: `PitchMath` (Hz/MIDI/names), `Fft`, `PitchAnalyzer` (STFT + salience + cancellation),
`Candidates` (segments, score, QuickGrab), `NoteEditor` (notes, undo/redo, quantise), `MidiFile` (SMF writer),
`AnalysisEngine` (lock-free FIFO, live thread, recording, offline job), `PreviewSynth` (voices + sample-accurate pattern playback).

## Tests

`ctest` runs 57 tests covering pitch conversion and note names, FFT correctness, analysis of silence / noise / sines /
chords / melodies, note timing, QuickGrab and sensitivity behaviour, invalid and extreme input (NaN, empty, tiny buffers),
the note editor (undo/redo, gestures, quantise), MIDI file structure (the tests parse the written bytes back),
the FIFO under two threads, recording with bit-exact capture, the 120 s limit, overflow handling, cancelling background
jobs, and the preview synth (it is fed back into the analyser and must sound like the pattern).

The JUCE layer has no automated tests; it needs a manual check in a host.
