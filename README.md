<img src="docs/images/logo.png" alt="" width="140" align="right">

# Antiphon

[![Build](https://github.com/chalkwalk/antiphon/actions/workflows/build.yml/badge.svg)](https://github.com/chalkwalk/antiphon/actions/workflows/build.yml)
[![Licence: GPLv3](https://img.shields.io/badge/licence-GPLv3-blue.svg)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-antiphon.chalkwalkmusic.com-00b4d8.svg)](https://antiphon.chalkwalkmusic.com)

**Jam with strangers, from inside your DAW.**

Antiphon is a [NINJAM](https://www.cockos.com/ninjam/) client shaped as an audio
plugin -- VST3, CLAP, AU on macOS, and a standalone app. You insert it in your
DAW, connect to a public server, and play with whoever is in the room. Your own
signal passes through with no added delay. Everyone else arrives one interval
later, locked to the beat, and each of them can be routed to its own output bus
so your DAW records the jam as separate stems. (The AU build is the exception:
Audio Unit cannot change its bus layout at a plugin's request, so it is one
stereo in and one stereo out, and stems need the VST3 or CLAP build.)

Every other NINJAM client is a standalone application that owns your sound card.
Antiphon is the plugin, so the jam happens in the session you were already
working in -- with your own effects, your own monitoring, and your own recorder.

It also runs **standalone**, straight into an audio interface, if you would
rather not open a DAW at all. That is a supported way to use it, with one
limitation worth knowing up front: standalone gives you a single input and a
single output, so the multi-channel transmit and per-player stem recording below
are plugin-only features.

![The Antiphon window: local channel strips on the left, remote players in the
centre, chat on the right](docs/images/antiphon.png)

---

## Table of contents

1. [Status](#status)
2. [Quick start](#quick-start)
3. [The practice room](#the-practice-room)
4. [How a NINJAM jam works](#how-a-ninjam-jam-works)
5. [The interface](#the-interface)
6. [Walkthroughs](#walkthroughs)
7. [Chat and voting](#chat-and-voting)
8. [Accessibility](#accessibility)
9. [Troubleshooting](#troubleshooting)
10. [Licensing](#licensing)
11. [How this was built](#how-this-was-built)
12. [Contributing](#contributing)

---

## Status

**Public beta.** There is no installer, but you do not have to build it:
every push to `main` publishes VST3, CLAP and standalone builds for Linux,
macOS and Windows as [CI
artefacts](https://github.com/chalkwalk/antiphon/actions/workflows/build.yml).
Open the newest green run and take the one for your platform.

Three honest caveats about those downloads: GitHub requires you to be signed in
to fetch them, they expire after a while, and **the macOS and Windows builds
have never been loaded in a host** -- CI compiles and tests them, which is not
the same thing. The macOS build is also unsigned, so Gatekeeper will object.

Where things stand:

| | |
|---|---|
| **Works** | Connecting, transmitting, receiving, multi-channel, stem routing, chat, voting, the metronome, DAW tempo sync |
| **Verified** | Interoperability with the official NINJAM reference client, measured -- interval grid, transmit alignment, audio in both directions, chat. See [`docs/PARITY.md`](docs/PARITY.md) |
| **Used on** | Linux, CLAP format, one DAW, plus the standalone. Once on macOS, as an AU: built, loaded in a host and used to join a jam, by a contributor on their own machine |
| **Builds on** | Linux, macOS and Windows -- all three compile and pass the full unit suite in CI, with no platform-specific source |
| **Not yet** | Loaded in a host on Windows: nothing there has opened a window, opened a device or joined a jam. macOS has been through that path exactly once, by hand -- it is not regularly tested and nothing automated instantiates the plugin on any platform. No packaged installers, no release |

On Linux it works today, whether you take the CI build or compile it yourself.
macOS has been run for real once, including with a screen reader, and the
report was good -- but once is once, and it is not part of any automated check,
so treat it as encouraging rather than as a guarantee. Windows builds and tests
clean, and "compiles and passes its tests" is not the same as "works in your
DAW". A signed, packaged release is [on the roadmap](ROADMAP.md); the CI builds
are what exist until then.

---

## Quick start

### Build

You need CMake, a C++17 compiler, and git.

```bash
git clone --recurse-submodules https://github.com/chalkwalk/antiphon.git
cd antiphon
cmake -B build
cmake --build build -j $(nproc)
```

If you cloned without `--recurse-submodules`:
`git submodule update --init --recursive`.

This produces:

- **Standalone** -- `build/src/Antiphon_artefacts/Standalone/Antiphon`
- **VST3** -- `build/src/Antiphon_artefacts/VST3/`
- **CLAP** -- `build/src/Antiphon_artefacts/CLAP/`
- **AU** -- `build/src/Antiphon_artefacts/AU/`, macOS only

Copy the VST3 or CLAP into wherever your DAW looks for plugins. On macOS the AU
goes in `~/Library/Audio/Plug-Ins/Components/`; it is there for Logic Pro and
GarageBand, which load nothing else. Prefer the VST3 or CLAP if your DAW takes
one, because only those two can give each remote player its own output bus.

### Your first connection

Launch the Standalone. Click **Connect...**, and in the dialog:

- Pick a server from the list (it is fetched live, so you can see which rooms
  have people in them), or type a host and port.
- Enter a username. Leave **Anonymous** ticked.
- Click Connect.

You should see the tempo appear in the header, the phase bar start moving, and
the metronome start clicking. Play something. In one interval's time, so will
everyone else.

`ninbot.com:2049` is the usual busy public server.

---

## The practice room

Playing to a one-interval delay is a strange feeling the first time, and a room
of strangers is a poor place to find that out. So Antiphon can start a band on
your own machine.

Open **Browse**, press **Practice room**. The header turns violet and reads
*Practice room -- your own band*, and four players appear in the mixer.

A fifth member is there to teach, with no instrument and no channel. It says
six things, each one when you have actually done the thing it is about, and
then it leaves for good -- so the room settles back to a band once you have got
it. Nothing needs silencing and there is nothing to dismiss.

It is a **real NINJAM room**, not a simulation: a server on the loopback
interface with four bots connected to it as ordinary clients. Everything works
exactly as it does in a public room -- the phase bar, per-player faders, routing
each bot to its own output bus, chat, DAW sync, recording and stems -- because
none of it knows the far side is local. Nothing reaches the internet, because
there is no internet involved.

The band arrives **silent** and tells you how to start it. Talk to them in chat:

| Say | And |
|---|---|
| `band, start` | they begin playing |
| `band, stop` | they play an ending, then wait, still there |
| `[key: D minor]` or `/key D minor` | the room changes key |
| `\| Am \| F \| C \| G \|` | they play those changes |
| `shake` | they find a different figure |
| `what are you playing?` | they tell you |
| `Ravo: quiet` | that one stops talking |
| `band, go home` | they leave |

Disconnecting shuts the room down.

The bots live in [chalkwalk-jambot](https://github.com/chalkwalk/chalkwalk-jambot),
which is JUCE-free and builds on its own; what stays here is the hosting.

---

## How a NINJAM jam works

**Read this part.** NINJAM does something unusual, and if you do not know what it
is you will conclude the plugin is broken.

### The interval

Time is divided into **intervals**. An interval is `BPI` beats long -- Beats Per
Interval -- at the server's `BPM`. A typical room is 16 BPI at 120 BPM, so an
interval is 8 seconds.

Every client records one complete interval of your playing, compresses it,
uploads it, and everyone else plays it back during the **following** interval.

```
        interval 1        interval 2        interval 3
you:   [ you play A ]    [ you play B ]    [ you play C ]
them:  [   silence   ]   [ you hear A ]    [ you hear B ]
```

So:

- **You hear yourself live.** Local audio is never delayed.
- **You hear everyone else exactly one interval late.**
- **Everyone else hears you one interval late**, in exactly the same way.

### Why this is good, actually

It sounds like a fatal flaw and it is the entire point. Real-time jamming over
the internet is impossible -- physics puts a floor under the latency, and 40 ms
is already enough to make a band fall apart. NINJAM does not fight that. It makes
the delay so long that it becomes musical.

What you get is a rolling call-and-response: everyone plays over the bar that
everybody else just played. A loop builds; you answer it; your answer becomes
part of the loop other people answer. It is a genuinely different way to play
with people, and once it clicks it is hard to stop.

That is what the name means. An antiphon is a response sung to the phrase that
just finished.

### What follows from it

- **The first interval is silent.** Nothing has arrived yet. This is normal.
- **A quiet interval may not arrive at all.** The audio codec only emits data
  every few kilobytes, so near-silence can produce nothing to send. If someone is
  barely playing, they may simply not appear that interval.
- **Everyone must agree on tempo.** The server sets BPM and BPI. In a DAW, your
  project tempo has to match, or nothing will line up -- see
  [Jamming from your DAW](#2-jamming-from-your-daw).
- **You can vote to change tempo.** See [Chat and voting](#chat-and-voting).

---

## The interface

```
+--------------------------------------------------------------------------+
|  server BPM | BPI | phase progress bar | host BPM | sync state            |
+---------------+----------------------------------------------+-----------+
| Local input   |  Remote players                               |  Chat     |
| channels      |  [Player A]   [Player B]   [Player C] ...     |  panel    |
+---------------+----------------------------------------------+-----------+
|  Connect | Channel:[+] | Input bus:[+][-] | Output bus:[+][-] | ...       |
+--------------------------------------------------------------------------+
```

The header tells you where you are at a glance, without reading: **navy** means
connected and in sync, **amber** means something needs your attention (a failed
connection, or a tempo mismatch), **grey** means idle. The phase bar runs teal
and moving when you are in the jam, grey and still when you are not.

### Local channels (left)

One strip per thing you send. Each strip, top to bottom:

| Control | Does |
|---|---|
| **Name** | What other players see. Name it "Guitar", not "Channel 1". |
| **Pan** | Position in the stereo field, for both your monitor and what you send. |
| **VU meters** | Your level, post-fader, after mono summing. |
| **Fader** | Volume, in dB. Affects both what you hear and what you send. |
| **M** | **Monitor mute.** Silences it *for you only*. The room still hears you. |
| **S** | **Monitor solo.** Same -- local only. |
| **TX** | **Transmit.** Turn this off and the room stops hearing you. Green = on. **Hold it** (or press Ctrl+Alt+Shift+T) to toggle it *and* apply that to the whole of the interval so far -- see below. |
| **Input bus** | Which plugin input feeds this channel. |
| **Mono** | Sum the bus to mono. Halves the data you send. |
| **Remove** | Delete the strip. Does not remove the bus. |

#### Transmit, and taking it back

TX gates the audio, not the whole interval. Toggle it part way through and the
interval still goes out, silent for the stretches you had it off -- toggle it
rhythmically and that is what the room hears. Leave it off for a whole interval
and nothing is sent at all.

Because Ninjam plays everything an interval late, you have until the end of the
current interval to change your mind. **Hold TX** (or press Ctrl+Alt+Shift+T) and
the toggle applies to the interval from its start:

- Holding it **on** shares what you have already played this interval.
- Holding it **off** takes the interval back before anyone hears it.

The button flashes and a screen reader says which happened. If the interval
boundary has already passed, it says so instead -- that one has gone.

Worth knowing: for this to work, audio you are **not** transmitting is held in
memory for up to one interval. It never leaves your machine unless you use the
gesture, but it is kept rather than discarded, and that is the price of being
able to change your mind.

The important distinction: **M and S are for you, TX is for them.** You can mute
yourself in your own headphones while still playing to the room, or keep
listening to yourself while going silent to everyone else.

### Remote players (centre)

One card per player, with all of their channels underneath their name. Each
channel gets a pan, a fader, a VU meter, M, S, an output bus and:

| Control | Does |
|---|---|
| **M** / **S** | Mute and solo, in your mix only -- never what others hear. Solo is one bus across local *and* remote channels, and it overrides mute, so a channel that is both muted and soloed is heard. |
| **R** (Recv) | **Stop downloading this channel.** Different from mute: mute receives the audio and silences it, Recv-off tells the server not to send it at all. It also silences the channel immediately rather than waiting for the server, and because it is upstream, solo cannot bring back a channel you are not receiving. |
| **Output bus** | Which plugin output this channel goes to -- this is how you record stems. |

Your routing is remembered per player, per channel. If someone drops out and
reconnects, they come back on the bus you put them on.

### Toolbar (bottom)

| Button | Does |
|---|---|
| **Connect...** | Opens the server browser. |
| **Disconnect** | Leaves the server. |
| **Channel: [+]** | Adds a local channel. |
| **Input bus: [+] [-]** | Adds or removes a plugin input bus. |
| **Output bus: [+] [-]** | Adds or removes a stereo output bus, for stems. |
| **Metronome** + slider | The click, and its volume. Off by default in a DAW, on in standalone. |
| **Sync** | Arms tempo sync to your DAW transport. See below. |
| **Chat** | Show or hide the chat panel. |
| **Test Tone**, **Save Tx Audio**, **Save Rx Audio** | Diagnostics. Test Tone transmits a known signal instead of your input; the Save toggles dump what you sent and received to your desktop as `.ogg` and `.wav`. |

Channels and buses are **independent**: adding a channel does not add a bus, and
several channels can share one.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Alt+C` | Focus the chat message box |
| `Ctrl+Alt+S` | Arm Sync |
| `Ctrl+Alt+A` | Write an accessibility audit report to the desktop |

Everything is also reachable by Tab. These use `Ctrl+Alt` to stay clear of what
your DAW claims, and none of them is the only way to reach its action.

---

## Walkthroughs

### 1. Your first jam (standalone)

No DAW required -- an interface, an instrument, and a room.

1. Launch the Standalone. The window appears immediately and then opens your
   audio device in the background. If the device cannot be opened -- most often
   because something else already has it -- Antiphon says so and shows you a
   device picker instead of failing silently. Pick one that works and press
   **Use this device**; it is remembered for next time.
2. **Connect...** -> pick a room with people in it -> username -> Connect.
3. The header goes navy, the tempo appears, the phase bar starts moving.
4. Play something. Watch your VU meter -- if it is not moving, your input is not
   reaching the plugin, which is an audio device problem, not a NINJAM one.
5. Wait one interval. The other players appear as cards in the middle, and you
   hear them.
6. Set levels: your own fader on the left, theirs in the middle. Remote players
   default to -12 dB, which is deliberate -- it is what every other client does,
   so you are not louder than everyone else in the room.

### 2. Jamming from your DAW

This is what Antiphon is for, and it takes one extra step: getting your DAW's
clock and NINJAM's clock to agree.

1. Insert Antiphon on your **master bus**.
2. Connect as above. The header will turn amber and tell you the tempo does not
   match.
3. **Set your DAW project tempo to the server's BPM**, shown in the header. The
   plugin cannot set it for you -- there is no way for a plugin to do that
   reliably across hosts.
4. The warning clears. The status now says *"Tempo matches. Press Sync, then
   start the DAW transport."*
5. Click **Sync**.
6. **Start your DAW transport.** The plugin locks the interval grid to that
   moment.
7. You are in. Header navy, phase bar moving, transmitting and receiving.

**Why the extra button?** Because re-syncing mid-jam cuts short the interval you
are currently transmitting, and everyone else hears that as a glitch. Sync only
ever happens when you ask for it -- an accidental stop and start of your
transport will not re-phase a jam in progress.

If you *do* want to re-sync (you moved something, you want to realign), press
Sync again and restart the transport.

A tip: set your DAW's loop to exactly one interval's length. Then the jam and the
session agree about where "one" is.

### 3. Recording the jam as stems

The reason to be in a DAW at all.

1. Get into a jam as above.
2. For each player you want separately: click **Output bus: [+]** to add a
   stereo bus, then set that player's channel to it in its dropdown.
3. In your DAW, route Antiphon's extra outputs to tracks. (How depends on the
   host -- in Reaper, the plugin's extra output channels appear on the track and
   you route them onward; in Bitwig, use the plugin's multi-out.)
4. Record-arm those tracks and hit record.

You now have every player on their own track, in time with your project, ready to
edit, process and mix like any other recording.

Bus 1 is the main mix. Anything you leave on bus 1 stays there, so you can record
a couple of people separately and leave the rest summed.

### 4. Sending more than one channel

If you are playing guitar and singing, send them separately so the room can mix
you properly.

1. **Input bus: [+]** to add a second plugin input, and route your second source
   to it in the DAW.
2. **Channel: [+]** to add a second strip.
3. Set the new strip's **Input bus** dropdown to bus 2.
4. Name them. "Guitar" and "Vox" beats "Channel 1" and "Channel 2" -- everyone in
   the room sees these names.
5. Tick **Mono** on anything that is genuinely mono. It halves what you upload.

Other players now see you as one card with two faders and can turn your guitar
down without turning your voice down.

### 5. Dropping out without leaving

- Turn **TX** off on your channels: you stay in the room, hear everything, chat,
  but nobody hears you. Good for tuning up, taking a call, or listening.
- Turn **M** on instead: the room still hears you, you do not.

---

## Chat and voting

The chat panel is on the right and can be hidden with the **Chat** button. It
ghosts out when you are not connected, and clears when you join a new session.

| You type | Does |
|---|---|
| `hello` | Says hello to the room |
| `/me plays a wrong note` | Third-person message |
| `/msg bob you there?` | Private message to bob |
| `/key Dm` | Tells the room the key (any client can type `/key D minor` at the start of a line) |
| `/chords Am F C G` | Tells the room the chords |
| `/chords ii V I` | The same, in degrees, once a key is set |
| `/topic Jam in D minor` | Sets the room topic |
| `!vote bpm 130` | Proposes a tempo change |
| `!vote bpi 8` | Proposes an interval length change |
| `/kick bob` | Admin only |

Votes need a majority of the room. When one passes, the tempo changes for
everyone at the next interval -- **including you**, so in a DAW you will need to
change your project tempo again and re-Sync.

Ninjam has no protocol field for a key or a chart, so both ride on chat as text
every other client shows plainly. A chart appears above the phase bar with each
chord where its change falls, so you can see the next one coming; the roman
numerals sit at the right of the row above. `| Dm7 | C# Csus |` is two bars, and
the second holds two chords, so Dm7 lasts twice as long as either of them.

Degrees are turned into chords by your own client before anything is sent, so
`/chords ii V I` leaves as `| Dm7 | G7 | Cmaj7 |` and everyone else in the room
sees chords they already understand. Each chord is spelled against the key
rather than the whole chart being spelled one way: D major takes sharps, and a
flattened second in it is still `Eb7`. If a chart makes the key obvious and nobody
has set one, Antiphon offers it on the chip under the chat -- and stays quiet
when the chords are genuinely ambiguous.

---

## Accessibility

Accessibility is a goal of this project, not an afterthought. Every control has a
name and a description, values are spoken with units ("-12.0 dB", "left 20"),
channel strips and player cards are focus containers so you navigate player by
player, and the header -- which is otherwise just pixels -- is exposed as text and
is the first thing Tab reaches.

Discrete events are announced: connecting, sync changes, tempo changes, votes,
players joining and leaving. Continuous values like meters are deliberately *not*
announced, because doing so would interrupt constantly; focus a control to hear
its value.

**Platform reality, stated plainly:** JUCE has screen-reader backends on macOS
(VoiceOver) and Windows (NVDA, JAWS, Narrator), and **none on Linux** -- Orca sees
an opaque window. The annotations are built anyway because the work is identical
on every platform and effective on two of three.

Full detail, including known gaps and what has *not* been verified, is in
[`docs/ACCESSIBILITY.md`](docs/ACCESSIBILITY.md).

---

## Troubleshooting

**"DAW tempo does not match the server"**
Set your project tempo to the BPM shown in the header. The plugin cannot change
it for you.

**"Press play in the DAW to join the jam"**
You have armed Sync. Start the transport.

**Nothing is happening at all, header is grey/amber**
Check the connection. Amber after a connect attempt means it failed -- wrong
host, wrong port, or the server refused the username.

**Nobody can hear me**
In order: is **TX** green on your channel? Is your VU meter moving? Is your fader
up? Is the header navy (you are only transmitting when in sync)?

**I cannot hear a specific player**
Check their **R** button -- if Recv is off, the server is not even sending you
their audio. Then check M, S on other channels (a solo elsewhere silences them),
and their output bus, in case they are going somewhere you are not listening to.

**Someone appears but is silent**
They may not be playing, or they may be playing so quietly that their interval
produced no data to send. Both look identical from here.

**The first interval was silent**
Expected. Nothing had arrived yet.

**The standalone says the audio device did not respond**

Something else is holding it -- a DAW, another audio application, or a sound
server in a bad state. Antiphon shows a device picker; choose a different output
and input and press **Use this device**. Your choice is saved, so the next launch
goes straight to it. Run it from a terminal to see the startup log: every step is
reported on stderr with an `[antiphon]` prefix.

**The standalone only offers me one input**

That is JUCE's standalone host, not a bug: one input bus and one output bus.
Multi-channel transmit and stem recording need the plugin in a DAW.

**Typing in a text field does nothing (Linux, in a DAW)**
This should be fixed -- it was a JUCE focus bug on Linux, patched in this repo.
If you still see it, that is a bug worth reporting.

---

## Licensing

Antiphon is **GPLv3** ([`LICENSE`](LICENSE)). JUCE is AGPLv3-or-commercial, and
the open-source route obliges anything built on it to be copyleft; GPLv3
satisfies that and is compatible with the Xiph and CLAP components.

Third-party components and their obligations -- JUCE, libogg/libvorbis,
clap-juce-extensions, the Inter typeface -- are documented in
[`THIRDPARTY.md`](THIRDPARTY.md). No NINJAM reference source is vendored here;
those were read as protocol documentation, and
[`docs/references/SOURCES.md`](docs/references/SOURCES.md) records what was read
and at which revision.

---

## How this was built

Antiphon was written by [ChalkWalk](https://github.com/chalkwalk) in
collaboration with **Claude Opus 5**, working against the constraints in
[`PRINCIPLES.md`](PRINCIPLES.md) and the standing instructions in
[`AGENTS.md`](AGENTS.md) -- which is checked in, and is the honest record of how
the work is actually done.

That is worth stating plainly rather than leaving to be inferred, and it is also
why this repository is unusually strict about evidence. Interoperability claims
are measured against the official client and written down with their method
([`docs/PARITY.md`](docs/PARITY.md)); the sanitiser baseline is zero warnings
with no remembered exceptions; every UI control is checked for a screen-reader
name by a test that fails the build. Those gates exist because a claim is only
worth what was done to check it.

---

## Contributing

Bug reports, cross-platform testing and screen-reader feedback are all wanted --
see [`CONTRIBUTING.md`](CONTRIBUTING.md). The most useful thing right now is
running it somewhere that is not Linux.

---

## For developers

- [`CONTRIBUTING.md`](CONTRIBUTING.md) -- how to contribute, and what is needed most
- [`AGENTS.md`](AGENTS.md) -- how to work in this repo
- [`PRINCIPLES.md`](PRINCIPLES.md) -- the twelve principles behind every decision
- [`NON-GOALS.md`](NON-GOALS.md) -- what Antiphon deliberately refuses to be
- [`DESIGN.md`](DESIGN.md) -- architecture
- [`ROADMAP.md`](ROADMAP.md) -- what is next
- [`docs/PROTOCOL.md`](docs/PROTOCOL.md) -- the NINJAM wire format
- [`docs/PARITY.md`](docs/PARITY.md) -- interoperability, measured
- [`docs/references/SOURCES.md`](docs/references/SOURCES.md) -- what was read to write this
- [`test/README.md`](test/README.md) -- the test suite
