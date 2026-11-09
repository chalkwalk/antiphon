# Testing

Three layers, cheapest first. Layers 1 and 2 are fully automated and hermetic
(no network, no audio device, no server). Layer 3 needs a local server and, for
the cross-client comparison, a second client.

```
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
```

Or run the binary directly, optionally filtering by test-suite name:

```
./build/test/NinjamTests_artefacts/NinjamTests                 # everything
./build/test/NinjamTests_artefacts/NinjamTests IntervalClock   # one suite
./build/test/NinjamTests_artefacts/NinjamTests Loopback Audio  # several
```

Suites in `NinjamTests`: `AccessibilityAudit`, `AudioDeviceStartup`,
`AudioLoopback`, `ChatFormat`, `ClipsortLog`, `ClockModel`, `CueVoice`,
`GainRamp`, `GainUtils`, `HostControl`, `HostGrid`, `KeyTag`, `LeadLine`,
`LoopbackProtocol`, `MetronomeVoice`, `PlayheadGrid`, `PracticeRoom`,
`PracticeServer`, `RealServer`, `ReferenceFixtures`, `SharedContract`,
`Shortcuts`, `StemRender`, `SyncAnnouncements`, `TransmitSpans`.
`AccessibleNaming` runs in `AntiphonAudit` instead (below).

**The wire format, the codec and the clock are not here.** `Sha1`,
`VorbisCodec`, `NinjamProtocol` (with its truncation sweep), `IntervalClock`
and `ChannelMix` moved with their code to chalkwalk-ninjam, whose Catch2 suite
is `libs/ninjam/test/` and runs in our ctest as `chalkwalk_ninjam`. The same
goes for `chalkwalk_music`, `chalkwalk_dsp` and `chalkwalk_jambot`: every
submodule's suite runs here, so a submodule bump is verified rather than
assumed. Change those tests in the library, with `-DCHALKWALK_NINJAM_DIR=...`
(see `AGENTS.md`).

---

## Layer 1 -- unit tests

Pure logic, no I/O.

| Suite | What it pins |
|---|---|
| `MetronomeVoice` | Click pitch, and that it is independent of tempo and sample rate. |
| `AudioDeviceStartup` | The standalone device-open policy: that a finished probe beats the clock, that the budget expiring is a timeout, and that a wedged probe returning late cannot resurrect startup. |
| `ClockModel` | Synced or not: each row of the state table, the grid each state runs on, that a jump is flagged on exactly the update that enters Synced, and the invariants over every input combination (in the jam only when connected). |
| `PlayheadGrid` | Loop wraps are continuous time, other moves are jumps, a BPI change re-anchors, and the padding a jump needs. |
| `SyncAnnouncements` | Every sentence, word for word against the spec's table, and ASCII. |
| `CueVoice` | The DAW-stopped tone: pitch by measurement at 44.1 and 48 kHz, under -60 dB within 150 ms, silent after. |
| `GainUtils` | dB<->linear round-trip, the -inf floor, meter release as a rate in dB/s independent of block size. |
| `AccessibilityAudit` | The MISSING NAME / DUPLICATE NAME / NO DESCRIPTION rules, against a synthetic tree -- the real UI cannot be compiled into this target. |
| `ReferenceFixtures` | Our parsers and builders against wire captures taken from the reference client. |
| `GainRamp` | A ramp reaches its target and stays; 5 ms at the session rate; muting a full-scale tone never steps more than a hair. |
| `TransmitSpans` | Where transmit was on within an interval, and the rhythm of repeated toggles, applied with ramped edges. |
| `HostGrid` | The grid derived from the host's PPQ: no drift over a long take, every beat exactly once, beat 0 after an interval start. |
| `HostControl` | What TempoFollow does to the DAW: nothing at the room's tempo, the room's tempo once otherwise, never when off. |
| `ChatFormat` | Server notices, your own lines, actions, private messages, topic and joins rendered as distinct things. |
| `KeyTag` | The key tag in chat: only the tagged form, wherever it sits; a malformed tag yields no key, not a wrong one. |
| `ClipsortLog` | The session archive's manifest: clips belong to the interval above them, tempo per clip, bad lines counted and skipped. |
| `StemRender` | One clip into one interval: short leaves silence, long is truncated, mono feeds both sides. |
| `LeadLine` | Mode to brightness and scale, chord tones as pitch classes, and the lead's onsets against its figure. |
| `SharedContract` | The chalkwalk-dsp primitives as this repository relies on them (polyBLEP, Hermite). |

### Running the parser tests under a sanitiser

The truncation sweeps only have teeth under ASan -- without it, a read past the
end of a `MemoryBlock` usually succeeds silently. They live in chalkwalk-ninjam
now; a sanitiser build compiles the submodules with the same flags, so
`ctest --test-dir build-asan -R chalkwalk_ninjam` runs them under it.

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --target NinjamTests -j $(nproc)
./build-asan/test/NinjamTests_artefacts/Debug/NinjamTests
```

UBSan reports a handful of shifts inside libvorbis itself. Those are
pre-existing and benign; ignore anything not in `src/`.

## Layer 2 -- in-process loopback

`FakeNinjamServer` listens on `127.0.0.1` on an ephemeral port, performs the
real auth handshake against the real `NinjamClient` socket path, and echoes
uploads back as downloads. It needs no changes to production code -- the client
takes a host and port.

`LoopbackProtocol` covers the handshake (including verifying the SHA1 **on the
wire**), auth denial, config changes, usermask, chat both directions, the
keep-alive cadence, malformed-input survival, and reconnect hygiene.

`AudioLoopback` pushes real audio all the way round -- `processCapturedAudio` ->
socket -> echo -> `handleMessage` -> `swapIntervalBuffers` -> `getDecodedAudio`
-- at 44.1, 48 and 96 kHz, and checks pitch, level, interval length, the
one-interval delay, mixer controls, output-bus routing, and that an interval
running short fades out rather than cutting.

`PracticeServer` and `PracticeRoom` run the practice room for real: a loopback
server, and the band in it, driven over the same socket path a player uses.

Assertions are statistical (RMS, zero-crossing rate), never sample-by-sample:
Vorbis is lossy and has codec delay.

## The accessibility gate

`AntiphonAudit` is a separate console app, because `NinjamTests` cannot hold the
UI: `PluginProcessor` and the editor need the `JucePlugin_*` defines that only
`juce_add_plugin` supplies, and linking `juce_gui_basics` there would break the
headless run. So it links the plugin's own library and drives the genuine
editor. It needs no display.

```
./build/test/AntiphonAudit_artefacts/AntiphonAudit   # exit code = findings
ctest --test-dir build -R accessibility-audit
```

Five states, because rules alone are not enough -- the tree that ships is the one
that rots, and a surface nobody audits is a surface nobody checks:

| State | Why |
|---|---|
| default | The editor as it opens |
| two channels, extra buses | `DUPLICATE NAME` is container-scoped, so it has nothing to say until there are two strips |
| connect dialog | Its own top-level window, so walking the editor never reaches it. It went unaudited for its whole life |
| audio device trouble view | The standalone's recovery screen, in its own window. Found two unnamed dropdowns inside JUCE's own device picker |
| two remote players, three channels | Remote strips do not exist until somebody joins; driven by `FakeNinjamServer` |

`AccessibleNaming`'s unit tests ride along in this binary rather than in
`NinjamTests`, because they need `juce_gui_basics`, which the headless suite
deliberately does not link. A failure there fails the same gate.

**Add a state when you add a surface.** The audit also fails if a state examines
exactly what the previous one did -- a state that was never reached reports a
clean verdict that means nothing, and that is not hypothetical: "two channels"
silently audited the default tree until the message loop was pumped.

## Layer 3 -- real server

### Start a server

```
scripts/testserver.sh                  # builds out of tree on first run
scripts/testserver.sh --clean-archive  # wipe /tmp/njarchive first
```

The reference server is GPLv2 and is not vendored here. On first run the script
shallow-clones it to `/tmp/njsrc` at a pinned revision and builds it out of tree
in `/tmp/njbuild`; nothing it fetches lands in a tracked path. See
`docs/references/SOURCES.md`.

Config is `test/fixtures/testserver.cfg`. Three settings matter and are easy to
get wrong:

- `AnonymousUsers multi` -- without it, a second client from `127.0.0.1` is
  rejected, so you cannot run ours and a reference client side by side.
- `MaxChannels 32 32` -- the second number caps anonymous users. The stock
  sample config sets it to 2.
- `ServerLicense` is left unset. When set, the server waits for a licence
  agreement this client does not implement, and login hangs with no error.

### Automated check against it

```
NINJAM_TEST_SERVER=127.0.0.1:2049 NINJAM_ARCHIVE=/tmp/njarchive \
  ./build/test/NinjamTests_artefacts/NinjamTests RealServer
```

Skipped entirely when `NINJAM_TEST_SERVER` is unset, so the default suite stays
hermetic. It connects, uploads three intervals of test tone, and verifies the
server archived them.

**The archive has a 30-second warm-up.** The server only opens a session
directory on a periodic check that runs every 30 seconds, and only while an
authenticated user is connected (`ninjamsrv.cpp:1232-1243`). Anything uploaded
before that check fires is broadcast to other clients but never written to
disk. The test waits for the directory to appear; when doing this by hand, stay
connected for at least 30 seconds before the intervals you intend to measure.

`clipsort.log` is written with buffered stdio and is only flushed when the
session closes, so stop the server before reading the manifest.

### Measuring the archive

```
scripts/analyze_archive.py /tmp/njarchive
scripts/analyze_archive.py /tmp/njarchive --user njtest
```

Needs an Ogg decoder: `soundfile`, `oggdec`, or `ffmpeg`.

Layout the server produces: `<archive>/YYYYMMDD_HHMM.ninjam/<first-hex-char>/<32-hex-guid>.OGG`,
plus a `clipsort.log` manifest interleaving `interval <n> <bpm> <bpi>` and
`user <guid> "<username>" <chidx> "<channame>"`. The `user` lines between two
`interval` lines belong to that interval. Payload bytes are `fwrite`n verbatim
(`usercon.cpp:798`) -- the file is exactly the Ogg bitstream the client emitted.

Sample output:

```
  user                     ch          iv   rate ch   frames  vs exp     rms   peak  impulse
  njtest@127.0.0.1         testtone     0  48000  2   192000  100.0%  0.1786  0.986        0

  njtest@127.0.0.1: 3 clips, rate(s) [48000]
    impulse offset  min 0  max 0  spread 0 samples
    length vs expected  min 100.0%  max 100.0%
```

What to read:

- **rate** must equal the session sample rate. A mismatch means the encoder was
  handed the wrong rate and every listener will hear us detuned.
- **vs exp** must be ~100%. Short intervals mean the same thing.
- **impulse spread** is the headline number. It should be near zero and, above
  all, stable. A constant non-zero offset is a fixed latency worth
  characterising; an offset that drifts interval to interval is a clock bug.

## Layer 4 -- differential comparison against the official client

Parity with the reference client was established by a headless harness that
linked the official NINJAM client and drove both directions of the audio path:
pitch, level and sample-exact interval timing. That harness was GPLv2-linking
and temporary by design, and has been removed.

What it proved survives in two durable forms, and neither needs the harness:

- **`ReferenceFixtures`** replays wire captures the official client actually
  produced (`test/fixtures/reference/`) against our parsers and builders. Fast,
  hermetic, no server and no timing. Do not hand-edit those files -- their whole
  value is that no code of ours produced them.
- **`docs/PARITY.md`** records the measurements, with the method and the
  instrument calibration for each number.

The manual comparison below still works, and is how a new claim gets checked.

### Comparing against a known-good client

This is what the rig is for.

1. Start the server, wait ~30 s for a session to open.
2. Connect our Standalone, enable **Test Tone** in the toolbar, connect
   anonymously.
3. Connect Jamtaba or ReaNINJAM as a second user playing a distinguishable
   signal.
4. Run for ~10 intervals, then stop the server so the manifest flushes.
5. `scripts/analyze_archive.py /tmp/njarchive` and read our rows against
   theirs.
6. Reciprocally, confirm our client plays back the reference client's audio
   phase-locked, one interval late.

Do **not** expect byte-identical Ogg between different encoders -- compare
decoded PCM, declared rate, frame counts and impulse offsets.

`ninjam/cliplogcvt/` in the reference sources -- which `scripts/testserver.sh`
has already fetched to `/tmp/njsrc` -- builds with `make`, needs libogg and
libvorbis headers, and reassembles a user's channel to WAV with
`-concat -decode`, which is useful for listening to a whole session.

### What the automated RealServer test does and does not prove

It uploads exact interval-sized buffers straight to `processCapturedAudio`, so
it proves the encode, framing and upload path and that the server accepts and
stores our intervals. It bypasses the capture ring buffer, so it does **not**
measure the alignment of the audio-thread capture. That is what the Test Tone
toggle plus a real Standalone run measures.

Capture is split at the interval boundary, so a transmitted interval ends on the
exact sample rather than being rounded up to a multiple of the block size -- an
impulse offset here should be near zero and, above all, stable. The alignment
itself has since been measured differentially against the reference client; see
`docs/PARITY.md`, which is the durable record.
