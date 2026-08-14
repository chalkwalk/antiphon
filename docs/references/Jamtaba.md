# Jamtaba Reference Implementation

> Reading notes from the design phase, kept for the reasoning they record.
> Repository revisions and licensing: [SOURCES.md](SOURCES.md). Where these
> notes disagree with `DESIGN.md`, `DESIGN.md` is right.

## Overview
**Jamtaba 2** is one of the most complete and widely used Ninjam clients. It can be run as a standalone application, a VST plugin (Windows only), or an AU plugin (macOS only). The standalone version can even host other VST/AU instruments and effects.

## Architecture

### Libraries and Tools
- **Qt Framework**: Jamtaba relies heavily on Qt for its GUI, threading, sockets, JSON processing, and HTTP requests.
- **Audio/MIDI**: Uses `portaudio` for standalone audio I/O and `rtmidi` for MIDI processing.
- **Encoding/Decoding**: Uses `minimp3`, `libvorbis`, and `libogg`.

### Key Features
- **Standalone Host**: The standalone app is basically a mini-DAW. Users can plug in their camera, audio, and load plugins directly within Jamtaba.
- **Plugin Versions**: The VST and AU versions allow users to insert Jamtaba into their DAW. However, due to Qt's shared library issues inside DAWs, statically compiling Qt is necessary for the VST plugin, which makes the build process extremely complex.
- **Rich UI**: The Qt interface is highly polished, with visual metronomes, chat tabs, and mixer channels for every remote user.

## Takeaways for JUCE Plugin
- **Framework Choice**: Jamtaba proves that using a heavy framework like Qt for a plugin can be problematic (evident by the complex static-compile instructions for the VST version). JUCE is specifically designed for VST/AU/AAX plugin development, ensuring we won't face the same static-linking build nightmares on Windows and macOS.
- **Complexity**: Jamtaba is "much more complex than what I want". We want to keep our plugin clean and simple, focusing specifically on operating inside a DAW (as an effect plugin on the master bus) rather than trying to become a standalone host.
- **I/O Routing**: Our plan to have 8 input and 8 output buses that can dynamically be instantiated is a more flexible, DAW-centric approach than treating the plugin as a fixed standalone application.

## Tempo and interval limits (read 2026-08-14)

Read alongside the reference server to explain why a BPI of 1024 typed into
JamTaba does nothing at all, with no error shown.

`src/Common/ninjam/client/ServerInfo.h:154-157`:

```
MIN_BPM = 40      MAX_BPM = 400
MIN_BPI = 2       MAX_BPI = 192
```

Two things follow, and both are traps for anyone comparing clients:

- **These are JamTaba's own, and they are tighter than the server's admin path**
  (which allows 2..1024 BPI and 20..400 BPM). A value JamTaba refuses may be
  perfectly legal on the server.
- **The refusal is silent.** `ServerInfo.cpp:117` and `:130` apply the new value
  only `if` it is in range, with no `else` -- so an out-of-range BPI is dropped
  on the floor and the server never hears about it. Nothing appears in chat,
  and the tempo simply does not change.

JamTaba also rejects `!vote key ...` client-side before it reaches the wire,
which matches the server: `!vote` is bpm and bpi only.

The practical lesson for us: **a value being refused tells you nothing about
which side refused it.** Ours are in `ChatFormat`, named for the path they
belong to.

### The silent-drop bug, seen from outside

`ServerInfo::setBpm`/`setBpi` (`ServerInfo.cpp:112-134`) are the **incoming**
setters -- what applies the tempo the server reports. Both are written as

```cpp
if (bpm >= MIN_BPM && bpm <= MAX_BPM) { this->bpm = bpm; return true; }
return false;
```

with no `else`. Against a server legitimately at 39 BPM (settable by an admin,
below the 40 vote minimum), JamTaba therefore **keeps showing the previous
tempo** and never mentions it. Antiphon shows 39, which is correct, and looks
wrong beside it.

Worth remembering when a bug report says "your client shows a different tempo
from JamTaba": the two clients disagreeing does not tell you which one is
following the server.

For contrast, the reference client does none of this:
`NJClient::updateBPMinfo` (`justinfrankel/ninjam njclient.cpp:725-732`) assigns
`m_bpm` and `m_bpi` with no validation whatsoever. ReaNINJAM is built on that
client, so it is the canonical behaviour and JamTaba is the outlier. We match
the reference. There is no case for bug-for-bug parity here -- but there is a
case for not *creating* a room state JamTaba cannot follow, since it is the
most widely used client and it fails silently rather than loudly.
