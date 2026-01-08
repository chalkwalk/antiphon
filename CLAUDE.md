# CLAUDE.md — Ninjam JUCE Plugin

This is the canonical reference for the project. `GEMINI.md` is the original design doc and is kept for historical context; this file supersedes it as the working map.

## What this is

A clean, cross-platform (Linux / Windows / macOS) Ninjam client as a **JUCE audio plugin** (VST3 + CLAP + Standalone). The plugin lives inside a DAW, typically on the master bus, and injects Ninjam's interval-delayed playback model into the host. Local audio passes through without delay; remote players' audio arrives one interval late, phase-locked to the local metronome.

Design principles: clean modern C++ against JUCE — no NJClient wrapper, no Qt, no heavy dependencies. Simple, DAW-centric, not a standalone host.

---

## What's built

- JUCE plugin skeleton: VST3 + CLAP + Standalone, CMake build, stereo I/O
- Host sync: reads DAW BPM/PPQ via `AudioPlayHead`; BPM mismatch warning; "Start transport" prompt
- Internal metronome: BPI/BPM-based click, 880 Hz downbeat / 440 Hz beats; volume control; off by default in plugin mode; only active when connected
- Networking: full TCP connect/auth/keepalive loop in a `juce::Thread`
- Protocol: auth, server config, user info + `SET_USERMASK`, interval begin/write (OGGv only), chat, keepalive, `CLIENT_SET_CHANNEL_INFO`
- Ogg/Vorbis encode/decode via first-party `VorbisCodec.{h,cpp}` (pimpl wrapping direct libogg/libvorbis APIs); `VorbisDecoder`/`VorbisEncoder` classes
- SHA1 via first-party `Sha1.{h,cpp}` (minimal FIPS 180-1; only `add` + `result` interface used for auth)
- Queue-based interval playback: per-(user, channel) `ChannelStream` with `DecodedInterval` queue; late WRITE chunks extend the live interval's readable range atomically; local metronome boundary (sole authority) pops the queue; 256-sample crossfade at swap boundaries
- Remote mixer: per-user volume/pan/mute/solo + VU peak, applied in `getDecodedAudio`
- Dynamic local channels: each `LocalChannel` has name, mono/stereo, vol/pan, monitor mute/solo, xmit toggle, VU peaks, `AbstractFifo` ring buffer, and an explicit `inputBusIndex`. Monitor and transmit are independent gain stages: mute/solo affect only local headphone mix; xmit gates what is sent to the server; vol/pan apply to both. Channel count and input bus count are managed independently.
- Per-channel bus routing: each local channel has an explicit input bus index (dropdown in strip); each remote channel has an output bus index (dropdown in strip) routing its decoded audio to a specific stereo output bus for DAW stem recording. Output bus count is dynamic. Routing persisted by (username, channelIndex) and reapplied when users rejoin.
- Server browser popup: live room list fetched from ninbot.com, private server entry, username/password/anonymous fields
- State persistence: `getStateInformation`/`setStateInformation` saves host, credentials, channel layout (including `inputBusIndex` per channel), mixer positions, metronome state + volume, chat panel visibility, remote output bus routing
- Chat: receive + display, send MSG/ADMIN/PRIVMSG, voting commands; collapsible via Chat toggle in toolbar; visibility persisted; ghosted (disabled, dimmed) when disconnected; cleared on next successful connect
- UI: three-column elastic layout (local left | remote centre | chat right); 40/60 proportional split with 200 px floor; local left-flush, remote right-flush; vertical channel strips with rotary pan knob, vertical fader, flanking VU bars, input/output bus dropdowns; dark theme via `NinjamLookAndFeel`; tooltips on all controls; TX/Recv buttons green/red coded; phase progress bar (pauses + dims when disconnected); header background tint reflects connection state (navy/grey/amber); toolbar buttons enable/disable by connection state and bus count; debug Tx/Rx file dumps

What remains is captured in the work table below.

---

## Build & run

```
mkdir -p build && cd build && cmake ..      # configure (first time, from repo root)
cmake --build build -j $(nproc)            # build (always from repo root)
```

No generator flag — use whatever CMake picks (usually Ninja or Make). Targets:
- `Ninjam_Standalone` — easiest for iteration
- `Ninjam_VST3` — the VST3; CLAP is built from the same target via `clap_juce_extensions_plugin`

Automated tests:

```
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
./build/test/NinjamTests_artefacts/NinjamTests IntervalClock   # filter by suite
```

Three layers -- unit tests, an in-process loopback server exercising the real
socket path, and an opt-in rig against a local `ninjamsrv` whose session archive
is measured by `scripts/analyze_archive.py`. **See `test/README.md`** for the
full story, including how to run the parsers under ASan and how to compare our
transmitted audio against Jamtaba/ReaNINJAM.

Manual smoke test: launch Standalone, connect to `ninbot.com:2049` as `anonymous`, exercise the feature.

**Debug dumps:** UI toggles **Save Tx / Save Rx** write `tx.ogg`, `tx.wav`, `rx.ogg`, `rx.wav` to the desktop. Useful for diagnosing encode/decode problems without a full round-trip.

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
| Encode/decode | `test/VorbisCodecTests.cpp` | Always test at 44.1 **and** 48 kHz. |
| Anything crossing the socket | `test/LoopbackTests.cpp` | `FakeNinjamServer` needs no production changes -- point the client at `127.0.0.1`. |
| Mixing, routing, playback delay | `test/AudioLoopbackTests.cpp` | Drives the real path end to end. |
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
  sources rather than sharing them -- see the comment at the top of
  `test/CMakeLists.txt` for why.
- **Keep testable logic out of `PluginProcessor`.** It cannot be compiled into
  the test target (it needs `JucePlugin_*` defines). That is the whole reason
  `IntervalClock` and `MetronomeVoice` exist as separate modules. New
  audio-thread logic belongs in a module like those, with `processBlock` as a
  thin caller.
- **Do not link `juce_audio_utils` into the test target.** It drags in
  `juce_audio_processors`/`juce_audio_devices` and thus X11/ALSA, which breaks
  headless runs.
- **Parser changes get an ASan run.** Over-reads pass silently otherwise. Build
  instructions are in `test/README.md`; ignore UBSan noise from inside libvorbis.
- **Audio-thread code must not allocate, lock, do file I/O, or log.**
  `IntervalClock::advance()` and `MetronomeVoice::render()` honour this;
  `getDecodedAudio` (takes `downloadMutex`) and the Save Tx/Rx toggles (file I/O)
  do not -- work item #28. Do not add to that list.

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

### Debugging: what actually works here

Learned the hard way; each of these cost real time.

- **Reach for a sanitiser before gdb.** The default build has no `-g`, so gdb
  backtraces are useless address soup. ASan found a use-after-free instantly,
  with file and line, in code gdb could not even name. Build it once and keep it
  around:

  ```
  cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
  cmake --build build-asan -j $(nproc)
  ./build-asan/test/NinjamTests_artefacts/Debug/NinjamTests
  ```

  Note the binary is under `Debug/` in that build, not beside the artefacts dir.
  Ignore UBSan output from inside libvorbis; it is pre-existing and benign.

- **For gdb, you need symbols.** Use a RelWithDebInfo build rather than the
  default:
  `cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=RelWithDebInfo`. Worth it for
  logic bugs where ASan has nothing to say.

- **TSan for the threading bugs ASan cannot see.** This codebase has four
  threads touching shared state, and both bugs found so far were threading
  ones. TSan needs its own build (it cannot be combined with ASan):
  `-DCMAKE_CXX_FLAGS="-fsanitize=thread -g"`.

- **Never diagnose a "hang" through `timeout` and a pipe.** JUCE's output is
  block-buffered, so `timeout 300 ... | tail` throws away everything the run
  printed when it kills the process, and a merely slow test is indistinguishable
  from a deadlocked one. `stdbuf` does not help (C++ iostreams do their own
  buffering). Redirect to a file and let the process finish.

- **Blocking calls that turn a failure into a hang.** `juce::ChildProcess::
  readAllProcessOutput()` blocks until the child exits -- never call it on a
  process that is still running. `StreamingSocket::waitForNextConnection()`
  blocks forever. Both were mistaken for deadlocks; bound them and report why
  instead.

- **When a test fails, suspect the measurement first.** A "294.7 Hz instead of
  440" turned out to be the test's own pitch detector gating on peak, with the
  peak set by a transient. Cross-check with an independent method (autocorrelation,
  a Python script over the raw capture) before believing a number.

- **Kill strays.** Interop runs leave `ninjamsrv` processes behind when the test
  is killed: `pkill -f njinterop; rm -rf /tmp/njinterop_*`.

### Before claiming a change works

```
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Plus, when the change warrants it: the ASan build for parser work, and a
Standalone launch for anything touching the UI or the audio thread. Quote the
actual output -- "tests pass" without having run them is how the hardcoded
48 kHz encoder survived this long.

---

## Code map

All our code lives in `src/` — ~2 k lines, fits in your head.

| File | Role |
|---|---|
| `PluginProcessor.{h,cpp}` | `juce::AudioProcessor`. Owns `NinjamClient`. Runs host sync, internal metronome (with volume), interval boundary detection (local metronome is sole authority -- `intervalBeginSignal` is drained but never acted on), per-channel local capture in `processBlock`. Phase advance gated on `isConnected()`; `phaseResetPending` atomic flag triggers audio-thread reset on disconnect. Tracks `lastConnectFailed` for header tint. Manages `vector<shared_ptr<LocalChannel>>` with `addLocalChannel`/`removeLastLocalChannel` and `addInputBus`/`removeLastInputBus`/`addOutputBus`/`removeLastOutputBus`. `LocalChannel::inputBusIndex` explicit field; `savedRemoteRoutings` map persists output bus assignments. `canAddBus`/`canRemoveBus` enabled for both. State persistence. |
| `PluginEditor.{h,cpp}` | Main UI. Three-column elastic layout: local strips (left, ~40% with 200 px floor), remote cards (centre, ~60%), chat (right, 320 px, collapsible). `relayoutChannelArea()` computes the 4-case proportional split and positions strips; called from `resized()` and 30 Hz `timerCallback`. Local strips left-flush, remote strips right-flush within their viewport. `updateToolbarStates()` enables/disables toolbar buttons by connection state and bus count (called every tick). Bottom toolbar: Connect/Disconnect, compact groups `Channel:[+]` `Input bus:[+][-]` `Output bus:[+][-]`, Metronome toggle + volume slider, SaveTx/SaveRx, Chat. Status bar + phase progress bar at top; header background tints navy/grey/amber by connection state; phase bar teal when connected, grey when not. Chat panel ghosted (disabled, dimmed background) when disconnected; cleared on `onConnected`. `TooltipWindow` (700 ms). |
| `LocalChannelStrip.{h,cpp}` | Vertical 90 px-wide strip per local input channel. Top-to-bottom: name editor (22 px), pan rotary (44 px), L/R VU bars flanking a vertical fader (flex), M+S row (22 px), TX button (22 px), input bus dropdown (22 px), Mono+Remove row (22 px). `updateInputBusCount(n)` repopulates dropdown. Reads peaks from `LocalChannel` atomics. |
| `ServerBrowserDialog.{h,cpp}` | Popup dialog. Fetches live room list from ninbot.com. Table of servers (BPM, BPI, players). Host/port/username/password/anonymous fields. |
| `NinjamClient.{h,cpp}` | Networking + protocol + encode/decode. `juce::Thread` subclass. Socket read loop, message dispatch, remote-user state, queue-based interval playback (`ChannelStream`/`DecodedInterval`/`PendingDownload`), Tx/Rx file dumps. `ChannelStream` carries crossfade fields (`fadeOut`/`fadeOutPos`/`fadeTotal`/`fadeRemaining`) for 256-sample crossfade at swap boundaries. `RemoteUserChannel` carries `outputBusIndex`; `getDecodedAudio` routes each stream to `busIdx*2`/`busIdx*2+1` in the output buffer. Diagnostic atomics (`diagSwapsByFallback`, `diagUnderrunBlocks`, etc.) log playback health via `dumpDiagnostics()`. ~half the complexity. |
| `RemoteUserStrip.{h,cpp}` | Card per remote user: 22 px username header, then channel rows arranged horizontally. `getPreferredWidth()` = 8 + n*90 + (n-1)*4. `updateOutputBusCount(n)` fans out to all child rows. Height is set by the remote viewport. |
| `RemoteChannelRow.{h,cpp}` | Vertical 90 px-wide strip per remote channel. Top-to-bottom: channel name label (22 px), pan rotary (44 px), single VU bar + vertical fader (flex), M+S row (22 px), output bus dropdown (22 px), R (Recv) button (22 px). Green/red colour coding on R; tooltips on all controls. `updateOutputBusCount(n)` repopulates dropdown. |
| `NinjamLookAndFeel.{h,cpp}` | `LookAndFeel_V4` subclass. Dark blue theme, teal accent (#00b4d8). Custom rotary, button, toggle, text editor outline. Disabled buttons get alpha * 0.5 automatically. |
| `Sha1.{h,cpp}` | Minimal first-party SHA1 (FIPS 180-1). Interface: constructor + `add(const void*, int)` + `result(void*)`. Used only for the double-hash challenge-response in `CLIENT_AUTH_USER`. Replaced WDL `sha1.{h,cpp}`. |
| `VorbisCodec.{h,cpp}` | First-party Ogg/Vorbis encode/decode wrapping direct libogg/libvorbis APIs. `VorbisDecoder`: `decode(data,len)`, `available()`, `pcm()`, `skip(n)`, `sampleRate()`, `numChannels()`. `VorbisEncoder`: constructed with `(sampleRate, numChannels, bitrateKbps, serialNumber)`, `encode(float*,n)`, `available()`, `data()`, `advance()`. Both use pimpl to keep libogg/libvorbis types out of headers. Replaced WDL `vorbisencdec.h`. |
| `NinjamProtocol.{h,cpp}` | Pure wire format: framing, message parsing, message building, `computeAuthHash`. No sockets, no threads, no state. All parsing goes through a bounds-checked `Reader` whose accessors fail rather than read past the payload -- `NinjamClient` owns dispatch only. Ninjam is little-endian throughout. |
| `IntervalClock.{h,cpp}` | Sample-exact beat/interval grid. Replaced a float phase accumulator whose wrap residual made the boundary walk a sample per interval, jittering every transmitted interval's length. `samplesPerInterval()` uses the reference client's arithmetic verbatim (`njclient.cpp:806`, truncated) so our boundaries align with other clients; beat offsets are rounded, since they only drive the local click. `advance()` emits events and never allocates. |
| `MetronomeVoice.{h,cpp}` | One-shot click, split out so its pitch is testable. 880/660/440 Hz by downbeat/bar/beat, `phaseInc = 2*pi*freq/sampleRate`. |
| `test/` | `juce::UnitTest` console app (`NinjamTests`) plus `FakeNinjamServer`, the in-process loopback. Production sources are re-listed in `test/CMakeLists.txt` rather than shared -- `juce_generate_juce_header` only works on `juce_add_*` targets, and each target needs its own `JuceHeader.h`. `PluginProcessor.cpp` and the UI are excluded (they need `JucePlugin_*` defines). |
| `utils/` | Remaining vendored WDL headers: `heapbuf`, `queue`, `wdlstring`, etc. `sha1.{h,cpp}` and `vorbisencdec.h` are no longer used by our code. Treat as near-third-party; edit only if necessary. |

---

## Runtime architecture

**Threads:**

| Thread | What it does |
|---|---|
| Audio (`processBlock`) | Host sync read, metronome click synthesis, interval boundary detection, local audio capture into `captureBuffer`, mixing decoded remote streams into output via `getDecodedAudio`. At the interval boundary, fires `callAsync` to hand off the captured block. |
| `NinjamClient` (`run()`) | Socket read loop, keep-alive every 3 s, `handleMessage` dispatch. All listener callbacks are bounced to the message thread via `callAsync`. |
| Message thread | `onConnected`, `onServerConfig`, `onChatMessage`, etc. Also runs `processCapturedAudio` (encoding) — intentionally off the audio thread. |
| UI timer (30 Hz) | `timerCallback`: syncs remote user strips to `getRemoteUsers()`, calls `repaint()`. |

**Shared state:** protected by `juce::CriticalSection` — `downloadMutex` (covers `guidToInterval`, `channelStreams`, `remoteUsers`), `chatMutex`, `txFileMutex`, `rxFileMutex`.

**Interval playback model** (`NinjamClient`): each `(username, channelIndex)` pair has a `ChannelStream` holding a `deque<shared_ptr<DecodedInterval>>`. The network thread decodes WRITE chunks into the matching `DecodedInterval` (looked up by GUID in `guidToInterval`), incrementing `writePos` atomically. The audio thread reads `[readPos, writePos.load())` from `stream.current`. At each metronome "1", `swapIntervalBuffers()` pops the queue front into `current` — this realises the one-interval playback delay. `PendingDownload` (in `guidToInterval`) is erased when `flags & 1` arrives, marking `finalReceived = true`.

**Interval detection** (`PluginProcessor::processBlock`): uses `internalBpm`/`internalBpi`, not the host's. Server BPM/BPI updates overwrite these in `onServerConfig`. Host BPM/PPQ is read only for display and the mismatch warning.

---

## Protocol crib sheet

All message framing: 1-byte type + 4-byte little-endian payload length + payload.

| Msg | Direction | Hex | Notes |
|---|---|---|---|
| SERVER_AUTH_CHALLENGE | S→C | `0x00` | 8-byte challenge + more; we read first 8 |
| SERVER_AUTH_REPLY | S→C | `0x01` | 1-byte flag: 1 = granted, 0 = denied |
| SERVER_CONFIG_CHANGE | S→C | `0x02` | 2-byte BPM + 2-byte BPI, **little-endian** uint16 each (`references/ninjam/ninjam/mpb.cpp:192-195`) |
| USER_INFO_CHANGE | S→C | `0x03` | List of: active(u8), chIdx(u8), vol(i16-**le**), pan(i8), flags(u8), username(NUL), chanName(NUL). Fixed part is 6 bytes, not 4. |
| DOWNLOAD_INTERVAL_BEGIN | S→C | `0x04` | 16-byte GUID + 4-byte estSize + 4-byte fourCC + 1-byte chIdx + NUL-term username. fourCC `OGGv` = audio; `JTBv` = Jamtaba video (skip). |
| DOWNLOAD_INTERVAL_WRITE | S→C | `0x05` | 16-byte GUID + 1-byte flags (1 = final) + Ogg payload |
| CHAT_MESSAGE | S↔C | `0xC0` | 5 NUL-terminated strings: type, then type-specific params. Types: MSG, PRIVMSG, TOPIC, JOIN, PART, ADMIN |
| KEEP_ALIVE | C→S | `0xFD` | Zero-byte payload; send every 3 s |
| CLIENT_AUTH_USER | C→S | `0x80` | 20-byte SHA1 + NUL-term username + 4-byte caps (LE) + 4-byte version (LE). Hash = `SHA1(SHA1(user+":"+pass) + challenge[0..8])`. Caps = 1, version = `0x00020000`. |
| CLIENT_SET_USERMASK | C→S | `0x81` | List of: NUL-term username + 32-bit channel bitmask (LE). Bit N = subscribe to channel N. Sent on every USER_INFO_CHANGE to subscribe all channels; also the mechanism for the per-channel Recv toggle. |
| CLIENT_SET_CHANNEL_INFO | C→S | `0x82` | 2-byte LE mpisize (= 4) + per channel: NUL-term name + 2-byte LE volume (0 = 0dB) + 1-byte pan (-128..127) + 1-byte flags. Server reads mpisize bytes of metadata after each name; broadcasts USER_INFO_CHANGE to all clients. |
| UPLOAD_INTERVAL_BEGIN | C→S | `0x83` | Exactly 25 bytes: 16-byte GUID + 4-byte estsize LE + 4-byte fourcc LE (OGGv = bytes 'O','G','G','v') + 1-byte chidx. No channel name string. |
| UPLOAD_INTERVAL_WRITE | C→S | `0x84` | 16-byte GUID + 1-byte flags (1 = final) + Ogg chunk |

**Reference**: `references/ninjam/ninjam/mpb.h` for message constants, `server/usercon.cpp` for server-side behaviour, `njclient.cpp` for client-side reference flows.

---

## Prioritised work

Legend — **Complexity**: S = hours (self-contained), M = 1–3 days, L = 3–7 days, XL = weeks.  
**UX impact**: High = major daily improvement; Medium = noticeable; Low = polish.

**Completed:** #1 SET_USERMASK, #2 OGGv filter, #3 metronome default, #4 password/port UI, #5 tempo sync UX, #6 server browser, #7 channel info names, #8 dynamic local channels, #9 per-channel xmit toggle, #10 monitor/transmit gain split, #11 vertical strip UI redesign, #12 VU meters (local strips + remote strips), #13 multi-channel remote strips, #14 per-channel Recv toggle, #15 state persistence, #16 per-channel bus routing (input + output), #17 per-channel ring buffers, #18 SHA1 replacement (first-party), #19 VorbisCodec replacement (direct libogg/libvorbis), #20 playback phase-lock fix (removed signal-driven swaps; local metronome is sole authority), #21 interval crossfade (256-sample fade at swap boundary), #22 elastic channel panel layout (40/60 proportional, left/right justified), #23 UI polish -- compact toolbar groups, metronome volume slider, hover fix, button enable/disable states, connection-state header tint, phase bar state colour, chat ghost + clear-on-reconnect.

**Test infrastructure and the bugs it found (see `test/README.md`):** three-layer
suite -- unit tests, in-process loopback, local-server archive rig. Fixed along
the way, each with a test that fails without the fix:

- **Encoder sample rate was hardcoded to 48000** regardless of the session rate. At 44.1 kHz we transmitted 8.8% sharp and 8% short; at 96 kHz, 150% long and an octave down. (`NinjamClient.cpp` `processCapturedAudio`; the debug WAV dumps had the same bug.)
- **Five unbounded string reads past the end of the payload** in the `0x03` and `0xC0` parsers, including a 6-vs-4-byte off-by-two. `MemoryBlock` is sized to exactly the payload and is not NUL-padded, so these walked the heap. Reproduced as a heap-buffer-overflow under ASan; now impossible by construction.
- **Interval length jittered by a sample** every interval, because the float phase accumulator wrapped by subtraction. Now an integer grid matching the reference client.
- **Metronome click sounded at a third of its nominal pitch** (880 Hz came out near 293) -- the old formula swept `2*pi*freq/bpm` radians over a `3/bpm`-second click and never referenced the sample rate.
- **Reconnecting left the previous session's users and channel streams in place**, so a rejoined session showed phantom users whose orphaned streams were swapped silently forever.
- **Jamtaba `JTBv` video intervals were queued as audio**, creating a permanently silent channel stream that swapped every interval. Now filtered on fourCC.
- `CLAUDE.md` claimed `0x02`/`0x03` integers were big-endian; they are little-endian (`mpb.cpp:192-195`). The code was right, the doc was wrong.

Characterised, not bugs: Ogg only emits a page every ~4 kB, so a short or tonal
interval produces **no** decodable audio until the end-of-stream flush. Interval
delivery is therefore all-or-nothing, and a receiver cannot start playing an
interval early just because some WRITE chunks arrived. Pinned by a test.

| # | Item | Type | Complexity | UX Impact | Key notes |
| 24 | Lock-free TX handoff | Architecture | M | Low | `processBlock` does `callAsync` with a full buffer copy at each interval boundary. Replace with a `juce::AbstractFifo` FIFO to eliminate the copy and reduce TX latency jitter. (RX path already lock-light.) Much safer to attempt now the loopback tests exist. |
| 27 | Capture alignment at the interval boundary | Correctness | M | Medium | **Measured, confirmed real.** The ring buffer is drained at the boundary but filled in whole blocks, so a transmitted interval is cut on a block edge rather than the exact sample. Live measurement against the reference client (137 bpm / 16 bpi / 48 kHz, 6 intervals, 96 notes from a DAW arpeggiator): note spacings **within** an interval average -7.4 samples of error, but every one of the 5 interval **seams** was long, mean **+54.5 samples (+1.29 ms), sd 24**. All five the same sign. Under one buffer, as the mechanism predicts. Not audible as a flam (a human confirmed our bursts land on the beat by ear) but it is a real defect. Fixing it means draining the ring to an exact sample count at the boundary rather than in whole blocks. |
| 28 | Audio-thread hygiene | Correctness | S | Low | `getDecodedAudio` takes `downloadMutex` on the audio thread; `setSaveTx/Rx` are called from `processBlock` every block and do file I/O on the toggling call. |
| 29 | Destructor race in `NinjamClient` | Correctness | S | Low | `~NinjamClient` closes the socket while `run()` may be blocked in `read()`. Currently only visible as test-teardown flakiness. |
| 30 | Underrun tail | Correctness | S | Low | `getDecodedAudio` leaves the rest of the block silent when the decoded interval runs short, rather than holding or fading. |
| 25 | Video support | Future | XL | High | Jamtaba-proprietary extension only. Not planned; see Video section below. |
| 26 | OSC tempo sync | Future | M | Medium | Send `/tempo/raw {bpm}` OSC to localhost when server BPM changes (so DAW can auto-adjust). Reference: `abNinjam`. Not planned. |

---

## UI/UX specification

The current implementation matches this spec. Use it as the reference for future changes.

### Overall layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│  [Status bar: server BPM | BPI | phase progress | host BPM | sync state] │
├───────────────┬──────────────────────────────────────────────┬───────────┤
│ Local input   │  Remote players (horizontally scrollable)    │  Chat     │
│ channel       │                                              │  panel    │
│ strips        │  [Player A]   [Player B]   [Player C] …     │           │
│ (vertical,    │   vert.strip   vert.strip   vert.strip       │ (default  │
│  left panel)  │   vol/pan/vu   vol/pan/vu   vol/pan/vu       │  visible, │
│               │   M  S  Recv   M  S  Recv   M  S  Recv       │  toggle-  │
│               │                                              │  able)    │
├───────────────┴──────────────────────────────────────────────┴───────────┤
│  [Metronome toggle + vol]  [+ Add channel]  [Server browser / Connect]   │
└──────────────────────────────────────────────────────────────────────────┘
```

Three columns: local input strips (left), remote player strips (centre, horizontal scroll), chat (right, collapsible). Full-width status bar at top. Toolbar at bottom.

The channel panel (everything between the chat column and the left edge) uses an **elastic 40/60 split** with a 200 px floor on each side. Local strips are left-flush within their allocation; remote strips are right-flush within theirs. When neither side fills its allocation the unused space appears as a gap between them. When one side overflows its hard allocation but the other has slack, it borrows that slack; when both overflow each gets exactly its hard allocation and scrollbars appear. `relayoutChannelArea()` in the editor recalculates this every 30 Hz tick.

### Local input channel strips (left panel)

Each channel strip:
- **Editable channel name** — protocol-level; transmitted to the server in `CLIENT_SET_CHANNEL_INFO`; other players see it.
- **Mono/Stereo toggle** — toggles mono summing of the assigned input bus; does not add or remove a bus.
- **Vertical VU meter** — live input level.
- **Vertical volume fader** — scales both local monitor and transmitted audio (see next point).
- **Pan knob**.
- **Monitor Mute / Monitor Solo** — affect only what you hear of your own audio in the plugin output; do not affect what is encoded and sent.
- **Xmit toggle** — whether this channel's audio is sent to the server. Mid-interval toggle: sends silence for the remainder of the current interval, then stops at the next boundary.
- **Input bus dropdown** — selects which plugin input bus feeds this channel. Multiple channels can share a bus. Defaults to bus 1.
- **Remove button** — deletes the channel strip (does not remove the input bus).

Toolbar buttons manage channels and buses independently, grouped as compact labelled groups:
- **Channel: [+]** — adds a new local channel strip (defaults to input bus 1).
- **Input bus: [+] [-]** — adds or removes a plugin input bus. Removing reroutes any channel using that bus to bus 1. The [-] button is disabled when only one input bus exists.

Plugin starts with one channel and one input bus. **Local audio always passes through without delay**, regardless of connection state.

### Remote player strips (centre area)

One vertical card per player, scrollable horizontally. A player with multiple channels shows all their channels stacked inside the same card, under a shared username header.

Per channel within a card:
- **Channel name label** (as sent by the remote player).
- **Vertical VU meter** — post-fader decoded level.
- **Vertical volume fader**.
- **Pan knob**.
- **Mute / Solo** — independent of local input mute/solo. Solo silences all other remote channels.
- **Recv toggle** — sends `CLIENT_SET_USERMASK` (0x81) with the channel bit cleared; the server stops forwarding that user's audio entirely (bandwidth saving). Distinct from Mute: Mute receives+decodes but silences output; Recv-off prevents download.
- **Output bus dropdown** — routes this channel's decoded audio to a specific plugin output bus for DAW stem recording. Defaults to bus 1 (main mix). Multiple channels can share a bus. Routing is persisted by (username, channelIndex) and restored when the user rejoins.

Toolbar buttons manage output buses: **Output bus: [+] [-]** add or remove a stereo output bus. The [-] button is disabled when only one output bus exists. Removing the last bus used by a remote channel reroutes it to bus 1.

### Status bar and sync UX

Always visible at the top. Shows:
- Server BPM and BPI.
- Host DAW BPM (plugin mode) or "Standalone".
- Interval phase progress indicator (bar from 0 → BPI beats).
- Connection status: Disconnected / Connecting / Connected as user@host.

**Header background tint** reflects connection state:
- Connected, in sync: dark navy `0xff0d0d1a` (normal).
- Disconnected after a failed connect attempt, or BPM mismatch active: dim amber `0xff2a1a0a`.
- Idle / never connected: dark grey `0xff111111`.
The amber clears when the user clicks Connect again.

**Phase bar** advances only when connected. It is teal (`0xff00b4d8`) when connected, grey (`0xff444444`) when not. Beat tick marks and flash effects are suppressed when disconnected. Phase resets to 0 on disconnect.

**BPM mismatch**: when server BPM ≠ host BPM, show a prominent warning. Remote audio stops; local audio is not transmitted. The plugin cannot push tempo to the DAW (that requires OSC, which is deferred). The user must change the DAW tempo manually; the warning disappears automatically when they match.

**Pending state**: once BPM matches (warning gone), if the DAW transport is not running, prompt "Start transport to begin." When the transport starts, the plugin phase-locks to the DAW's "1" and enters normal transmit/receive mode.

**Standalone mode**: no DAW transport concept. Behaves as if transport is always running. BPM/BPI follow the server from the moment of connection. Playback and transmit begin automatically.

### Metronome

- Toggle button in toolbar.
- Default: **off** in plugin/DAW mode, **on** in standalone.
- **Volume slider** (horizontal, ~60 px) immediately right of the toggle. Range 0--1, persisted in state.
- Downbeat at 880 Hz, bar-start at 660 Hz, beat clicks at 440 Hz.
- Only active when connected; the per-sample click logic skips entirely when disconnected.

### Connection / server browser

A panel or popup (triggered from the toolbar) containing:
- **Server list**: static curated list of well-known public servers. Live room info (BPM, BPI, player count, player names) can optionally be fetched from `http://ninbot.com/app/servers.php` (Jamtaba's public JSON API). Show this info in the list before the user connects.
- **Private server**: free-form `host:port` text field.
- **Username** field.
- **Anonymous toggle**: when on, no password is sent. When off, shows a password field.
- **Connect / Disconnect** buttons.

### Chat panel

- Scrolling message history. Formatted: `<user> text`, `* user /me text`, `[PM] <user> text`, `*** server event`.
- Single-line input with hint text showing supported commands.
- Commands: `!vote bpm <n>`, `!vote bpi <n>`, `/me <text>`, `/topic <text>`, `/kick <user>`, `/msg <user> <text>`.
- Collapsible via a toggle button. Defaults to visible.
- **When disconnected**: both the history display and input are visually ghosted (near-black background `0xff0a0a0a`, very dark text `0xff2e2e2e`); input is disabled and shows "(not connected)" placeholder. Prior messages remain readable but dimmed.
- **On next successful connect**: history is cleared before new JOIN/TOPIC messages arrive; normal colours and input are restored.

### Toolbar button states

The Connect button is disabled while connected; Disconnect is disabled while disconnected. The `[-]` bus buttons are disabled when only one bus of that type exists. All other toolbar buttons are always enabled. `updateToolbarStates()` is called every 30 Hz tick (no-op when state is unchanged).

---

## Video support (future work — not planned)

Video is a **Jamtaba-proprietary extension** to the Ninjam protocol. ReaNINJAM and abNinjam do not support it; they ignore the extra channel data.

**How Jamtaba does it** (`references/JamTaba/src/Common/`):

- **Capture**: Qt's `QCamera` + `CameraFrameGrabber` (`QAbstractVideoSurface`) grabs webcam frames.
- **Encode**: FFMpeg (`AV_CODEC_ID_H264`), max 320×240 px, 64–400 kbps. Emits raw H.264 NAL data. (`video/FFMpegMuxer.cpp:172`)
- **Transport**: Sent as **channel index 1** (audio is always channel 0) using standard `UPLOAD_INTERVAL_BEGIN` (0x83) + `UPLOAD_INTERVAL_WRITE` (0x84), but with fourCC = `JTBv` instead of `OGGv`. The server routes `JTBv` intervals just like audio — it doesn't decode them. (`MainController.cpp:382–393`, `ClientMessages.cpp:459–471`)
- **Receive**: On `DOWNLOAD_INTERVAL_BEGIN` with fourCC ≠ `OGGv`, buffer the raw bytes. On interval complete (flags = 1), hand to FFMpeg for demux/H.264 decode. (`ninjam/client/Service.cpp:302–337`)
- **Display**: `VideoWidget` renders decoded frames below the remote user's audio channel strip.

**For us when this is tackled:**
- Camera capture: `juce_video` module (`juce::CameraDevice`) wraps AVCapture / DirectShow / V4L2. Add `juce_video` to `target_link_libraries` in `src/CMakeLists.txt`.
- Encode/decode: FFMpeg as an external dependency (significant). Alternative: libtheora (native OGG video, simpler but lower quality).
- Follow the `JTBv` fourCC and channel-index-1 convention for interoperability with Jamtaba users.
- Non-Jamtaba clients silently ignore `JTBv` intervals.
- Currently our code doesn't check fourCC at all (work item #2) — fix that before adding video.

---

## Reference implementations (`references/`)

Read-only submodules. Don't build or modify them. Each has a sibling `.md` at `references/<name>.md` — **read the summary first**, dip into source only for specifics.

| Submodule | Primary use |
|---|---|
| `ninjam/` | Authoritative protocol reference. `njclient.cpp`, `netmsg.cpp`, `mpb.cpp`, `server/usercon.cpp`. |
| `old_client/` | Context for our vendored WDL code. |
| `abNinjam/` | OSC tempo sync reference; headless properties-file approach. |
| `ninjam-next-plugin/` | Modern JUCE VST3/AU; wraps NJClient. Good for `AudioPlayHead` phase ring-buffer sync. |
| `JamTaba/` | Rich UI reference; only client with video support. Cautionary tale on Qt+plugin complexity. |
| `libninjam/`, `JamWide/` | Additional implementations, lower priority. |

---

## Submodules under `modules/`

Built into the project. Do not edit.
- `JUCE`
- `modules/ogg` + `modules/vorbis` — Ogg/Vorbis codec
- `modules/clap-juce-extensions` — CLAP plugin format support

---

## Style notes

- C++17. `juce::` types preferred over `std::` where both exist (`juce::String`, `juce::AudioBuffer`, `juce::CriticalSection`).
- 2-space indent, braces on same line, members lowerCamelCase.
- No comments except for non-obvious invariants or protocol workarounds. No narration.
- **ASCII only in source files** — no non-ASCII characters anywhere (comments, string literals, or identifiers). Use `--` for em dash, `->` for arrows. In string literals that appear in the UI, use plain ASCII equivalents. Reason: `juce::String(const char*, size_t)` asserts ASCII validity; non-ASCII source bytes also cause compiler warnings on some platforms.
