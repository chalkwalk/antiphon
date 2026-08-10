---
id: troubleshooting
title: Troubleshooting
sidebar_position: 8
---

# Troubleshooting

## Things that are not bugs

**The first interval was silent.**
Expected. Nothing had arrived yet. See [Your first jam](./your-first-jam.md).

**I hear everyone several seconds late.**
That is NINJAM working correctly. It is the entire idea. Really, read
[Your first jam](./your-first-jam.md).

**Someone appears in the room but is silent.**
They may not be playing, or they may be playing so quietly that their interval
produced no data to send -- the codec only emits data every few kilobytes. Both
look identical from here.

**The standalone only offers me one input.**
That is JUCE's standalone host, not a bug: one input bus and one output bus.
Multi-channel transmit and stem recording need the plugin in a DAW.

## Connection and sync

**"DAW tempo does not match the server"**
Set your project tempo to the BPM shown in the header. The plugin cannot change
it for you, and [there is a reason](./daw-sync.md).

**"Press play in the DAW to join the jam"**
You have armed Sync. Start the transport.

**Nothing is happening at all; the header is grey or amber.**
Check the connection. Amber after a connect attempt means it failed -- wrong
host, wrong port, or the server refused the username. Try `ninbot.com:2049` as
`anonymous` to establish whether the problem is you or the server.

## Audio

**Nobody can hear me.**
In order:

1. Is **TX** green on your channel?
2. Is your VU meter moving?
3. Is your fader up?
4. Is the header navy? You only transmit when in sync.

**I cannot hear a specific player.**
Check their **R** button first -- if Recv is off, the server is not even sending
you their audio. Then check M and S on other channels, since a solo elsewhere
silences them, and check their output bus in case they are going somewhere you
are not listening to.

**A stem is much quieter than I expected.**
Remote channels default to 0.25, deliberately, to match the reference client.
See [Recording the jam as stems](./routing-stems.md).

## Standalone startup

**"The audio device did not respond."**
Something else is holding it -- a DAW, another audio application, or a sound
server in a bad state. Antiphon shows a device picker: choose a different output
and input and press **Use this device**. Your choice is saved, so the next launch
goes straight to it.

Run it from a terminal to see the startup log. Every step is reported on stderr
with an `[antiphon]` prefix, including which device it tried and why it gave up.

## Linux, in a DAW

**Typing in a text field does nothing.**
This should be fixed. It was a JUCE focus bug on Linux -- an embedded plugin
window never considered itself focused, so a clicked text field showed a caret
and then ignored the keyboard -- and this repository carries a patch for it. If
you still see it, that is a bug worth
[reporting](https://github.com/chalkwalk/antiphon/issues/new/choose).

## Still stuck

Open an issue. The template asks for platform, plugin format, DAW, sample rate
and buffer size, because a surprising number of audio bugs live at exactly one
combination of those.
