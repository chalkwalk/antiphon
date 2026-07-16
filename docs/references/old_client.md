# Old Client (Original Ninjam Client)

> Reading notes from the design phase, kept for the reasoning they record.
> Repository revisions and licensing: [SOURCES.md](SOURCES.md). Where these
> notes disagree with `DESIGN.md`, `DESIGN.md` is right.

## Overview
The `old_client` reference is the original open-source Ninjam client implementation developed by Cockos (the creators of Reaper). It consists of the core Ninjam protocol logic and uses WDL (Whale's Dev Library) for utility functions, networking, and Ogg/Vorbis encoding/decoding.

## Architecture
- **Language**: C++
- **Dependencies**: WDL, libvorbis, libogg
- **Core Components**:
  - `njclient.cpp` / `njclient.h`: The main protocol implementation. Handles the state machine for connecting, authenticating, and managing remote channels.
  - `netmsg.cpp`: Network message framing for the Ninjam protocol.
  - `mpb.cpp` (Multi-Participant Buffer): The core audio scheduling engine. It manages the buffering and synchronized playback of incoming audio streams based on the interval timer.
  - `WDL/vorbisencdec.h`: The Ogg/Vorbis wrappers. This is a large, header-only implementation (~3700 lines) that handles the buffering, encoding, and decoding of floating-point audio into Ogg packets, and vice versa. It relies on standard Ogg/Vorbis libraries but adds significant buffering and memory management on top.

## Relevance to Our Project
- **Protocol Reference**: It is the definitive source for how the Ninjam binary protocol works, especially for edge cases not documented elsewhere.
- **Audio Scheduling**: The `mpb.cpp` logic provides a reference for how to buffer and align remote streams to the local interval bounds.
- **WDL Usage**: As seen in other clients (`abNinjam` and `ninjam-next-plugin`), it is common practice to simply vendor this WDL code rather than write a custom Ogg/Vorbis wrapper from scratch. Antiphon started that way and then stopped: `src/VorbisCodec.{h,cpp}` and `src/Sha1.{h,cpp}` replaced the two WDL headers that had been taken, and no WDL code remains in the product. See `THIRDPARTY.md`.

## Drawbacks
- The codebase is quite old and uses archaic C++ naming conventions and memory management (e.g., heavily reliant on `malloc`/`free` and manual pointer manipulation, which resulted in the `-Wsign-conversion` warnings during our CMake build).
- The WDL wrappers are extremely verbose (~3700 lines) compared to a modern C++ wrapper around `libvorbis`.
