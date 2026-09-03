---
id: your-first-jam
title: Your first jam
sidebar_position: 3
---

# Your first jam

NINJAM does something unusual, and if you do not know what it is you will
conclude the plugin is broken. So: this page, before anything else.

## The interval

Time is divided into **intervals**. An interval is `BPI` beats long -- Beats Per
Interval -- at the server's `BPM`. A typical room is 16 BPI at 120 BPM, so an
interval is eight seconds.

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

## Why this is good, actually

It sounds like a fatal flaw and it is the entire point.

Real-time jamming over the internet is impossible. Physics puts a floor under
the latency, and 40 ms is already enough to make a band fall apart. NINJAM does
not fight that. It makes the delay so long that it stops being latency and
becomes **form**.

What you get is a rolling call-and-response: everyone plays over the bar that
everybody else just played. A loop builds; you answer it; your answer becomes
part of the loop other people answer. It is a genuinely different way to play
with people, and once it clicks it is hard to stop.

That is what the name means. An antiphon is a response sung to the phrase that
just finished.

## How to actually play in it

The shift that makes it work is giving up on reacting. You are not playing
*with* what you hear; you are playing *to* it, for the people who will hear you
next interval.

- **Lay something down and let it come back.** The first couple of intervals are
  you talking to an empty room. Play something worth answering.
- **Think in whole intervals.** A phrase that starts halfway through an interval
  arrives halfway through somebody else's. Land on the boundary.
- **Listen to the loop, not the moment.** What you hear is a complete, finished
  bar from every other player. Treat it as a backing track that changes every
  interval.
- **Do not chase the click.** The metronome is your grid, and it is the same
  grid everyone else is on.

## What follows from the interval

- **The first interval is silent.** Nothing has arrived yet. Normal.
- **A quiet interval may not arrive at all.** The audio codec only emits data
  every few kilobytes, so near-silence can produce nothing to send. If someone is
  barely playing, they may simply not appear that interval. Also normal.
- **Everyone must agree on tempo.** The server sets BPM and BPI. In a DAW your
  project tempo has to match, or nothing lines up -- see
  [Syncing with your DAW](./daw-sync.md).
- **You can vote to change tempo.** See [Chat and voting](./chat-and-voting.md).

## Transmitting, and not transmitting

You do not have to send anything. A channel transmits only when you tell it to,
and you can **drop out without leaving** -- stop transmitting, keep listening,
keep chatting. This is a normal and unremarkable thing to do in a NINJAM room,
whether you are tuning up, taking a break, or just there to listen.

## The practice room

Playing to a one-interval delay is a strange feeling the first time, and a room
full of strangers is a poor place to discover that. So Antiphon can start a
**practice room**: a band of four bots on your own machine that you can play
with, and talk to.

Open **Browse** and press **Practice room**. The header turns violet and reads
*Practice room -- your own band*, and four players appear in the mixer.

There is a fifth name in the room with no instrument. It is there to teach: it
says six things, each one at the moment you have done the thing it is about --
your first interval, your first key change -- and then it says so and stops.
You will not need to dismiss it, and the lessons do not come back.

It stays in the room after that, because it is also the one that answers for the
band -- who is here, what the key is, whether everyone is playing. Six lessons,
and then it only speaks when the room does something or you ask it something.

It is a real NINJAM room, not a simulation. The server runs on your own machine
and the bots are ordinary clients on it, so everything works exactly as it does
in a public room -- the phase bar, per-player faders, routing each bot to its
own output bus, chat, DAW sync, recording and stems. Nothing you do there
reaches the internet, because there is no internet involved.

The band arrives **silent** and tells you how to start it. Talk to them in chat:

| Say | And |
|---|---|
| `band, start` | they begin playing |
| `band, stop` | they play an ending and go quiet, still there |
| `[key: D minor]` or `/key D minor` | the room changes key |
| `\| Am \| F \| C \| G \|` | they play those changes |
| `band, shake` | they find a different figure |
| `band, what are you playing?` | they tell you |
| `band, go home` | they leave |

**Say who you mean.** The bots ignore anything not addressed to them -- `band`,
`everyone`, `bots`, or one player's name. They are ordinary NINJAM clients that
can join any server, and a band that answered every line typed between two
people would be unusable in a room with people in it. Once you have spoken to
one it keeps listening to you for a few turns, so a bare `shake` works in the
middle of a conversation and not out of the blue.

Disconnecting shuts the room down.
