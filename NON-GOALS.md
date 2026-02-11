# Antiphon -- Non-Goals

> This file is the long form of the *Non-Goals* index in `PRINCIPLES.md`. It
> exists because a focused tool is defined as much by what it refuses as by what
> it does. Each fence below names what **prompted** it (usually another Ninjam
> client, or a request that arrives repeatedly), the **principle that rejects
> it**, and -- crucially -- **what we offer instead**. A non-goal is not a gap;
> it is a decision.
>
> Read this alongside `PRINCIPLES.md`. When a feature request arrives, check
> here first: if it matches a fence, the answer is already written, and the
> reply is the alternative, not "no".
>
> Fences are cited as `fence #N` and the numbers are stable anchors. The index
> table in `PRINCIPLES.md` mirrors this numbering exactly.

## The three fences

Almost every refusal traces to one of three costs:

- **The DAW already does it, better.** Antiphon is a plugin inside a host with a
  transport, a mixer, a recorder and a file browser (`PRINCIPLES §2`). Building
  a worse copy of one of those inside the plugin window is the single easiest
  way to make this project large and bad.
- **It puts us on the wrong side of the protocol.** Ninjam works because every
  client speaks the same wire format. Anything that requires other clients to
  cooperate with something we invented, or that only works when everyone is
  running Antiphon, is not a Ninjam feature (`PRINCIPLES §10`).
- **It argues with the interval.** The one-interval delay is the musical form,
  not a defect (`PRINCIPLES §1`). Features that exist to hide it, shorten it or
  work around it are solving the wrong problem.

The test is constructive: we reject the *form*, then offer the version of the
same desire that fits.

## The catalogue

| # | Non-goal | Prompted by | Rejected by | What we offer instead |
|---|---|---|---|---|
| 1 | **Video** | Jamtaba, the only client with it | §2, §6, §10 | `JTBv` intervals are filtered on fourCC and ignored, so a Jamtaba user in the room costs us nothing. Their video simply does not appear. See "Notes on the close calls". |
| 2 | **Being a standalone host or DAW** | Jamtaba and the original Ninjam client are both standalone apps that own an audio device | §2 | The Standalone build exists, and works, but it is a development convenience and a fallback -- not the product. The plugin is the product, and the host is the app. |
| 3 | **Replacing the DAW's mixer or recorder** | Every standalone client ships its own mixer, meters, recorder and effects | §2 | Per-channel **output bus routing**: each remote channel goes to a stereo bus of your choosing, so the DAW records the jam as stems and processes them with plugins you already own. Per-channel **input bus routing** does the same on the way in. |
| 4 | **Hosting a Ninjam server** | `ninjamsrv` is in `references/`, and "why not both" is a natural question | §2 | The **server browser** (live room list from ninbot.com) and a private-server host/port field. Running a server is a separate job with a separate operational story; `scripts/testserver.sh` drives one for testing and that is the extent of it. |
| 5 | **Wrapping `NJClient`** | The obvious shortcut -- a decade of proven correctness, available as a library | §6 | First-party protocol, SHA1 and Ogg/Vorbis layers, **differentially tested against `NJClient`** (`docs/PARITY.md`). We get the correctness by measurement rather than by inheritance, and we do not get WDL, a GPLv2 dependency in the audio path, or an architecture built around owning an audio device. |
| 6 | **Timeline / loop-point integration** | The reference client integrates with REAPER's timeline and loop points | §1, §2 | Lock to the **transport start** instead, armed explicitly with the Sync button. In a session/clip view the timeline advances but is musically meaningless, and jogging the playhead during a live jam is not a real use case. Transport-start covers both common setups -- clip launching and a loop sized to the interval -- without knowing anything about the timeline. |
| 7 | **Pushing tempo into the DAW** | `abNinjam` sends `/tempo/raw {bpm}` over OSC when the server tempo changes | §10 | The **BPM mismatch warning**, which clears itself when you match the tempo manually. There is no host-agnostic way for a plugin to set the DAW's tempo; the OSC route works for one DAW with one configuration and would be a support burden disguised as a feature. Parked in `ROADMAP.md`, not planned. |
| 8 | **MIDI or other non-audio channels** | "It's a jam plugin, why not send notes" | §10 | Nothing. Ninjam channels carry Ogg audio; a MIDI channel would be an invention only Antiphon users could hear (see the second fence). Play the notes and transmit the audio. |
| 9 | **A social layer** -- accounts, profiles, presence, friend lists, session history | Every modern collaboration tool has one | §2 | The server's own user list and chat, which is what the protocol provides and what every other client shows. Identity on a Ninjam server is a username and, at most, a password. |
| 10 | **Codecs other than Ogg Vorbis** | Opus is better; the fourCC field looks like an extension point | §10 | `OGGv` only, which is what every client decodes. The fourCC field is an extension point *for the server to route opaquely*, not a codec negotiation -- there is no handshake in which we could agree on Opus, so a room with one Antiphon user would simply be a room where one person is silent. |
| 11 | **A "zero-latency" or low-latency jam mode** | The recurring first reaction to Ninjam | §1 | Local passthrough is **already** undelayed (§3) -- you hear yourself live. Everyone else is one interval late because that is what Ninjam is. A mode that shortened the interval toward zero would be a different, worse, and much harder protocol. |
| 12 | **Feature parity with the reference client** | It is the reference; surely we should match it | §2, §4 | Parity where it is *observable by other players* -- the wire format, the interval grid, the default gains -- and nothing more. `docs/PARITY.md` is about interoperability, not feature count. The reference client's local recording, its licence-agreement flow and its REAPER integration are all things we deliberately do not have. |

## Notes on the close calls

Four of these are narrower than they first read, and the boundaries are worth
stating so they are not re-litigated from the summary line.

**Video (#1) is a scope decision, not a technical one.** It is well understood
how Jamtaba does it -- H.264 at 320x240 via FFmpeg, sent on channel index 1 with
fourCC `JTBv` through the ordinary upload messages, which the server routes
without decoding. `DESIGN.md` §14 records the mechanism in full, deliberately, so
that the decision can be revisited on evidence rather than re-researched. What
rejects it is the cost: `juce_video` for capture plus FFmpeg as a hard
dependency, against §6, in service of a feature exactly one other client
implements. If Ninjam video ever becomes something people expect, the note in
DESIGN is where a future attempt starts.

**Standalone (#2) is supported, not featured.** The distinction matters because
it decides arguments. Standalone gets the *same* code, and it is how the audio
path is iterated on -- but when standalone ergonomics and plugin ergonomics
conflict, the plugin wins. The one place this is visible in the design is sync:
standalone has no transport to lock to, so `SyncState` runs it straight to
`Running` on connect, and the Sync button is a plugin concept.

**"The DAW does it" is not a licence to route badly (#3).** Refusing to build a
mixer is not refusing to build the routing that makes the DAW's mixer usable.
Output bus routing, persisted per (username, channel) and reapplied when a
player rejoins, is real work in service of this fence -- the alternative is a
stereo pair with everybody summed into it, which makes the DAW's mixer useless
and would quietly force us to grow our own.

**Tempo push (#7) is parked, not condemned.** Unlike the others this is a fence
about *mechanism*, not desire: wanting the DAW to follow the server tempo is
entirely reasonable, and the mismatch warning is a workaround, not a solution.
If a host-agnostic route appears -- a standard the plugin formats agree on -- the
fence comes down. Until then, `abNinjam`'s localhost-OSC approach is documented
in `references/abNinjam.md` and remains what someone should copy if they want it
for their own setup.

---

When in doubt, the question is never "do other clients have it?" -- it is "would
a player in a room with four strangers running four different clients be better
off, and could we still prove we are in time with them?"
