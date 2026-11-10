# Completed work

The archive. Work areas move here from `ROADMAP.md` once every checkbox in them
is ticked, compressed to the intent, what actually shipped, and any load-bearing
decision made along the way. Withdrawn work is archived too, with the reason.

Git history is the full record -- `git log -p ROADMAP.md` and the blame view
recover any compressed detail.

Grouping mirrors `ROADMAP.md`.

---

## Legacy id map

Work was originally tracked as a flat numbered list (`#1`--`#30`) in the old
`CLAUDE.md`. Commit messages, `docs/PARITY.md` and source comments still cite
those numbers, so they are mapped here. **New work is named, not numbered**
(`ROADMAP.md`).

| Old | Name |
|---|---|
| #1 | SET_USERMASK subscription |
| #2 | OGGv fourCC filter |
| #3 | Metronome default off in plugin mode |
| #4 | Password and port UI |
| #5 | Tempo sync UX |
| #6 | Server browser |
| #7 | Channel info names |
| #8 | Dynamic local channels |
| #9 | Per-channel xmit toggle |
| #10 | Monitor/transmit gain split |
| #11 | Vertical strip UI redesign |
| #12 | VU meters |
| #13 | Multi-channel remote strips |
| #14 | Per-channel Recv toggle |
| #15 | State persistence |
| #16 | Per-channel bus routing |
| #17 | Per-channel ring buffers |
| #18 | First-party SHA1 |
| #19 | First-party VorbisCodec |
| #20 | Playback phase-lock fix |
| #21 | Interval crossfade |
| #22 | Elastic channel panel layout |
| #23 | UI polish pass |
| #24 | Lock-free TX handoff |
| #25 | Video support *(parked)* |
| #26 | OSC tempo sync *(parked)* |
| #27 | Capture alignment at the interval boundary *(**withdrawn** -- below)* |
| #28 | Audio-thread hygiene |
| #29 | Destructor race in NinjamClient *(as "Client shutdown race")* |
| #30 | Underrun tail |

---

## Protocol and networking

### Full client implementation

TCP connect, auth, keepalive and read loop in a `juce::Thread`; message dispatch
for auth challenge/reply, server config, user info, interval begin/write, chat
and keepalive; `CLIENT_SET_CHANNEL_INFO`, `CLIENT_SET_USERMASK` and the upload
path. Wire format lives in `NinjamProtocol.{h,cpp}`, kept pure -- no sockets, no
threads, no state -- so it can be tested with malformed input directly.
(#1, #7)

### OGGv fourCC filter

Jamtaba transmits H.264 video on channel 1 with fourCC `JTBv`, and the server
routes it opaquely alongside audio. We were queueing those intervals as audio,
creating a permanently silent channel stream that swapped every interval forever.
Now filtered: `OGGv` is decoded, everything else is ignored. This is what makes a
mixed room with Jamtaba users cost us nothing. (#2)

### First-party SHA1

Replaced WDL's `sha1.{h,cpp}` with a minimal FIPS 180-1 implementation
(`Sha1.{h,cpp}`), used only for the double-hash challenge-response in
`CLIENT_AUTH_USER`. Tested against published vectors, and -- because the auth
path depends on it -- with incremental `add` proven equal to the monolithic
result at every split point. (#18)

### First-party Ogg/Vorbis codec

Replaced WDL's `vorbisencdec.h` with `VorbisCodec.{h,cpp}`, wrapping libogg and
libvorbis directly behind a pimpl so their types stay out of the header. Removed
the last codec dependency on vendored third-party code. (#19)

### Client shutdown race

Shipped. The socket now has exactly one owner: the network thread creates it,
uses it and closes it, and `disconnectFromServer` signals rather than reaching
in.

- [x] `disconnectFromServer` no longer touches the socket. It used to call
      `close()` to interrupt a blocking read, while `run()` closed it as well on
      the way out -- both writing the socket's own members, since
      `StreamingSocket::close()` clears its `hostName`. A real race, not a false
      positive: the fd could in principle be reused between the two closes.
- [x] `readFull` waits with a timeout instead of blocking for a whole message.
      The blocking form only re-checked `threadShouldExit` between reads, so a
      peer that sent half a message and stalled held the thread until the socket
      gave up -- which is what made the external close look necessary in the
      first place. Signalling is now enough.
- [x] Confirmed under TSan: **0 warnings**, down from 29. Restoring the external
      close brings all 29 back, so the fix is the fix rather than a change of
      timing.

**The baseline is now zero, in both sanitisers.** Any TSan warning is a
regression, and so is any ASan error. The only remaining sanitiser output is
four UBSan lines from inside vendored libvorbis (`bitwise.c`, `floor1.c`,
`psy.c`), which is not ours and is documented as ignorable in `AGENTS.md`.

---

### Can one machine mix a room in the gap?

*Answered 2026-11-09: yes, with a wide margin. Carried on in ninjam-balcony.*

Asked here in August because this repository then owned the codec and the
channel mix the answer depended on: can a server that streams a room to
listeners decode every channel of an interval, mix it and encode one stream in
the gap at the interval boundary, where any overrun is permanent lag? Measured
over a real archive, on a development machine and on a small Arm host, with
rooms stacked up to 40-odd channels: the worst interval stayed well inside one
interval, and memory stayed flat in room size. No redesign.

That server is now **ninjam-balcony**, a listener bot in its own repository
(not yet published), built on chalkwalk-ninjam and chalkwalk-jambot rather than
on this tree. It carries the figures and their method, and measures its own
cost. The temporary `antiphon-stems --bench` mode that took the first number
has been removed: it timed a Vorbis encode the bot does not do, on a JUCE build
the bot does not use.

### A listener bot, as a companion repository

*Moved out 2026-11-08.* The idea (2026-11-06) of a bot that joins a room as one
user and serves the jam, with its chat, to listeners in a browser. Not part of
Antiphon: its design, plan and open work live in ninjam-balcony.

## Audio and timing

### Sample-exact interval clock

Replaced a float phase accumulator whose wrap residual walked the interval
boundary a sample per interval, jittering the length of every transmitted
interval. `IntervalClock` is now an integer grid that emits events with exact
in-block sample offsets and never allocates.

**The load-bearing decision:** `samplesPerInterval()` reproduces
`njclient.cpp:806` **verbatim, including its truncation**, so our boundaries line
up with every other client on the server. Rounding it "correctly" would desync us
by ~0.87 samples per interval at 137 bpm / 11 bpi. Beat offsets inside the
interval are rounded, since they only drive the local click.

### Playback phase-lock fix

Removed signal-driven interval swaps. The local metronome is now the sole
authority for when the playback queue advances; `intervalBeginSignal` is drained
and never acted on. Before this, network jitter yanked the interval clock
mid-interval and discarded seconds of un-played audio. (#20, now `PRINCIPLES §9`)

### Interval crossfade

256-sample crossfade at swap boundaries, masking the discontinuity from
un-played tail samples in the outgoing interval. (#21)

### Metronome click as a testable module

`MetronomeVoice` was split out so its pitch could be asserted. The previous
formula swept `2*pi*freq/bpm` radians over a `3/bpm`-second click and never
referenced the sample rate: the 880 Hz downbeat sounded near 293 Hz. Now
`phaseInc = 2*pi*freq/sampleRate`, with 880/660/440 by downbeat/bar/beat, and
the tests assert *measured* pitch, never the formula. (#3 covered the default-off
behaviour in plugin mode.)

### Sample-exact transmit capture

Capture is split at the interval boundary rather than flushed on whole blocks, so
a transmitted interval ends on the exact sample rather than being rounded up to a
multiple of the block size. (Commit `02a58b2`. See the withdrawn item below --
this change is correct on its own merits, but it fixed nothing measurable.)

### DAW sync via transport start

`SyncState`, a five-state machine (`Disconnected` -> `TempoMismatch` ->
`ReadyToSync` -> `WaitingForPlay` -> `Running`), pure and JUCE-free so its
transitions are directly testable.

**The load-bearing decision:** lock to the transport **start**, not the timeline.
The reference client integrates with REAPER's timeline and loop points; a generic
plugin cannot, and it would be the wrong model anyway -- in a session/clip view
the timeline advances but is musically meaningless. Sync is armed explicitly with
a button because re-phasing mid-jam truncates the interval being transmitted and
other players hear the glitch. (#5; `DESIGN.md` §5, `NON-GOALS.md` fence #6)

### Standalone detection at runtime

`JucePlugin_Build_Standalone` is a *project-level* flag meaning "Standalone is one
of the `FORMATS`", and it is 1 in the shared-code target the VST3 and CLAP link
against -- so it was true in every format. Branching on it had compiled the whole
DAW sync flow out of the plugin: `hasTransport` forced false, tempo never
compared, the state machine running on connect. Replaced with
`isStandaloneApp()` (a runtime `wrapperType` check), and a `no-build-standalone-
macro` ctest now fails the build if the macro returns to `src/`.

### Channel mix consolidation

`ChannelMix` gave the mono/pan/gain rules one home. They had been written out
three times -- capture path, monitor mix, peak meters -- and had drifted: "mono"
summed in none of them (it selected the left channel and discarded the right),
and the meters ignored the flag entirely. Mono now sums and halves, so a
correlated stereo source keeps its level; meters read post-mono, post-monitor
gain. (`PRINCIPLES §8`)

### Decibel level model

Faders and meters presented in dB rather than linear amplitude, sharing one scale
via `GainUtils` so a meter reading and a fader reading are comparable. -60 dB
floor (displayed "-inf"), +6 dB ceiling. Meter release specified as a rate in dB
per second, so it is independent of block size and sample rate. Every gain in the
audio model stays a linear multiplier -- only the presentation changed, so
persisted state was unaffected.

### Reference-client default gain

Remote channels default to 0.25 linear (-12.04 dB), matching the reference client
(`njclient.cpp:2948`, `:1967`) and confirmed by measurement -- the reference
client plays our tone back at 0.253 of what we sent. Unity would have made us
12 dB louder than everyone else in the room. The UI fader initialises from the
same constant so the two cannot drift.

### Underrun tail

When a decoded interval ran short, the mixer left the rest of the block silent:
a hard cut, the most audible way to fail. It now ramps from the last sample to
zero over 256 samples, carried across blocks and started even when the signal
ran out on a block boundary, and a late interval that resumes ramps back in.
"Short" and "late" are distinguished by `finalReceived` and documented in
`DESIGN.md` section 2.2; the mixer treats them alike until more audio arrives.
Tested in `AudioLoopbackTests` with a deliberately truncated interval at two
block alignments, each proven red first (a 0.50 and a 0.42 step against the
sine's own 0.029); TSan clean. The late-resume ramp has no automated test.

### Audio-thread hygiene

Two paths violate `PRINCIPLES §7` (the audio thread does not allocate, lock,
block or log). Both are known, both are small, and both are the kind of defect
that appears as an unreproducible dropout in someone else's DAW.

- [x] `getDecodedAudio` takes `downloadMutex` on the audio thread. The RX path is
      otherwise lock-light -- the network thread publishes progress with an
      atomic `writePos` -- so the lock is protecting the *container*, not the
      audio. Replace with a structure the audio thread can traverse without
      blocking.
      - [x] **The dangerous half is gone.** The network thread used to hold this
            lock across the whole Vorbis decode and resample of a WRITE chunk --
            milliseconds -- while the audio thread blocked on it every block.
            The lock now covers the map lookup only. Safe because
            `guidToInterval` is mutated solely by the network thread and the
            audio thread never reads it.
      - [x] Removed the data race that narrowing exposed: the decode called
            `AudioBuffer::getWritePointer`, which writes `isClear`, while the
            audio thread's `addFrom` read it. The lock had been hiding it.
            Write pointers are now taken once before the interval is shared.
            Confirmed with TSan: the warning disappeared, and the only races
            left are the known shutdown one below.
      - [x] **The lock is gone.** `channelStreams` (a `std::map` walked under
            the lock) is now `streamSlots`, a fixed array of 64 slots that are
            never created or destroyed while the client lives, so the audio
            thread walks all of them without blocking and can never see a
            half-built or freed entry.

            `SpscRing` (`src/SpscRing.h`) carries ownership in a circle so that
            freeing never lands on the audio thread:

              ready:   network -> audio   "here is a decoded interval"
              retired: audio -> network   "done with this one, free it"

            The retire direction is the load-bearing half -- dropping the last
            reference to a `DecodedInterval` frees a multi-megabyte buffer, and
            that must not happen inside the callback. `retired` is sized above
            `ready.capacity() + 2` (the most the audio thread can hold, being
            `current` and `fadeOut`), so handing an interval back can never
            fail and the audio thread is never stuck holding one.

            State is split by owner. Playback cursors are audio-thread only;
            volume, pan, mute, solo and output bus are atomics the UI writes and
            the audio thread reads; `peakLevel` is an atomic the audio thread
            writes and the UI reads -- it had been a plain float shared between
            them, a race in every build that ever ran. `remoteUsers` stays a
            locked map for the UI, under `usersMutex`, which the audio thread
            never takes. The audio thread never touches a `juce::String`.

            Slot lifecycle is Free -> Live -> Draining -> Free. The network
            thread claims and marks draining; only the audio thread publishes
            kFree, because only it knows when it has let go. On disconnect the
            slots are marked draining rather than freed, since the audio thread
            may be mid-block inside them.

            Verified: full suite green, and TSan reports zero races in the RX
            path over 81 864 assertions -- the only warnings left are the known
            shutdown race below. `AudioLoopbackTests` cycles a channel out and
            back 80 times, more than there are slots, so it fails unless
            released slots are genuinely reclaimed; confirmed by breaking the
            hand-back and watching it go red.
- [x] `setSaveTx` / `setSaveRx` are called from `processBlock` on **every block**
      and do file I/O on the toggling call. Move the toggle handling off the
      audio thread entirely; the audio thread should only ever see a flag.
      Now applied from the message thread when the toggle changes
      (`applyDebugCaptureSettings`); `processBlock` no longer mentions them.
- [x] **Re-audit of `processBlock`, 2026-08-08.** Four findings; three fixed
      here, one deferred to a tracked work area. The result, so the list stays
      authoritative:

      - [x] **`localChannelMutex` was taken three times per block** -- the
            monitor mix, `captureInputRange` and `fireCaptureLambdas`. This was
            worse than the RX lock, because of what held the other end: the
            editor's 30 Hz timer holds it while *constructing channel strips*,
            `addLocalChannel` held it across a 30-second ring allocation (about
            11 MB at 96 kHz), and `setStateInformation` holds it while parsing
            XML. The audio thread could block on any of those.

            Fixed the same way as the RX path. `localChannels` stays a locked
            `std::vector` for the UI; the audio thread walks `audioChannels`, a
            fixed array of raw pointers published by `publishLocalChannels()`
            with a release store on the count. Safe without a lock because a
            `LocalChannel` is never destroyed while the processor lives:
            removing one moves it to `spareLocalChannels` and shrinks the count,
            and a later add takes it back out. A recycled channel never resizes
            its own ring -- only `prepareToPlay` does, which the host does not
            run concurrently with `processBlock`.
      - [x] **`getDecodedAudio` took `rxFileMutex` on every block**, even with
            Save Rx off, and `isSavingRx` was a plain `bool` written by the
            message thread while the audio thread read it. Now an atomic checked
            before the lock is considered, so the normal path takes no lock at
            all. The file write itself stays on the audio thread when the toggle
            is on: writing the mix as the audio thread sees it is the whole
            point of the toggle, and it is never on in ordinary use.
      - [x] **`clockEvents` and `captureSegments` cannot outgrow their
            reserves.** Checked rather than assumed: `clockEvents` reserves at
            least 64 events, and exceeding it needs 63 beats inside one block --
            about 1.5 million samples at 120 bpm and 48 kHz. `captureSegments`
            reserves 8 against one segment per interval boundary crossed. Both
            are safe by orders of magnitude, and no change was made.
      - [x] **`MessageManager::callAsync` still fires from the audio thread** at
            each interval boundary in `fireCaptureLambdas`. *(Done 2026-11-09:
            see Lock-free TX handoff, below.)* It is a `new`, a
            lock, an array append and a `write()` syscall
            (`juce_Messaging_linux.cpp:72`). Once per interval rather than per
            block, and removing it means the redesign already tracked below as
            *Lock-free TX handoff* -- so it is recorded there, not fixed here.
            The lock around it is gone; the post is not.

### Lock-free TX handoff

*2026-11-09.* The last `PRINCIPLES 7` violation on the audio thread. At each
interval boundary `processBlock` posted one `callAsync` per channel to copy the
interval out of the channel's ring: a `new`, the message queue's lock, an array
append and, on Linux, a `write()` to a socket (`juce_Messaging_linux.cpp:72`).

Now the boundary pushes a small record per channel -- channel, length, which
transmit spans, ramp, mono -- onto a fixed `HandoffQueue` (`src/HandoffQueue.h`,
over `juce::AbstractFifo`), and the processor's existing 30 Hz timer drains it
and does the same copy, spans and `enqueueCapturedAudio` the lambda did.

**Measured, development machine, 4 000 calls from a high-priority thread, three
runs:** `callAsync` a median 2.25 us, 99th percentile 7-8 us, worst 17-43 us;
the push a median 0.11 us, 99th 0.4 us, worst 1.3-12.5 us (the worst of both
is the scheduler as much as the call). Twenty times cheaper at the median, and
nothing in it can block.

**What it costs:** up to 33 ms between the boundary and the copy. The upload
is already spread over half the interval, so that comes out of the other half,
which at any interval longer than a fraction of a second is nothing. Whether an
interval is transmitted is still decided at the boundary, not at the drain.

**Verified on the wire,** standalone against a local reference `ninjamsrv`
archiving what it received, Test Tone on, 120 bpm / 8 bpi at 44.1 kHz: 21
intervals, every one 176 400 samples (100.0% of the interval), and the
three-quarter probe burst at the same offset in every one, spread 0 samples,
where the probe puts it (peak-picked with `scripts/analyze_archive.py`, not the
calibrated correlation in `docs/PARITY.md`). `test/HandoffQueueTests.cpp` covers
order, capacity, dropping and a two-thread run.

If the queue fills -- four boundaries of every channel unread, which takes a
message thread stalled for several intervals -- the record is dropped and
counted, and the drain never reads more than the ring holds.

### Transmit gating is per interval, not per sample

`xmitEnabled` is read once, at the interval boundary, while `captureInputRange`
fills the ring unconditionally. So the flag decides all-or-nothing for a whole
interval, and toggling within one has no effect except through its final state:

- ends **on** at the boundary: the *entire* interval is sent, including every
  stretch where TX was off -- audio you may never have meant to share;
- ends **off**: the whole interval is dropped, including the part recorded
  while TX was on.

Agreed behaviour: TX gates the audio, the interval boundary gates the
transmission. Anything TX was off for becomes silence, the interval keeps its
exact length, and an interval with TX off throughout sends nothing at all --
which is what happens today and is what the reference client does for a
non-broadcasting channel. The protocol has no objection: an interval is just an
Ogg stream of N samples, and Vorbis encodes silence almost for free.

- [x] `ChannelMix::write` takes a `transmitting` flag and writes silence rather
      than input when it is false. The gate went there, not into
      `captureInputRange`, because `PluginProcessor` cannot be compiled into the
      test target -- logic left in it is untestable by construction.
- [x] An interval is sent when transmit was on for *any* part of it, tracked by
      `LocalChannel::txActiveThisInterval`. Reading `xmitEnabled` at the
      boundary instead would discard an interval you played most of and then
      switched off during. An interval you were silent for throughout still
      sends nothing, which is what the reference client does for a channel that
      is not broadcasting.
- [x] Tested in `ChannelMixTests.cpp`, block by block with the flag changing --
      the same shape as `processBlock` across an interval. Proven to have teeth
      by ignoring the gate and watching 28 assertions go red.

---

## Mixing, routing and channels

### Dynamic local channels

Each `LocalChannel` carries a name, mono/stereo, volume, pan, monitor mute/solo,
an xmit toggle, VU peaks, an explicit `inputBusIndex` and an `AbstractFifo` ring
buffer. Channel count and input bus count are managed independently. (#8, #17)

### Monitor and transmit as independent gain stages

Volume and pan apply to both; mute and solo affect **only** the local headphone
mix; the xmit toggle gates only what is sent to the server. This is the
distinction that makes it safe to mute yourself while still playing to the room.
(#9, #10)

### Per-channel bus routing

Each local channel names an input bus; each remote channel names an output bus,
so the DAW records the jam as stems. Bus counts are dynamic; removing a bus
reroutes anything using it to bus 1. Remote routing is persisted by
`(username, channelIndex)` and reapplied when a player rejoins -- a dropout must
not scatter your stems. (#16)

### Per-channel Recv toggle

Clearing a channel's bit in `CLIENT_SET_USERMASK` makes the server stop
forwarding it entirely. Deliberately distinct from Mute, which receives and
decodes but silences output: Recv is a bandwidth control, Mute is a mix control.
(#14, #1)

### Multi-channel remote players

A player transmitting several channels shows them all under one username header,
each with its own fader, pan, mute, solo, Recv and output bus. (#13)

---

## User interface

### Synced or not (2026-11-06)

A screen reader user joined a room whose tempo his DAW did not match and was
told to vote for his own tempo; when the room changed tempo he thought it had
stopped. Both were the same fault: the old `SyncState` had five in-between
states, an armed Sync button and a transport-start edge, and in most of them
a player heard nothing and was told nothing useful. Design:
`docs/superpowers/specs/2026-11-06-sync-and-tempo-design.md`.

- **`ClockModel`** replaces `SyncState` and `RunGate`: synced or not, never
  between. Not synced keeps the room playing on Antiphon's clock.
- **`PlayheadGrid`**: synced, the interval position comes from the playhead,
  through loop wraps; a jump pads the interval and seeks playback.
- **`HostControl` / `ReaperBridge`**: Reaper follows the room's tempo, and Sync
  now can press play and set the loop. No other DAW is touched.
- **`SyncAnnouncements`**: one sentence per event, held to the spec's table.
- **The Clock group, Play, Ctrl+Alt+S/I/F/G**, and the "Your DAW is at N BPM --
  Propose" chip removed.
- **`CueVoice`**: a tone once an interval while the DAW is stopped under a
  synced jam.
- **Chat keeps its keys**: focus stays after sending, is drawn, and its loss to
  the DAW is said.

### Vertical strip redesign and elastic layout

Vertical 90 px channel strips with rotary pan, vertical fader and flanking VU
bars, replacing the original horizontal rows. The channel panel uses an elastic
40/60 split with a 200 px floor per side; local strips are left-flush, remote
strips right-flush, so unused space appears as a gap rather than a ragged edge,
and each side borrows the other's slack before scrollbars appear. (#11, #12, #22)

### Server browser

Popup dialog with a live room list fetched from ninbot.com (BPM, BPI, player
count), a private-server host/port field, and username/password/anonymous entry.
Launched as a real `DialogWindow` rather than a child overlay -- see the keyboard
focus entry below. (#4, #6)

### Chat

Receive and display, send `MSG` / `ADMIN` / `PRIVMSG`, voting commands.
Collapsible, with visibility persisted. Ghosted (disabled, dimmed, "(not
connected)" placeholder) when disconnected, and cleared on the next successful
connect so a new session does not open with the last one's backlog.

### State legibility pass

Header background tints navy/grey/amber by connection state; the phase bar runs
teal when connected and grey and still when not; toolbar buttons enable and
disable by connection state and bus count rather than failing silently; TX and
Recv are green/red coded; tooltips on every control. Compact toolbar groups
(`Channel:[+]`, `Input bus:[+][-]`, `Output bus:[+][-]`) and a metronome volume
slider. (#23; now `PRINCIPLES §12`)

### Embedded typeface

Inter embedded as two static cuts (Regular and Bold) via
`juce_add_binary_data`, installed as the LookAndFeel's default. A
platform-default sans varies by distro and machine -- a look the product never
chose. The variable font is deliberately not vendored: a face JUCE must instance
per size reintroduces exactly the variability the embed exists to remove.
Licence obligations in `THIRDPARTY.md`.

### Keyboard focus inside a plugin window

Two problems, two fixes.

`ServerBrowserDialog` became a real `DialogWindow`. An embedded plugin view can
never hold the X input focus -- JUCE decides focus with
`isParentWindowOf(ourWindow, focusedWindow)` and the host's window is our
*ancestor*, not our descendant -- so `takeKeyboardFocus()` bails out and no text
field inside the editor can be typed into. A desktop window has its own peer.

For the inline case (the chat field), JUCE itself is patched
(`patches/juce-linux-plugin-keyboard-focus.patch`): `takeKeyboardFocus()` asks the
peer for focus and the X `FocusIn` lands afterwards, by which time
`handleFocusGain()` has no record of the intended target and focuses the
*top-level* component instead. Symptom: caret and selection appear, typing does
nothing; invisible in the standalone, where the peer already holds focus. The
patch records the target before the grab. Verified live in a DAW by typing a
chat reply (`docs/PARITY.md`).

`EDITOR_WANTS_KEYBOARD_FOCUS TRUE` was the other half -- it defaults to FALSE,
leaving hosts free to keep the keyboard.

### State persistence

Host, credentials, channel layout (including per-channel input bus), mixer
positions, remote output bus routing, metronome state and volume, chat
visibility. Passwords XOR-obfuscated so they are not plain text in a DAW project
file -- obfuscation, not encryption, and not represented as more. (#15)

---

## Accessibility

### Annotation pass

Every control that takes keyboard focus given a name and a description. The
single-glyph buttons mattered most: `M`, `S`, `TX`, `R`, `+` and `-` previously
announced as those literal characters. Faders and pans speak values with units
("-12.0 dB", "left 20", "centre"). Channel strips and remote players became focus
containers with their own names, so a reader navigates player by player and hears
"Instrument, Mute" instead of forty flat controls with repeating names.

### The status readout

`StatusReadout` -- the header's information is painted as `Graphics` calls and
therefore does not exist for a reader at all. The component draws nothing and
occupies the same area, purely so that information has a place in the
accessibility tree. First in tab order, because "where am I and is it working" is
the first question.

### Announcements

`Announcer` speaks discrete events only -- connect, disconnect, sync change,
tempo and BPI change, votes, joins and parts -- with three verbosity levels
(Important by default, chat at All only), identical-message suppression and a
minimum gap. Continuous values are deliberately never announced: meters and
interval position change tens of times a second and would make the app less
usable, not more.

### The mechanical audit

`AccessibilityAudit` walks the component tree and reports MISSING NAME, DUPLICATE
NAME (within a container) and NO DESCRIPTION; `Ctrl+Alt+A` writes a report to the
desktop. It exists because JUCE has no AT-SPI backend on Linux, so a
reader-based check there would pass *vacuously* while the annotations rotted. The
rules live apart from the tree adapter so they can be unit-tested headlessly, in
a target that does not link `juce_gui_basics`.

---

## Testing infrastructure

### The three hermetic layers

Unit tests over the JUCE-free modules and the parsers; an in-process
`FakeNinjamServer` on `127.0.0.1` exercising the real socket path and handshake
with no production changes; and an opt-in rig against a local `ninjamsrv` whose
session archive is measured by `scripts/analyze_archive.py`. Operational guide in
`test/README.md`.

### The differential rig

The official `NJClient` driven headless, which was possible at all because it is
a library rather than an app and takes an explicit sample rate. Deliberately
self-contained, GPLv2-linking, and always meant to be deleted before release --
which it since has been. Everything it established is recorded in
`docs/PARITY.md`, and the wire captures it took are replayed hermetically by the
`ReferenceFixtures` suite.

### Bugs the suite found

Each now has a test that fails without its fix.

- **Encoder sample rate hardcoded to 48000** regardless of session rate. At
  44.1 kHz we transmitted 8.8% sharp and 8% short; at 96 kHz, 150% long and an
  octave down. Invisible to listening at 48 kHz. Codec tests now run at 44.1
  **and** 48 kHz.
- **Five unbounded string reads past the end of the payload** in the `0x03` and
  `0xC0` parsers, including a 6-vs-4-byte off-by-two in the `USER_INFO_CHANGE`
  fixed part. A `MemoryBlock` is sized to exactly the payload and is not
  NUL-padded, so these walked the heap; reproduced as a heap-buffer-overflow
  under ASan. Now impossible by construction via a bounds-checked `Reader`.
- **`callAsync` use-after-free** in `NinjamClient`, found by ASan as
  `stack-use-after-return` where gdb had nothing useful to say. Fixed with
  `callAsyncIfAlive` and a shared alive flag the destructor clears.
- **`writeFull` called unlocked from three threads**, interleaving frames and
  desynchronising the server -- presenting as a random mid-jam disconnect. Now
  serialised by `writeMutex`.
- **`SERVER_AUTH_REPLY` was missing its `errmsg` + `maxchan` tail**, so the
  reference client saw `maxchan = 0` and refused to transmit to our fake server.
  Exactly the self-referential blind spot the differential rig exists to find.
- **Interval length jittered by a sample** every interval (the float phase
  accumulator, above).
- **Metronome click at a third of its nominal pitch** (above).
- **Reconnecting left the previous session's users and channel streams in
  place**, so a rejoined session showed phantom users whose orphaned streams
  swapped silently forever.
- **Jamtaba `JTBv` intervals queued as audio** (above).

### Characterised, not bugs

Ogg emits a page only every ~4 kB, so a short or tonal interval produces **no**
decodable audio until the end-of-stream flush. Interval delivery is therefore
all-or-nothing, and a receiver cannot start playing an interval early just
because some `WRITE` chunks arrived. Pinned by a test so nobody "fixes" it.

---

## Release engineering

### Excise the reference-client harness

The harness linked GPLv2 reference sources and was temporary by construction --
`docs/PARITY.md` exists because that was always the plan. It has been removed,
along with the six reference submodules and the copy of the original client that
sat in `references/`, and the removal was applied to the whole published history
rather than only the tip: nothing in this repository has ever contained a
reference source.

What replaced it, and why the loss is smaller than it looks:

- **`test/fixtures/reference/`** holds bytes the official client actually
  produced, captured before the harness went. `ReferenceFixtures` replays them
  against our parsers and builders on every run -- hermetic, fast, no server, no
  timing, no GPL linkage. This is the durable half, and it was built for exactly
  this moment.
- **`docs/PARITY.md`** keeps the one-time measurements with their methods.
- **`docs/references/SOURCES.md`** records every repository that was read and the
  revision it was read at, so a citation in `src/` can still be followed. Source
  comments now cite upstream (`justinfrankel/ninjam njclient.cpp:806`) rather
  than a path that no longer resolves.
- **`scripts/testserver.sh`** still runs a real `ninjamsrv`; it now shallow-clones
  the reference sources to a scratch directory on first use instead of expecting
  a submodule.

What genuinely went with it is the ability to make a *new* differential claim
without rebuilding the rig. The interoperability items still open in
`ROADMAP.md` say so explicitly.

---

### Clear the clang-tidy backlog

`clang-tidy` was adopted advisory, with 142 findings, after six checks that
fight JUCE and audio idiom were switched off (the raw number was 756; the
reasoning is in `.clang-tidy`). It is a blocking CI step since 2026-11-09.

- **The CI count had never been a count.** The step configured without
  building, so no generated `JuceHeader.h` existed, every unit failed to parse,
  and it printed `findings: 0` for months. It now builds only the generated
  headers, and a unit that fails to parse fails the step.
- **Measured properly** with clang-tidy 21 (a Ninja tree with only the
  `JuceLibraryCode/*.h` targets built, `run-clang-tidy -j 4` over
  `src/*.cpp tools/*.cpp`, unique `file:line:col`): 308, of which 140 were the
  submodules' headers -- now excluded by `ExcludeHeaderFilterRegex` -- and 168
  ours, plus the static analyzer's findings, which every interim count left out.
- **Cleared in batches,** suite and audit green after each: the bugprone ones
  with an answer, the mechanical fix-its, explicit narrowing casts in pixel
  arithmetic, designated initialisers in every multi-field aggregate (a style
  decision, now in `AGENTS.md`), the deprecated `createWriterFor` replaced by
  `AudioFormatWriterOptions` (two of the four leaked the stream on failure),
  and nested conditionals rewritten as `if`s.
- **Real bugs it found:** the transmit capture buffer was not cleared before
  the FIFO filled it, so an interval with less than a full FIFO behind it
  would have sent stale heap memory; and `antiphon-stems` overwrote one of two
  stems whose channels shared a name (found on the way, by the 2026-08-05 jam).
- **Checked false positives, recorded so they are not re-investigated:** the
  analyzer reports three findings inside JUCE headers, at the end of paths
  that start in ours. `AudioBuffer`'s "null `channels`" assumes an allocation
  can return null, which JUCE's `HeapBlock` does not (it throws). The
  `GenericInterpolator` over-read in `NinjamClient`'s resampler cannot happen:
  `numOut = floor(toCopy / ratio)`, so with the carried sub-sample position
  below one, the interpolator consumes at most `toCopy` inputs. A NOLINT cannot
  reach a line in a submodule, so the gate counts findings located in our files
  only, and `WarningsAsErrors` stays off. The `HostGrid` rounding the adoption
  notes defended now simply uses `std::lround`; the `dangling-else` they
  mentioned in `RemoteChannelRow.cpp` is no longer in the code.
- **clang-tidy is pinned** in CI (PyPI `clang-tidy==21.1.6`, beside the pinned
  clang-format), because each major release adds and renames checks.

## Withdrawn

### Capture alignment at the interval boundary (#27)

**Withdrawn -- most likely never real.**

Originally recorded as measured and confirmed: +54.5 samples mean across five
interval seams, all positive, from an arpeggiator run scored by onset detection.

Re-measured against the reference client with the calibrated `IntervalProbe`
(`docs/PARITY.md`):

- Our interval placement sits **on** the reference-to-reference baseline at both
  100/16 and 137/11.
- A constant transmit offset cancels out of every inter-onset interval anyway, so
  an error of that kind could not produce a seam-only artifact.
- Repeating the original arpeggiator measurement *after* the supposed fix gave
  +52.2 sd 21.5 -- statistically identical to the figure it was meant to remove.

Presumed an onset-detection artifact near a seam. The sample-exact capture commit
(`02a58b2`) is correct on its own merits and stays.

This entry is kept rather than deleted because it is the clearest instance of
`PRINCIPLES §5`: three "failures" in this project have been measurement error,
and this is the one that got as far as a fix.

### Idle CPU

An idle, disconnected window burned about 55% of a core. Profiled with
`gprofng` (perf is unavailable: `perf_event_paranoid=4` and sudo needs a
password), which put `paintEntireComponent` at 66% of all CPU and
`drawFittedText` at 25% -- none of it audio or networking.

Four causes, all the same shape: redrawing what had not changed. The 30 Hz tick
repainted the whole editor though only the header animates; it relaid out the
channel area every frame, each `setBounds` dirtying a component and buying
another repaint; meter updates repainted the entire strip, redrawing the static
dB scale for a bar a few pixels wide; and the header repainted while frozen.
Meters now redraw only when the bar would land on different pixels (2% of its
length, with the last step to silence exempt or a decaying bar parks just above
empty), and the phase bar steps in **sixteenth notes** -- 6.7-9.1 repaints/s
against 30, and what other clients do. Idle fell to ~13% of a core.

Then measured the case idle profiling could not reach: a local `ninjamsrv` with
three reference clients transmitting tones of varying amplitude and chatting,
driven through the real UI with `xdotool` (rather than adding a connect flag to
the shipped binary) and recorded with `gprofng -y SIGUSR1` so only the connected
period counted. **4.36 s CPU over 40 s -- about 11% of a core** with three
moving meters, sixteen chat messages and an animating phase bar.
`getDecodedAudio` is 1.4%.

Two further font optimisations were tried and **both were rejected by
measurement**, recorded because they look obviously right:

- Hoisting inline `FontOptions{}.withHeight(...)` to constants. `Font::compare`
  bottoms out in a full FontOptions content comparison with no pointer-equality
  fast path, so reusing one Font costs the same. Saves construction, not
  comparison.
- Skipping text outside the clip region. No measurable change (4.363 s ->
  4.383 s). The cost is `GlyphCache::Key::operator<`, the per-glyph raster
  cache, paid only for glyphs actually drawn -- clipped text never paid it. The
  guard was reverted rather than kept unmeasured.

**Decision: 11% under load is acceptable and the area is closed.** The cheap
wins are spent; what remains is the cost of rasterising glyphs that really are
redrawn, and reducing it means drawing less text (one dB scale per column
rather than per strip), which is a layout change rather than an optimisation.
OpenGL was considered and not pursued: JUCE 8 already uses Direct2D on Windows
and CoreGraphics on macOS, so it would accelerate Linux alone, add a third
render path, and carry the usual host-context risks in a plugin -- and it would
have masked the real defect rather than removed it.
