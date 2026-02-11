# Antiphon -- Principles

> The principles below are the touchstones for every decision in this project.
> When a proposal conflicts with one of them, the proposal is the thing that has
> to bend.
>
> They are deliberately opinionated. Antiphon is not a neutral tool; it is a
> stance about what a Ninjam client should be once you accept that the DAW is
> already there. The principles exist so that stance survives contact with
> feature requests, contributors, and our own future temptations.
>
> Read this file before adding a work area to `ROADMAP.md`, before opening a
> section in `DESIGN.md`, and before approving a feature idea. If an idea cannot
> be expressed within these principles, it does not belong in Antiphon -- or the
> principles need revising first, deliberately, not silently.
>
> **Citing principles.** Principles are referenced by number (`PRINCIPLES §N`)
> across `DESIGN.md`, `ROADMAP.md`, `NON-GOALS.md` and source comments, and the
> `NON-GOALS.md` fences are referenced as `fence #N`. The numbers are **stable
> anchors** -- do not renumber casually. The short titles are mnemonics, not the
> citation key. If a renumber is ever unavoidable, sweep every `PRINCIPLES §N`
> and `fence #N` reference in the docs **and** in `src/` in the same change.

## North Star

Antiphon makes a Ninjam session feel like part of your DAW. The interval delay
is the musical form, not a defect to hide; the DAW is the mixer, the recorder
and the host; and correctness means agreeing with the client every other player
on the server is actually running -- not with ourselves.

Where two principles are in tension, the North Star is the tie-breaker: the
reading that leaves a player jamming with strangers, in time, inside the tools
they already know, wins.

## How to use this document

Run a proposal through this gate **before** it reaches `DESIGN.md`. Each rung
names what the proposal must answer and the principle that owns the test. If any
answer is "no" or "well, except...", the proposal bends, not the principle.

1. **Form** -- Does it work *with* the interval delay rather than against it?
   (§1)
2. **Scope** -- Is it something the DAW cannot already do better? (§2)
3. **Passthrough** -- Does local audio still reach the output undelayed and
   unconditionally? (§3)
4. **Interop** -- Would the reference client, Jamtaba and ReaNINJAM all still
   understand us? Name the protocol messages involved. (§4, §10)
5. **Evidence** -- If it makes a timing or level claim, what is the number, what
   is the method, and how was the instrument calibrated? (§5)
6. **Dependencies** -- Does it stay first-party, or does it drag in a framework?
   (§6)
7. **Audio thread** -- Does the new work allocate, lock, block or log on the
   audio thread? Where is the JUCE-free, testable module? (§7)
8. **Duplication** -- Is any rule it needs already defined somewhere? Does it get
   one home? (§8)
9. **Clock** -- Does anything other than the local metronome move the playback
   clock? (§9)
10. **Reader** -- Is every control it adds named, described and reachable by
    keyboard? Does anything new get announced on a timer? (§11)
11. **Legibility** -- Can a player tell the resulting state from across the room,
    without reading? (§12)
12. **Fences** -- Does it clear the `NON-GOALS.md` catalogue?

---

## 1. The interval is the instrument

Ninjam's defining property is that you hear everyone else exactly one interval
late. This is not latency to be minimised; it is the musical form. The whole
session is a rolling call-and-response in which every player answers the bar
everybody just played. Antiphon exists to make that form comfortable, not to
apologise for it.

**Consequence.** There is no "low-latency mode", no partial-interval playback,
no attempt to start an interval early because some of its packets arrived.
Interval delivery is all-or-nothing by construction (`DESIGN.md` §2), and the
UI's job is to make the delay *legible* -- the phase bar, the metronome, the
tempo readout -- not to disguise it.

This is also why the client is called Antiphon. The name is the design.

## 2. The DAW is the mixer, the recorder and the host

Antiphon is a plugin. It sits on a bus inside a host that already has a
transport, a mixer, a recorder, a plugin chain, an undo stack and a file
browser. Every one of those is better than anything we would write, and every
one of them is a thing we therefore do not write.

**Consequence.** We route; we do not mix. Remote players get an output bus each
so the DAW records them as stems. Local channels take an input bus each so the
DAW's routing decides what we transmit. We add no effects, no recorder, no file
management, no transport of our own. The Standalone build exists so a change can
be iterated on in seconds without launching a DAW -- it is a development
convenience and a fallback for players without a host, not the product.

The corollary is a hard limit on surface area: a feature request that amounts to
"add a thing my DAW already has" is answered by routing, not by building it.

## 3. Local audio is never delayed

Your own signal passes through Antiphon to the output with no added delay, in
every state: connected, connecting, disconnected, tempo-mismatched, or waiting
for the transport. A plugin on the master bus that goes silent or laggy because
a network socket dropped is a plugin nobody will leave inserted.

**Consequence.** The passthrough path is unconditional and is never gated on
connection state. Only *remote* audio and *transmission* depend on being
connected and in sync.

## 4. The reference client is the correctness bar

Our own tests can only show that we are self-consistent. They cannot catch a
wire format we have misread the same way twice, or an interval length that is
wrong in a way our clock and our decoder agree about. The implementation every
other player on the server is running is `NJClient`; disagreement with it is the
definition of the bug we care about.

**Consequence.** Interoperability claims are made differentially, against the
reference client driven headless, with two independent clocks and never ours on
both sides. Six defects have been found this way that were invisible to our own
suite *and* to listening -- including a hardcoded 48 kHz encoder that detuned us
by 8.8% at 44.1 kHz. The measured record is `docs/PARITY.md`; it is deliberately
durable, because the harness in `test/refclient/` is temporary and will be
excised before release.

The most load-bearing instance of this principle is `IntervalClock::
samplesPerInterval()`, which reproduces `njclient.cpp:806` **verbatim, including
its truncation**. Rounding it "correctly" would desync us from every other
client on the server by ~0.87 samples per interval at 137 bpm / 11 bpi.

## 5. Measure; do not listen

Every timing or level claim in this project is a number with a stated method,
and the measuring instrument is calibrated against synthetic ground truth
*before* its output is believed.

**Consequence.** Audio assertions are statistical -- RMS, pitch by zero
crossings, energy in a band -- never sample-by-sample, because Vorbis is lossy
and has codec delay. And when a test fails, the measurement is the first
suspect, not the code. Across this project three "failures" have turned out to
be measurement error: a pitch detector gating on a transient peak (reported
294.7 Hz for a 440 Hz tone), two attempts at interval alignment reporting
standard deviations of 32765 and 15746 samples, and work item #27, which was
recorded as a confirmed +54.5-sample defect and turned out to be an
onset-detection artifact that survived its own fix unchanged.

`docs/PARITY.md` carries the calibration table. Read it before quoting a number
from this project.

## 6. First-party code, no wrappers

Antiphon is clean modern C++ against JUCE. There is no `NJClient` wrapper, no
Qt, no framework dragged in for one feature. The protocol, the SHA1, the
Ogg/Vorbis layer and the interval clock are all ours, at about 6 000 lines
total.

**Consequence.** The whole client fits in your head, which is what makes the
differential testing in §4 tractable: when a byte on the wire is wrong we can
find it. The reference implementations under `references/` are read-only
protocol documentation, not dependencies, and nothing from them is linked into
the product.

This principle rejects the obvious shortcut. Wrapping `NJClient` would have been
faster and would have inherited a decade of correctness -- along with WDL, a
GPLv2 dependency in the audio path, and an architecture built for a standalone
app with its own audio device. We took the cost once.

## 7. The audio thread does not allocate, lock, block or log

`processBlock` and everything it calls runs under a hard real-time deadline.
Anything that can block -- a heap allocation, a mutex, a file write, a `DBG` --
is a dropout waiting for the wrong moment.

**Consequence.** Logic that the audio thread needs lives in JUCE-free,
unit-tested modules: `IntervalClock`, `MetronomeVoice`, `ChannelMix`,
`SyncState`, `GainUtils`, `IntervalProbe`. `processBlock` is a thin caller over
them. This is not stylistic -- `PluginProcessor.cpp` **cannot** be compiled into
the test target at all (it needs the `JucePlugin_*` defines), so logic left
there is logic that can never be tested.

Two known violations are tracked in `ROADMAP.md` under *Audio-thread hygiene*
(`getDecodedAudio` takes `downloadMutex`; the Save Tx/Rx toggles do file I/O).
Do not add to that list.

## 8. One definition, shared

A rule with more than one consumer gets exactly one home. Every module in this
codebase named for a rule rather than a component exists because that rule had
been written out two or three times and the copies had drifted.

**Consequence.**

- `ChannelMix` exists because the mono/pan/gain rules were written separately in
  the capture path, the monitor mix and the peak meters. "Mono" summed in none
  of them -- it selected the left channel and discarded the right -- and the
  meters ignored the flag entirely.
- `GainUtils` exists so the faders and the meters share one dB scale, and so
  they agree with each other.
- `IntervalProbe` is shared by the plugin's Test Tone, the test suite and the
  reference-client harness, because two definitions of the probe would drift and
  a timing test would then be comparing an implementation against itself.
- `NinjamClient::kDefaultRemoteChannelVolume` is the single source for both the
  audio path and the fader's initial position.

When you find yourself writing a rule down a second time, stop and give it a
header.

## 9. The local metronome is the sole authority for interval swaps

The playback clock advances on the local interval grid and on nothing else.
`intervalBeginSignal` is set by the network thread when a download begins, and
it is drained by the audio thread and **never acted upon**.

**Consequence.** Network jitter cannot move the playback clock. This was not
free -- signal-driven swaps were how the client originally worked, and removing
them was the fix for playback that wandered out of phase with the metronome. The
grid comes from `IntervalClock`; the network is a source of audio, not of time.

## 10. Interoperate; do not extend

We implement the protocol that other clients speak. We do not invent messages,
we do not negotiate private extensions, and we tolerate extensions we have not
implemented rather than breaking on them.

**Consequence.** Jamtaba's `JTBv` video intervals are filtered on fourCC and
ignored, so a Jamtaba user in the room costs us nothing -- before that filter
existed they were queued as audio, creating a permanently silent channel stream
that swapped every interval forever. `OGGv` is the only fourCC we play. Unknown
message types are skipped, not fatal.

The same principle points outward: we do not push tempo into the DAW, because
there is no protocol-sanctioned way to do it (see fence #7).

## 11. Accessibility is a goal, not a feature

Antiphon is deliberately narrow -- connect, set levels, transmit, chat, stay in
time -- and that narrowness makes it a realistic candidate for being *genuinely*
usable with a screen reader rather than nominally compatible with one. That is a
goal of the project, and it is designed for rather than retrofitted.

**Consequence.** Three rules follow.

- **Every control that takes focus has a name and a description.** Single-glyph
  buttons (`M`, `S`, `TX`, `R`, `+`, `-`) are the ones that matter most, and
  values are spoken with units ("-12.0 dB", "left 20") rather than as bare
  numbers. Strips and player cards are focus containers, so a reader navigates
  player by player instead of through forty flat controls with repeating names.
- **Only discrete events are announced.** Meters and interval position change
  tens of times a second; announcing them would make the app less usable, not
  more. Continuous values are available on demand by focusing the control.
- **Coverage is checked mechanically.** JUCE has no AT-SPI backend, so on Linux
  a reader-based check would pass vacuously while the annotations rotted.
  `AccessibilityAudit` walks the component tree and reports controls that would
  announce as nothing or as their neighbour; its rules are unit-tested
  headlessly.

The long form, including the honest platform table and the known gaps, is
`docs/ACCESSIBILITY.md`.

## 12. The surface announces its state

A player mid-jam is not reading. Connection state, sync state and tempo
agreement must be legible from colour and motion before any text is parsed.

**Consequence.** The header tints navy / grey / amber by connection state; the
phase bar runs teal when connected and grey, still, when not; the chat panel
ghosts when there is nothing to send to; toolbar buttons disable rather than
failing silently; TX and Recv are green/red coded. A state that is only
discoverable by reading a status string is a state the UI has not finished
announcing -- and per §11 it is also a state a reader user cannot get to.

---

## Non-Goals -- what Antiphon refuses to become

The standing refusals. `NON-GOALS.md` is the **authoritative catalogue** -- it
carries the long form: what prompted each fence, the principle that rejects it,
and what we offer instead. The numbering here mirrors that catalogue exactly, so
`fence #N` resolves to the same entry in both files. This index is a pointer, not
a second copy -- edit the fence text in `NON-GOALS.md`.

| # | Refusal | Rejected by |
|---|---|---|
| 1 | Video (Jamtaba's `JTBv` extension) | §2, §6, §10 |
| 2 | Being a standalone host or DAW | §2 |
| 3 | Replacing the DAW's mixer or recorder | §2 |
| 4 | Hosting a Ninjam server | §2 |
| 5 | Wrapping `NJClient` or any existing client | §6 |
| 6 | Timeline / loop-point integration | §1, §2 |
| 7 | Pushing tempo into the DAW | §10 |
| 8 | MIDI or other non-audio channels | §10 |
| 9 | A social layer (accounts, profiles, presence) | §2 |
| 10 | Codecs other than Ogg Vorbis | §10 |
| 11 | A "zero-latency" or low-latency jam mode | §1 |
| 12 | Feature parity with the reference client | §2, §4 |
