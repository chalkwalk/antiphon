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

Suites: `Sha1`, `VorbisCodec`, `NinjamProtocol`, `IntervalClock`,
`MetronomeVoice`, `SyncState`, `AudioDeviceStartup`, `Shortcuts`, `ChannelMix`,
`GainUtils`,
`AccessibilityAudit`, `ReferenceFixtures`, `LoopbackProtocol`,
`AudioLoopback`, `RealServer`.

---

## Layer 1 -- unit tests

Pure logic, no I/O.

| Suite | What it pins |
|---|---|
| `Sha1` | FIPS 180-1 vectors; incremental `add` equals monolithic at every split point (the auth path depends on this). |
| `VorbisCodec` | Encoder honours its constructed sample rate; round-trip level and pitch; frame count; mono; truncated and garbage input. |
| `NinjamProtocol` | Message layouts byte for byte; little-endian fields; auth hash against goldens from an independent SHA1; **every parser fed every truncation of every message**. |
| `IntervalClock` | Interval length constant over 200 intervals; event positions independent of block size; exactly `bpi` beats per interval; no drift over an hour; tempo changes take effect at a boundary. |
| `MetronomeVoice` | Click pitch, and that it is independent of tempo and sample rate. |
| `AudioDeviceStartup` | The standalone device-open policy: that a finished probe beats the clock, that the budget expiring is a timeout, and that a wedged probe returning late cannot resurrect startup. |
| `SyncState` | The five-state DAW sync machine: that several transitions can land in one call, that a stop/start never re-phases without a request, that standalone runs straight to `Running`. |
| `ChannelMix` | Mono sums and halves rather than discarding a side; pan law; that mute and solo never reach the transmitted signal. |
| `GainUtils` | dB<->linear round-trip, the -inf floor, meter release as a rate in dB/s independent of block size. |
| `AccessibilityAudit` | The MISSING NAME / DUPLICATE NAME / NO DESCRIPTION rules, against a synthetic tree -- the real UI cannot be compiled into this target. |
| `ReferenceFixtures` | Our parsers and builders against wire captures taken from the reference client. |

### Running the parser tests under a sanitiser

The truncation sweeps only have teeth under ASan -- without it, a read past the
end of a `MemoryBlock` usually succeeds silently.

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
one-interval delay, mixer controls, and output-bus routing.

Assertions are statistical (RMS, zero-crossing rate), never sample-by-sample:
Vorbis is lossy and has codec delay.

## Layer 3 -- real server

### Start a server

```
scripts/testserver.sh                  # builds out of tree on first run
scripts/testserver.sh --clean-archive  # wipe /tmp/njarchive first
```

The build is deliberately out of tree (`/tmp/njbuild`): building in place drops
object files into the pinned `references/ninjam` submodule.

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

## Layer 4 -- differential tests against the official client (temporary)

`test/refclient/` holds a headless harness around the official NINJAM
reference client, and the interop tests that drive it. Both directions of the
audio path have been confirmed against it: pitch, level and sample-exact
interval timing. See `test/refclient/README.md`.

It is deliberately self-contained and **meant to be deleted** once the parity it
proves is captured as golden fixtures -- `rm -rf test/refclient`, or
`git filter-repo --path test/refclient --invert-paths` for the published
history. The build guards `add_subdirectory(refclient)` with an `EXISTS` check,
so its absence changes nothing else.

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

`references/ninjam/ninjam/cliplogcvt/` (build with `make`, needs libogg and
libvorbis headers) reassembles a user's channel to WAV with `-concat -decode`,
which is useful for listening to a whole session.

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
