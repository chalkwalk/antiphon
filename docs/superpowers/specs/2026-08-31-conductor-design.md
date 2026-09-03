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

**The index names the interval a command takes effect FROM, and a START may
land inside it.** Corrected 2026-09-01, against the code: `PracticeRoom::onTick`
catches a start mid-interval on purpose, because *"waiting for the next head
would put an interval of silence between asking the band to play and hearing
it, and unlike an ending there is nothing musical happening in that gap"*. A
stop lands at the head of its interval; a start lands as soon as there is time
to render inside it. Applying one rule uniformly would insert a silent interval
for the sake of tidiness, which is a regression dressed as consistency.

What the index buys is unchanged either way: every member applies the same
command in the same interval regardless of when the message reached it, so the
dispatch window stops being a race.

**The room already latches, and that is most of the win already banked.**
`latchBand()` at tick 0 takes every bot's phase together, so the band wraps up
and resolves as a band even though the renders are seconds apart. What the index
adds is the residual window `ROADMAP.md` records under *A command reaches the
band one bot at a time*: a command landing INSIDE the dispatch catches some bots
before the latch and some after. This design subsumes that item.

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

## Speech: one answer or many

**Revised 2026-09-01.** The first cut of this split on how many bots were
ADDRESSED. That is the wrong axis. The right one is **how many answers the
question has**:

- **Many answers** -- "what are you playing?" put to the whole band -- each
  member answers for itself. No arbitration is needed because they are not
  competing: they are saying different true things, and a single reply would be
  a worse answer rather than a tidier one.
- **One answer** -- who is here, what key, what chart, has the band stopped --
  the conductor says it once.
- **Addressed to the conductor** -- band level by definition.

`BotAnswer`, the 150-case addressing corpus and `Participant`'s role/sound
matching are untouched either way.

**Why the axis matters.** Under the old framing, `band stop` looked like four
bots competing to confirm, which is why the current code ranks a bot that ACTED
above one that had nothing to do (`kIdleSpeakerPenaltyMs`) -- with the band half
stopped, "already stopped" from the idle one would tell the room nothing was
happening while three bots ended the tune. Under the right framing that is not a
contest at all: it is ONE fact about the band, and the conductor states it.

**Which exposes an ordering constraint.** The conductor can only state that fact
if it knows the band's play state -- and under the intent-command model it knows
it because it ISSUED the command. So command confirmations must follow the
commands, not precede them. A conductor answering `band stop` before it owns
`stop` would be guessing at state it does not have, which is the same mistake in
a new place.

**One interface, not a scatter of callbacks.** The conductor needs four things
from whoever hosts the room: the current interval, a way to command the band,
the band's play state, and a way to ask for another player. They are one
relationship, so they are one abstract class -- a host that implements three of
four should not compile, where four loose `std::function`s would fail at
runtime instead. `addPlayer` defaults to returning false, which states "this
room does not grow" rather than leaving it indistinguishable from "nobody wired
it up".

Coordination between the conductor and its members uses PRIVMSG where a message
is genuinely needed. That is not in tension with commands being direct calls:
chat coordination is not on a musical deadline, so a wire hop costs nothing,
whereas an intent naming an interval index is only meaningful within one pump.

This is the split that deletes rank-stagger arbitration: it only ever arbitrated
the plural cases. `PracticeBot::speakDelayMs` and the rank ordering go with it.

**And it is still failing, which is the case for doing this rather than tuning
it again.** Observed on Linux, 2026-09-01, about one run in seven:

```
!!! Test 8 failed: an already-announced bot spoke again:
MSG|Pemo[lead-bot]|say "band play" to start us and "band stop" ...
```

A bot whose arrival window had closed posted the joining instructions a second
time. That is the same fault as the 2026-08-22 double roster, which was
"fixed" by replacing a hash modulo with rank times 400ms -- so the margin was
widened and never made safe, and the second occurrence is the evidence that
widening is not a fix. Arbitration by timer cannot be made correct by choosing
a bigger timer; it can only be made unlikely.

The conductor does not narrow the race. It removes the question, because there
is no longer a set of peers who might each answer.

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

## A future direction: "be a rock band"

Not scheduled, and recorded because it is the clearest argument for the whole
design rather than a feature request.

Telling the band a STYLE -- be a rock band, be a jazz band -- and having the
roles and instruments fall out of it is trivial with a conductor and nearly
impossible without one. One authority assigns roles; four peers negotiating a
style by deterministic agreement would have to agree on the assignment as well
as the style, with no way to break a tie that does not reintroduce the
arbitration this design removes.

It depends on roles being assignable at all (*A band of more than four*) and on
stylistic parameters that do not exist yet, so it is a direction rather than a
plan.

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

## Voting: CORRECTED, and the earlier decision was harmful

**The shape recorded here on 2026-08-31 -- "the conductor votes, members
abstain" -- is wrong and would break a room.** It was reasoned from first
principles without reading `BOT-CHAT.md` section 8, which had already worked
the problem out against the reference server.

**Abstaining is not neutral.** A NINJAM tempo change is a vote of *everyone
connected*, bots included, voted or not: the denominator is `vucnt`, every user
with `m_auth_state > 0` (`justinfrankel/ninjam server/usercon.cpp:1192-1200`,
and the arithmetic at `:1239`). So a bot that does not vote is a vote against.

| Humans | Bots | Needed at 60% | Humans can carry it? |
|---|---|---|---|
| 1 | 0 | 1 | yes |
| 1 | 4 | 3 | **never** |
| 3 | 4 | 5 | **never, even unanimously** |

One human plus a voting conductor is two of the three needed. **Members
abstaining leaves the room unable to change tempo at all** -- the band would be
worse than playing alone, which is the exact failure that section exists to
prevent.

**The rule is `BOT-CHAT.md` section 8's** -- and its *shape* stands, though its
arithmetic was corrected on 2026-09-03 once the server was actually read. A bot
never proposes a value, so no tempo change can originate with the band. The band
moves only when the humans present have already cast what a room of just them
would have needed -- `humanVotes >= (H * threshold + 50) / 100`, the server's
own line evaluated on the human population -- tested only while the band is
still silent, so `N` *is* the human count and nothing has to be disentangled. A
change of leading candidate resets it: the band supports a value, not the idea
of changing.

That gate replaced a strict majority, which was a guess at a formula the server
publishes and was wrong in **33** of the swept cases, in both directions. See
section 8; the correction matters here only in that the conductor evaluates it.

**What the conductor changes is HOW the band then votes, and it is the last
place the deleted arbitration was still assumed.** Section 8 point 4 has each
bot wait its own delay and, on waking, check whether the motion has already
carried -- delay-and-watch, the mechanism this design removed everywhere else.
It was the right answer for peers: it casts only the votes the band's own
presence made necessary, with no ranking and no message passing.

A conductor knows all of it at once, so it counts instead of racing.

### How the conductor votes

The vote line carries both numbers -- `N/M`, cast over **required**, parsed by
`ChatFormat::parseVote`. `M` is a vote count, not a head count:
`(vucnt * threshold + 50) / 100`, formatted straight into the line
(`justinfrankel/ninjam server/usercon.cpp:1239`). So the shortfall is arithmetic
rather than a guess, and the threshold percentage never has to be recovered for
it:

```
needed = M - N
```

evaluated at the moment the gate trips, which is the one moment `N` is known to
be purely human (section 8 point 2). Then:

| `needed` | What the conductor does |
|---|---|
| `<= 0` | nothing -- the room carried it without the band |
| `1` | votes, and that is the whole of it |
| `> 1` | votes, and commands `needed - 1` members to vote the same value |

Members are picked in roster order. Order does not matter when the count is
exact, and a deterministic pick is one less thing to reproduce in a test.

**There is no fourth row, and an earlier draft of this section was wrong to
have one.** It said the band could be short even unanimously -- at a 100%
threshold, one abstention -- and gave the conductor a refusal for it. That case
does not exist. At the moment the gate trips, `N` is `(H * t + 50) / 100` and
the shortfall is

```
need(H + B, t) - need(H, t)   <=   B      for every t, H and B
```

which is what the gate buys: it trips only when the humans have covered their
own share, so what remains is the band's share and the band always has exactly
that many members. Swept for `t` 1..100, `H` 1..20, `B` 1..8 -- no case exceeds
`B`, and the bound is tight (`t = 89, H = 5, B = 8` reaches it).

What can still leave the band short is a **race, not a rule**: a human joining
between the gate tripping and the votes landing raises `vucnt`, and `M` with it.
That wants a re-read of the count before casting rather than a policy.

**Commanded, not messaged.** The natural way to describe this is the conductor
private-messaging the members it needs, and that is exactly right across a
process boundary -- if members ever become separate clients, `/msg` is the path
and nothing above changes. In one process it is a `BandControl` call, because
that interface already exists for precisely this and a chat round trip between
two objects in the same address space would be ceremony.

It is, though, the **first band command with no interval attached**. Play and
stop name the interval they take effect from because they change what the pump
renders; a vote is an act on the server, immediate and outside the audio
timeline entirely. That is a genuine second shape for `BandControl::command`,
and it should be admitted as one rather than smuggled in with `atInterval = -1`.

**Expiry has to be timed, because it is not announced.** (BUILT: the latch
keeps the time it was set and the timeout the line reported, and lets go when
that has passed. Without it the band backs a value once and never again.) The server prints a
leading candidate and prints the change; it never prints a failure -- the tally
is only ever recomputed when somebody votes, so an expiring vote makes no
traffic at all. So "the vote failed" is the conductor's own timer since the last
vote line for that candidate, with no `setting BPM to` seen. The duration comes
off the line (`VoteState::timeoutSeconds`, `[each vote expires in %ds]`) and is
**not** 60: the server's default is 120, and the 60 written down here for a year
came from the example config. It then drops the latch and says
nothing -- the room can see the tempo it still has, and narrating a change that
did not happen is the chorus this design exists to avoid.

That fits the split the rest of the design rests on: whether the gate has
tripped is one answer about the room, so it is the conductor's; voting is an
act, so it is the members'. Deciding is central, acting is collective -- as with
play and stop.

**Still not scheduled, and still wants measuring.** `ROADMAP.md` carries
*Measure the server's vote threshold*. The mechanics above are read from the
reference source rather than observed, and this project's own rule is that a
number needs a method (`PRINCIPLES §5`). Read, then verified against a real
`ninjamsrv`, then built.

## Open questions

Genuinely unsettled, listed rather than papered over:

1. **The vote threshold**, per the section above. The RULE is settled and
   corrected; what is unmeasured is the server's actual arithmetic, which was
   read from the reference rather than observed.
2. **What the conductor says when it refuses at the cap.** A refusal is a rule
   speaking, not a failure, so it wants a line rather than a silent `false`.
3. **Removing a bot**, and what that does to the roles below it. `BOT-CHAT.md`
   16.10 already lists this as unexamined.
4. **What a role change sounds like** when the band closes ranks -- 16.10 again;
   probably wants an interval or phrase boundary, like the ending.
5. **The public-server shape.** One binary, one conductor, N members, joining a
   stranger's room with the cap at four. Sketched, not designed.
