---
id: intro
title: What Antiphon is
sidebar_position: 1
slug: /
---

# What Antiphon is

Antiphon is a **[NINJAM](https://www.cockos.com/ninjam/) client shaped as an
audio plugin** -- VST3, CLAP, and a standalone application. You insert it in your
DAW, connect to a public server, and play with whoever is in the room.

Your own signal passes through with no added delay. Everyone else arrives one
interval later, locked to the beat, and each of them can be routed to its own
output bus so your DAW records the jam as separate stems.

Every other NINJAM client is a standalone application that owns your sound card.
Antiphon is the plugin, so the jam happens in the session you were already
working in -- with your own effects, your own monitoring, and your own recorder.

## Read this before you decide it is broken

NINJAM does something unusual. **You hear everyone else one interval late** --
typically several seconds. This is not a bug, not a misconfiguration, and not
something to tune away. It is the whole idea, and
[Your first jam](./your-first-jam.md) explains why it turns out to be musical
rather than annoying.

If you take one thing from this guide, take that.

## Where things stand

Antiphon is **public beta, built from source.** Honestly:

| | |
|---|---|
| **Works** | Connecting, transmitting, receiving, multi-channel, stem routing, chat, voting, the metronome, DAW tempo sync |
| **Verified** | Interoperability with the official NINJAM reference client, measured -- interval grid, transmit alignment, audio in both directions, chat |
| **Tested on** | Linux, CLAP format, one DAW, plus the standalone |
| **Not yet** | Windows and macOS builds, packaged installers, VST3 testing, a release |

If you are on Linux and comfortable with CMake, it works today. If you are
waiting for a download, that is still ahead.

## Where to go next

- **[Getting started](./getting-started.md)** -- build it, install it, connect.
- **[Your first jam](./your-first-jam.md)** -- what the interval is, and why.
- **[Recording the jam as stems](./routing-stems.md)** -- the reason this is a
  plugin.
- **[Syncing with your DAW](./daw-sync.md)** -- tempo, transport, and practising
  alone.
- **[Accessibility](./accessibility.md)** -- what works, and what is unproven.
- **[Troubleshooting](./troubleshooting.md)** -- when something is wrong.
