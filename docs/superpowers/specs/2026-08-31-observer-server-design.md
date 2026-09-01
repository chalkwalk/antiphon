# A Ninjam server with observers

**Status:** design, 2026-08-31. Nothing is built and nothing is scheduled. This
records what the thing is and what it costs, so that deciding to start it later
is a decision about priority rather than about design.

---

## What it is

A Ninjam server, first-party and full-parity, which also serves a web page where
guests can **listen to the jam and take part in the chat** without a Ninjam
client. It runs on a Raspberry Pi on the same LAN as the players.

Two things it is not. It is not a bot: a client-side observer receives intervals
a whole interval late and would emit its output another interval behind, putting
guests two intervals adrift for no reason, and on anything but a LAN it sends
the room's audio over the network a second time before a single listener hears
it. And it is not a cut-down server: any Ninjam client -- ReaNINJAM, Jamtaba,
Antiphon -- must be able to join it and work correctly.

## Decisions taken

| | |
|---|---|
| Scope | **Full ninjamsrv parity.** Any client, working correctly. |
| Repo | **`chalkwalk-ninjam-server`**: a JUCE-free library plus a binary. |
| Seed | **Grows from Antiphon's `PracticeServer`,** which then moves out. |
| Observers in the room | **Not in the user list.** Their chat is ordinary `MSG` traffic. |
| Observer latency | **One interval behind -- the same place a player sits.** |
| Guest access | **A room secret in the link; the guest types a display name.** |
| Web transport | Audio and chat over plain HTTP where possible. |

## Why observers are not room members

A guest sends chat and does not appear in anybody's user list. The server
broadcasts a `CHAT_MESSAGE` of type `MSG` with the guest's display name as the
sender and never sends a `JOIN` for them.

This is cheaper than it sounds and better than the alternative. A Ninjam client
renders a `MSG` with whatever username string arrives, so **no client needs
changing and the server maintains no synthetic users.** The cost is that players
cannot see who is listening, which is a real loss and is accepted: the mixer
stays about people making sound.

Guest names need a marker so that a guest cannot impersonate a player. The exact
form is open (see below).

## The observer audio path, and the one number that decides it

**A Ninjam interval is already a media segment.** Four to eight seconds,
self-contained, its own headers. That is the shape HLS and DASH were built
around, and it is why this is far less work than "write a streaming server".

But the server currently never decodes anything -- it is a pure relay -- and to
produce one listenable stream it must decode every player's channels for
interval N, mix them, and encode the result.

**It cannot do that incrementally.** `AGENTS.md`: *"Interval delivery is
all-or-nothing. Ogg emits a page only every ~4 kB, so a quiet or tonal interval
decodes to nothing until the end-of-stream flush. Do not build anything that
assumes a partially received interval is playable."* So the mix cannot be
streamed as uploads arrive; it happens in the gap at the interval boundary.

That turns the latency target into a budget:

> **Interval N is complete at the end of N. Players hear it throughout N+1. For
> an observer to sit where a player sits, the whole decode-mix-encode must
> finish in the gap at the boundary.**

Every millisecond over is permanent extra lag, not a one-off. Miss by enough and
observers slip to two intervals and the design's whole point -- chat landing on
the right passage -- is gone.

This is the same problem as *The interval boundary is a compute spike* in
`ROADMAP.md`, at the other end of the wire, and it wants the same treatment:
measure on the machine that has to do it, on an optimised build, before
believing anything. It is the number that decides whether a Pi is the right
host.

**Delivery.** Plain HLS is the simplest thing that could work and is probably
too slow: browsers habitually buffer two or three segments before starting,
which lands observers exactly where we do not want them. The likely answer is
Media Source Extensions with the server pushing each mixed interval as it is
published, so the buffer is one segment because we control it rather than
because a player heuristic agreed.

**Codec.** MSE cannot append Ogg, so the mix must be re-containered. Vorbis in
WebM costs no new dependency, since libvorbis is already vendored for the
protocol. Opus sounds better per bit and is BSD, at the cost of libopus and a
decision. **AAC is out**: the usable encoder's licence is not GPL-compatible.
Safari's MSE support is the compatibility risk in either case and wants
checking before the choice is made rather than after.

## Chat needs no WebSocket

Worth recording because it removes a dependency argument before it starts.
Downward chat is **Server-Sent Events** -- a long-lived HTTP response, which
browsers reconnect automatically. Upward chat is a `POST`. That is the whole
feature over ordinary HTTP.

If audio also goes over HTTP, an embedded server needs no WebSocket support at
all. **cpp-httplib** (MIT, header-only) covers it; **civetweb** (MIT) if a
socket upgrade is wanted later. **Mongoose is out** despite being the obvious
choice by popularity: GPLv2-or-commercial, and GPLv2-only does not combine with
GPLv3.

## The extraction, measured

`PracticeServer` is 638 lines and already does auth, room membership, usermask
subscriptions, interval relay, chat and topic. Its JUCE surface is shallow:

| | count | replacement |
|---|---|---|
| `juce::String` / `StringArray` | 30 | `std::string` / `std::vector` |
| `ScopedLock` / `CriticalSection` | 15 | `std::lock_guard` / `std::mutex` |
| `juce::uint*` | 14 | `<cstdint>` |
| `Thread`, `MemoryBlock`, `Random` | 4 | `std::thread`, `std::vector<uint8_t>`, `<random>` |
| **`StreamingSocket`** | **2** | **a socket layer -- the only real work** |

Everything but the socket is a type swap. A BSD-sockets layer with a Winsock
shim is on the order of 150 lines, which is squarely the ecosystem's rule:
*take a dependency when the thing has a specification you could fail to meet;
write it yourself when it is small enough to test exhaustively.*

The result is JUCE-free and MIT like its siblings, consuming `chalkwalk-ninjam`
for framing, SHA1 and the Vorbis codec -- **and it builds on a headless Pi with
no JUCE at all**, which is worth something on its own.

One parity item falls out of the audit: `juce::Random` currently generates the
auth challenge. On loopback that is fine. On a real server the challenge is what
stops replay and wants a proper entropy source.

## What "full parity" actually means

`PracticeServer.h` is honest that it is *"not a general-purpose server, and not
trying to be: no licences, no persistence, no anonymous-user rules, no bans."*
Parity is that list plus:

- the licence agreement in the auth handshake;
- anonymous-user policy;
- user accounts with per-privilege flags -- topic, bpm, chat, kick;
- bans, max users, per-user channel limits;
- the config file format;
- **voting**, which in Ninjam is chat commands against a server-configured
  threshold;
- **session archives**, in the format ninjamsrv writes.

**No reference source enters the repository.** The NINJAM sources are GPLv2 and
this would be a first-party implementation (`PRINCIPLES §6`), written from the
protocol, with ninjamsrv read for behaviour and cited by file and line in the
`docs/references/SOURCES.md` pattern. That is a real cost on a parity server,
because parity means matching behaviour that can only be learned by reading or
by probing.

**Verify it the way the client was verified.** `docs/PARITY.md` exists because
differential testing against the reference found things nothing else would.
`scripts/testserver.sh` and `RealServerTests` stay pointed at the real
ninjamsrv: their value is that they are not us.

## Two things this unlocks that are not observers

**The vote threshold.** `ROADMAP.md` carries *"Measure the server's vote
threshold"* as an open item, and the conductor's voting design is deferred
behind it (`2026-08-31-conductor-design.md`). Writing the server is how that
question gets answered definitively -- though compatibility still means matching
ninjamsrv's semantics rather than inventing our own.

**Session archives.** Antiphon already consumes them: `ClipsortLog`,
`AntiphonStems`, `scripts/analyze_archive.py`. A first-party server that writes
the same format closes that loop and makes the stems path exercisable without
fetching and building ninjamsrv out of tree.

## Open questions

1. **The boundary budget, measured.** Decode N channels, mix, encode, on a Pi,
   at a realistic room size. Everything else here is contingent on it.
2. **Codec and container** -- Vorbis-in-WebM for no new dependency, or Opus for
   quality -- and what Safari will actually play.
3. **How a guest name is marked** so it cannot be mistaken for a player's.
4. **Moderation.** A web chat into a private jam needs at least a mute, and
   probably a way to revoke the room secret without restarting.
5. **What the page shows** beyond a play button and the chat: who is playing,
   the key and chart, the phase bar. Antiphon already renders all of it.
6. **Whether Antiphon's practice room takes this as a dependency** immediately
   or after the server has run for a while. It is a much larger thing than a
   practice room needs, and the extraction only pays once there is one
   implementation rather than two.

## Not now

This is not scheduled. The conductor work is in flight, the sampled-instrument
question is unanswered, and the cross-platform items in *Active focus* are
untouched. Recorded so that starting it later begins from a decision rather than
from a blank page.
