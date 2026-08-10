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

Practice mode goes further: it lets you play against your own past interval
without any of it reaching the server. Nothing you play in practice is ever
transmitted, at any layer. See [Syncing with your DAW](./daw-sync.md).
