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

## 4. Design Decisions
1. **Ninjam Core Codebase**: We will write our own clean/modern protocol code in C++ using JUCE, rather than wrapping the old Cockos `NJClient`. The original clients will serve as a protocol reference.
2. **Audio Bus dynamic behavior**: The plugin will have a fixed bus count (e.g., 1 main stereo input bus and 1 main stereo output bus) but will dynamically change the number of channels within those buses as needed, which is better supported by DAWs than dynamically adding buses.
3. **Third-Party Libraries**: `libvorbis` and `libogg` will be included as submodules in the repository so the codebase is standalone and does not rely on system package managers.

## 5. Implementation Phases
### Phase 1: Project Skeleton & Build System
- [x] 1. **Initialize Git and Submodules**: Create repo structure. Add `libogg` and `libvorbis` as git submodules.
- [x] 2. **Setup CMake**: Create `CMakeLists.txt` using `juce_add_plugin` for VST3 and CLAP formats. Link JUCE modules and the Ogg/Vorbis static libraries.
- [x] 3. **Basic JUCE Plugin Skeleton**: Implement `PluginProcessor` (with 8in/8out dynamic channel support) and a blank `PluginEditor`. Verify build and plugin loads in a DAW.

### Phase 2: Audio Framework Foundation
- [x] 4. **Implement I/O Bus Routing**: Configure JUCE to declare 1 main stereo input bus and 1 main stereo output bus by default, but allow up to 8 channels. Implement a basic audio pass-through in `processBlock`.
- [x] 5. **Add Host Sync API**: Use `AudioPlayHead` to read DAW BPM and PPQ (transport position). Display this information simply on the plugin UI.
- [x] 6. **Implement Internal Metronome**: Create an internal interval timer based on a hardcoded BPI and BPM. Synthesize a basic metronome click on the interval beats and mix it into the output buffer.
- [x] 7. **Phase Alignment Logic**: Calculate the offset between the DAW's "1" (from PPQ) and the internal Metronome's "1". Add a warning to the UI if the DAW BPM does not match the internal BPM.

### Phase 3: Networking & Protocol Skeleton
- [x] 8. **Basic TCP Socket Connection**: Use JUCE's `StreamingSocket` (or ASIO/native if necessary) to manage a connection to a generic server on port 2049. Add simple Connect/Disconnect buttons and status text to the UI.
- [x] 9. **Ninjam Handshake (Client -> Server)**: Implement the initial protocol handshake (Client Auth -> Challenge -> Sha1 Hash -> Auth Reply). Send hardcoded anonymous credentials for testing.
- [x] 10. **Handle Server Config Messages**: Receive and parse Server License, BPM, and BPI messages. Update the internal metronome and UI with the received BPM/BPI.
- [x] 11. **Keep-Alive & User Management**: Implement keep-alive pings. Parse incoming user connection/disconnection messages and populate a basic list of Remote Users in the UI.

### Phase 4: Audio Encoding & Decoding
- [x] 12. **Ogg/Vorbis Encoder Wrapper**: Create a C++ wrapper class around `libvorbisenc` to take blocks of floats and compress them into Ogg pages.
- [x] 13. **Local Audio Capture & Interval Buffering**: Buffer the incoming audio from the DAW during the duration of one interval. As the interval finishes, pass the buffer to the Encoder wrapper.
- [x] 14. **Transmit Audio to Server**: Prepend the Ninjam Ogg interval header to the encoded Ogg pages. Transmit the payload over the network socket to the server.
- [x] 15. **Ogg/Vorbis Decoder Wrapper**: Create a C++ wrapper class around `libvorbisfile` or raw vorbis APIs to decode incoming Ogg pages back into float audio.
- [x] 16. **Receive Audio from Server**: Parse incoming audio streams (matched by User ID and Channel Index). Route the payload into the respective user's Decoder wrapper.

### Phase 5: Playback & Mixing
- [x] 17. **Delayed Playback Engine**: Buffer the decoded remote streams. Trigger playback of the buffered streams exactly one interval *after* they were generated by the remote user, synced to the local metronome's "1".
- [x] 18. **Local Mixer UI**: Build UI controls (Volume, Pan, Mute, Solo) for the local transmit channels. Apply these DSP gains to the audio *before* encoding.
- [x] 19. **Remote Mixer UI & DSP**: Build dynamic UI sub-mixers for each remote user that joins. Apply Volume, Pan, Mute, and Solo to the decoded streams in the `processBlock` before mixing them into the master output.

### Phase 6: Polish and Chat
- [ ] 20. **Text Chat Implementation**: Parse incoming chat messages from the server and display them in a scrolling UI text box. Add a text input field to send chat messages to the server.
- [ ] 21. **Voting Commands**: Add support for `/bpi`, `/bpm`, and `/kick` commands in the chat box, sending the appropriate protocol messages.
- [ ] 22. **OSC Sync (Optional/Stretch Goal)**: Implement a UDP OSC sender to transmit `/tempo/raw {bpm}` to `localhost` when the server BPM changes.
- [ ] 23. **Final UI Styling**: Apply custom LookAndFeel classes. Ensure dynamic resizing of the mixer panels as users join/leave.
