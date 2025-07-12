# Ninjam JUCE Client Design Document

## 1. Goal
The objective is to build a clean, simple, and cross-platform (Linux, Windows, macOS) Ninjam client using the JUCE framework. The plugin will allow remote musical performance over the internet by injecting the Ninjam latency-compensation model (delaying playback by a set musical interval) directly into the user's DAW.

## 2. Core Architecture

### 2.1 Audio Framework & Processing
- **JUCE**: We will use JUCE as our primary framework for the DSP, GUI, and plugin deployment. The primary targets will be **CLAP and VST3**. This solves the complex cross-platform UI and plugin build problems seen in other reference implementations (like Jamtaba's Qt static linking issues or abNinjam's minimalist/headless approach). We will use `juce_add_plugin` combined with `clap_juce_extensions` in CMake.
- **Bus Layout**: The plugin will explicitly define 8 input buses and 8 output buses. Initially, these will start with 0 channels. As the user configures routing, these can be set to 1 (mono) or 2 (stereo) channels. A typical setup places the plugin on the master track (Stereo In -> Stereo Out) to capture the local mix and play it back alongside the remote Ninjam streams.
- **Encoding/Decoding**: 
  - We will integrate `libogg` and `libvorbis` to encode the local audio buses into Ogg blocks and decode the incoming remote streams.
  - The plugin will buffer its inputs over the duration of the current interval, encode them, and transmit them to the server.
  - Simultaneously, it will receive encoded chunks from the server, decode them, and delay their playback until roughly the next interval boundary.

### 2.2 Synchronisation and Interval Timing
- **The "Interval"**: The core mechanism of Ninjam is playing based on the BPI (Beats Per Interval) and the BPM. The plugin will maintain an internal interval timer.
- **DAW Host Sync**: 
  - The plugin will monitor the DAW's host transport (`AudioPlayHead`). This allows the plugin to **read** the DAW's tempo and playback state (the "1").
  - **Limitation of VST3/CLAP**: Audio plugins fundamentally cannot *push* tempo changes to the host DAW. They can only read the current tempo. 
  - If the DAW's tempo doesn't match the Server's BPM, the plugin will notify the user with a mismatch warning.
  - The user must manually adjust the DAW tempo to match, then pause and hit "Play" in the DAW so the DAW's "1" aligns with the Plugin's "1" (the start of the interval).
  - *Optional OSC Sync*: To bypass the manual tempo adjustment, we may later add an OSC output feature (like abNinjam). If enabled, the plugin can send an OSC message (e.g., `/tempo/raw {int}`) to command the DAW to change its tempo automatically when the server BPM changes. (Note: Reaper supports OSC natively very well; Bitwig supports it via controller scripts; Ableton via Max4Live/third-party; Studio One/FL Studio have little to no native OSC support).
  - A ring buffer or offset accumulator will be used to track phase alignment and phase-lock the metronome between the DAW and the incoming streams.

### 2.3 Networking and Connection
- We will implement the Ninjam networking protocol using standard sockets (potentially juce::StreamingSocket or custom ASIO/native sockets if non-blocking paradigms are required for the binary protocols).
- **Server Communication**: We will connect to Ninjam servers (port 2049 conventionally).
- The protocol consists of:
  - Initial handshake/authentication.
  - Server configuration (License, BPM, BPI).
  - Keeping channels alive.
  - Sending interval headers and Ogg stream payloads.
  - Receiving channels from other users (`user@host`, channel indices).
  - Receiving and sending Chat and Vote commands.

## 3. User Interface (GUI)

The UI will be designed for clarity and functionality within a DAW environment.
- **Connection Panel**: Input fields for Server Address, Username, and Password. Connect/Disconnect buttons.
- **Sync & Transport**:
  - Display the current Server BPM and BPI.
  - Display the Host (DAW) BPM and phase.
  - A visual indicator (e.g., a flashing light or progress bar) showing the passage of the interval beats.
  - A large warning if DAW sync vs Server sync is mismatched.
- **Local Mixer**:
  - Volume, Pan, Mute, and Solo for the local transmit channels.
  - Input routing configuration (mapping hardware/DAW inputs to the 8 Ninjam logical channels).
- **Remote Mixer**:
  - Automatically populated list of connected users.
  - Sub-mixers for each user's channels (often stereo or mono).
  - Usually playing back at -6dB by default to leave headroom for local monitoring.
- **Chat & Voting Panel**:
  - Real-time text chat.
  - Commands interface to vote for new BPI/BPM (e.g., `/bpi 16`, `/bpm 120`).
- **Metronome**:
  - Metronome volume/mute switch. It should click strictly according to the incoming server BPI over the local interval phase.

## 4. Open Questions / Clarifications
Before proceeding to implementation, please clarify the following points:
1. **Ninjam Core Codebase**: Do we want to write the Ninjam network/protocol logic entirely from scratch in C++ using pure JUCE sockets, or should we port/wrap the Cockos `NJClient` C++ code directly (similar to how `ninjam-next-plugin` does)? Writing it from scratch is cleaner and more modern, but wrapping `NJClient` saves a massive amount of protocol reverse-engineering.
2. **Audio Bus dynamic behavior**: JUCE handles dynamic I/O bus configurations via `isBusesLayoutSupported()`. Should we expose all 8 buses to the DAW from the start and let the user attach sidechains, or do you prefer the plugin requests new buses from the host dynamically as local channels are added?
3. **Third-Party Libraries**: Are we happy to use CMake or a package manager (like CPM/vcpkg) for `libvorbis` and `libogg`, or do we want to include them directly in the source tree?
