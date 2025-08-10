# CLAUDE.md — Ninjam JUCE Plugin

This is the canonical reference for the project. `GEMINI.md` is the original design doc and is kept for historical context; this file supersedes it as the working map.

## What this is

A clean, cross-platform (Linux / Windows / macOS) Ninjam client as a **JUCE audio plugin** (VST3 + CLAP + Standalone). The plugin lives inside a DAW, typically on the master bus, and injects Ninjam's interval-delayed playback model into the host. Local audio passes through without delay; remote players' audio arrives one interval late, phase-locked to the local metronome.

Design principles: clean modern C++ against JUCE — no NJClient wrapper, no Qt, no heavy dependencies. Simple, DAW-centric, not a standalone host.

---

## What's built

All of GEMINI.md phases 1–6 are complete:

- JUCE plugin skeleton: VST3 + CLAP + Standalone, CMake build, stereo I/O
- Host sync: reads DAW BPM/PPQ via `AudioPlayHead`; displays mismatch warning
- Internal metronome: BPI/BPM-based click, 880 Hz downbeat / 440 Hz beats, synthesised in `processBlock`
- Networking: full TCP connect/auth/keepalive loop in a `juce::Thread`
- Protocol: auth challenge/reply, server config, user info, interval begin/write, chat, keepalive
- Ogg/Vorbis encode/decode via vendored WDL `vorbisencdec.h` + `libogg`/`libvorbis` submodules
- Double-buffered interval playback: `IntervalBuffer` per remote channel, swapped at metronome "1"
- Remote mixer: per-user volume/pan/mute/solo applied in `getDecodedAudio`
- Local transmit mixer: volume/pan/mute applied before encoding in `processBlock`
- Chat: receive + display, send MSG/ADMIN/PRIVMSG, voting commands
- UI: dark theme via `NinjamLookAndFeel`, remote user strips, chat panel, debug Tx/Rx file dumps

What's **not** yet right about the current state is captured in the work table below.

---

## Build & run

```
mkdir -p build && cd build && cmake ..      # configure (first time, from repo root)
cmake --build build -j $(nproc)            # build (always from repo root)
```

No generator flag — use whatever CMake picks (usually Ninja or Make). Targets:
- `Ninjam_Standalone` — easiest for iteration
- `Ninjam_VST3` — the VST3; CLAP is built from the same target via `clap_juce_extensions_plugin`

Testing is manual: launch Standalone, connect to `ninbot.com:2049` as `anonymous`, exercise the feature. No automated test suite.

**Debug dumps:** UI toggles **Save Tx / Save Rx** write `tx.ogg`, `tx.wav`, `rx.ogg`, `rx.wav` to the desktop. Useful for diagnosing encode/decode problems without a full round-trip.

---

## Code map

All our code lives in `src/` — ~2 k lines, fits in your head.

| File | Role |
|---|---|
| `PluginProcessor.{h,cpp}` | `juce::AudioProcessor`. Owns `NinjamClient`. Runs host sync, internal metronome, interval boundary detection, and local capture in `processBlock`. Holds single-channel local Tx mixer state (volume/pan/mute) and feature toggles. |
| `PluginEditor.{h,cpp}` | Main UI. 30 Hz `Timer` syncs `RemoteUserStrip` children to `NinjamClient::getRemoteUsers()`. Chat input, connection inputs, local mixer sliders, debug toggles. |
| `NinjamClient.{h,cpp}` | Networking + protocol + encode/decode. `juce::Thread` subclass. Socket read loop, message dispatch, remote-user state, chat log, interval buffers, Tx/Rx file dumps. ~half the complexity. |
| `RemoteUserStrip.{h,cpp}` | One horizontal mixer strip per remote user. Controls only channel 0 of each user. |
| `NinjamLookAndFeel.{h,cpp}` | `LookAndFeel_V4` subclass. Dark blue theme, teal accent (#00b4d8). Custom rotary, button, toggle, text editor outline. |
| `utils/` | Vendored WDL headers: `vorbisencdec.h` (Ogg/Vorbis wrapper), `sha1.{h,cpp}`, `heapbuf`, `queue`, `wdlstring`, etc. Editable if necessary; treat as near-third-party. |

---

## Runtime architecture

**Threads:**

| Thread | What it does |
|---|---|
| Audio (`processBlock`) | Host sync read, metronome click synthesis, interval boundary detection, local audio capture into `captureBuffer`, mixing decoded remote streams into output via `getDecodedAudio`. At the interval boundary, fires `callAsync` to hand off the captured block. |
| `NinjamClient` (`run()`) | Socket read loop, keep-alive every 3 s, `handleMessage` dispatch. All listener callbacks are bounced to the message thread via `callAsync`. |
| Message thread | `onConnected`, `onServerConfig`, `onChatMessage`, etc. Also runs `processCapturedAudio` (encoding) — intentionally off the audio thread. |
| UI timer (30 Hz) | `timerCallback`: syncs remote user strips to `getRemoteUsers()`, calls `repaint()`. |

**Shared state:** protected by `juce::CriticalSection` — `downloadMutex` (covers `activeDownloads` + `remoteUsers`), `chatMutex`, `txFileMutex`, `rxFileMutex`.

**Interval buffer model** (`NinjamClient::IntervalBuffer`, one per remote channel): double-buffered `juce::AudioBuffer`. The network thread decodes into `backBuffer`; the audio thread reads from `frontBuffer`. `swapIntervalBuffers()` at the metronome "1" flips them — this realises the one-interval playback delay.

**Interval detection** (`PluginProcessor::processBlock`): uses `internalBpm`/`internalBpi`, not the host's. Server BPM/BPI updates overwrite these in `onServerConfig`. Host BPM/PPQ is read only for display and the mismatch warning.

---

## Protocol crib sheet

All message framing: 1-byte type + 4-byte little-endian payload length + payload.

| Msg | Direction | Hex | Notes |
|---|---|---|---|
| SERVER_AUTH_CHALLENGE | S→C | `0x00` | 8-byte challenge + more; we read first 8 |
| SERVER_AUTH_REPLY | S→C | `0x01` | 1-byte flag: 1 = granted, 0 = denied |
| SERVER_CONFIG_CHANGE | S→C | `0x02` | 2-byte BPM + 2-byte BPI, big-endian uint16 each |
| USER_INFO_CHANGE | S→C | `0x03` | List of: active(u8), chIdx(u8), vol(i16-be), pan(i8), flags(u8), username(NUL), chanName(NUL) |
| DOWNLOAD_INTERVAL_BEGIN | S→C | `0x04` | 16-byte GUID + 4-byte estSize + 4-byte fourCC + 1-byte chIdx + NUL-term username. fourCC `OGGv` = audio; `JTBv` = Jamtaba video (skip). |
| DOWNLOAD_INTERVAL_WRITE | S→C | `0x05` | 16-byte GUID + 1-byte flags (1 = final) + Ogg payload |
| CHAT_MESSAGE | S↔C | `0xC0` | 5 NUL-terminated strings: type, then type-specific params. Types: MSG, PRIVMSG, TOPIC, JOIN, PART, ADMIN |
| KEEP_ALIVE | C→S | `0xFD` | Zero-byte payload; send every 3 s |
| CLIENT_AUTH_USER | C→S | `0x80` | 20-byte SHA1 + NUL-term username + 4-byte caps (LE) + 4-byte version (LE). Hash = `SHA1(SHA1(user+":"+pass) + challenge[0..8])`. Caps = 1, version = `0x00020000`. |
| **CLIENT_SET_USERMASK** | **C→S** | **`0x81`** | **List of: NUL-term username + 32-bit channel bitmask (LE). Bit N = subscribe to channel N. Mask 0 = unsubscribe entirely (server stops forwarding that user's audio). Must send this for every user on USER_INFO_CHANGE to receive any audio. Currently not implemented — see work table.** |
| CLIENT_SET_CHANNEL_INFO | C→S | `0x82` | Announces our local channel names. Payload: list of NUL-term channel name strings. |
| UPLOAD_INTERVAL_BEGIN | C→S | `0x83` | 16-byte GUID + 4-byte nch (LE) + NUL-term channel name. fourCC field omitted (server infers Ogg). |
| UPLOAD_INTERVAL_WRITE | C→S | `0x84` | 16-byte GUID + 1-byte flags (1 = final) + Ogg chunk |

**Reference**: `references/ninjam/ninjam/mpb.h` for message constants, `server/usercon.cpp` for server-side behaviour, `njclient.cpp` for client-side reference flows.

---

## Prioritised work

Legend — **Complexity**: S = hours (self-contained), M = 1–3 days, L = 3–7 days, XL = weeks.  
**UX impact**: Critical = blocks core use case; High = major daily improvement; Medium = noticeable; Low = polish.

| # | Item | Type | Complexity | UX Impact | Key notes |
|---|---|---|---|---|---|
| 1 | Send `SET_USERMASK` (0x81) on `USER_INFO_CHANGE` | Bug | S | Critical | Without this, any standards-compliant server won't forward audio to us. Auto-subscribe to all channels for every user on `USER_INFO_CHANGE`. Reference: `njclient.cpp:1184–1189`. Also the underlying mechanism for the Recv toggle (#11). |
| 2 | Filter non-`OGGv` fourCC in `DOWNLOAD_INTERVAL_BEGIN` | Bug | S | Medium | We currently create a `VorbisDecoder` for every interval regardless of fourCC. Jamtaba users send `JTBv` video intervals; trying to Vorbis-decode them is wrong. Check fourCC at `NinjamClient.cpp:295–350` and skip non-`OGGv`. |
| 3 | Metronome default: on in Standalone, off in plugin | Bug | S | Medium | `metronomeEnabled` always initialises `true`. In plugin/DAW mode the DAW's own click is already running; default should be off. Detect with `juce::PluginHostType` or check `JucePlugin_Build_Standalone`. |
| 4 | Add password field and port to connection UI | Feature | S | Medium | Currently hardcodes empty password and port 2049. Add: port field (default 2049), anonymous toggle, password `TextEditor` shown only when not anonymous. |
| 5 | Tempo sync UX | Feature | M | High | Prominent status bar: server BPM, host BPM, phase progress bar. Clear mismatch warning (audio stops when BPM differs). "Pending" state when BPM matches but transport not running → "Start transport to begin." Clears automatically when transport starts. Standalone: always-running, no transport concept, follows server BPM/BPI. |
| 6 | Server browser panel / popup | Feature | M | High | Static list of public servers (hand-curated). Optionally fetch live room info from `http://ninbot.com/app/servers.php` (Jamtaba uses this; JSON with BPM, BPI, player list per room). Show room state before connecting. Also folds in #4 (credentials). |
| 7 | Send `CLIENT_SET_CHANNEL_INFO` (0x82) | Feature | S | Low | Formally announces our local channel names to the server. Currently channel name only appears in each `UPLOAD_INTERVAL_BEGIN`. Should be sent on connect and on any channel name change. |
| 8 | Dynamic local input channels | Feature | L | High | Add/remove local channel strips. Each channel: mono or stereo (adds/removes plugin-level input buses accordingly). Requires: refactoring `captureBuffer` from a single stereo buffer into a vector of per-channel capture buffers; `PluginProcessor` needs a `LocalChannel` struct list instead of the current single `localTxVolume/Pan/Mute`. This is the largest single architecture change. |
| 9 | Per-channel Xmit toggle | Feature | M | High | Each local channel independently transmits or not. Mid-interval toggle → send silence for the rest of the current interval, then stop. Needs per-channel encoder state in `NinjamClient::processCapturedAudio`. Depends on #8. |
| 10 | Monitor mix vs transmit level separation | Feature | M | High | Current `localTxVolume/Pan/Mute` controls both what you hear and what is encoded. Split into two independent gain stages: transmit gain (pre-encode) and monitor gain (post-mix, local only). Depends on #8. |
| 11 | Full vertical strip UI redesign | Feature | L | High | Current layout: horizontal sliders and text rows. Target: Jamtaba-style vertical strips with vertical faders and VU meters. Local strips on left, remote player cards in a horizontally scrollable centre, collapsible chat on right. This is a near-complete rewrite of `PluginEditor::resized()` and `RemoteUserStrip`. |
| 12 | VU meters on all channel strips | Feature | M | High | Atomic peak-level float per channel written in the audio thread, read at 30 Hz in the UI timer. Draw as a vertical bar in each strip. Needed on both local input strips and remote player strips. |
| 13 | Multi-channel remote user strips | Feature | M | High | `RemoteUserStrip` controls only channel 0. Need per-channel sub-rows (vol/pan/mute/solo/recv) within each player's card. Depends on #11 for final layout. |
| 14 | Recv toggle per remote channel | Feature | S | Medium | Toggle the corresponding bit in `SET_USERMASK` for that user/channel. Server stops forwarding audio (bandwidth saving; distinct from mute, which still downloads but silences). Depends on #1 and #13. |
| 15 | State persistence | Feature | M | Medium | Implement `getStateInformation`/`setStateInformation`. Save: server host/port, username, anon flag, local channel count/names/mono-stereo, mixer positions, metronome state. |
| 16 | Plugin output bus routing per remote channel | Feature | L | Medium | Route individual remote player channels to specific plugin output buses (for stems/multi-track in the DAW). Currently all remote audio goes to the main stereo output. Requires dynamic output bus management and a routing selector per remote channel strip. |
| 17 | `captureBuffer` → per-channel ring buffers | Architecture | M | Low | Current single fixed-size 2 min/48 kHz allocation can silently overflow in long intervals. Replace with per-channel `AbstractFifo`-based ring buffers sized to ~2 intervals. Depends on #8. |
| 18 | Lock-free interval handoff | Architecture | M | Low | `processBlock` currently calls `callAsync` with a full buffer copy at each interval boundary. Replace with a `juce::AbstractFifo` FIFO to eliminate the copy and reduce latency jitter. |
| 19 | Video support | Future | XL | High | Jamtaba-proprietary extension only. Not planned; see Video section below. |
| 20 | OSC tempo sync | Future | M | Medium | Send `/tempo/raw {bpm}` OSC to localhost when server BPM changes (so DAW can auto-adjust). Reference: `abNinjam`. Not planned. |

---

## UI/UX specification

This is the target design. The current implementation does not yet match it.

### Overall layout

```
┌──────────────────────────────────────────────────────────────────────────┐
│  [Status bar: server BPM | BPI | phase progress | host BPM | sync state] │
├───────────────┬──────────────────────────────────────────────┬───────────┤
│ Local input   │  Remote players (horizontally scrollable)    │  Chat     │
│ channel       │                                              │  panel    │
│ strips        │  [Player A]   [Player B]   [Player C] …     │           │
│ (vertical,    │   vert.strip   vert.strip   vert.strip       │ (default  │
│  left panel)  │   vol/pan/vu   vol/pan/vu   vol/pan/vu       │  visible, │
│               │   M  S  Recv   M  S  Recv   M  S  Recv       │  toggle-  │
│               │                                              │  able)    │
├───────────────┴──────────────────────────────────────────────┴───────────┤
│  [Metronome toggle + vol]  [+ Add channel]  [Server browser / Connect]   │
└──────────────────────────────────────────────────────────────────────────┘
```

Three columns: local input strips (left, fixed width), remote player strips (centre, horizontal scroll), chat (right, collapsible). Full-width status bar at top. Toolbar at bottom.

### Local input channel strips (left panel)

Each channel strip:
- **Editable channel name** — protocol-level; transmitted to the server in `CLIENT_SET_CHANNEL_INFO` and `UPLOAD_INTERVAL_BEGIN`; other players see it.
- **Mono/Stereo toggle** — determines how many plugin input buses this channel consumes.
- **Vertical VU meter** — live input level.
- **Vertical volume fader** — scales both local monitor and transmitted audio (see next point).
- **Pan knob**.
- **Monitor Mute / Monitor Solo** — affect only what you hear of your own audio in the plugin output; do not affect what is encoded and sent.
- **Xmit toggle** — whether this channel's audio is sent to the server. Mid-interval toggle: sends silence for the remainder of the current interval, then stops at the next boundary.
- **Remove button** — deletes the channel strip and its plugin input buses.

The **+ Add channel** button in the toolbar creates a new strip. Plugin starts with one stereo channel. **Local audio always passes through without delay**, regardless of connection state.

### Remote player strips (centre area)

One vertical card per player, scrollable horizontally. A player with multiple channels shows all their channels stacked inside the same card, under a shared username header.

Per channel within a card:
- **Channel name label** (as sent by the remote player).
- **Vertical VU meter** — post-fader decoded level.
- **Vertical volume fader**.
- **Pan knob**.
- **Mute / Solo** — independent of local input mute/solo. Solo silences all other remote channels.
- **Recv toggle** — sends `CLIENT_SET_USERMASK` (0x81) with the channel bit cleared; the server stops forwarding that user's audio entirely (bandwidth saving). Distinct from Mute: Mute receives+decodes but silences output; Recv-off prevents download.
- **Output routing selector** — routes this channel's audio to a specific plugin output bus for DAW stem capture. Multiple channels can share an output.

### Status bar and sync UX

Always visible at the top. Shows:
- Server BPM and BPI.
- Host DAW BPM (plugin mode) or "Standalone".
- Interval phase progress indicator (bar from 0 → BPI beats).
- Connection status: Disconnected / Connecting / Connected as user@host.

**BPM mismatch**: when server BPM ≠ host BPM, show a prominent warning. Remote audio stops; local audio is not transmitted. The plugin cannot push tempo to the DAW (that requires OSC, which is deferred). The user must change the DAW tempo manually; the warning disappears automatically when they match.

**Pending state**: once BPM matches (warning gone), if the DAW transport is not running, prompt "Start transport to begin." When the transport starts, the plugin phase-locks to the DAW's "1" and enters normal transmit/receive mode.

**Standalone mode**: no DAW transport concept. Behaves as if transport is always running. BPM/BPI follow the server from the moment of connection. Playback and transmit begin automatically.

### Metronome

- Toggle button (on toolbar or top area).
- Default: **off** in plugin/DAW mode, **on** in standalone.
- Volume control (small fader or knob).
- Downbeat accent at 880 Hz, beat clicks at 440 Hz (already implemented).

### Connection / server browser

A panel or popup (triggered from the toolbar) containing:
- **Server list**: static curated list of well-known public servers. Live room info (BPM, BPI, player count, player names) can optionally be fetched from `http://ninbot.com/app/servers.php` (Jamtaba's public JSON API). Show this info in the list before the user connects.
- **Private server**: free-form `host:port` text field.
- **Username** field.
- **Anonymous toggle**: when on, no password is sent. When off, shows a password field.
- **Connect / Disconnect** buttons.

### Chat panel

- Scrolling message history. Formatted: `<user> text`, `* user /me text`, `[PM] <user> text`, `*** server event`.
- Single-line input with hint text showing supported commands.
- Commands: `!vote bpm <n>`, `!vote bpi <n>`, `/me <text>`, `/topic <text>`, `/kick <user>`, `/msg <user> <text>`.
- Collapsible via a toggle button. Defaults to visible.

---

## Video support (future work — not planned)

Video is a **Jamtaba-proprietary extension** to the Ninjam protocol. ReaNINJAM and abNinjam do not support it; they ignore the extra channel data.

**How Jamtaba does it** (`references/JamTaba/src/Common/`):

- **Capture**: Qt's `QCamera` + `CameraFrameGrabber` (`QAbstractVideoSurface`) grabs webcam frames.
- **Encode**: FFMpeg (`AV_CODEC_ID_H264`), max 320×240 px, 64–400 kbps. Emits raw H.264 NAL data. (`video/FFMpegMuxer.cpp:172`)
- **Transport**: Sent as **channel index 1** (audio is always channel 0) using standard `UPLOAD_INTERVAL_BEGIN` (0x83) + `UPLOAD_INTERVAL_WRITE` (0x84), but with fourCC = `JTBv` instead of `OGGv`. The server routes `JTBv` intervals just like audio — it doesn't decode them. (`MainController.cpp:382–393`, `ClientMessages.cpp:459–471`)
- **Receive**: On `DOWNLOAD_INTERVAL_BEGIN` with fourCC ≠ `OGGv`, buffer the raw bytes. On interval complete (flags = 1), hand to FFMpeg for demux/H.264 decode. (`ninjam/client/Service.cpp:302–337`)
- **Display**: `VideoWidget` renders decoded frames below the remote user's audio channel strip.

**For us when this is tackled:**
- Camera capture: `juce_video` module (`juce::CameraDevice`) wraps AVCapture / DirectShow / V4L2. Add `juce_video` to `target_link_libraries` in `src/CMakeLists.txt`.
- Encode/decode: FFMpeg as an external dependency (significant). Alternative: libtheora (native OGG video, simpler but lower quality).
- Follow the `JTBv` fourCC and channel-index-1 convention for interoperability with Jamtaba users.
- Non-Jamtaba clients silently ignore `JTBv` intervals.
- Currently our code doesn't check fourCC at all (work item #2) — fix that before adding video.

---

## Reference implementations (`references/`)

Read-only submodules. Don't build or modify them. Each has a sibling `.md` at `references/<name>.md` — **read the summary first**, dip into source only for specifics.

| Submodule | Primary use |
|---|---|
| `ninjam/` | Authoritative protocol reference. `njclient.cpp`, `netmsg.cpp`, `mpb.cpp`, `server/usercon.cpp`. |
| `old_client/` | Context for our vendored WDL code. |
| `abNinjam/` | OSC tempo sync reference; headless properties-file approach. |
| `ninjam-next-plugin/` | Modern JUCE VST3/AU; wraps NJClient. Good for `AudioPlayHead` phase ring-buffer sync. |
| `JamTaba/` | Rich UI reference; only client with video support. Cautionary tale on Qt+plugin complexity. |
| `libninjam/`, `JamWide/` | Additional implementations, lower priority. |

---

## Submodules under `modules/`

Built into the project. Do not edit.
- `JUCE`
- `modules/ogg` + `modules/vorbis` — Ogg/Vorbis codec
- `modules/clap-juce-extensions` — CLAP plugin format support

---

## Style notes

- C++17. `juce::` types preferred over `std::` where both exist (`juce::String`, `juce::AudioBuffer`, `juce::CriticalSection`).
- 2-space indent, braces on same line, members lowerCamelCase.
- No comments except for non-obvious invariants or protocol workarounds. No narration.
