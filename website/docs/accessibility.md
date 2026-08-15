---
id: accessibility
title: Accessibility
sidebar_position: 7
---

# Accessibility

Accessibility is a goal of this project, not an afterthought -- and this page is
written to be honest about the difference between what has been built and what
has been proven.

## What is built

- **Every control has a name and a description.** This is enforced by a test
  that walks the real component tree across every UI state and fails the build
  if a control arrives unnamed. It is not a review pass that might lapse; it is
  a gate.
- **Values are spoken with units** -- "-12.0 dB", "left 20" -- rather than as
  bare numbers.
- **Channel strips and player cards are focus containers**, so you navigate
  player by player rather than through every control in the window in sequence.
- **The header is exposed as text** and is the first thing Tab reaches. It is
  otherwise just pixels: tempo, interval, connection state, the key of the room.
- **Discrete events are announced**: connecting, sync changes, tempo changes,
  votes, players joining and leaving.
- **Continuous values are deliberately not announced.** Meters move constantly;
  speaking them would interrupt everything else. Focus a control to hear its
  value.
- **Colour is never the only carrier.** Every chat category keeps a text prefix,
  and states that differ by colour also differ by text.

## What is not proven

**JUCE has screen-reader backends on macOS (VoiceOver) and Windows (NVDA, JAWS,
Narrator), and none on Linux.** Orca sees an opaque window.

Antiphon is developed on Linux. So all of the work above is, at the time of
writing, **unexercised on the only platforms where it can actually be used.**
The annotations are built anyway, because the work is identical on every
platform and effective on two of the three -- but nobody should mistake "built"
for "verified".

That is the single largest gap in this project, and it is why a report from
someone using a screen reader is the most valuable thing it can receive. There
is an [accessibility issue
template](https://github.com/chalkwalk/antiphon/issues/new/choose) for exactly
this, and you do not need to diagnose anything: "the transmit button says
nothing when I tab to it" is a complete and useful report.

Also unassessed: the standalone build's audio-device picker, which is stock JUCE
and was never written with this in mind.

## Keyboard

The interface is navigable by keyboard throughout, and text entry happens in
real dialog windows rather than overlays -- which, beyond being the correct
thing to do, is the only arrangement that reliably takes keyboard focus inside a
plugin window on Linux.

Shortcuts are on `Cmd+Option` (macOS) and `Ctrl+Alt` (Windows/Linux) for global actions, avoiding VoiceOver's `Control+Option` VO key collisions while staying clear of DAW host key bindings. They are matched by key code rather than by the character produced, so they work on non-QWERTY layouts.

| Shortcut (macOS / Win+Linux) | Action |
|---|---|
| `Cmd+Option+S` / `Ctrl+Alt+S` | Arm Sync (then start the DAW transport) |
| `Cmd+Option+C` / `Ctrl+Alt+C` | Focus the chat message box |
| `Cmd+Option+O` / `Ctrl+Alt+O` | Open Server Browser / Connect dialog |
| `Cmd+Option+M` / `Ctrl+Alt+M` | Toggle Mute All local channels |
| `Cmd+Option+H` / `Ctrl+Alt+H` or `?` | Open Keyboard Shortcuts Help dialog |
| `Cmd+Option+L` / `R` or `Ctrl+Alt+L` / `R` | Focus Local Channels / Remote Users section |
| `Alt+Arrows` | Navigate between channels and remote players |
| `Cmd+Option+1`..`9` / `Ctrl+Alt+1`..`9` | Jump directly to Local Channel 1..9 |
| `Cmd+Option+Shift+1`..`9` / `Ctrl+Alt+Shift+1`..`9` | Jump directly to Remote Player 1..9 |
| `M` / `S` / `X` | Toggle Mute / Solo / Transmit on active channel |
| `Up` / `Down` / `+` / `-` | Adjust volume for active channel (+/- 1 dB) |
| `Left` / `Right` | Adjust panning for active channel |

## Full detail

The complete story, including the known gaps and the things that have explicitly
*not* been verified, is in
[`docs/ACCESSIBILITY.md`](https://github.com/chalkwalk/antiphon/blob/main/docs/ACCESSIBILITY.md)
in the repository.
