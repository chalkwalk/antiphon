---
id: daw-sync
title: Syncing with your DAW
sidebar_position: 5
---

# Syncing with your DAW

You are either **synced** to your DAW or **not synced** -- never something in
between -- and Antiphon always tells you which, in words a screen reader
speaks and in the header.

## Getting synced

1. Insert Antiphon on your **master bus**.
2. Connect. Antiphon says what it found in one sentence: the room's tempo, and
   whether you are synced.
3. **Match the tempo**, exactly. In Reaper, Antiphon sets it for you. In any
   other DAW, set your project tempo to the room's BPM: "close" is not synced.
4. **Press play in your DAW.** Antiphon follows your playhead, so the room's
   intervals land on your bars, and a loop in your project stays on them.

Press `Ctrl+Alt+S` at any point and Antiphon does the next step for you where
it can, or tells you what it is. `Ctrl+Alt+I` says where you stand.

## Not synced is not silent

Until you are synced -- wrong tempo, DAW stopped, a DAW that reports no
position -- the room plays on Antiphon's own clock at the room's tempo, and
your live audio still goes out. You hear the jam and the jam hears you; only
your DAW is out of step with it, and you are told why.

## When your DAW stops mid-jam

The room keeps playing on Antiphon's clock. The header says so, a screen
reader hears it once, and a short high tone repeats once an interval until you
press play again -- so a stray key that stopped the transport cannot leave you
out without knowing. Press play from any bar and you are synced on the
playhead.

## When the room changes tempo

A passed vote changes the tempo for everyone at the next interval. **Reaper
follows it** (turn off *Reaper follows room tempo* if you would rather it did
not). In any other DAW you are told the new tempo and that you are not synced
until you set it -- you keep hearing the room meanwhile.

## Why only Reaper?

Because Reaper is the one DAW that lets a plugin set its tempo, press play and
set the loop, through its own API, under both CLAP and VST3. There is no
host-agnostic way to do it, so every other DAW is only ever read.

## Antiphon's clock

For session or clip view, or a DAW you are not playing from, choose
**Antiphon's clock** in the Clock group (`Ctrl+Alt+F`) and use **Play**
(`Ctrl+Alt+G`). The jam then ignores your DAW entirely. It is the only way to be
unsynced on purpose, and each new join starts synced again.

:::tip
Set your DAW's loop to exactly one interval's length -- in Reaper, **Loop one
interval** does it. Then the loop and the jam agree about where "one" is, and
everything you record lands on a bar line.
:::

## Practising alone

You can run the interval clock without being in a jam at all: press play in your
DAW with nothing connected and the metronome, the phase bar and the interval
grid all run, derived from the host's own tempo and position. A project at
120 BPM gives you the interval you would get in a 120 BPM room.

For playing *with* somebody, there is the **practice room** -- a real band on
your own machine. See [Your first jam](./your-first-jam.md#the-practice-room).
