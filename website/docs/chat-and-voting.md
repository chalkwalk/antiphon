---
id: chat-and-voting
title: Chat and voting
sidebar_position: 6
---

# Chat and voting

The chat panel is on the right, and can be hidden with the **Chat** button. It
ghosts out when you are not connected, and clears when you join a new session.

Chat is how a NINJAM room organises itself. There is no other social layer --
no accounts, no profiles, no presence beyond the user list the server provides.
Identity on a NINJAM server is a username and, at most, a password.

## What you can type

| You type | Does |
|---|---|
| `hello` | Says hello to the room |
| `/me plays a wrong note` | Third-person message |
| `/msg bob you there?` | Private message to bob |
| `/topic Jam in D minor` | Sets the room topic |
| `!vote bpm 130` | Proposes a tempo change |
| `!vote bpi 8` | Proposes an interval length change |
| `/kick bob` | Admin only |

Admin commands such as `/bpm` and `/bpi` are passed through to the server, which
will tell you if you lack the privilege.

## Voting

Votes need a majority of the room. Antiphon shows a vote in progress as a chip
you can click to add your vote, rather than making you type the command again.

When a vote passes, the tempo changes for everyone at the next interval --
including you. If you are in a DAW, that means matching your project tempo and
pressing **Sync** again. See [Syncing with your DAW](./daw-sync.md).

## Keys and chord progressions

NINJAM has no protocol field for the key of a jam, so Antiphon does what Jamtaba
does for chords: it sends an ordinary chat message in a tagged form.

- `/key Dm` sends `[key: D minor]`, which Antiphon shows in the header and every
  other client shows as plain text. Nothing is invented on the wire and no other
  client has to cooperate.
- A line like `| Dm7 | G7 | Bb | Am7 |` is recognised as a chord progression and
  displayed as one.

Keys are read **only** from that tagged form, never from free chat text.
Guessing at prose is how you end up with a header confidently announcing that
the room is playing in "I am tired" -- which is a real entry in another client's
own test suite.
