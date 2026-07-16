# ninjam-next-plugin Reference Implementation

> Reading notes from the design phase, kept for the reasoning they record.
> Repository revisions and licensing: [SOURCES.md](SOURCES.md). Where these
> notes disagree with `DESIGN.md`, `DESIGN.md` is right.

## Overview
`ninjam-next-plugin` is a modern JUCE-based VST3/AU plugin for Ninjam. It wraps the official Cockos Ninjam C++ `NJClient` and provides a JUCE-based graphical interface and DAW host sync features.

## Architecture

### Libraries and Tools
- **JUCE Framework**: Used for the audio plugin structure, GUI, and audio block processing.
- **CMake**: Used for project configuration instead of Projucer.
- **Cockos `njclient`**: Included as a submodule. The plugin uses the original `NJClient` class directly rather than rewriting the Ninjam core logic in modern C++.

### Key Components
- **`NinjamClientService`**: A wrapper class around `NJClient`. It operates via a `juce::Timer` to read state (users, chat, sync progress) from the Core `NJClient` and provides methods like `processAudioBlock()`.
- **Host Sync**: Includes transport sync with ring-buffer phase alignment. It tracks the DAW's PPQ and BPM using JUCE's `AudioPlayHead` and aligns it with Ninjam's generated interval.
- **Classic/Add Local/Listen Local**: Provides monitoring modes where the local signal can be muted (if monitored through the DAW) or added to the mix.

## Takeaways for JUCE Plugin
- **Wrapper vs Rewrite**: `ninjam-next-plugin` wraps the original Cockos C++ client logic. Wrapping the C++ `NJClient` directly saves a lot of time implementing the Ninjam protocol, network sockets, and vorbis encode/decode logic. However, the original code is quite old (lots of raw pointers and thread safety via custom Mutexes). We need to decide if we want to wrap it or adapt it.
- **Audio Alignment**: The `phaseRingBuffer` approach used here is very relevant. Because the DAW block sizes don't perfectly align with the Ninjam intervals, buffering the inputs and outputs with a ring buffer to sync the metronome '1' to the DAW's '1' is crucial.
- **Limitations for us**: The user noted that this plugin uses Windows and Mac specific tooling which doesn't work well for them (likely due to its build scripts or how the `ninjam` submodule is wired up). We need to ensure our cross-platform CMake/JUCE setup is completely portable and Linux-friendly.
