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

## Tempo and interval limits (read and MEASURED 2026-08-14)

Read to settle a confusing observation: a BPI of 124 was set from JamTaba
against a live server, persisted across a reconnect, and yet `MAX_BPI` is 64.

Both are true, because there are **two** paths with **different** limits:

| Path | BPM | BPI | Gate | Source |
|---|---|---|---|---|
| `!vote bpm\|bpi <n>` | 40..400 | 2..64 | anyone | `server/usercon.h:57-60`, applied `usercon.cpp:1169,1174` |
| `/bpm <n>`, `/bpi <n>` | 20..400 | 2..1024 | `PRIV_BPM` | literals at `usercon.cpp:1481-1498` |

- The strings "BPM parameter must be between 20 and 400" and "BPI parameter
  must be between 2 and 1024" are **the server's**, from the admin path
  (`usercon.cpp:1488,1497`). They are easy to mistake for a client's own
  validation, because they arrive as an ordinary `MSG`.
- An out-of-range `!vote` is **not** told it was out of range. The bounds test
  is folded into the same condition that recognises the subcommand, so failing
  it falls through to "[voting system] !vote requires <bpm|bpi> <value>
  parameters" (`usercon.cpp:1184`) -- a complaint about a command whose shape
  was fine.
- `!vote` accepts **bpm and bpi only**. Any other `!command` gets "Unknown
  !command. Commands available: !vote, !topic" (`usercon.cpp:1288`). There is
  no key anywhere in the protocol.

### Vote threshold

`(vucnt * m_voting_threshold + 50) / 100` (`usercon.cpp:1239`) -- integer
division of a round-half-up, **not** a ceiling. `vucnt` counts every user with
`m_auth_state > 0` (`usercon.cpp:1192-1200`): everyone connected, whether they
voted or not. So not voting counts against the motion.

`SetVotingThreshold` is a percentage set in the server config; `example.cfg:60`
documents "can be 1-100%, or >100 to disable". A client never has to know it --
the threshold arrives as the denominator of `N/M` in the vote line.

Consumed by `ChatFormat::isVotableBpm`/`isVotableBpi` and documented for players
in `docs/PROTOCOL.md`.

### Measured, not just read

Against a real `ninjamsrv` built by `scripts/testserver.sh` at the pinned
revision, with `SetVotingThreshold 50` and a user holding `CBTKV`. Every line
below is the server's own reply:

```
ADMIN 'bpi 125'      -> CONFIG bpm=120 bpi=125      "tester sets BPI to 125"
ADMIN 'bpi 1000'     -> CONFIG bpm=120 bpi=1000     "tester sets BPI to 1000"
ADMIN 'bpi 1025'     -> "BPI parameter must be between 2 and 1024"
ADMIN 'bpm 39'       -> CONFIG bpm=39  bpi=1000     "tester sets BPM to 39"
MSG '!vote bpi 125'  -> "[voting system] !vote requires <bpm|bpi> <value> parameters"
MSG '!vote bpi 64'   -> "[voting system] setting BPI to 64"   (1 user, 50%)
MSG '!vote key Cm'   -> "[voting system] !vote requires <bpm|bpi> <value> parameters"
```

Four things this pins down that reading alone left ambiguous:

1. **The admin path really does reach 1024, and a BPM of 39 really is settable.**
   A room can legitimately sit at 39 BPM / 1000 BPI. Any client that will not
   display that is wrong about the room.
2. **The two paths produce completely different errors for the same number.**
   `bpi 125` is accepted by ADMIN and rejected by `!vote`, and the `!vote`
   rejection blames the command's *parameters*. Anyone diagnosing "125 was
   refused" needs to know which path they used first.
3. **Without `PRIV_BPM` the admin path says so plainly** -- "No BPM/BPI
   permission" -- and with `SetVotingThreshold` unset, voting answers
   "[voting system] Voting not enabled". Neither is a range problem, and both
   look like one from a distance.
4. **`!vote key Cm` is consumed and answered with an error.** It is *not*
   relayed to the room as ordinary chat, so no other client ever sees it. Any
   scheme that hoped to tally a key vote by watching `!vote key` lines in chat
   cannot work -- see `docs/BOT-CHAT.md`.
