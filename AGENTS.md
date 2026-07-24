# AGENTS.md -- Antiphon orientation notes

## What this project is

**Antiphon** is a Ninjam client shaped as a JUCE audio plugin (VST3 + CLAP +
Standalone). It lives inside a DAW, typically on the master bus, and injects
Ninjam's interval-delayed playback model into the host: local audio passes
through undelayed, remote players arrive one interval late, phase-locked to the
local metronome, and each remote channel can be routed to its own output bus so
the DAW records the jam as stems.

Clean modern C++ against JUCE. No `NJClient` wrapper, no Qt, no heavy
dependencies -- about 6 000 lines in `src/`, and it is meant to fit in your head.

Authoritative docs (read these before designing anything new):

- **`PRINCIPLES.md`** -- twelve principles every change must satisfy, with a
  proposal gate at the top.
- **`NON-GOALS.md`** -- the standing refusals, and what we offer instead.
- **`DESIGN.md`** -- architecture: the interval model, threads, sync, mixing,
  routing, UI.
- **`ROADMAP.md`** -- named work areas with checkboxes. `docs/COMPLETED.md` is
  the archive.
- **`README.md`** -- the user manual.
- **`docs/PROTOCOL.md`** -- wire format, and the traps in it.
- **`docs/PARITY.md`** -- what has been verified against the reference client,
  with the measured numbers.
- **`docs/ACCESSIBILITY.md`** -- the accessibility story, honestly.
- **`test/README.md`** -- how to run every test layer.

Ordering for any new work: **PRINCIPLES -> DESIGN -> ROADMAP**. If a proposal
cannot be expressed within the principles, it is not ready for the roadmap.

## Status

`ROADMAP.md` is the live source; update that file, not this one, when focus
changes. As of 2026-08-10:

- The client **works**: connects to real servers, transmits and receives in time
  with other clients, verified differentially against the reference client at two
  tempos (`docs/PARITY.md`).
- The accessibility pass has landed -- every control named, header readable,
  audit running headlessly.
- The **GitHub move** has landed: GPLv3, `CONTRIBUTING.md`, and a history with
  no reference sources in it at any point.
- Next: **cross-platform builds**. Everything so far is Linux and CLAP only,
  which matters most for accessibility: JUCE has screen-reader backends on macOS
  and Windows and **none** on Linux, so that work is currently unexercised.

## Layout map

```
CMakeLists.txt              # root: JUCE patching, submodules, src/, test/
patches/*.patch             # applied to the JUCE submodule at configure time
assets/fonts/               # Inter (OFL-1.1), embedded as binary data
src/
  CMakeLists.txt            # juce_add_plugin(Antiphon), binary data, CLAP
  PluginProcessor.{h,cpp}   # AntiphonAudioProcessor -- JUCE entry, owns NinjamClient
  PluginEditor.{h,cpp}      # AntiphonEditor -- three-column elastic layout
  StandaloneApp.cpp         # our JUCEApplication: window first, then the device
  NinjamClient.{h,cpp}      # networking, dispatch, remote state, playback queue
  NinjamProtocol.{h,cpp}    # pure wire format: framing, parse, build, auth hash
  VorbisCodec.{h,cpp}       # first-party Ogg/Vorbis, pimpl over libogg/libvorbis
  Sha1.{h,cpp}              # minimal FIPS 180-1, auth path only
  # --- JUCE-free, testable modules (this is where audio-thread logic goes) ---
  IntervalClock.{h,cpp}     # sample-exact beat/interval grid; njclient.cpp:806
  HostGrid.h                # offline: the grid derived from the host's PPQ
  MetronomeVoice.{h,cpp}    # one-shot click, 880/660/440 Hz
  SyncState.h               # 5-state DAW sync machine (transport start, not timeline)
  Shortcuts.h               # Ctrl+Alt shortcut mapping; matches key code, not text
  AudioDeviceStartup.h      # 4-state standalone device-open policy, with a budget
  ChannelMix.h              # mono/pan/gain: one home for three rules that drifted
  MusicalKey.h              # key and mode: parse, display, scale notes
  ClipsortLog.{h,cpp}       # session archive manifest: read and write
  StemRender.h              # one clip into one interval, resampled and aligned
  GainUtils.h               # dB<->linear, fader and meter scales, formatting
  IntervalProbe.h           # shared test signal: plugin Test Tone and the tests
  ChatFormat.{h,cpp}        # chat rendering: vote lines, chord progressions
  # --- UI ---
  LocalChannelStrip.{h,cpp} # 90px vertical strip per local input channel
  RemoteUserStrip.{h,cpp}   # card per remote player, channels arranged horizontally
  RemoteChannelRow.{h,cpp}  # 90px vertical strip per remote channel
  ServerBrowserDialog.{h,cpp} # DialogWindow: ninbot.com room list + connect fields
  AntiphonLookAndFeel.{h,cpp} # dark theme, teal #00b4d8
  StatusReadout.h           # header info for a screen reader; first in tab order
  AudioTroubleView.h        # standalone: what shows when no device would open
  AccessibleNaming.h        # a control adopts the label attached to it
  # --- accessibility ---
  Announcer.h               # discrete-event speech, verbosity, rate limiting
  AccessibilityAudit.{h,cpp}# name/description coverage rules (JUCE-free, testable)
  AccessibilityTree.h       # adapts a live Component tree into the audit's shape
test/
  CMakeLists.txt            # re-lists production sources -- see below
  README.md                 # the operational testing guide
  FakeNinjamServer.{h,cpp}  # in-process loopback server
  AuditMain.cpp             # AntiphonAudit: accessibility gate over the real UI
  fixtures/reference/       # wire captures the OFFICIAL client produced
  fixtures/testserver.cfg   # config for the local ninjamsrv
tools/
  StemsMain.cpp             # antiphon-stems: session archive -> WAV stems
scripts/
  testserver.sh             # fetches, builds and runs a local ninjamsrv out of tree
  analyze_archive.py        # measures a server session archive
docs/references/            # what was read to write this, and at which revision
modules/                    # ogg, vorbis, clap-juce-extensions submodules
```

## Canonical commands

The single source of truth for how this repo is built and tested.

First-time clone: `git submodule update --init --recursive`

```bash
# Configure (once, or after CMakeLists changes). No generator flag -- use
# whatever CMake picks.
cmake -B build
# Build (always from the repo root)
cmake --build build -j $(nproc)
# Test
ctest --test-dir build --output-on-failure
# Or run the binary directly, filtering by suite name
./build/test/NinjamTests_artefacts/NinjamTests IntervalClock
# Accessibility gate over the real component tree; exit code is the finding
# count, and the report goes to stdout. Runs headless, no display needed.
./build/test/AntiphonAudit_artefacts/AntiphonAudit
# Offline: turn a session archive into WAV stems.
./build/tools/AntiphonStems_artefacts/AntiphonStems <session-dir> -o stems/
```

Targets: `Antiphon_Standalone` (easiest for iteration), `Antiphon_VST3`. CLAP is
built from the same target via `clap_juce_extensions_plugin`.

Sanitiser builds -- keep them around, they are worth more than gdb here:

```bash
# ASan + UBSan
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j $(nproc)
./build-asan/test/NinjamTests_artefacts/Debug/NinjamTests   # note: under Debug/

# TSan (cannot be combined with ASan)
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=thread -g"

# Symbols for gdb
cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

**Manual smoke test:** launch Standalone, connect to `ninbot.com:2049` as
`anonymous`, exercise the feature.

**Debug dumps:** the **Save Tx** / **Save Rx** toggles write `tx.ogg`, `tx.wav`,
`rx.ogg`, `rx.wav` to the desktop -- useful for diagnosing encode/decode problems
without a full round-trip. **Test Tone** injects `IntervalProbe` into every input
bus, which is what makes transmit alignment measurable.

## Submodules are patched at configure time

`patches/*.patch` are applied by the block at the top of the root
`CMakeLists.txt`. Each entry names the submodule it targets, so both `JUCE` and
`modules/clap-juce-extensions` are covered. Each patch is idempotent
(`git apply --reverse --check` detects an already-applied patch), so configuring
twice is a no-op and a fresh clone gets them once.

**Do not hand-edit a submodule.** Change the `.patch` file, revert the
submodule, and reconfigure.

**When writing a patch, preserve line endings.** JUCE sources are CRLF. Editing
one through a tool that normalises newlines rewrites every line and produces an
8000-line diff instead of a six-line one.

- **`juce-linux-plugin-keyboard-focus.patch`** -- an embedded plugin window on
  Linux never considers itself focused, so `Component::takeKeyboardFocus()` asks
  the peer for focus and the X `FocusIn` lands afterwards. By then
  `ComponentPeer::handleFocusGain()` has no record of the intended target and
  focuses the *top-level* component instead, stealing focus from the text field
  that was clicked. Symptom: caret and selection appear, typing does nothing;
  invisible in the standalone, where the peer already holds focus. The patch
  records the target before the grab.
  See <https://forum.juce.com/t/any-idea-why-vst-plugins-on-linux-immediately-lose-focus/35456>.

- **`juce-bus-layout-change-notification.patch`** and
  **`clap-bus-layout-rescan.patch`** -- a plugin with dynamic buses had no way
  to tell a host that its I/O topology changed. `ChangeDetails` carries
  latency, parameter, program and state flags but nothing for buses, and
  neither the VST3 nor the CLAP wrapper acted on a bus change, so
  `updateHostDisplay()` after `addBus()`/`removeBus()` did nothing at all: the
  host kept routing to the layout it cached at load and the new bus never
  appeared. The pair adds `ChangeDetails::busLayoutChanged` and maps it to
  VST3's `kIoChanged` and CLAP's `audioPortsRescan(CLAP_AUDIO_PORTS_RESCAN_LIST)`.
  Used by `addInputBus`/`removeLastInputBus`/`addOutputBus`/`removeLastOutputBus`.

---

## Testing expectations

Read this before changing anything. The bugs the suite found were all invisible
to listening -- audio a few percent sharp, an interval a sample short, a parser
reading past a buffer. Assume your change has the same failure mode.

### Which layer to reach for

| Change touches | Write | Notes |
|---|---|---|
| Wire format, parsing, message building | `test/NinjamProtocolTests.cpp` | Add the message to the **truncation sweep** in `runTruncationTests()`, not just a happy-path case. |
| Beat/interval timing | `test/IntervalClockTests.cpp` | Sweep bpm x bpi x sample rate x block size. A bug that only shows at 44.1 kHz or block size 61 is the normal case, not the exotic one. |
| Click synthesis | `test/MetronomeVoiceTests.cpp` | Assert measured pitch, never the formula. |
| DAW sync transitions | `test/SyncStateTests.cpp` | `SyncState` is pure; drive it directly. |
| Standalone audio-device startup | `test/AudioDeviceStartupTests.cpp` | `AudioDeviceStartup` is pure. `StandaloneApp.cpp` around it cannot be compiled into the test target -- it needs the `JucePlugin_*` defines, same as `PluginProcessor`. |
| Mono/pan/gain rules | `test/ChannelMixTests.cpp` | |
| dB scales, meter ballistics | `test/GainUtilsTests.cpp` | |
| Encode/decode | `test/VorbisCodecTests.cpp` | Always test at 44.1 **and** 48 kHz. |
| Anything crossing the socket | `test/LoopbackTests.cpp` | `FakeNinjamServer` needs no production changes -- point the client at `127.0.0.1`. |
| Mixing, routing, playback delay | `test/AudioLoopbackTests.cpp` | Drives the real path end to end. |
| Accessibility naming rules | `test/AccessibilityAuditTests.cpp` | Synthetic node tree; the real UI cannot be compiled into the test target. |
| A new control, or a new UI state | `test/AuditMain.cpp` | The `AntiphonAudit` target links the plugin's own library and audits the **real** editor across five states. Add a state when you add a surface -- an unaudited state is how the connect dialog stayed unchecked for its whole life. |
| Server-visible behaviour | `test/RealServerTests.cpp` | Opt-in via `NINJAM_TEST_SERVER`; keep the default suite hermetic. |

### Rules that are easy to get wrong

- **Fix bugs test-first, and prove the test has teeth.** Write the failing test,
  then fix. If a fix is a one-liner, temporarily reinstate the bug and confirm
  the test goes red -- a test that passes both ways is worthless. That is how the
  48 kHz encoder bug was pinned.
- **Audio assertions must be statistical.** RMS, and pitch by zero crossings
  (`test/TestSignal.h`). Vorbis is lossy and has codec delay; sample-by-sample
  comparison against the input will never hold.
- **A new `src/*.cpp` must be added to BOTH `src/CMakeLists.txt` and
  `test/CMakeLists.txt`.** The test target deliberately re-lists production
  sources rather than sharing them -- `juce_generate_juce_header` only works on
  `juce_add_*` targets, and each target needs its own `JuceHeader.h`. See the
  comment at the top of `test/CMakeLists.txt`.
- **Keep testable logic out of `PluginProcessor`.** It cannot be compiled into
  the test target at all (it needs `JucePlugin_*` defines). That is the whole
  reason `IntervalClock`, `MetronomeVoice`, `SyncState`, `ChannelMix`,
  `GainUtils` and the `AccessibilityAudit` rules exist as separate modules. New
  audio-thread logic belongs in a module like those, with `processBlock` as a
  thin caller.
- **Do not link `juce_audio_utils` into the test target.** It drags in
  `juce_audio_processors`/`juce_audio_devices` and thus X11/ALSA, which breaks
  headless runs.
- **Parser changes get an ASan run.** Over-reads pass silently otherwise. Ignore
  UBSan noise from inside libvorbis.
- **Audio-thread code must not allocate, lock, do file I/O, or log**
  (`PRINCIPLES §7`). The audio thread now takes **no lock at all**: remote
  playback walks `NinjamClient::streamSlots` and local channels walk
  `AntiphonAudioProcessor::audioChannels`, both fixed arrays of never-destroyed
  objects published with a release store on a count. The one remaining
  violation is the `callAsync` at each interval boundary in
  `fireCaptureLambdas`, tracked as *Lock-free TX handoff* in `ROADMAP.md`.
  **Do not add to that list**, and in particular do not reach for a lock to
  share new state with the audio thread -- publish it the way those two do.
  Save Rx does file I/O on the audio thread while it is on, deliberately: it
  captures the mix as the audio thread sees it.

### Invariants worth not breaking

- **`IntervalClock::samplesPerInterval()` truncates on purpose.** It reproduces
  `njclient.cpp:806` verbatim so our interval boundaries line up with every other
  client on the server. Rounding it "correctly" would silently desync us from
  Jamtaba and ReaNINJAM. Beat offsets inside the interval are rounded, because
  they only drive the local click.
- **The local metronome is the sole authority for interval swaps.**
  `intervalBeginSignal` is drained and never acted on. Network jitter must not
  move the playback clock.
- **Interval delivery is all-or-nothing.** Ogg emits a page only every ~4 kB, so
  a quiet or tonal interval decodes to nothing until the end-of-stream flush. Do
  not build anything that assumes a partially received interval is playable.
- **`kDefaultRemoteChannelVolume` is 0.25, deliberately.** It matches the
  reference client. Unity would make us 12 dB louder than everyone else in the
  room.

---

## Debugging: what actually works here

Learned the hard way; each of these cost real time.

- **Reach for a sanitiser before gdb.** The default build has no `-g`, so gdb
  backtraces are useless address soup. ASan found a use-after-free instantly,
  with file and line, in code gdb could not even name. Build commands above.

- **TSan for the threading bugs ASan cannot see.** Four threads touch shared
  state here, and every threading bug found so far was of that kind.

  **The baseline is zero warnings, in both TSan and ASan.** Anything else is a
  regression you introduced -- there is no list of known-acceptable findings to
  check against, and that is deliberate. A suite with a remembered exception is
  one nobody reads carefully.

  The only sanitiser output that is not ours is four UBSan lines from inside
  vendored libvorbis (`bitwise.c`, `floor1.c`, `psy.c`). Ignore those; do not
  ignore anything in `src/`.

- **Never diagnose a "hang" through `timeout` and a pipe.** JUCE's output is
  block-buffered, so `timeout 300 ... | tail` throws away everything the run
  printed when it kills the process, and a merely slow test is indistinguishable
  from a deadlocked one. `stdbuf` does not help (C++ iostreams do their own
  buffering). Redirect to a file and let the process finish.

- **Blocking calls that turn a failure into a hang.**
  `juce::ChildProcess::readAllProcessOutput()` blocks until the child exits --
  never call it on a process that is still running.
  `StreamingSocket::waitForNextConnection()` blocks forever. Both were mistaken
  for deadlocks; bound them and report why instead.

- **When a test fails, suspect the measurement first** (`PRINCIPLES §5`). A
  "294.7 Hz instead of 440" turned out to be the test's own pitch detector gating
  on peak, with the peak set by a transient. Cross-check with an independent
  method (autocorrelation, a Python script over the raw capture) before believing
  a number. Three "failures" in this project have been measurement error, one of
  them all the way through a fix (`docs/COMPLETED.md`, Withdrawn).

- **Kill strays.** A killed `RealServer` run leaves `ninjamsrv` behind:
  `pkill -f ninjamsrv; rm -rf /tmp/njarchive`.

---

## Code conventions and gotchas

- **C++17.** `juce::` types preferred over `std::` where both exist
  (`juce::String`, `juce::AudioBuffer`, `juce::CriticalSection`).
- **2-space indent, braces on same line, members lowerCamelCase.**
- **No comments except for non-obvious invariants or protocol workarounds.** No
  narration. When a comment is warranted it explains *why*, and often cites a
  line in a reference implementation by upstream repository and file --
  `(justinfrankel/ninjam njclient.cpp:806)`. `docs/references/SOURCES.md` maps
  each prefix to a repository and the revision it was read at.
- **ASCII only in source files** -- no non-ASCII characters anywhere (comments,
  string literals, or identifiers). Use `--` for an em dash, `->` for arrows. In
  UI string literals use plain ASCII equivalents. Reason:
  `juce::String(const char*, size_t)` asserts ASCII validity, and non-ASCII
  source bytes cause compiler warnings on some platforms.
- **Never branch on `JucePlugin_Build_Standalone` in `src/`.** It is a
  project-level flag meaning "Standalone is one of the `FORMATS`", and it is 1 in
  the shared-code target that VST3 and CLAP link against -- so it is true in
  *every* format. It once compiled the whole DAW sync flow out of the plugin. Use
  `AntiphonAudioProcessor::isStandaloneApp()` (runtime `wrapperType`). The
  `no-build-standalone-macro` ctest fails if the macro comes back.
- **Never open an audio device before the window exists.** JUCE's stock
  standalone does exactly that and a blocking backend leaves no application at
  all. `src/StandaloneApp.cpp` replaces it; see `DESIGN.md` §16 before changing
  standalone startup.
- **Text entry belongs in a `juce::DialogWindow`,** not a child overlay, even
  with the focus patch: a dialog is a real top-level window with its own peer and
  takes focus normally. See `DESIGN.md` §10.8.
- **`modules/` is read-only** -- ogg, vorbis and clap-juce-extensions are
  upstream submodules. Same rule as `JUCE/`: change a patch, not the submodule.
- **No reference source enters this repository.** The NINJAM sources are GPLv2
  and Antiphon is a first-party implementation (`PRINCIPLES §6`); they were read,
  never vendored, and the published history has never contained them. Read
  `docs/references/<name>.md` first and fetch the upstream repository into a
  scratch directory if you need specifics -- `scripts/testserver.sh` shows the
  shape. If you find yourself adding a submodule under `references/`, stop.

### Before claiming a change works

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Plus, when the change warrants it: the ASan build for parser work, and a
Standalone launch for anything touching the UI or the audio thread. **Quote the
actual output** -- "tests pass" without having run them is how the hardcoded
48 kHz encoder survived as long as it did.

---

## Documentation maintenance

Eight files at root, each with one job. Put a fact in exactly one of them.

| File | Owns |
|---|---|
| `AGENTS.md` | How to work in this repo. Map and rulebook, not reference. |
| `PRINCIPLES.md` | Why. Stable `§N` anchors, cited from everywhere. |
| `NON-GOALS.md` | What we refuse, and what we offer instead. `fence #N`. |
| `DESIGN.md` | What the software is. Stable `§N` anchors. |
| `ROADMAP.md` | What it is becoming. Named work areas, checkboxes. |
| `README.md` | The user manual. |
| `CONTRIBUTING.md` | How an outside contributor works here, and what is most needed. |
| `THIRDPARTY.md` | Component licences and their obligations. |

`CONTRIBUTING.md` is the outward-facing subset of this file plus the project's
priorities for outside help. When a rule here changes and a contributor would
need to know, change both -- but keep the detail here and the summary there.

`CLAUDE.md` and `GEMINI.md` are **symlinks** to this file; they are tracked, and
should stay symlinks.

Conventions:

- **Ship the doc change with the code change.** A shipped work area moves to
  `docs/COMPLETED.md` in the same commit that finishes it.
- **`PRINCIPLES §N` and `fence #N` are stable anchors.** If a renumber is ever
  unavoidable, sweep every reference in the docs **and** in `src/` in the same
  change.
- **Keep `README.md` honest.** Anything it marks as working must actually work.
  It may run ahead of reality for clearly-labelled planned items, never for
  present-tense claims.
- **A number quoted anywhere needs a method.** `docs/PARITY.md` carries the
  measurements and the instrument calibration; cite it rather than restating
  figures (`PRINCIPLES §5`).
