# Antiphon -- Design

The current, overall design of the software. This file tracks what Antiphon
**is**; `ROADMAP.md` tracks what it is becoming, and `PRINCIPLES.md` tracks why.

Sections carry stable numbers and are cited as `DESIGN §N` from the other docs
and from source comments. Do not renumber casually.

Wire format is not here -- see `docs/PROTOCOL.md`. Interoperability evidence is
not here -- see `docs/PARITY.md`. Accessibility detail is not here -- see
`docs/ACCESSIBILITY.md`.

---

## 1. Vision

Antiphon is a Ninjam client shaped as an audio plugin (VST3 + CLAP +
Standalone), built on JUCE. It is inserted in a DAW -- typically on the master
bus -- and injects Ninjam's interval-delayed playback model into the host.

The shape follows from two observations. First, Ninjam sessions are already
musical work, and musical work belongs in the tool where the rest of the
musician's setup lives: the DAW has the mixer, the recorder, the effects and the
transport (`PRINCIPLES §2`). Second, every existing client is a standalone app
that owns an audio device, which means getting a Ninjam jam into a DAW involves
loopback devices and a second mixer. Antiphon removes that layer by being the
plugin.

What that buys, concretely:

- Local audio passes through with no added delay, always (`PRINCIPLES §3`).
- Remote players' audio arrives one interval late, phase-locked to the local
  metronome, and can be routed to individual output buses so the DAW records the
  jam as **stems**.
- Local channels take audio from individual input buses, so the DAW's routing
  decides what is transmitted.
- The jam's tempo grid is locked to the DAW transport, so anything recorded
  lines up with the project.

The client is called Antiphon because the design is the name: an antiphon is a
response sung to the phrase that just finished, which is exactly the musical
form Ninjam's interval delay produces (`PRINCIPLES §1`).

The **Standalone build is a supported secondary use case**: an interface, an
instrument and a jam, with no DAW involved. It runs the same code and the same
surface, within one hard limit -- JUCE's standalone host offers a single input
and output bus, so stem routing and multi-channel transmit belong to the plugin
(§16, `NON-GOALS.md` fence #2).

**Non-goals** are catalogued in `NON-GOALS.md` and are load-bearing for
understanding the shape of this file: no video, no server, no mixer, no
timeline integration, no wrapped `NJClient`.

---

## 2. The interval model

Ninjam divides time into **intervals** of `bpi` beats at `bpm`. Each client
records one whole interval, encodes it, uploads it, and every other client plays
it back during the *following* interval. You hear yourself live; you hear
everyone else exactly one interval late.

Three properties of this model shape everything downstream.

**Interval delivery is all-or-nothing.** Ogg emits a page only every ~4 kB, so a
quiet or tonal interval decodes to *nothing* until the end-of-stream flush. A
receiver therefore cannot start playing an interval early just because some
`DOWNLOAD_INTERVAL_WRITE` chunks have arrived. This is characterised, not a bug,
and is pinned by a test. Do not build anything that assumes a partially received
interval is playable.

**Interval length is integer and truncated.** `IntervalClock::
samplesPerInterval()` reproduces `njclient.cpp:806` verbatim, *including its
truncation*, so our interval boundaries line up with every other client on the
server. At 137 bpm / 11 bpi / 48 kHz the true length is 231240.87 samples; both
we and the reference client use 231240. Rounding it "correctly" would slip us
~0.87 samples per interval against the rest of the room. Beat offsets *inside*
the interval are rounded, because they only drive the local click and nobody
else hears them. See `docs/PARITY.md` for the measurement.

**Swaps happen at the local boundary and nowhere else.** See §4.

### 2.1 Crossfade at the swap

Swapping from one decoded interval to the next produces a discontinuity whenever
the outgoing interval had un-played tail samples -- which is normal, since the
sender's interval and ours are the same length but the decoder's output is not
sample-aligned to it. The playback slot therefore keeps the outgoing interval in
`fadeOut` and crossfades 256 samples across the boundary.

---

## 3. Threads and shared state

Four threads touch shared state. Both threading bugs found in this project so
far were in this table's blind spots, so it is worth knowing cold.

| Thread | What it does |
|---|---|
| **Audio** (`processBlock`) | Reads host sync (BPM/PPQ/playing), snapshots the input buses, drives `SyncState` and `IntervalClock`, synthesises the metronome click, mixes the local monitor path, captures local audio into each channel's ring buffer, detects interval boundaries, and mixes decoded remote streams into the output via `getDecodedAudio`. At an interval boundary it fires `callAsync` to hand the captured audio off. |
| **`NinjamClient`** (`run()`, a `juce::Thread`) | Socket read loop, keep-alive every 3 s, `handleMessage` dispatch, Ogg decode of incoming `WRITE` chunks. All listener callbacks are bounced to the message thread. |
| **Message thread** | `onConnected`, `onServerConfig`, `onChatMessage`, and `processCapturedAudio` -- encoding is deliberately *off* the audio thread. |
| **UI timer** (30 Hz) | `timerCallback`: syncs remote player strips against `getRemoteUsers()`, relayouts the elastic channel area, updates toolbar enable states and the status readout, repaints. |

**Locks** (`juce::CriticalSection`): `usersMutex` covers `remoteUsers` and the
channel-to-slot map; `localChannelMutex` covers the UI's `localChannels` vector;
`writeMutex` serialises whole frames onto the socket; plus `chatMutex`,
`channelInfoMutex`, `txFileMutex`, `rxFileMutex`.

**The audio thread takes none of them.** That is a rule, not an observation:
what holds the other end of `localChannelMutex` is a megabyte-scale ring
allocation (`addLocalChannel`), an XML parse (`setStateInformation`) and the
editor's 30 Hz timer constructing channel strips. The audio thread reads
`audioChannels`, a fixed array of raw pointers published with a release store on
`audioChannelCount`; a `LocalChannel` is never destroyed while the processor
lives, so removing one shrinks the count and parks the object in
`spareLocalChannels` for a later add to reuse. `guidToInterval` needs no lock at
all -- only the network thread touches it.

**Two hazards, both already bitten, both now structurally handled:**

- **`writeFull` was called unlocked from three threads**, which interleaves
  frames and desynchronises the server -- presenting as a random mid-jam
  disconnect. `writeMutex` now serialises whole frames.
- **`callAsync` use-after-free.** A queued listener callback could still be in
  the message queue when `NinjamClient` was destroyed. Every such lambda now
  goes through `callAsyncIfAlive`, capturing a `shared_ptr<atomic<bool>>` the
  destructor clears first. This is airtight only because the destructor runs on
  the message thread; if that ever changes, this needs rethinking.

**The RX path takes no lock on the audio thread.** Remote channels live in
`streamSlots`, a fixed array of 64 slots created once with the client and never
destroyed, so the audio thread walks all of them without blocking and can never
observe a half-built or freed entry. Within a slot every field has exactly one
owning thread: playback cursors are audio-thread only, mix parameters are
atomics the UI writes and the audio thread reads, and `peakLevel` is an atomic
going the other way. The audio thread never touches a `juce::String` -- copying
one touches a refcount.

Decoded intervals move between the threads through two `SpscRing`s per slot
(`src/SpscRing.h`), which carry ownership in a circle so that freeing never
lands on the audio thread:

    ready:   network -> audio    "here is a decoded interval"
    retired: audio   -> network  "done with this one, free it"

The retire direction is the load-bearing half: dropping the last reference to a
`DecodedInterval` frees a multi-megabyte `AudioBuffer`, which must never happen
inside the callback. `retired` is sized above `ready.capacity() + 2` -- the most
the audio thread can hold at once, being `current` and `fadeOut` -- so handing an
interval back cannot fail. Progress within an interval is published as it always
was, by the atomic `writePos`, with the audio thread reading
`[readPos, writePos.load())`.

A slot's life is Free -> Live -> Draining -> Free. The network thread claims a
free slot and marks a departed one draining; only the audio thread publishes
`kFree`, because only it knows when it has let go of the pointers. Disconnect
therefore marks slots draining rather than freeing them: the audio thread may be
mid-block inside one, and the memory is reclaimed when the slot is next claimed.
Running out of slots drops the channel and logs it, rather than growing an array
the audio thread is walking.

---

## 4. The interval clock

`IntervalClock` (`src/IntervalClock.{h,cpp}`) owns the beat/interval grid. It is
a sample-exact integer counter, JUCE-free and unit-tested.

`advance(numSamples, events)` emits `IntervalStart` and beat events with their
exact sample offsets within the block, and never allocates -- the caller supplies
a reused vector. `splitAtIntervalStarts()` turns those events into
`BlockSegment`s so the capture path can split a block at the boundary.

It replaced a float phase accumulator that wrapped by subtraction, whose residual
walked the boundary a sample per interval and jittered every transmitted
interval's length.

**The local metronome is the sole authority for interval swaps**
(`PRINCIPLES §9`). `NinjamClient::intervalBeginSignal` is set by the network
thread when a download begins; `processBlock` stores `false` into it and
otherwise ignores it entirely. Network jitter must never move the playback clock.

---

## 5. Sync to the host

`SyncState` (`src/SyncState.h`) is a five-state machine, pure and JUCE-free so
its transitions can be tested directly.

```
Disconnected -> TempoMismatch -> ReadyToSync -> WaitingForPlay -> Running
```

| State | Meaning | Shown as |
|---|---|---|
| `Disconnected` | Not on a server | "Not connected" |
| `TempoMismatch` | Connected, DAW is at a different BPM | "DAW tempo does not match the server -- change it to continue" |
| `ReadyToSync` | Tempo agrees; waiting for the user to press **Sync** | "Tempo matches. Press Sync, then start the DAW transport." |
| `WaitingForPlay` | Armed; waiting for the transport-start edge | "Press play in the DAW to join the jam" |
| `Running` | In step, transmitting and playing | "In sync" |

`update()` returns `true` on exactly the transport-start edge it was armed for,
and that return value is what resets the interval clock.

**Why transport start and not the timeline.** The reference client integrates
with REAPER's timeline and loop points. A generic plugin cannot do that, and it
would be the wrong model anyway: in a session/clip view the timeline advances but
is musically meaningless, and jogging the playhead during a live jam is not a
real use case. Locking to transport start covers both common setups -- clip
launching, and a loop sized to the interval -- without needing to know anything
about the timeline. This is `fence #6`.

**Why an explicit Sync button.** Re-phasing mid-jam cuts the interval being
transmitted short, which other players hear as a glitch, so it must only ever
happen when asked for. The state machine is deliberately re-armable from
`Running`, but only on request -- an accidental transport stop/start never
re-phases a jam in progress.

**Three details that are easy to get wrong:**

- Transitions are written as *sequential steps*, not a switch, so several can
  happen in one call. A Sync press arriving in the same block as the tempo
  starting to match would otherwise be dropped and the button would feel dead.
- Tempo is compared at whole-BPM resolution, because the server only carries an
  integer.
- Standalone has no transport (`hasTransport` false) and runs straight to
  `Running` on connect. This is detected with `isStandaloneApp()` -- a runtime
  `wrapperType` check, **never** the `JucePlugin_Build_Standalone` macro, which
  is a project-level flag that is true in every format and once compiled the
  whole DAW sync flow out of the plugin.

Transmission and remote playback are gated on `syncState.isRunning()`, not
merely on being connected, so a jam can never start out of phase with the DAW.
Local passthrough is gated on nothing (`PRINCIPLES §3`).

---

## 6. Local capture and the channel mix

Each local channel is a `LocalChannel` in `AntiphonAudioProcessor`, holding: a
protocol-level name, mono/stereo, volume, pan, monitor mute/solo, an xmit
toggle, VU peaks, an explicit `inputBusIndex`, and a `juce::AbstractFifo` ring
buffer.

**Monitor and transmit are independent gain stages.** Volume and pan apply to
both. Mute and solo are **monitor-only** and must never change what other players
hear. The xmit toggle gates only what is sent to the server. `ChannelMix`
(`src/ChannelMix.h`) is the single home for these rules, and exists because they
had previously been written out three times -- in the capture path, the monitor
mix and the peak meters -- and had drifted:

- "Mono" summed in none of them. It selected the left channel and silently
  discarded the right half of a stereo source. It now sums and **halves**, so a
  correlated stereo source keeps its level rather than doubling.
- The meters ignored the mono flag entirely and went on showing an independent
  stereo pair. They now meter post-mono and post-monitor-gain, so the VU shows
  what you actually hear and transmit.

**Input snapshot.** JUCE aliases the input and output buses in one buffer, so
`processBlock` copies every input channel into `inputSnapshot` *before* clearing
or mixing into the output.

**Capture is split at the interval boundary.** `splitAtIntervalStarts` divides
the block into segments; samples before the boundary belong to the interval that
is ending and go into the buffer we transmit, samples after it start the next.
Capturing whole blocks and flushing at the boundary instead would round every
transmitted interval up to a multiple of the block size.

**Handoff.** At the boundary, each channel's ring contents are handed to
`processCapturedAudio` via `callAsync` -- encoding runs on the message thread,
never the audio thread. The `callAsync` currently copies the whole buffer;
replacing it with a lock-free FIFO is tracked in `ROADMAP.md` under *Lock-free TX
handoff*.

---

## 6.1 What you hear, what they hear, and what is stored

Four controls look similar and are not. The distinction is the signal chain:

```
your input --[TX]--> transmit ring --[spans]--> encode --> the other players
           \--[Mute/Solo]--> [Vol/Pan] --> your monitor

server --[Recv]--> decode --[Solo/Mute]--> [Vol/Pan] --> your mix
```

| Control | Affects your monitor | Affects what others hear |
|---|---|---|
| **Mute** (local) | yes | **no** |
| **Solo** (local or remote) | yes | **no** |
| **TX** | no | **yes** -- this is the one |
| **Recv** (remote) | yes | no -- tells the server to stop sending |

**Solo is one global bus.** Soloing a local channel silences remote players and
vice versa. This mirrors the reference client, where both mix decisions test the
combined `m_issoloactive` mask (`njclient.cpp:1307` and `:1388`, bits set at
`:1750` and `:1886`). Two independent solo buses -- what we had -- meant solo did
not do the one thing solo is for.

**Solo overrides mute** rather than combining with it: a channel that is both is
heard (`:1307`, `:1388`).

**Recv is upstream of everything**, so solo cannot recover a channel we have
asked the server to stop sending. It also mutes locally and immediately, because
the server side cannot be instant -- an interval may already be in flight. No
handover is needed: once the server does stop, there is nothing left to mute.

**Every gain change is ramped over 5 ms** (`GainRamp`), because a step in gain is
a discontinuity and a discontinuity is a click -- on the transmit path, a click
baked into everyone else's mix. Not zero-crossing detection: left and right cross
at different times, silence and DC never cross, and a crossing removes the
discontinuity in value but not in slope.

### Transmit is recorded, then applied

The transmit ring stores **what you played**, un-gated. `TransmitSpans` records
**which parts you agreed to send**, as the points where TX changed rather than a
flag per sample -- about 2 KB per channel against 11.5 MB for a second audio
buffer. The two are combined at the interval boundary, with each edge ramped.

An interval is uploaded if TX was on for *any* part of it; the rest is silence.
An interval you were silent for throughout is not sent at all, which is what the
reference does for a channel that is not broadcasting.

Keeping the audio rather than gating at capture is what makes the retroactive
gesture possible: holding TX (or Ctrl+Alt+Shift+T) toggles it *and* applies that
to the whole interval so far, so you can share what you just played or take it
back before anyone hears it. The rewrite is anchored at the moment the button
went down, not when the hold timer expired, and is void if the interval boundary
has passed -- that interval has already gone out.

**The cost, stated plainly: audio you are not transmitting is held in memory for
up to one interval.** It never leaves the machine unless you perform the
gesture. Gating at capture would destroy it and make retroactive transmit
impossible; this is the trade that buys the feature.

---

## 7. Remote playback, mixing and routing

Each `(username, channelIndex)` pair holds one of the fixed `streamSlots`
described in section 3, claimed on its first `DOWNLOAD_INTERVAL_BEGIN`.

- The network thread allocates a `DecodedInterval`, keeps ownership of it in the
  slot's `owned` list, and publishes the pointer through the slot's `ready` ring.
- It decodes `DOWNLOAD_INTERVAL_WRITE` chunks into that interval, found by GUID
  in `guidToInterval`, and publishes progress by incrementing an atomic
  `writePos`.
- `PendingDownload` holds the decoder and borrows the interval; it is erased when
  `flags & 1` arrives, marking `finalReceived`.
- At each metronome interval boundary, `swapIntervalBuffers()` pops `ready` into
  `current`. **This is what realises the one-interval playback delay.**
- The audio thread reads `[readPos, writePos.load())` from `slot.current` in
  `getDecodedAudio`, and pushes each finished interval to `retired` for the
  network thread to destroy.

**Sample-rate conversion.** A remote client may be running at a different sample
rate from us. `PendingDownload` carries a `juce::LagrangeInterpolator` per
channel, engaged when the decoder's declared rate differs from the session rate.

**Mixing.** `getDecodedAudio` applies per-channel volume, pan, mute and solo, and
updates the VU peak. The default remote channel gain is
`kDefaultRemoteChannelVolume = 0.25f` (-12.04 dB), matching the reference client
(`njclient.cpp:2948` and `:1967`) and confirmed by measurement -- the reference
client plays our transmitted tone back at 0.253 of the level we sent. Do not
"fix" this to unity; it would make us 12 dB louder than everyone else in the
room. The UI fader initialises from the same constant so the two cannot drift.

**Output bus routing.** Each `RemoteUserChannel` carries an `outputBusIndex`;
`getDecodedAudio` routes its audio to output channels `busIdx*2` and
`busIdx*2+1`. Routing is persisted by `(username, channelIndex)` in
`savedRemoteRoutings` and reapplied when a player rejoins -- a jam where someone
drops and reconnects must not scatter your stems.

**Recv vs Mute.** Mute receives and decodes but silences the output. Recv-off
sends `CLIENT_SET_USERMASK` with that channel's bit cleared, so the server stops
forwarding the audio at all -- a bandwidth control, not a mix control.

**Known gap:** when a decoded interval runs short, `getDecodedAudio` leaves the
rest of the block silent rather than holding or fading. Tracked in `ROADMAP.md`
under *Underrun tail*.

**Diagnostics.** `NinjamClient` carries sticky atomic counters
(`diagSwapsByFallback`, `diagUnderrunBlocks`, `diagSamplesDroppedOnSwap`,
`diagLastIntervalSamples`, ...) which `dumpDiagnostics()` logs as deltas. They
exist because playback-health bugs here are silent -- a stream that swaps forever
with nothing in it sounds identical to one that is simply quiet.

---

## 8. Levels: the dB model

Every gain in the audio model is a **linear multiplier**; only the *presentation*
is in decibels. Persisted state and the audio path are therefore unaffected by
the scale.

`GainUtils` (`src/GainUtils.h`) owns the mapping: `kMinDb = -60` (treated as
silence, displayed "-inf"), `kMaxDb = +6` for a little headroom above unity,
0.1 dB steps.

**Why.** A fader linear in amplitude is a bad control: unity lands at the
midpoint, the whole of -inf..-12 dB is squeezed into the bottom eighth of travel,
and half the range is spent above unity. Linear in dB puts the resolution where
the ear is.

Meters use the same scale, in dBFS, so a meter reading and a fader reading are
directly comparable -- a linear meter puts everything below -20 dBFS in the
bottom tenth of its height. Meter release is specified as a **rate in dB per
second** rather than a per-block coefficient, so it is independent of block size
and sample rate.

Values are spoken to a screen reader with units ("-12.0 dB"), which is the same
formatter (`PRINCIPLES §8`).

---

## 9. Codec layer

`VorbisCodec.{h,cpp}` wraps libogg/libvorbis directly, using pimpl to keep their
types out of the header. It replaced WDL's `vorbisencdec.h`.

- `VorbisDecoder`: `decode(data, len)`, `available()`, `pcm()`, `skip(n)`,
  `sampleRate()`, `numChannels()`.
- `VorbisEncoder`: constructed with `(sampleRate, numChannels, bitrateKbps,
  serialNumber)`, then `encode(float*, n)`, `available()`, `data()`, `advance()`.

**The encoder is constructed with the session sample rate**, which sounds
obvious and was the single worst bug this project has had: it was hardcoded to
48000, so at 44.1 kHz we transmitted 8.8% sharp and 8% short, and at 96 kHz an
octave down and 150% long. It was invisible to our own tests, invisible to
listening at 48 kHz, and only fell out of a differential run. Every codec test
runs at 44.1 **and** 48 kHz for this reason.

SHA1 (`Sha1.{h,cpp}`) is likewise first-party -- a minimal FIPS 180-1
implementation used only for the double-hash challenge-response in
`CLIENT_AUTH_USER`. It replaced WDL's `sha1.{h,cpp}`.

---

## 10. UI architecture

### 10.0 State legibility, and where standalone differs

Three states have to be readable without learning anything, and all three were
once encoded as near-invisible colour shifts. The vocabulary lives in
`AntiphonTheme` (`src/AntiphonLookAndFeel.h`) and every control draws through
`AntiphonLookAndFeel::paintControlSurface`, so they cannot drift apart again:

| State | How it reads |
|---|---|
| On | Lit: the teal product accent, dark text, brighter edge |
| Off | Flat dark control surface, grey text |
| Unavailable | One shared low-contrast treatment, *legible* grey text |

Disabled text is deliberately readable rather than merely dark. You have to be
able to tell what it is you cannot use; the previous `0xff1e1e1e` on
`0xff121212` read as a rendering fault, not as a disabled control. The chat
panel uses the same treatment as the buttons, and says in words why it is
unavailable.

**The standalone and the plugin are allowed to diverge**, because they can do
different things. Local channels and buses exist so a DAW can route tracks in
and record stems out, and **Sync** locks our interval grid to the DAW transport;
none of that has a counterpart in the standalone. Those controls stay visible
there -- the shape of the app should not change under you -- but disabled,
prefixed by a "DAW only" note, with the reason in each tooltip and accessible
description. `AntiphonEditor::applyHostContextToControls()` settles this once at
construction, since which controls can ever apply is fixed for the life of an
instance; `updateToolbarStates()` returns early in the standalone so its 30 Hz
tick cannot quietly undo it.

Bus changes are signalled to the host through `ChangeDetails::busLayoutChanged`,
which does not exist in stock JUCE -- see the patch pair in `AGENTS.md`.

### 10.1 Overall layout

```
+--------------------------------------------------------------------------+
|  Status bar: server BPM | BPI | phase progress | host BPM | sync state    |
+---------------+----------------------------------------------+-----------+
| Local input   |  Remote players (horizontally scrollable)     |  Chat     |
| channel       |                                               |  panel    |
| strips        |  [Player A]   [Player B]   [Player C] ...     |           |
| (vertical,    |   vert.strip   vert.strip   vert.strip        | (default  |
|  left panel)  |   vol/pan/vu   vol/pan/vu   vol/pan/vu        |  visible, |
|               |   M  S  Recv   M  S  Recv   M  S  Recv        |  toggle-  |
|               |                                               |  able)    |
+---------------+----------------------------------------------+-----------+
|  Connect | Channel:[+] | Input bus:[+][-] | Output bus:[+][-] | Metro...  |
+--------------------------------------------------------------------------+
```

Three columns: local input strips (left), remote player cards (centre,
horizontally scrollable), chat (right, collapsible). Full-width status bar at
top, toolbar at bottom.

**Elastic 40/60 split.** The channel panel divides between local and remote with
a 200 px floor on each side. Local strips are left-flush within their allocation;
remote strips are right-flush within theirs, so unused space appears as a gap
between them rather than a ragged edge. When one side overflows its hard
allocation but the other has slack, it borrows that slack; when both overflow,
each gets exactly its allocation and scrollbars appear.
`relayoutChannelArea()` recalculates this on `resized()` and on every 30 Hz tick.

### 10.2 Local channel strip

90 px wide, top to bottom: name editor (22 px, protocol-level -- transmitted via
`CLIENT_SET_CHANNEL_INFO` and seen by other players), pan rotary (44 px), L/R VU
bars flanking a vertical fader (flex), M+S row, TX button, input bus dropdown,
Mono + Remove row.

Mono toggles mono summing of the assigned bus; it does not add or remove a bus.
Multiple channels may share an input bus.

### 10.3 Remote player card

One card per player: a 22 px username header, then one `RemoteChannelRow` per
channel arranged horizontally, so a player transmitting guitar and vocals shows
both under one name. `getPreferredWidth()` is `8 + n*90 + (n-1)*4`.

Each row, top to bottom: channel name label, pan rotary, VU bar + vertical
fader, M+S row, output bus dropdown, R (Recv) button.

### 10.4 Status bar and state legibility

Per `PRINCIPLES §12`, state is announced by colour and motion before text.

- **Header tint** by connection state: dark navy when connected and in sync;
  dim amber after a failed connect or while a BPM mismatch is active; dark grey
  when idle. The amber clears when Connect is clicked again.
- **Phase bar** advances only when in sync. Teal when connected, grey when not.
  Beat ticks and flashes are suppressed when disconnected; phase resets to 0.
- **Chat panel** is ghosted (disabled, near-black, dim text, "(not connected)"
  placeholder) when disconnected, and cleared on the next successful connect so
  a new session does not open with the last one's backlog.
- **Toolbar states** are recomputed every tick by `updateToolbarStates()`:
  Connect disabled while connected, Disconnect while disconnected, `[-]` bus
  buttons disabled at one bus.

### 10.5 Toolbar

Connect/Disconnect, then compact labelled groups `Channel:[+]`,
`Input bus:[+][-]`, `Output bus:[+][-]`, then the metronome toggle and its volume
slider, then Save Tx / Save Rx / Test Tone (debug), then Sync and Chat.

Channel count and bus count are managed **independently**: adding a channel does
not add a bus, and removing a bus reroutes anything using it to bus 1.

### 10.6 Metronome

`MetronomeVoice` is a one-shot click, split into its own module so its pitch is
testable: 880 Hz downbeat, 660 Hz bar start, 440 Hz beats,
`phaseInc = 2*pi*freq/sampleRate`. It exists because the previous formula swept
`2*pi*freq/bpm` radians over a `3/bpm`-second click and never referenced the
sample rate -- 880 Hz came out near 293.

Off by default in plugin mode, on in standalone. Volume is a toolbar slider,
persisted. Active only when in sync; the per-sample click path is skipped
entirely otherwise.

### 10.7 Look and feel, typeface

`AntiphonLookAndFeel` is a `LookAndFeel_V4` subclass: dark blue theme, teal
accent `#00b4d8`, custom rotary, button, toggle and text editor outline.
Disabled buttons get `alpha * 0.5` automatically.

The surface renders in **Inter**, embedded as two static cuts (Regular and Bold)
via `juce_add_binary_data`, because a platform-default sans varies by distro and
machine -- a look the product never chose. The variable font is deliberately not
vendored: a face JUCE must instance per size reintroduces exactly that
variability. Licence obligations are in `THIRDPARTY.md`.

### 10.8 Text entry and keyboard focus

**Text entry belongs in a `juce::DialogWindow`, not a child overlay.** An
embedded plugin window on Linux can never hold the X input focus -- JUCE decides
focus with `isParentWindowOf(ourWindow, focusedWindow)` and the host's window is
our *ancestor*, not our descendant -- so `takeKeyboardFocus()` bails out and no
text field inside the editor can be typed into. A dialog is a real top-level
window with its own peer and takes focus normally. `ServerBrowserDialog` is
launched via `DialogWindow::LaunchOptions::launchAsync()`.

For the inline case (the chat field), JUCE itself is patched --
`patches/juce-linux-plugin-keyboard-focus.patch`. See `AGENTS.md`.

`EDITOR_WANTS_KEYBOARD_FOCUS TRUE` in `src/CMakeLists.txt` is the other half:
it defaults to FALSE, leaving hosts free to keep the keyboard for their own
shortcuts.

---

## 11. Accessibility architecture

Summary only; `docs/ACCESSIBILITY.md` is the long form, including the honest
platform table (macOS and Windows work; Linux has no JUCE AT-SPI backend) and the
known gaps.

Three components carry it:

- **`StatusReadout`** -- the header's information exists only as `Graphics`
  calls, so it does not exist at all for a reader. `StatusReadout` draws nothing
  and occupies the same area, purely so that information has somewhere to live in
  the accessibility tree. It is first in tab order, because "where am I and is it
  working" is the first question. It only touches JUCE when the words actually
  change, so a reader is not told the same thing thirty times a second.
- **`Announcer`** -- speaks discrete events (connect, disconnect, sync change,
  tempo/BPI change, votes, joins and parts). Three verbosity levels, Important
  by default, chat at All only. Identical messages are suppressed and a minimum
  gap enforced, so six players joining at once is not six interruptions.
  Continuous values are never announced (`PRINCIPLES §11`).
- **`AccessibilityAudit`** + **`AccessibilityTree`** -- the auditor walks a plain
  node tree and reports MISSING NAME, DUPLICATE NAME (within a container) and NO
  DESCRIPTION. `AccessibilityTree` adapts a live `juce::Component` tree into that
  shape. They are separate files so the *rules* stay testable in the headless test
  target, which does not link `juce_gui_basics`. `Ctrl+Alt+A` writes a report to
  the desktop.

---

## 12. State persistence

`getStateInformation` / `setStateInformation` save: host, port, username,
password and anonymous flag; the local channel layout including each channel's
`inputBusIndex`, name, mono flag, volume and pan; remote mixer positions;
`savedRemoteRoutings` (remote output bus by username and channel index);
metronome enable and volume; and chat panel visibility.

Passwords are XOR-obfuscated so they are not plain text in a DAW project file.
This is obfuscation, not encryption, and is not represented as more than that --
a DAW project file is not a secret store.

---

## 13. Testing architecture

Four layers, cheapest first. `test/README.md` is the operational guide; this is
the shape and the reasoning.

| Layer | What it is | Hermetic |
|---|---|---|
| 1. Unit | `juce::UnitTest` suites over the JUCE-free modules and the parsers | yes |
| 2. Loopback | `FakeNinjamServer` on `127.0.0.1`, real socket path, real handshake, uploads echoed back as downloads | yes |
| 3. Real server | Opt-in via `NINJAM_TEST_SERVER`; a local `ninjamsrv` whose session archive is measured by `scripts/analyze_archive.py` | no |
| 4. Differential | `test/refclient/` -- the official `NJClient` driven headless, both directions | no, temporary |

Two structural decisions worth knowing:

- **The test target re-lists production sources** rather than sharing a library
  target. `juce_generate_juce_header` only works on `juce_add_*` targets and each
  target needs its own `JuceHeader.h`. Consequence: a new `src/*.cpp` must be
  added to **both** `src/CMakeLists.txt` and `test/CMakeLists.txt`.
- **`PluginProcessor.cpp` and the UI cannot be compiled into the test target**
  at all -- they need the `JucePlugin_*` defines. This is the whole reason
  `IntervalClock`, `MetronomeVoice`, `ChannelMix`, `SyncState`, `GainUtils` and
  the `AccessibilityAudit` rules exist as separate modules (`PRINCIPLES §7`).
  Logic left in `PluginProcessor` is logic that can never be tested.

**Layer 4 is temporary by design.** `test/refclient/` links GPLv2 reference
sources and is meant to be deleted before release; the build guards its
`add_subdirectory` with an `EXISTS` check so its absence changes nothing. What it
established is recorded in `docs/PARITY.md`, which is the durable artefact.

---

## 14. Video, for the record

Video is **not planned** (`fence #1`). This section records the mechanism so the
decision can be revisited on evidence rather than re-researched.

Video is a **Jamtaba-proprietary extension**. ReaNINJAM and abNinjam do not
support it; they ignore the extra channel data, and so do we.

How Jamtaba does it (`references/JamTaba/src/Common/`):

- **Capture**: Qt `QCamera` + `CameraFrameGrabber`.
- **Encode**: FFmpeg, `AV_CODEC_ID_H264`, max 320x240, 64-400 kbps, emitting raw
  H.264 NAL data (`video/FFMpegMuxer.cpp:172`).
- **Transport**: sent as **channel index 1** (audio is always channel 0) using
  the standard `UPLOAD_INTERVAL_BEGIN` / `UPLOAD_INTERVAL_WRITE` messages, but
  with fourCC `JTBv` instead of `OGGv`. The server routes it like audio without
  decoding (`MainController.cpp:382-393`, `ClientMessages.cpp:459-471`).
- **Receive**: on `DOWNLOAD_INTERVAL_BEGIN` with a non-`OGGv` fourCC, buffer the
  raw bytes; on `flags & 1`, hand to FFmpeg (`ninjam/client/Service.cpp:302-337`).
- **Display**: a `VideoWidget` below the player's audio strip.

If it were ever built here: `juce_video` (`juce::CameraDevice`) wraps
AVCapture/DirectShow/V4L2 for capture; FFmpeg would be a significant new
dependency, with libtheora as a simpler, lower-quality alternative. Follow the
`JTBv` fourCC and channel-index-1 convention for Jamtaba interoperability.

We already filter on fourCC, which is what stops `JTBv` intervals being queued as
silent audio streams -- so the current behaviour in a mixed room is correct, just
video-less.

---

## 15. Reference implementations

`references/` holds read-only submodules. Each has a sibling summary at
`references/<name>.md` -- **read the summary first**, dip into source only for
specifics. Nothing here is linked into the product.

| Submodule | Primary use |
|---|---|
| `ninjam/` | Authoritative protocol reference. `njclient.cpp`, `netmsg.cpp`, `mpb.cpp`, `server/usercon.cpp`. |
| `old_client/` | Context for the vendored WDL code in `utils/`. |
| `abNinjam/` | OSC tempo sync reference (`fence #7`); headless properties-file approach. |
| `ninjam-next-plugin/` | Modern JUCE VST3/AU wrapping `NJClient`. Useful for `AudioPlayHead` phase sync patterns. |
| `JamTaba/` | Rich UI reference; the only client with video (§14). Cautionary tale on Qt-plus-plugin complexity. |
| `libninjam/`, `JamWide/` | Additional implementations, lower priority. |

---

## 16. The standalone application

The Standalone build is a supported secondary use case (`PRINCIPLES §2`), not a
development convenience: an interface, an instrument, a jam, no DAW. It is
shaped by what JUCE's standalone host offers -- **one input bus and one output
bus** -- so `disableNonMainBuses()` turns off the extra buses that exist for stem
routing. Multi-channel transmit and stem recording are plugin-side by
construction.

`src/StandaloneApp.cpp` replaces JUCE's stock `StandaloneFilterApp`, enabled by
`JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1`. It exists because of one specific
defect in the stock one.

### 16.1 Why we do not use JUCE's standalone window

`StandaloneFilterWindow` builds its `StandalonePluginHolder` **as an argument to
its own delegating constructor**. The holder's constructor runs `init()` ->
`setupAudioDevices()` -> `deviceManager.initialise()`, so the audio device is
opened to completion *before any window exists*. Two consequences, both real:

- **A backend that blocks leaves no application.** No window, no error, no route
  to the device picker -- the process simply sits at the command line. Observed
  directly: an ALSA-to-PipeWire open against a host running two competing
  pipewire daemons deadlocks in a futex on the message thread, having spawned
  its worker threads but never opened a PCM device or connected to the X server.
- **A backend that fails cleanly is silent.** `reloadAudioDeviceState` discards
  the `String` error that `deviceManager.initialise()` returns, so the app comes
  up with no audio and no explanation.

### 16.2 What we do instead

**The window goes up first**, with the editor in it, before anything touches an
audio device. The device is then opened on a worker thread (`DeviceOpenProbe`)
under a budget, and a 50 ms timer on the message thread drives
`AudioDeviceStartup` -- a pure, JUCE-free state machine
(`Opening -> Ready | Failed | TimedOut`) tested without hardware in the loop.

| Outcome | What the user gets |
|---|---|
| `Ready` | Audio starts; the working device is written to `audioSetup` so the next launch reuses it |
| `Failed` | The backend's own error, plus a device picker |
| `TimedOut` | "The audio device did not respond..." plus a device picker |

Startup progress is written to **stderr**, not `juce::Logger` -- with no logger
installed `writeToLog` is a no-op, and "why is there no audio" is exactly the
question someone runs this from a terminal to answer.

**On timeout the probe thread and its `AudioDeviceManager` are deliberately
leaked.** The premise of the timeout is that the backend may never return, so
joining the thread would block for exactly as long as the bug being worked
around, and destroying the manager would try to close a device that never
opened. A leaked thread and a usable application beat a clean shutdown of a dead
one. A fresh manager backs the picker.

**Remembering the choice is what makes the fix stick.** Once a chosen device is
in `audioSetup`, subsequent launches ask for it by name rather than falling
through the default path that wedged.

Note that `AudioDeviceManager::createStateXml()` returns null when the setup
still matches the default, so `audioSetup` stays empty on a machine where the
default device simply works -- there is nothing to remember, and the next launch
takes the same default. The entry appears precisely in the case that matters:
after the user has picked something else in the recovery view.

### 16.3 Known limit

`AudioDeviceSelectorComponent` opens devices synchronously on the message
thread. Picking a device that wedges will therefore freeze the window, exactly
as the old startup did -- the difference is that this is now a deliberate user
action with a visible cause, rather than the app failing to start at all.
Fixing it properly means an asynchronous device picker, which is not built.
