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
- `/chords Am F C G` sends `| Am | F | C | G |`, which Jamtaba understands and
  Antiphon draws.

**Somebody on another client can set the key too**, in either of two ways: type
the tag `[key: D minor]` by hand, or put `/key D minor` at the **start** of a
line. Other clients pass an unknown slash command straight through as chat, so
the second works everywhere and is easier to type.

The two forms exist because neither can do the other's job. The tag is matched
anywhere in a line, so it can ride in the room topic -- which matters, because
NINJAM replays no chat to somebody who joins later, and the topic is the only
room state that persists. The `/key` form is matched only at the start of a
line, which is what lets anyone *talk about* it: a sentence mentioning
`/key D minor` in passing does not change the key, where a sentence mentioning
the tag would.

Keys are read **only** from those two forms, never from free chat text.
Guessing at prose is how you end up with a header confidently announcing that
the room is playing in "I am tired" -- which is a real entry in another client's
own test suite.

### Reading the chart

An announced chart appears as a row of chord names just above the phase bar,
each one where its change actually falls in the interval. The moving bar sweeps
through them, so you can see the next chord coming rather than being told it
exists after it arrives. The chord sounding now is the bright one, and the same
chart in roman numerals sits at the right of the row above.

Only a chart somebody announced is ever drawn. If nobody has said what you are
playing over, the row is not there.

**Bars matter.** `| Dm7 | C# Csus |` is two bars, and the second one holds two
chords -- so Dm7 lasts twice as long as either of them. Writing the same three
chords as `| Dm7 | C# | Csus |` gives each of them a third of the interval,
which is a different piece of music.

### Degrees, if you think that way

`/chords` also takes roman numerals and scale degrees, once a key is set:

- `/chords ii V I` in C major sends `| Dm7 | G7 | Cmaj7 |`
- `/chords i VI III VII` in D minor sends `| Dm | Bb | F | C |`
- `/chords 1 4 b6` sends `| C | F | Ab |`

Case carries the quality -- `IV` is major, `iv` is minor -- and a plain number
takes whatever chord the key already has on that degree. Your client works the
chords out and sends the ordinary chord names, so nobody else in the room needs
to know you typed it that way.

### The key nobody said

If someone announces a chart and no key has been set, Antiphon works out what
key the chords suggest and offers it on the chip under the chat, next to a
**Set key** button. Clicking it announces the key the same way `/key` would.

It only offers when the chords are actually decisive. `| Dm7 | G7 | Cmaj7 |` can
only be C major; `| Am | F | C | G |` is equally at home in C major and A minor,
so nothing appears -- a suggestion that is wrong half the time is worse than no
suggestion.
