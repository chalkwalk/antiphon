# Official Ninjam Reference Implementation

> Reading notes from the design phase, kept for the reasoning they record.
> Repository revisions and licensing: [SOURCES.md](SOURCES.md). Where these
> notes disagree with `DESIGN.md`, `DESIGN.md` is right.

## Overview
The official Ninjam repository contains the reference implementations for both the server and client, created by Cockos (the developers of REAPER). It serves as the baseline for the protocol and architecture.

## Architecture

### Libraries Used
It heavily relies on **WDL** (Whale's Dev Library), another open-source C++ library by Cockos.
- **JNetLib**: Used for networking and socket communication.
- **Vorbis/Ogg**: Used for audio compression.

### Server (`server/ninjamsrv.cpp`, `usercon.cpp`)
- **Routing, not mixing**: The server fundamentally does not decode or mix audio. It receives chunks of Ogg Vorbis data from each client and broadcasts them (multicast routing) to all other connected clients in the same channel/room.
- **Session Management**: Handles user authentication, configures the master BPM (Beats Per Minute) and BPI (Beats Per Interval).
- **Voting**: Administers the voting system where clients vote for a new BPM or BPI. Once a threshold is reached, the server broadcasts the new settings.
- **Chat**: Forwards chat messages globally or via private messages.

### Client (`njclient.cpp`)
- **Interval Sync**: Operating strictly on the interval (BPI/BPM). The audio is captured, encoded into Ogg Vorbis in blocks, and sent to the server.
- **Delayed Playback**: The core principle of Ninjam. Received streams are buffered and played back exactly one interval (e.g., 4 or 8 bars) later, synchronized with the local metronome.
- **Decoding**: It decodes streams from multiple users concurrently using `DecodeState` and overlaps the audio chunks to prevent clicking.
- **Local Channels**: Manages local audio encoding pipelines and local mixing (volume/pan) for each remote user.

## Takeaways for JUCE Plugin
- We must implement the Ogg Vorbis encoding/decoding on roughly the same chunk logic.
- The client must maintain strict interval timing, generating a local metronome, and pausing playback of remote streams until the interval boundary is hit.
- The networking involves sending and receiving bespoke Ninjam protocol headers followed by the compressed Ogg payloads.
