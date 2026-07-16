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
