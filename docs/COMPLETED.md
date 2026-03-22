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
| #24 | Lock-free TX handoff *(open -- see `ROADMAP.md`)* |
| #25 | Video support *(parked)* |
| #26 | OSC tempo sync *(parked)* |
| #27 | Capture alignment at the interval boundary *(**withdrawn** -- below)* |
| #28 | Audio-thread hygiene *(open)* |
| #29 | Destructor race in NinjamClient *(open, as "Client shutdown race")* |
| #30 | Underrun tail *(open)* |

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

---

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

`test/refclient/` -- the official `NJClient` driven headless, which is possible at
all because it is a library rather than an app and takes an explicit sample rate.
Deliberately self-contained and meant to be deleted before release; the build
guards its `add_subdirectory` with an `EXISTS` check. Everything it established is
recorded in `docs/PARITY.md`.

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
