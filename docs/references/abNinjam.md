# abNinjam Reference Implementation

> Reading notes from the design phase, kept for the reasoning they record.
> Repository revisions and licensing: [SOURCES.md](SOURCES.md). Where these
> notes disagree with `DESIGN.md`, `DESIGN.md` is right.

## Overview
`abNinjam` is a cross-platform (Linux, Windows, macOS) audio plugin implementation of a Ninjam client, built targeting the VST2 and LV2 plugin standards. It aims to provide Ninjam functionality inside a DAW without relying on heavy GUI frameworks.

## Architecture

### Libraries and Tools
- **Plugin APIs**: Uses raw VST2 SDK and LV2 headers instead of a framework like JUCE.
- **CMake**: Used for the build system.
- **Dependencies**: 
  - `libvorbis` / `libogg`: For audio encoding/decoding.
  - `zenity`: Built-in GUI fallback for quick dialogs (e.g., license agreement).
  - `liblo`: For OSC (Open Sound Control) communication.
- **GUI Framework**: It either has a minimal GUI or operates headlessly via a `connection.properties` file where the user configures host, user, and password.

### Key Features
- **Headless Mode / Properties File**: Can be configured entirely via a text file (`connection.properties`) which is useful for headless Linux setups or users who just want to route audio.
- **Auto Sync BPM via OSC**: It uses `liblo` to send OSC messages (`/tempo/raw {int}`) back to the host DAW. If the Ninjam server changes the BPM, the plugin can command the DAW to change its tempo. Likewise, if the DAW changes its tempo, the plugin can vote to change the server's BPM.
- **Auto Remote Volume**: Distributes remote volume automatically to protect against clipping.

## Takeaways for JUCE Plugin
- The OSC sync feature is a very clever way to sync the DAW tempo with the Ninjam server tempo. We could potentially use JUCE's OSC classes or `AudioPlayHead` API to achieve similar DAW sync and transport controls.
- The concept of a headless or property-file based configuration might be useful for testing or for users who want a set-and-forget Ninjam node.
- Since we are using JUCE, we won't need Zenity or raw VST2/LV2 SDKs, but the idea of keeping the plugin extremely lightweight is a good goal.
