---
id: daw-sync
title: Syncing with your DAW
sidebar_position: 5
---

# Syncing with your DAW

A NINJAM room has a tempo, and so does your project. They have to agree, and the
plugin cannot make that happen by itself.

## Matching tempo, then syncing

1. Insert Antiphon on your **master bus**.
2. Connect. The header turns amber and tells you the tempo does not match.
3. **Set your DAW project tempo to the server's BPM**, shown in the header.
4. The warning clears. The status now reads *"Tempo matches. Press Sync, then
   start the DAW transport."*
5. Click **Sync**.
6. **Start your DAW transport.** The plugin locks the interval grid to that
   moment.
7. You are in -- header navy, phase bar moving, transmitting and receiving.

## Why can the plugin not just set the tempo?

Because there is no reliable, host-agnostic way for a plugin to set a DAW's
tempo. One other NINJAM client does it by sending OSC to localhost, which works
for one DAW in one configuration and is a support burden disguised as a feature.

So Antiphon shows you a warning that clears itself when you match, and leaves
the tempo to you. If a standard for this ever appears, the decision gets
revisited.

## Why is Sync a separate button?

Because re-syncing mid-jam cuts short the interval you are currently
transmitting, and everyone else hears that as a glitch.

Sync only ever happens when you ask for it. An accidental stop and start of your
transport will not re-phase a jam in progress. If you *do* want to re-sync --
you moved something, you want to realign -- press Sync again and restart the
transport.

Note that Antiphon locks to the **transport start**, not to a position on the
timeline. In a clip-launching session the timeline advances but is musically
meaningless, and jogging the playhead mid-jam is not a real use case.
Transport-start covers both common setups without needing to know anything about
your arrangement.

:::tip
Set your DAW's loop to exactly one interval's length. Then the jam and the
session agree about where "one" is, and everything you record lands on a bar
line.
:::

## When the room votes to change tempo

A passed vote changes the tempo for everyone at the next interval -- **including
you**. In a DAW that means changing your project tempo again and pressing Sync
again. There is no way around this; the room moved, and your session has to
follow.

## Practising alone

You can run the interval clock without being in a jam. The **practice echo**
plays your own previous interval back to you, one interval late, exactly as the
room would hear it -- so you can get the feel of playing to a delay before
inflicting it on strangers.

**Nothing you play in practice is ever transmitted.** That is true at every layer
of the plugin, not merely as a UI state, and it is deliberately tested to stay
true.

Offline, with no server to define the grid, the interval is derived from the
host's own tempo and position, so practising in a project at 120 BPM gives you
the interval you would get in a 120 BPM room.
