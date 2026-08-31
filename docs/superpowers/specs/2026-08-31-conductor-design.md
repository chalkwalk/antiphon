# The Conductor: one leader, and intent-only commands

**Status:** design, approved 2026-08-31. **No code described here exists yet.**
The one thing that has shipped is its prerequisite: `jambot::Conductor` was
renamed to `jambot::IntervalPump` (jambot `ca35f3a`) to free the name, because
renaming after building on it would be noise inside a real change.

**Scope note:** "the band is one process" constrains where the BOTS live
relative to each other, not which server they join. One binary running a
conductor and N members can join any Ninjam server, including a stranger's.
What it cannot do is scatter its members across processes.

**Supersedes:** the resolution recorded in `ed42a70` (*"the room takes a request
for a fifth player from chat, with no bot in the middle"*). There is a bot in
the middle now, and it is the right one -- see *What this supersedes*.

---

## The problem

Two problems, and they turn out to be one.

**Nothing can ask for a fifth player.** `PracticeRoom::addPlayer` has no caller
outside the tests, so a band that can grow is reachable from code and not from a
room. That was blocked on a design conflict -- `BOT-CHAT.md` section 7 wants the
tutor to *finish and part*, section 16.9 wants it to *recruit in session*, and a
bot that has gone cannot recruit.

**Four bots agree by luck, and the luck is running out.** The band coordinates
by *identical inputs, identical deterministic function*: every bot evaluates the
same function of `intervalIndex` and the room seed and arrives at the same
answer with no messages. That works beautifully for the music. It does not work
for SPEECH, because two bots reaching the same conclusion both say it. The
existing answer is rank-stagger arbitration -- wait `rank * 400ms`, then check
whether the job is already done -- and `ROADMAP.md` lists four uses for it, two
of them unbuilt.

That mechanism has already failed in production. Rank-times-400ms replaced a
hash modulo after two of four bots landed 32ms apart, both timers fired in one
scheduling wake on macOS, and the arrival roster posted twice. It had never
actually held; Linux passed on a margin nobody had measured. Found by CI on
2026-08-22, the first run on a non-Linux compiler since the bots were written.

Arbitration is a workaround for having no authority. Give the band an authority
and the whole category goes away.

## The shape

**One conductor per band, always present.** An ordinary Ninjam client with no
instrument, no channels and no audio -- the shape `TutorBot` already proves
works and which costs the server almost nothing. It speaks for the band, decides
what the band does, and asks the room for new players.

**`TutorBot` becomes a subclass of it.** Teaching is a finite script layered on
leading, which makes "the tutor is a special instantiation of the conductor"
literally true and means a room contains *exactly one* instrument-less bot in
both cases.

**The band is one process, always.** This is a load-bearing constraint, not an
implementation detail. See *Why one process* below.

## The command model: intent, never timing

The conductor decides **what** and **from which interval**. It never decides
**when**.

Every command names an **absolute interval index**:

- `play from N`
- `key is D minor at N`
- `form AABA from section 3`

Members compute the actual notes and timing from the deterministic function they
already share. The conductor makes the scheduling *judgment* -- an interval is
several seconds, so it can tell whether a change still fits into N+1 or has to
land at N+2 -- but the judgment is baked into the index rather than expressed as
a deadline.

**Why this matters.** A deadline (`start now`) converts a property that is
currently exact into a race that currently does not exist: four members would
receive it at four slightly different times. An absolute index means the same
thing regardless of when it arrives.

**The safety rule:** a command naming an interval that has already passed is
**rejected, not applied late**. A slow path can never make one member play
something the others did not.

## Why one process

`jambot::IntervalPump` (renamed from `Conductor` in `ca35f3a` to free the name)
owns the interval counter, and it is **local**:

```cpp
auto nextDue = clock::now();
int intervalIndex = 0;
...
++intervalIndex;
nextDue += period;
```

It starts at 0 when `start()` is called and free-runs on `steady_clock`.
`PracticeRoom` owns exactly one, driving every bot.

So `intervalIndex` is agreed across band members **because they share a process
and a single pump object** -- not because of anything on the wire. Specifically
it is:

- **Not on the wire.** Ninjam carries no global interval counter, by design.
  `PracticeServer.h`: *"the interval grid is entirely client-side (every client
  plays each received interval starting at its own downbeat), so there is no
  clock here at all."*
- **Not shared with the human player.** Antiphon's `IntervalClock` counts
  separately, and the pump starts when the room starts, not when anyone
  connects.
- **Free-running**, resynced only by the skip-forward-on-overrun rule, so it
  drifts against any other client's clock.

`IntervalPump.h` states this as a deliberate choice rather than a limitation:

> FREE-RUNNING, and deliberately not synchronised to any player's grid. Ninjam's
> absolute interval phase is free: every client plays a received interval
> starting at its OWN downbeat, so phase offsets between clients cancel out per
> listener (`PRINCIPLES` 9). Chasing somebody's phase here would add a
> dependency for no audible difference.

**Consequences.** "At interval N" is unambiguous only inside the band's own
process. The moment the band spans processes, that reasoning stops applying and
you are chasing phase after all -- a clock-sync problem with real error bars,
reintroducing exactly the race the intent-only model exists to avoid.

**So control is direct calls, not private messages.** PMs were the original
instinct and the instinct was right about the thing that mattered -- centralised
decision-making removes the synchronisation problems. The transport turned out
to be incidental: in one process the same benefit costs no wire hop. PMs remain
available for what they are actually good at, a human privately addressing one
bot.

## Speech: singular stays, plural moves

- **Singular** -- "Mirn, what are you playing?" -- the addressed member answers.
  This needs no arbitration because exactly one bot was addressed. `BotAnswer`,
  the 150-case addressing corpus and `Participant`'s role/sound matching are
  untouched.
- **Plural** -- the arrival roster, the tempo vote, the key-change
  acknowledgement, common questions -- the conductor.

This is the split that deletes rank-stagger arbitration: it only ever arbitrated
the plural cases. `PracticeBot::speakDelayMs` and the rank ordering go with it.

## Membership

A human asks the conductor. The conductor asks the room. `PracticeRoom::addPlayer`
still enforces the cap, keeping its one home for the reason `PracticeRoom.h:128`
gives: no path -- config, chat, a conductor -- can exceed it by not knowing about
it.

**This retires an earlier design entirely.** The previous plan was a chat
observer on `PracticeServer` plus a worker thread, because `PracticeServer` is a
single `juce::Thread` doing accept *and* relay, so calling `addPlayer` from its
`CHAT_MESSAGE` handler would deadlock -- the thread that must accept the new
bot's connection would be blocked inside the handler. The conductor is an
ordinary client receiving chat on its *own* client's thread, so the server
accepts on a different thread. The hazard does not exist. No `RoomRequest.h`, no
server hook, no worker thread.

## What the tutor loses, and why that is acceptable

`BOT-CHAT.md` section 7 makes *finishing* one of the tutor's three defining
properties: "six lines and it parts", and "a tutorial that leaves when you have
got it is a rare and good thing". A conductor cannot part, because the band
always has one.

So **"it parts" becomes "its teaching ends"**, and the room's headcount never
drops.

**Accepted 2026-08-31.** This is a real change to a documented property and is
recorded as one. The
*spirit* survives: section 7 argues against a tutorial that lingers uselessly,
and a conductor is not lingering -- it has an ongoing job. A leader who taught
you and then settled into leading is a coherent character. The alternative
preserves the letter at a cost section 7 would not accept: two instrument-less
bots in the room during a tutorial, which is the "another name in the room doing
nothing" objection that already killed the sixth-bot option.

Two consequences follow:

- The ten-line budget test must separate **teaching** lines from **leading**
  lines. The teaching script stays finite by construction; the conductor
  legitimately speaks for the band forever.
- `setOwner` stops being tutor-specific and moves up. The conductor wants to
  know who the owner is anyway -- it is the bot that cares whether the owner is
  present.

## Where the code lives

- `Conductor` and `TutorBot`: **chalkwalk-jambot**. They are bots.
- Room hosting, the cap, `addPlayer`: **Antiphon**. That is hosting.

Iterate with `CHALKWALK_JAMBOT_DIR`; bump the submodule and re-verify without it
before calling anything done.

## What this supersedes

- **`ed42a70`** -- "the room takes it from chat, no bot in the middle". Overruled.
  The conductor recovers 16.9's argument that recruiting is an arranging ACT
  rather than a command, which that decision had to spend.
- **`ROADMAP.md` *One arbitration primitive, four uses*** -- mostly ceases to
  exist. Two of the four were never built and now never need to be.
- **`BOT-CHAT.md` 16.9** -- the tutor recruits, becomes: the conductor recruits.
- **`BOT-CHAT.md` section 7** -- "it parts" is amended as above.
- **`BOT-CHAT.md` 16.10** -- still quotes 27%/54% CPU figures that `ROADMAP.md`
  already records as WITHDRAWN; they came from a build with no optimiser. Fix
  while in there.

## Voting: DEFERRED, and the decision was made on bad information

**Provisional, and not to be built yet.** The shape is "the conductor votes,
members abstain", but that was decided before reading what is already here, and
it rests on a number nobody has measured.

**What is already built and deliberate.** `BotAnswer.h:105` -- *"Asked to cast a
vote directly. A bot never starts one -- four bots voting on one person's say-so
is that person having four votes."* So the current position is not an oversight,
it is a considered refusal, and `BotChat.cpp:397` repeats the reasoning. Any
change here overturns a decision that was made on purpose.

**The argument for the conductor voting is not the one first given.** "It is
counted whether it means to or not" is true and beside the point. The real
hazard is the DENOMINATOR: if a Ninjam tempo vote needs a majority of users,
then one human among five silent bots can never reach it, and abstention does
not keep the band neutral -- it makes votes unwinnable by the only person in the
room who wants anything. Whether that is what happens depends on how the server
counts.

**And that is unmeasured.** `ROADMAP.md` already carries *Measure the server's
vote threshold* as an open item. Until it is measured, "the conductor votes"
might be necessary, harmful, or irrelevant, and there is no way to tell which.

**So: measure first, decide after, build last.** Nothing else in this design
depends on voting, so it is cleanly separable. What survives regardless is the
part that follows from the rest of the design rather than from the threshold: if
the band votes at all, it casts ONE vote, because plural matters are the
conductor's and a vote is the most plural thing a band does -- and a band that
swings every vote by weight of numbers is one nobody invites back.

## Open questions

Genuinely unsettled, listed rather than papered over:

1. **The vote threshold**, per the section above -- measure before deciding.
2. **What the conductor says when it refuses at the cap.** A refusal is a rule
   speaking, not a failure, so it wants a line rather than a silent `false`.
3. **Removing a bot**, and what that does to the roles below it. `BOT-CHAT.md`
   16.10 already lists this as unexamined.
4. **What a role change sounds like** when the band closes ranks -- 16.10 again;
   probably wants an interval or phrase boundary, like the ending.
5. **The public-server shape.** One binary, one conductor, N members, joining a
   stranger's room with the cap at four. Sketched, not designed.
