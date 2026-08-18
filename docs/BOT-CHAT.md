# Bots that talk

**Status: a proposal for review. Nothing here is built.** It is written to be
argued with; the open questions at the end are the parts I think are genuinely
undecided rather than merely unwritten.

Scope: making the practice room's bots feel like present, responsive players
through the chat channel, without a language model and without becoming a
novelty. `ROADMAP.md`'s **Tutorial bot** area is a subset of this and would be
absorbed by it.

---

## 1. Why most chat bots are bad, and what we are actually after

The failure mode is well known and it arrives fast. A bot that tries to hold a
conversation with pattern-matched replies is charming for about three exchanges
and irritating forever after, because the illusion is thin and everyone can feel
it thinning. The second failure mode is volume: a bot with something to say
about everything makes the chat pane useless for the thing chat is for, which is
the humans talking to each other.

So "more conversational" is the wrong target. The right one is narrower and much
more achievable:

> A bot should feel like a **player who is concentrating**. Present, aware of
> the music, quick to answer when spoken to, and otherwise quiet.

That is what a good session musician is like in a room. They are not making
small talk during the take. When you ask them what they are playing, they tell
you immediately and precisely, because they know. The presence comes from the
precision and the readiness, not from the chatter.

**Not having a language model is an advantage here, not a compromise.** A bot in
somebody's jam room must never say something strange, wrong, or embarrassing;
must behave identically every run so it can be tested; must work with no network
beyond the Ninjam socket; and must answer instantly. A table of cues and
templates gives all four by construction. An LLM gives none of them.

---

## 2. Five rules

Everything below is downstream of these, and any addition should be checked
against them.

1. **Silence is the default and speech is the exception.** A bot that says
   nothing for an hour is behaving correctly. Every line must earn itself
   against a budget.
2. **Only say what a player would know and a human would care about.** The bot's
   authority is that it is *playing*. What figure it is on, what key it thinks
   we are in, what it just changed -- that is real information nobody else in
   the room has. "How are you today?" is not.
3. **Never simulate understanding.** Unmatched input gets one honest, visibly
   limited reply, never a plausible-sounding guess. The bots should read as
   machines that play music and know it, because that is what they are, and it
   is more likeable than a bad person impression.
4. **Answer where you were asked.** A private message is answered privately. The
   room is addressed only when the whole room benefits.
5. **Be trivially silenceable**, in the same way a bot is already trivially
   evictable. `quiet` is as important a command as `part`.

Rule 3 is the one to hold hardest. The moment a bot answers "how's it going" with
"pretty good, how about you?", it has started a conversation it cannot finish.

---

## 3. What a bot actually knows

This turns out to be the crux, and it is more constrained than it looks.

**A bot is deaf.** `PracticeBot` sets `setDefaultRecvEnabled(false)` at
construction, which sends an empty usermask, which means the server never
forwards it anyone's audio -- deliberately, since that is what keeps a four-bot
room costing one client's worth of interval buffers rather than four.
An unsubscribed client is not sent interval data **at all**, so a bot does not
know who is playing, when they stopped, or whether anyone is there but silent.

So the honest inventory of what a bot can know today:

| Knows | From |
|---|---|
| Its own instrument, figure, character, seed | itself |
| The key and chart it is following | `[key: ...]` and `\| Am \| F \|` chat lines |
| Tempo and interval length | `SERVER_CONFIG_CHANGE` |
| Who is in the room, and their channel names | `USERINFO` broadcasts |
| Who joined and who left, and when | `JOIN` / `PART` |
| The topic, and everything said in chat | chat |
| What the other bots are playing | only if told; they do not share state |

And the one thing it cannot know: **whether you are playing at all.**

**The four players keep that boundary.** They interact in chat and do not
listen. It is worth stating precisely because it is *why* several tempting ideas
are absent below: a player bot cannot tell you that you dropped out, cannot
compliment a phrase, and must never sound as though it could.

**The tutor is the one exception, and only for the person it is teaching.** It
subscribes to the owner alone -- the same thing the echo bot already does with
`setListensTo` -- and it does so for one narrow purpose: to tell you *"that went
out and here is why nobody has heard it yet"* rather than saying so and hoping.
A tutorial that claims your audio reached the room without checking is a
tutorial that will eventually be wrong at the worst moment, when you are new and
have no way to tell which of you is mistaken.

That costs one player's worth of decoded interval buffers, on a bot that has no
instrument and leaves when it is finished. It is a fair price for the one thing
in the thread that cannot be faked. What the check is, and how carefully it has
to avoid becoming a judgement, is section 7.

Everything beyond that -- a bot that responds musically to what you played -- is
future work, sketched in section 14 because the shape of it is interesting and
worth not forgetting.

---

## 4. The mechanism: cues, not conversation

No dialogue tree, no state machine of intents, no matching against free text
beyond keywords. Just a table.

```
Cue = (trigger, guard, budget class, cooldown, templates)
```

- **Trigger** -- an event: a chat line addressed to me, a key change, a player
  joining, an interval boundary, N intervals since something.
- **Guard** -- a predicate over what the bot knows. "The key changed AND I have
  been playing for at least two intervals AND nobody has mentioned the key in
  the last ten."
- **Budget class** -- `answer` (replies, effectively unlimited but only when
  asked), `notice` (unprompted, strictly rationed), `teach` (the tutorial
  thread, once each, ever).
- **Cooldown** -- per cue and per topic, so the same observation cannot recur.
- **Templates** -- two or three phrasings with slots filled from real state. The
  bot's own seed picks which phrasing it uses, and keeps using it, so a bot
  sounds like itself all session and two bots do not sound like one.

Seeded phrasing is the whole of the "personality" mechanism, and it is a dozen
lines. It gives consistency, which is most of what reads as a person, without
anybody writing a character.

**Budgets, concretely.** A `notice` costs a token; the bucket holds two and
refills one every eight intervals, roughly half a minute at 120/8. Unspent
tokens do not accumulate. Four bots therefore cannot say more than about eight
unprompted lines in a five-minute stretch even if everything is happening at
once, and in a quiet room they say nothing at all.

---

## 5. Being addressed, and understanding what was said

### Who is being spoken to

Before what a message means comes who it is for, and in a room with four bots
and four humans this is the question that decides whether the feature is
tolerable. Four bots answering one question is the failure this whole design
exists to avoid, and it would happen on the very first "what are you playing".

**The rule: exactly the bots that were addressed answer, and nobody is
addressed by default.**

That is a correction to an earlier draft, which said "at most one bot ever
answers". One was standing in for "not all four", but it is the wrong number:
`delvo, mirn, can you turn it up` names two people and should get two
answers, exactly as it would from two humans. What has to be impossible is a
bot answering something that was not aimed at it -- not two bots answering
something that was aimed at both.

#### Not a grammar: a scan for names it already knows

There is no parsing of sentence structure here and there does not need to be.
A bot knows every username in the room from `USERINFO`, and that list is short
-- a jam is a handful of people. So addressing is a scan of the message's tokens
against a known, tiny vocabulary of proper nouns, which is a different and far
easier problem than working out what a sentence is doing.

Matching is on whole tokens, case-insensitively, with punctuation stripped, so
`delvo`, `Delvo,` and `@delvo` are one thing, and a longer word that merely
happens to contain it is not. Where
in the message the name falls changes only how strongly it counts:

| Signal | Example | Strength |
|---|---|---|
| private message | (any) | certain |
| name first, with a separator | `delvo: what are the changes`, `delvo, ...`, `@delvo ...` | very strong |
| name last | `what are the changes delvo` | very strong |
| the name alone | `delvo` | very strong -- see below |
| name anywhere | `what is delvo playing` | strong |
| several names | `delvo, mirn, turn it up` | each is addressed |
| instrument noun in the name position | `bass, what are you playing` | strong |
| near-miss on a name | `holis:`, `hollos` | strong, if unambiguous |
| continuation, from the person who opened it | `and the chords?` | moderate |
| nothing at all | `what are you playing` | none -- **nobody answers** |

The last row is the important one. **First contact has to be explicit.** An
unaddressed question in a room with eight participants is not a question for a
bot, and answering it is presumptuous.

#### The name on its own, and the attention window

Saying just `delvo` is the most natural way there is to start talking to
somebody, and it should work:

> `you: delvo`
> `Delvo[bass-bot]: here -- roots on the changes, D minor.`
> `you: what are the changes`
> `Delvo[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.`

The greeting is doing two jobs and the second one is why it is phrased that
way. It acknowledges, and it says what this bot is in a position to talk about
-- so a player who typed a name out of curiosity now knows what to ask next. A
bare "hey, what's up" would acknowledge and teach nothing, and rule 3 makes
that phrasing a promise we cannot keep anyway.

Being addressed in any form opens an **attention window** on that bot, and
while it is open the bot will answer follow-ups without being named again. That
is what makes it a conversation rather than a series of commands.

**The window belongs to a person, not to the room.** Only messages from
whoever opened it count as follow-ups; two other people talking to each other
are not talking to the bot, and the commonest way a design like this becomes
insufferable is by assuming otherwise. The window closes on a timeout of about
a minute, after a few turns, or when its owner addresses somebody else --
whichever comes first.

#### The room tells you who a message is not for

The strongest signal available is a negative one, and it costs nothing.

**Never answer a message aimed at somebody else.** A bot knows the room's user
list, so a message naming any other participant -- human or bot -- is not for
it. `dave, what pedal is that` is answered by nobody, with no understanding of
the sentence required.

**Channel names are evidence too.** Ninjam channels carry names, and players
name them after what they are playing. If a human in the room has a channel
called `guitar`, then the bare word "guitar" in chat is far more likely to be
about that person than to be an instruction, and it should not be treated as
one. The room is telling you what its common nouns refer to; the list is right
there in `USERINFO` and nothing else has to be inferred.

#### Bots never trigger bots

Four bots that can hear each other and answer each other is a room that fills
with chat and cannot be stopped, and it is the failure that would end this
feature permanently. It is worth more than a convention.

**The invariant: only a message from a human ever causes a bot to speak.** Not
"bots should ignore each other" -- that is the mechanism, and mechanisms fail.
Stated as a property of what can cause speech at all, a loop is structurally
impossible rather than merely unlikely, because the chain has no step that a
bot's own output can start.

This is load-bearing from the very first line, which is worth seeing clearly:
**the arrival roster names every bot in the band.** If bot messages were not
excluded, that one line would address all four at once, and each reply might
name others again. The feature would fail in its opening second.

So how a bot knows another bot is a bot matters, and there are two answers for
two situations.

**Bots we spawned: exactly.** A practice room creates its band and knows every
name in it, so it tells each bot its siblings. That is an exact list, not a
guess, and it covers the case that actually exists today -- the only way bots
enter a room at present is that somebody started a room full of them.

**Bots we did not spawn: the `-bot]` marker in the username, and that is
enough.** It is spoofable, but consider what spoofing buys: a human deliberately
naming themselves `Delvo[bass-bot]` to make bots ignore them. That is a person
choosing to be ignored, which is not an attack. The reverse -- a human causing a
loop -- requires them to impersonate a bot AND to keep emitting lines that
address other bots, at which point they are the loop rather than the bots, and
they can be evicted like anyone else.

Two further limits bound the damage if identification fails anyway: a bot
answers a given speaker at most once every few seconds, and there is a hard cap
on lines per minute. A spoofed name costs one exchange rather than an afternoon.

What is deliberately NOT relied on is anything cleverer -- no handshake, no
capability probe, no behavioural heuristic. A protocol between bots is state
they would have to agree about, and this whole design's advantage is that they
never have to.

#### Answer where you were asked

Rule 4, made concrete. A private message is answered privately; a room message
is answered in the room. Nothing else is natural -- a public question answered
in a PM looks like no answer at all, and a private one answered publicly is a
small betrayal.

This matters more than it looks, because **the public path is how anybody finds
out the feature exists.** A player watching somebody ask the keys bot to switch
to guitar has just learned that they can too. A design that only takes private
messages is undiscoverable by construction, however well it works.

#### One practical trap, already hit

Bot usernames must not contain spaces. Every Ninjam client sends a private
message as `/msg <user> <text>` and splits on the first space, so the original
name `Keys [bot]` could not be sent one at all: `/msg Keys [bot] guitar`
addresses a user called `Keys`, who does not exist, and fails silently.
Antiphon's own client does this (`PluginEditor.cpp`), and so, being the same
one-line parse, will everyone else's. This is half of why the names in §8 are
one token -- the other half is that they have to be sayable.

Two separate fixes, and both are worth doing because they fail differently.
The names lose their spaces, which fixes every client including the ones we do
not ship. And Antiphon resolves `/msg` and `/kick` against the room's user list
by longest match rather than against whitespace, which additionally reaches
humans whose names have spaces in them. Tab completion over the same list is
the obvious companion and is tracked in `ROADMAP.md`.

#### Leaving: the one thing that must work unaddressed

`part` is the exception to "nobody is addressed by default", and it is the only
one. Everything else can safely require a name; this cannot, because its failure
mode is a room full of bots that somebody cannot get rid of. That property
outranks conversational tidiness, and it holds wherever a bot is pointed rather
than only in a practice room.

- **`part` alone, as the entire message**, in the room: the whole band leaves.
- **`delvo, part`**: that one leaves.
- Anywhere inside a sentence: nothing. "what's your part", "the bass part",
  "learn my part" are ordinary jam chat and by far the commonest use of the
  word. The match is on the trimmed message being exactly `part`, which is what
  `isPartCommand` already does.

**The arrival line must not invite it.** A first-time player who types the first
command they are shown, out of curiosity, and watches the whole band vanish has
had a bad first minute -- so the roster leads with the interesting thing and
states the destructive one in terms nobody types idly:

> `say a name to talk to one of us. say "leave" and we all go home.`

That is a judgement with a cost, and the cost is worth writing down: naming it
at all is a small invitation, and not naming it leaves the eviction instruction
only in `help`. Safety wins, because the recovery is cheap in the case where
the accident is likely -- a practice room is restarted from Antiphon's own UI
in one click -- and expensive in the case where it is not, which is somebody
else's bots in a real room.

#### Two bots called Delvo

Names are picked at join and never change, because Ninjam sets a username at
authentication and there is no rename.

The full username -- `Delvo[bass-bot]` -- is unambiguous and always works. The
short handle `delvo` is a convenience, and it is **withdrawn the moment it
becomes ambiguous**: if any other participant's name matches or contains it, the
bot stops accepting the bare handle and answers only to its full username or to
its instrument. Silence beats a wrong answer, and this is the same rule as
"never answer a message aimed at somebody else" seen from the other side.

Two things keep that from happening often:

- **The pool is bigger than the band.** Names are drawn from a list of a dozen
  or more, and any that collide with somebody already in the room are skipped at
  join. A collision then requires a human to arrive later AND to be called the
  same thing.
- **The names are chosen not to be plausible usernames**, which is the real
  answer and is what makes the rest of this rare enough to ignore.

That last criterion is harder than it sounds, and it is worth being explicit
that an earlier suggestion failed it: `Hollis`, `Wren` and `Sabine` are real
first names, and real first names are exactly what people use as handles. What
is wanted is a coined word -- pronounceable, unambiguously spelled, and not
something anybody is called:

- **not an ordinary English word, a name, or a brand**, so it can be matched
  anywhere in a sentence safely;
- **one obvious pronunciation.** `Ravo` and `Pemo` were dropped for failing
  this: RAY-vo or RAH-vo, PEE-mo or PEH-mo, with nothing to decide between
  them;
- **a rime an English reader already owns.** This turned out to matter more
  than syllable count, which is what an earlier draft asked for instead.
  `Mirn` is one syllable and reads instantly, because `-irn` is *fern*, *burn*,
  *turn*. `Nolm`, `Selm`, `Velk` and `Cralt` are also one syllable and were all
  rejected on sight: `-olm`, `-elm`, `-elk` and `Cr-`/`-alt` are clusters with
  no familiar English pattern behind them, so they read as truncations, as
  though a letter were missing;
- **one token, no spaces**, so `/msg` reaches it in every client;
- **distinct first letters, and at least two edits apart**, so the near-miss row
  in the table above stays unambiguous. `Vurn` was dropped for failing the
  spirit of this rather than the letter: it is two edits from `Mirn` but shares
  its rime, and two names that are near-homophones aloud are the one thing a
  spoken address cannot afford.

**The band: `Mirn`, `Delvo`, `Pundo`, `Quado`.** Chosen by searching thirty
candidates and keeping the least occupied -- not by counting results, which no
search tool reports, but by asking the question that actually matters: is this
word already a person, a handle, or a brand that somebody might turn up using?
Eighteen were struck for being exactly that, including a Premier League
goalkeeper (`Kepa`), a techno producer on Drumcode (`Weska`, the worst possible
collision for a music program), an AI chat app (`Fenko`), and several ordinary
given names. What is left is owned by a dog chew, some industrial screwdrivers,
a Bhutanese stone-throwing sport and a gas-meter acronym.

Which name goes to which instrument comes from the room seed, so the same seed
brings the same players back and a shake does not.

**The tutor is not one of them: it is called `Tutor`.** It is a role rather
than a bandmate, and you address a role by what it is -- `tutor:` is what
anybody would type without being told, and nobody says the word casually in a
jam. It is matched in the address position only, exactly like `band`, for the
same reason.

**The pool is currently the same size as the band, which is a known gap.** §5
wants spares so that a name colliding with somebody already in the room can be
skipped at join; with four names and four players there is nothing to skip to,
and a collision falls straight through to the instrument fallback. That works,
but it is the degraded path rather than the intended one, and the fix is to
vet another handful of names against the criteria above.

#### The deliberate exception

`everyone`, `all`, `band`. Then they all answer, in a fixed order, one short
line each, because that is what was asked for.

These are matched **in the address position only**, unlike a name. "band" is an
ordinary word in a room full of musicians -- "nice band", "the band's tight" --
and a bot that answers those is the poltergeist this section is about. A name
like `delvo` is rare enough to be matched anywhere in a sentence; `band` is
not, and the difference is exactly why the names are what they are (§8).

#### One answer, without any coordination

Every bot sees the same chat and the same user list, so every bot can compute
every other bot's score for the same message and speak only if it is addressed.
Ties break on a fixed order all of them know. There is no protocol, no election
and no shared state -- the same trick as one bot acknowledging a key change,
and it works because the inputs are identical for everyone.

A worked case, with four bots and two humans in the room:

```
you: what are you playing
(nobody -- not addressed)
you: delvo
Delvo[bass-bot]: here -- roots on the changes, D minor.
you: and your sound?
Delvo[bass-bot]: fingered, fairly dark.
you: dave what pedal is that
(nobody -- that is for dave)
dave: what are you playing
(nobody -- dave has not addressed anyone, and Delvo's window is yours)
you: delvo, mirn, can you turn it up
Delvo[bass-bot]: up 2 dB.
Mirn[kit-bot]: up 2 dB.
you: band, what are you playing
Mirn[kit-bot]: five over eight, accents on 1 and 4.
Delvo[bass-bot]: roots, on the changes and the kick.
Pundo[keys-bot]: the chart, held, one chord a bar.
Quado[lead-bot]: eighths over D minor, resting on the weak beats.
```

`test/fixtures/bot-addressing.txt` is the corpus for this, and it is separate
from the phrase corpus because it tests a different axis: not what a message
means, but whose it is.

### What it means

What a bot should do with a message it has decided is for it is the harder half
of this document. Exact-match command
words are what makes a rule-based bot feel like talking to a wall: they work
when you happen to type the magic phrase and fail flatly otherwise, which
teaches you that the thing is a vending machine. The goal is not general
conversation -- it is that **within this narrow domain, indirect phrasing
works**, and hitting the fallback is rare enough to be measured as a defect.

### The intents

Twelve, and they are the whole surface:

| Intent | Answers with |
|---|---|
| `DESCRIBE_PART` | its figure and how it sits: "five over eight, accents on 1 and 4" |
| `DESCRIBE_SOUND` | its character: "deep kick, soft beater" |
| `REPORT_KEY` | the key it follows, and whether it was told or defaulted |
| `REPORT_CHART` | the chart, in letters and degrees |
| `REPORT_TEMPO` | tempo and interval length, and that the server owns them |
| `SET_KEY` | that the room decides the key, what it is now, and how to change it |
| `SET_TEMPO` | that a tempo is a server vote, what it is now, and how to call one |
| `SET_CHART` | that the room decides the chart, what it is now, and how to put one up |
| `RESET_CHART` | the chords the key implies, as a line to paste |
| `SET_KEY` | 14 |
  | `SET_TEMPO` | 12 |
  | `SET_CHART` | 10 |
  | `RESHUFFLE` | rerolls, and says what changed |
| `SET_QUIET` / `SET_LOUD` | stops or resumes speaking at all, per bot |
| `EXPLAIN_SELF` | what it is and how to remove it |
| `LEAVE` | parts, as now |

Slots ride along where they make sense: a key, a chord chart, a tempo, an
instrument name.

**`SET_KEY`, `SET_TEMPO` and `SET_CHART` are recognised even though a bot cannot
carry any of them out**, and that separation is the point. What a bot may *do* is a
question about authority; what it should *understand* is a question about not
being a wall. Answering "the key is Am" to somebody who just asked to play in G
minor is the most expensive kind of miss, because it looks like an answer and
ignores what was asked -- exactly the failure the fallback exists to prevent,
arriving through the front door instead.

So the reply is an honest one, in the shape section 5.3 already uses:

> `Pundo[keys-bot]: i cannot set the key -- that is whatever the room agrees. say it in chat and i will follow it.`
>
> `Mirn[kit-bot]: tempo is a server vote, not mine to give. vote for it and i will back you once the room has.`

### Being asked to change something they cannot

`SET_KEY`, `SET_TEMPO` and `SET_CHART` are recognised and none of them is a
thing a bot may decide. The reply is the same three parts every time, and the
order matters:

1. **That it cannot, and why** -- as a fact about the room, not an apology about
   being a bot. "the key is whatever the room agrees" is information; "sorry, i
   can't do that" is a wall.
2. **What the current one is** -- because the question is nearly always asked by
   somebody who does not know it, and answering only the first part sends them
   away with less than they came with.
3. **How to actually do it**, in a form that works in any client.

Part 3 is the one worth getting right, and two of the three are genuinely
universal:

| | How, for anyone | Universal? |
|---|---|---|
| Tempo | `!vote bpm 130`, `!vote bpi 16` | **yes** -- a server command, every client has it |
| Chart | type `\| Am \| F \| C \| G \|` in chat | **yes** -- Jamtaba's convention, not ours |
| Key | say `[key: G minor]` in chat, or leave it in the topic | ours, but plain enough to read anywhere |

The key tag is the honest exception and should not be oversold. `[key: ...]` is
an Antiphon convention (`MusicalKey::tagPrefix`); a Jamtaba user will not have
it parsed for them. It is written the way it is so that it still *reads* as a
sentence to somebody whose client knows nothing about it, which is the most that
can be claimed for it.

```
you: pundo, can we play in g minor
Pundo[keys-bot]: putting it up for the room -- was D minor. [key: G minor]

you: mirn, speed up
Mirn[kit-bot]: tempo is a server vote, not mine to give. we are at 120 bpm,
               8 bpi. type "!vote bpm 130" and i will back it once the room
               does.
```

**The key was the exception, and the fix was a second form.**
`MusicalKey::parseTagged` matches `[key:` anywhere in a line, so a reply
explaining the tag would set the key by explaining it -- the advice performs the
action. That is why `/key D minor` is now also accepted, matched only at the
**start** of a line: it is sayable, and it is typeable from any client, since
other clients pass an unknown slash command through as ordinary chat.

So a bot explains, and offers to act as a shortcut rather than as the only
option. `MusicalKey::announcementAdvice` is the one place that produces the
sayable form, and `test/BotAnswerTests.cpp` asserts that **no reply this
codebase can generate parses as a key announcement**. That test earns its place:
it caught the same class of bug a second time, when dropping a provenance suffix
left `describeChart` returning bare chart text that any client would have read
as somebody announcing a chart.

**The chart needs none of this.** A chart must *begin* its line
(`Harmony.cpp:622`), so `| Am | F |` is quotable mid-sentence and a bot can
simply explain it. The asymmetry is not inconsistency: it is the two parsers
being strict and loose for good reasons of their own -- the chart parser strict
to keep prose out, the key parser loose so a key can ride in the topic.

**Nothing a bot says is client-specific in the room.** The owner is the one
player whose client is known for certain. Shorthands belong in a private message
to them; room chat gets the portable form.

**The replies are `src/BotAnswer.{h,cpp}`**, pure functions over a small `Room`
struct, so every line a bot can say is readable -- and reviewable -- without
starting a room. `test/BotAnswerTests.cpp` prints the whole transcript, because
a sentence that reads badly is a defect no assertion catches.

**The two special cases are both about not implying a decision was made.**

Neither a key nor a chart is ever absent -- the room starts at C major
(`PracticeRoom.h`), and a key arriving sets `Harmony::defaultChart` -- but
being *defaulted* and being *chosen* are different facts, and reporting the
first as though it were the second tells somebody the room has settled on
something it has not:

```
Pundo[keys-bot]: nobody has named a key, so i defaulted to C major. name one
                 and i will put it up for the room.
```

A chart is never absent either, which corrected an earlier draft here: "playing
on the key alone" was false, and a bot being wrong about what it is playing is
a bot being wrong about the only thing it is authoritative on. It names the
chart it is actually on -- which doubles as the example, and a *safe* one, since
a generic `| Am | F | C | G |` pasted into a room in D minor would silently move
the harmony:

```
Quado[lead-bot]: nobody has put a chart up, so i am on | Dm | Bb | F | C |, the
                 default for the key. put one on a line of its own, starting
                 with a bar, and i will play it.
```

`REPORT_KEY` and `REPORT_CHART` carry the same two distinctions, which is why
the intent table says "and whether it was told or defaulted".

### Settled

- A **default is never reported as a decision**: key and chart each carry their
  source (defaulted / from the topic / said in chat, with who).
- A **topic value says so, and bounds its claim** to "nobody has said otherwise
  since i joined" -- the topic reaches only a joining client, so its age is
  unknowable.
- An **unreadable key is answered, not guessed**. Putting up the wrong key is
  worse than putting up none.
- The **tempo reply names both numbers always**, because 120 at 8 and 120 at 32
  are different rooms; names only the one that was asked to change; and
  **refuses what the server would refuse**, since an out-of-range `!vote` is
  answered with a complaint about the command's parameters.
- **A bot never starts a vote, even asked to.** Four bots backing one person on
  request is that person having four votes.
- A **two-part question gets one reply**, not two: chat is the scarce resource.
- **Mixed common and personal**: each addressed bot answers its personal part,
  and whichever wins the delay-and-watch race also carries the common one.

Not built: syncing the practice room's topic to the key, which needs a chat hook
on `PracticeServer` that does not exist yet. See `ROADMAP.md`.

### Common answers, and the one bot that gives them

Addressing decides *who* was asked. It does not decide how many should speak,
and for a whole class of question the answer is the same from every bot:

| Personal -- every addressed bot answers | Common -- exactly one answers |
|---|---|
| `DESCRIBE_PART`, `DESCRIBE_SOUND` | everything else |
| `EXPLAIN_SELF` | |

**Built.** An earlier version of this table put `RESHUFFLE`, `SET_QUIET`,
`SET_LOUD` and `LEAVE` in the personal column, which contradicts the rule stated
directly above it: "ok, something else" is the same sentence from all four, and
four bots saying it is exactly the chorus this exists to prevent. The test is
not which intent it is -- it is **whether the answer would differ between
bots**. Only three do: what each one is playing, what each one sounds like, and
what each one is.

**The half-stopped band is the case that rule alone gets wrong.** Tell a band
where two are playing and two are silent to stop, and the sentences genuinely
differ -- "we're wrapping it up" against "already stopped" -- but it is still
one thing happening to one band, and one bot should say so. Worse, whoever won
a flat race would answer for everybody, so a silent bot could tell the room
nothing was happening while the rest ended the tune.

So the delay has two tiers: **a bot that acted speaks ahead of one that had
nothing to do.** If nobody acted, the deferred line is the right answer and
still gets said.

The worked transcript above has `band, what are you playing` answered by all
four, and that is right -- those are four different answers. `band, what are the
chords` is not: it is one fact, and four bots reciting it is the chorus this
whole design exists to prevent.

**Acting is collective; speaking is arbitrated.** `band, shake` rerolls all four
parts, because each bot's action is its own. Only the *line about it* is
rationed.

**The arbitration is the one we already have**, for the fourth time: each bot
waits its own short staggered delay, and on waking checks whether the answer has
already been given. If it has, it says nothing.

That is preferable to the fixed order the key-change cue proposes ("lowest
instrument first"), and the reason is `SET_QUIET`: quiet is **per bot**, so a
fixed order picks a bot that may have been told to shut up, and the room gets
silence where it asked a question. Delay-and-watch degrades to the next bot
automatically, with no shared state and nothing to keep in sync. It should
replace the fixed order in section 6 as well.

One primitive, four uses: the arrival roster, the tempo vote, the key-change
acknowledgement, and this. That is the strongest argument that it is the right
primitive.

### Voting, and why four bots nearly break it

A NINJAM tempo change is a server vote, and the threshold is a proportion of
**everyone connected** -- bots included, because a bot is an ordinary client.
The denominator is `vucnt`, every user with `m_auth_state > 0`, voted or not
(`justinfrankel/ninjam server/usercon.cpp:1192-1200`). There is no vote
*against*: you vote within the window or you do not, so **abstaining is a vote
against**, and a silent bot is not a neutral one.

Four bots therefore do not merely fail to help. They take the room's tempo
control away, and the band as designed is worse than playing alone:

| Humans | Bots | Needed (at 60%) | Can the humans carry it, if the bots abstain? |
|---|---|---|---|
| 1 | 0 | 1 | yes |
| 1 | 4 | 3 | **never** |
| 2 | 4 | 4 | **never** |
| 3 | 4 | 5 | **never** -- even unanimously |

**What the server tells us, and what it does not.** The only vote traffic on the
wire is English in a chat line (`ChatFormat::parseVote`):

```
[voting system] leading candidate: 3/5 votes for 137 BPM [each vote expires in 60s]
[voting system] setting BPM to 137
```

That gives the count `N`, the threshold `M` and the value. It does **not** name
the voters, and it reports only the leading candidate, never the split. So "wait
until every human has voted, then follow the majority" cannot be implemented as
stated -- neither half is observable, and you can never distinguish a human who
has not voted yet from one who never will.

**The rule.** Enough is observable without them:

1. **A bot never proposes a value.** It votes only for a leading candidate that
   already exists, so no tempo change can ever originate with the band.
2. **Every vote before the band moves is a human one, by construction.** A bot
   knows the user list, and `BotNames::looksLikeBot` tells it which members are
   bots, so it knows `H`. It also knows that no bot votes until the gate below
   trips -- so while the band is waiting, `N` *is* the human count. Nothing has
   to be disentangled and no voter has to be identified: the only two inputs
   are how many humans are in the room and how many votes have been cast.
3. **A strict majority of humans must back the candidate** -- `humanVotes * 2 >
   H`. If that never happens, no bot votes, and the offer expires exactly as it
   would have in a room with no bots in it.

   **Strict, not "half is enough".** The two differ only when `H` is even and
   the room splits evenly, and there the weaker gate is wrong: at a 60%
   threshold it disagrees with a bot-free room at *every* even `H`, always by
   letting exactly half the room carry a change over the other half. In a
   two-person room that is one player overruling the other with the band's
   help, which is the precise failure this rule exists to prevent.

   The gate is tested **only while the band is still silent**, and the instant
   it trips the timers start. It is never re-tested, so it never has to be: any
   vote arriving later, human or bot, can only add to the total. That ordering
   is what makes the whole rule cheap -- there is no latch to keep, no bot
   votes to subtract, and no way for the band to be counting itself.
4. **Then they queue, the way they announce themselves.** Each bot waits its own
   delay -- a few seconds plus a small random spread from its own seed, well
   inside the 60-second expiry -- and **on waking, checks whether the motion has
   already carried. If it has, it does not vote.**

   This is the same shape as the arrival roster in section 6, and it earns the
   same thing twice over. The band casts *exactly* the votes its own presence
   made necessary and then stops, with no ranking between the bots and no
   message passing: whichever bot wakes to find the job done simply stays out of
   it. It also keeps four `!vote` lines from landing in the chat at once.
5. **A change of leading candidate resets everything** -- the gate reopens and
   the timers are dropped. The band's support is for a value, not for the idea
   of changing.

**This needs no coordination**, which is the reason to prefer it. Every input is
public, the rule is a pure function of them, and each bot reaches the same
answer independently. A rule they can all evaluate without talking to each other
beats a protocol between them, every time.

**Does it distort the outcome?** No -- and that is a stronger answer than the
one first written here, which used a ceiling where the server rounds half up.

The server's arithmetic is `(vucnt * threshold + 50) / 100` in integer division
(`justinfrankel/ninjam server/usercon.cpp:1239`), and `vucnt` is every
authenticated user. Swept against a bot-free room with the real formula, for
`B = 4` and `H` from 1 to 8, at both 50% and 60%: **identical at every `H`,
with no divergences at all.** A strict majority of humans carries exactly what
it would have carried alone, and a minority carries nothing.

The earlier draft reported one divergence at `H = 7`. That was the wrong
rounding, not a property of the rule.

**This needs no coordination**, which is the reason to prefer it. Every input is
public, the rule is a pure function of them, and each bot reaches the same
answer independently. That is the same trick as the arrival roster in section 6:
a rule they can all evaluate without talking to each other beats a protocol
between them, every time.

**The arithmetic is now read from the server source rather than assumed**, and
recorded in `docs/PROTOCOL.md`. What remains genuinely per-server is the
`SetVotingThreshold` percentage itself, which is configuration -- but the band
never needs it: `M` arrives in the vote line as the denominator of `N/M`, so a
bot reads the threshold off the room instead of predicting it. The sweep above
matters for judging the design, not for running it.

### The pipeline

Seven cheap stages, each independently testable, none of them machine learning
and none of them needing a data file:

1. **Extract slots from the raw text first**, before anything is lowercased --
   `MusicalKey::parseName` and `Harmony::parseChart` already do this well, and
   they need the capitals, since `Am` is a chord and `am` is a verb. This is a
   real advantage of the domain: the nouns already have robust parsers.
2. **Normalise**: lowercase, strip punctuation, collapse whitespace, drop a
   leading vocative (`kit,` / `hey kit` / `@kit`), expand contractions
   (`what're`, `whats`, `dont`), and drop politeness and filler -- `please`,
   `sorry`, `just`, `quickly`, `mate`. Half of "indirect" phrasing is padding,
   and removing it turns a hard sentence into an easy one.
3. **Stem**, so that `playing`, `plays`, `played` and `play` are one token. The
   **Porter stemmer** (1980) is the right tool: about 120 lines, purely
   algorithmic, no dictionary, and specified precisely enough to test against
   its own published vectors. It generalises to words nobody put in the lexicon,
   which a hand-written suffix list does not.
4. **Repair typos**: any token that matches nothing gets a **Damerau-Levenshtein**
   comparison against the lexicon, with the threshold scaled to length -- one
   edit up to five characters, two beyond. `chrods`, `tepmo`, `waht` all land.
   About twenty-five lines.
5. **Map tokens to concepts**. The lexicon is where the robustness actually
   lives: perhaps 150 surface words onto twenty concepts. `part`, `pattern`,
   `groove`, `beat`, `figure`, `rhythm`, `line`, `doing`, `playing` all mean
   `PART`. `sound`, `tone`, `timbre`, `kit`, `patch`, `voice` all mean `TONE`.
   This table is the single highest-value artefact in the feature and it is
   plain data.
6. **Read the shape of the sentence**, which is where the cheap grammar goes.
   Not a part-of-speech tagger -- that needs a lexicon or a model, and is not
   worth it -- but four flags that carry most of the same information:
   - **question**: leading wh-word, leading auxiliary (`are`, `is`, `do`, `can`,
     `could`, `will`), or a trailing `?`
   - **imperative**: leading known verb with no subject
   - **negation**: `not`, `n't`, `no`, `never`, `stop` -- and this one matters,
     because "don't be quiet" and "be quiet" share every content word
   - **second person**: `you`, `your`, `u`, `ur`
   
   "could you tell me what you're playing" reduces to `{QUESTION, YOU, PART}`,
   which is the same as "what are you playing" and the same as "whats ur groove".
7. **Score the intents and pick, or decline to.** Each intent is a small weighted
   bag: concepts that count for it, concepts that count against it, and a bonus
   for the right sentence shape. Best score wins **if it clears the runner-up by
   a margin**.

### The three outcomes, and why the middle one matters most

That last step is deliberately the same shape as `Harmony::inferKey`: score the
candidates, require a margin over the second best, and when the margin is not
there, say so rather than guess. One idea, used twice, and the second use gets
the first one's reasoning for free.

- **Confident** -- answer.
- **Ambiguous** -- two intents close together. **Ask a narrow question.** This is
  the single biggest difference between a bot that feels alive and one that
  feels like a wall, and it is nearly free: the bot already knows which two
  things it was torn between, so it can name them.
  > `you: tell me about your kick`
  > `Mirn[kit-bot]: the part or the sound? "what are you playing" or "what do you sound like".`
  >
  > Built, in a generic form: `not sure whether you want my part or my sound --
  > which?`. The named pair is what matters; the worked example above also
  > suggests the phrasing to type, which is a further step.
  >
  > This has to be checked **before** the winning intent is acted on.
  > `Reading::intent` still holds the winner when `ambiguous` is set, so a
  > switch on it fires first and answers one of the two confidently. That was
  > the bug: the clarify reply was written, and unreachable.
- **Nothing** -- below the floor. Even here it should not be a shrug. Report the
  concepts it *did* recognise, because that turns a dead end into a hint:
  > `you: is the snare a bit much on the turnaround`
  > `Mirn[kit-bot]: i caught "snare" but not the question. i can tell you my part, my sound, the key, the chords or the tempo -- and i cannot hear you, so i am no judge of what is too much.`

The last clause of that is worth keeping: it is honest, it is the answer to a
whole class of questions people will reasonably ask, and it says the limitation
once rather than pretending.

### One turn of memory

Elliptical follow-ups are most of what makes conversation feel connected, and
they cost almost nothing: remember the last intent and slot per conversant, for
a few turns.

> `you: what key are we in`
> `Delvo[bass-bot]: D minor, as announced in chat.`
> `you: and the chords?`
> `Delvo[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.`

`and the chords?` has no verb, no subject and no question word. It resolves
because the previous turn established that we are talking about the room's
harmony. Two fields of state.

### What is deliberately not built

- **A part-of-speech tagger or dependency parser.** Needs a lexicon or a model,
  and the four flags above capture what we would use it for.
- **WordNet or embeddings.** A data file, or arithmetic that is machine learning
  wearing a hat. The 150-word lexicon is smaller, faster and reviewable.
- **ELIZA-style pattern reflection.** The thing that feels alive for three
  exchanges. It is the anti-pattern this whole section exists to avoid.
- **Anything that learns.** Determinism is what makes the transcript testable.

Total: roughly 400 lines of mechanism, most of it table.

---

## 6. Speaking unprompted

The short list. Each is `notice`-class, guarded, and on a topic cooldown.

- **On arriving**: see the choreography below. One line for the whole band
  rather than one line each, which would be four lines of chat before anybody
  has said anything.
- **When the key changes**: at most one bot acknowledges, not all four. Which
  one is settled by the delay-and-watch arbitration in section 5, not by a fixed
  order: a fixed order can pick a bot that has been told `quiet`, and then the
  acknowledgement never comes.
- **When a chart arrives** that it cannot follow: "i can read `\| Am \| F \|` --
  that line did not parse." Useful, because the alternative is a chart that
  silently does nothing, which is exactly the bug the harmony work fixed at the
  UI layer.
- **When the tempo changes**: nothing. The header already says so, and a chorus
  of bots repeating the obvious is the failure mode in miniature.
- **When a human joins the practice room**: one greeting from one bot, with the
  `quiet` and `part` words in it. Never in a real room.

That is the entire list, and it is short on purpose. Everything else I
considered went on the list of things not to say -- including every idea that
began "when the player...", all of which need ears the bots do not have and are
not getting here. See §9.

### Arriving, in order

The opening ten seconds are the only ones where every player is definitely
reading the chat, so they are worth choreographing rather than leaving to
whoever connects first.

The awkwardness to design around is that a band is one thing and the bots are
five separate clients that join at slightly different moments. The answer is
that the band is announced ONCE, by the first bot to arrive, five seconds later,
listing **every bot it can see at that moment** -- not a roster it was handed.
Observed rather than configured, which matters for three reasons: a bot that
failed to connect is not announced as present, bots brought by two different
people still produce one sensible list, and nothing has to be told to anybody.

**The rule is one question, asked by each bot about itself: has somebody
announced ME?** If not, it announces -- itself and every bot it can see. If so,
it stays quiet.

Self-referential on purpose, and that is what makes it work. A bot cannot know
whether it is the FIRST to arrive: the membership list has not come through when
a client finishes authenticating, so every bot sees an empty room and every one
of them believes it is first. But a bot can always know whether it has been
INTRODUCED, because that is something it observes rather than something it has
to infer.

Everything falls out of that one question, including two cases a tiebreak
cannot reach:

- **Ordinary startup.** Whoever wakes first sees the whole band and names all of
  them; the rest find themselves already announced and say nothing. One roster.
- **A bot that joins an hour later.** It was in nobody's roster, so it speaks --
  and it names the band it can see, which by then is everybody. **The
  announcement lands when the band is complete** rather than being lost because
  the moment passed. This is the case the whole design is for: bands assemble
  raggedly.
- **A band whose other members never connected.** It announces itself alone,
  correctly, rather than waiting for a quorum that is not coming.

**The wait is four seconds plus up to two more, and the spread is doing real
work.** Without it every bot wakes at the same instant, nobody has been
announced yet, and all four announce at once -- which is what happens if you
remove it. With it, whoever wakes first names the others and the question
answers itself for everybody else. Derived from the bot's name rather than drawn
randomly, so a room stays reproducible.

In a practice room, where the room controls the timing, the whole thing is
deterministic:

```
t+0.0   Tutor[bot] joins        (if a tutor was asked for)
t+0.5   Mirn[kit-bot] joins          (sees no other bot: it will announce)
t+1.0   Delvo[bass-bot] joins         (sees Mirn: not its job)
t+1.5   Pundo[keys-bot] joins
t+2.0   Quado[lead-bot] joins

t+2.0   Tutor[bot]: hello -- i am the tutor. the band is coming in now.
t+5.5   Mirn[kit-bot]: The Understudies -- Mirn (kit), Delvo (bass),
                         Pundo (keys), Quado (lead).
t+5.5   Mirn[kit-bot]: say a name to talk to one of us. say "leave" and we all
                         go home.
```

The tutor speaks first and briefly, because the first line a new player sees
should be addressed to them rather than being a roster. Then the roster, once
every bot is in and the join notices have finished scrolling.

The band's NAME is used only when every bot in the list is one the announcer was
spawned alongside. Two strangers' bots in one room are a list, not a band, and
calling them one would be a small lie in the first line anybody reads.

**A bot that arrives later announces the band as it now stands**, which is the
same rule rather than an exception to it -- it was not in the roster, so it
posts one. Two implementations were tried and discarded before this: "did any
bot speak during my wait", which cannot tell a late arrival from a bot that lost
a race and produced four introductions and no roster; and a name tiebreak, which
produces one clean roster at startup and then leaves every later arrival silent
forever. Asking about oneself is the version that needs no exceptions.

**A human arriving later has missed it**, which is the one real gap. In a
practice room -- your own room, quiet by definition -- the roster is repeated
once for them, rate-limited to at most once every few minutes. In any other
room it is not, because unprompted speech outside practice is already the
narrower rule (§9), and a band that greets every arrival is a band that gets
kicked.

---

## 7. The tutor is a fifth bot

**Decided: teaching lives on its own bot, and the four players never do it.**
They are playing the changes; that is their whole job, and a drummer who
interrupts to explain the interval model is not a drummer.

So there is a fifth member of the room with no instrument, no channel and no
audio at all -- it joins, teaches, and leaves. Three properties follow, and each
is worth more than it costs:

- **It can be absent.** A room started by somebody who has done this before
  simply has four bots. Nothing needs to be silenced.
- **It finishes.** When the thread is done the tutor parts, of its own accord,
  and the room is left as a band. A tutorial that leaves when you have got it is
  a rare and good thing.
- **It is not a player, so it may speak more.** The budget that keeps the
  instrument bots quiet is about not drowning a jam; the tutor's whole purpose
  is speech, and it is finite by construction.

The thread: fires **once each, in order**, gated on something you have actually
done rather than on a timer.

1. On joining: what the room is, and that `part` sends any bot home.
2. On the first interval you play: what just happened, and why nobody has heard
   it yet.
3. On the second: why the band is a bar behind you, and that this is the form
   rather than a fault.
4. When you first set a key: that the band followed it, and that chords work the
   same way.
5. When you first shake: that the parts changed but the chart did not.
6. Then: "that is the whole of it -- i'll get out of the way. the band will keep
   playing." And it parts.

Six lines, and gone. A wizard with twenty tips is one nobody reads.

### Step 2, and the only listening in this design

Step 2 is the one that cannot be faked, so the tutor checks. Not "is that any
good" -- it has no business having an opinion -- but the far narrower question:
**does this look like an instrument somebody could hear?**

Every signal it needs is already in `src/AudioMeasure.h`, built for tuning the
band, plus a duty cycle and a transient count:

| Reading | Reads as | What it says |
|---|---|---|
| peak below about -60 dBFS | nothing arrived | "i am not seeing anything from you yet -- is the right input armed?" |
| rms below about -45 dBFS | there, but faint | "that went out, though it is quiet -- others may struggle to hear it" |
| very high crest, tiny duty cycle | clicks, not a part | "i am getting clicks rather than playing -- that is usually a buffer size" |
| peak at or above full scale, high duty | too hot | "that is clipping, and it will distort for everyone else" |
| pitched (a confident fundamental) **or** rhythmic (transients on a grid) **or** sustained with a plausible duty | somebody playing | the real line: what just happened, and why nobody has heard it yet |

Guitar, bass, keys, drums and a synth pad all land in the last row by different
routes, which is the point of testing three things and accepting any of them.

Three rules keep this from becoming a nag, and they matter more than the
thresholds:

- **It gates which encouraging line is said, never a criticism.** Every row
  above is diagnostic and actionable. None of them is an opinion about music.
- **Uncertainty says the neutral line.** A sparse part -- one note per interval,
  a held drone, someone warming up quietly -- must never be told it is not
  playing. When the reading is not clear, the tutor assumes you are playing and
  moves on.
- **Each of the first four rows fires at most once, ever**, and only after
  several consecutive intervals agree. A single quiet interval is a person
  thinking.

This is deliberately not musical analysis. It does not know what you played, and
after step 2 it never listens for anything else.

---

## 8. Personality without cuteness

Each bot's expertise is **what it actually does**, which is free and never
strained:

- the drummer talks about the groove -- pulses, accents, where the fill is;
- the bass player talks about the changes and the root it is landing on;
- the keys player talks about voicings and inversions;
- the lead talks about the key, the scale and what it is avoiding.

Ask the bass player about voicings and it says so and points at the keys player.
That is not a personality trait, it is a division of labour, and it produces the
same effect for none of the risk.

I would deliberately **not** give them moods, opinions about your playing,
jokes, or emoji. Every one of those is a thing that is funny twice.

### They speak in the first person

A bot says "i am on the keys", never "Ravo is on the keys". Every chat line
already carries its sender, so naming itself puts the name twice on one line --
`Ravo[keys-bot] Ravo is on the keys` -- and makes it read like a bot narrating
somebody else. It costs nothing and it is the difference between a player and a
status readout.

The exceptions are exact, and both are about the ROOM rather than the speaker:

- **Inside quotes**, where the name is text to type and typing it needs the
  name: `say "Ravo leave" and i go`.
- **The arrival roster**, which is a list of who is here. Naming everyone is
  the point of it.

Anything the room owns is "we" -- the key, the tempo, the chart. Anything the
bot owns is "i". A bot that said "my key" would be claiming an authority the
whole of section 5 exists to deny it.

`BotChatTests` asserts this over every reply the module can produce rather than
line by line, because the next reply somebody writes will make the same
mistake.

### Names, and a position reversed

An earlier draft of this section also refused them **names beyond their
instrument**, on the same grounds. That was wrong, and it is worth saying why
rather than quietly changing it, because the objection was sound and the
conclusion still did not follow.

The objection was to personality. A name given for charm is charm, and charm is
the thing that is funny twice. But a name is not only charm -- it is an
ADDRESS, and the addressing model in section 5 turns out to need one that is
rare.

Consider "the bass is too loud", in a room where the bass player is called
`Delvo[bass-bot]`. The token `bass` is present, section 5 scores a name appearing
anywhere in a sentence as strong, and the bass bot answers "roots, on the
changes" into a conversation about mixing. That is precisely the failure this
document exists to prevent, and it is caused by the name being an ordinary
word. The same collision makes the near-miss row in that table unusable:
edit distance one from `delvo` is safe, and edit distance one from `bass`
covers `base`, `bas` and `bass` itself.

So the two sections were already in conflict before anybody proposed a change.
Section 5 needs names rare enough to match anywhere in a sentence; section 8
forbade exactly that. Rare names are what make the natural forms work --
`what are the changes delvo`, `hey delvo whats your part` -- and without them
addressing collapses back to a rigid `name:` prefix, which is command syntax
wearing a conversation's clothes.

What a name has to be, then, and none of these is about character:

- **not an ordinary English word**, so it can be matched anywhere safely;
- **one token, no spaces**, so `/msg` reaches it in every client (§5);
- **pronounceable**, because a screen reader will read it aloud and
  `bot_3` is not a thing anybody says -- but note that this needs A
  pronunciation, not an agreed one. An earlier draft asked for "one obvious
  pronunciation" and cut `Ravo` for having two, which conflated saying a name
  with typing one. Addressing a bot is typing;
- **paired with the instrument somewhere**, so the room stays legible.

`Delvo[bass-bot]` satisfies all four: `delvo` is the handle, `bass` says what
it plays, `bot` is the marker bots recognise each other by, and there is no
space anywhere. The alternative of a bare `Delvo` with a channel named `bass`
is cleaner to say and worse to read in a client that does not show channels.

**The cost, stated plainly.** A human name raises expectations of human
conversation, and rule 3 then has to disappoint them. That is real, and it is
the strongest argument for the position being abandoned here. What keeps it
tolerable is that everything else about the bot is machine-shaped: the username
is visibly a label rather than a person, the first thing it ever says is a
terse fact about its part, and it never claims to be anything else. Delvo has
a bass line. Delvo does not have a day.

### Should the band have a name?

Probably, and for a narrower reason than it first appears.

It is **not** needed as an address. `band`, `everyone` and `all` are the words
people actually type, they are already the deliberate exception in section 5,
and a band name would only be a fourth synonym for them -- while being a
two-word phrase in most naturally chosen cases, which is exactly what the
matcher does not want.

Where it earns itself is the **join announcement**, which is the one line the
band gets to introduce itself with (§5) and the only real answer to
discoverability:

> `The Understudies: Delvo (bass), Mirn (kit), Pundo (keys), Quado (lead).`
> `Say a name to talk to one.`

That reads as a band arriving. A bare list of four usernames reads as four
processes starting. If the name is one word it can be an address as well, at no
extra cost; if it is two, it stays a label and nothing is lost.

The worry that a real band might be in the room does not survive contact: human
players do not introduce themselves collectively in jam chat, and if they did,
they would not answer to `band,` as a command prefix.

---

## 9. Where they stay quiet, and what they never say

**Real servers.** A bot can be pointed at any server. Unprompted speech should be
**off** outside the practice room -- reduced to the arrival line, because that
line is how strangers learn to evict it. Everything else waits to be asked. The
eviction rules exist because a bot nobody can get rid of is the nightmare; a bot
nobody can shut up is the same nightmare at lower volume.

**Screen readers.** Chat is announced, and `PRINCIPLES §11` refuses anything
announced on a timer. Bot chatter is the exact shape of the thing that rule
prohibits, and a budget is what keeps it on the right side. The `quiet` command
must therefore be genuinely global, not per-bot -- one word to the room and all
four stop.

**Never:**

- comment on how you are playing, at all -- they cannot hear you, and a
  compliment from something that cannot hear is worse than silence;
- pretend to hear, know, or feel anything they do not;
- answer general conversation, greet you twice, or fill a pause;
- ask a question they cannot handle the answer to;
- speak in the room when a private reply would do.

**And it is not a social layer.** `NON-GOALS.md` fence #9 refuses accounts,
profiles, presence and session history. This proposal stays behind that fence
because everything it adds is about *the music in this room now*, generated
locally, stored nowhere, and gone when the room closes. If a feature here ever
starts wanting to remember you between sessions, it has crossed the fence and
should be refused.

---

## 10. Worked transcripts

The most reviewable part. If these read as annoying, the design is wrong.

**Joining a practice room**

```
*** Mirn[kit-bot] joined
*** Delvo[bass-bot] joined
*** Pundo[keys-bot] joined
*** Quado[lead-bot] joined
*** Tutor[bot] joined
Tutor[bot]: practice room. four players, all bots. say "part" to any of them
             and it goes home.
you: /key Dm
~~ [key: D minor]
Delvo[bass-bot]: got it, D minor.
```

Five bots, two lines in the first minute, and only the bass acknowledged the
key. The other three heard it and changed what they were playing without
mentioning it, which is what a band does.

**Twenty minutes later, playing**

```
(nothing)
```

That is the design working.

**Indirect phrasing, which is the point of §5**

```
you: hey kit, whats ur groove?
Mirn[kit-bot]: five over eight, accents on 1 and 4. fill every fourth interval.
you: could you tell me what key we're supposed to be in
Delvo[bass-bot]: D minor, as announced in chat.
you: and the chords?
Delvo[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.
you: bass, giv me somthing else
Delvo[bass-bot]: shook. four onsets now, off the kick rather than on it.
```

Not one of those is a command word. Vocative and filler stripped, `ur` expanded,
`groove` and `whats` mapped, `giv`/`somthing` repaired, `and the chords?`
resolved from the previous turn.

**When it is not sure, and when it is lost**

```
you: tell me about your kick
Mirn[kit-bot]: the part or the sound? "what are you playing", or "what do you sound like".
you: the sound
Mirn[kit-bot]: deep kick, soft beater. tuned low, and the beater is soft enough to
           lose the click.
you: is the snare a bit much on the turnaround?
Mirn[kit-bot]: i caught "snare" but not the question. i can tell you my part, my
           sound, the key, the chords or the tempo -- and i cannot hear you, so
           i am no judge of what is too much.
you: thanks!
(nothing)
```

Three things there. The clarification names both candidates rather than
shrugging. The failure reports what it *did* recognise and volunteers the one
limitation that makes the question unanswerable in principle. And "thanks!" gets
**silence, not a fallback** -- the fallback is for something that looks like a
request, and courtesy is not a request. A bot that answers "thanks" with a menu
is the wall this design is trying not to be.

**The tutor finishing**

```
Tutor[bot]: that's the whole of it -- i'll get out of the way. the band will
             keep playing.
*** Tutor[bot] left
```

**In a real room, uninvited**

```
*** Mirn[kit-bot] joined
Mirn[kit-bot]: kit here. "part" sends me home.
(silence, whatever happens, unless someone addresses it)
```

---

## 11. Testing a talking bot

The reason to build it this way is that all of it is testable, and none of it
needs a human to judge.

- **Determinism**: a seed and a script of events produce a byte-identical
  transcript. That is the whole test harness, and it is the same shape as
  `test/PracticeRoomTests.cpp` already uses.
- **The budget is an assertion**: drive a hundred events at a bot and assert it
  spoke at most N times. This is the test that keeps it from becoming annoying,
  and it is the one I would write first.
- **Silence is an assertion**: a quiet room produces an empty transcript. Assert
  it, or the default will rot.
- **`quiet` is an assertion**: after `quiet`, no cue of `notice` class fires,
  ever, for any bot.
- **Understanding is a corpus and a number**, and the corpus exists:
  **`test/fixtures/bot-phrases.txt`**, 617 lines written the way people type in
  chat -- lowercase, unpunctuated, abbreviated, misspelled, padded with
  politeness, often not a question at all. Twenty-six of them are the same
  phrasings with one mechanical slip of the finger, generated rather than
  chosen, so that robustness to typing is measured against typos nobody picked
  to suit the repair.

  | | |
  |---|---|
  | `DESCRIBE_PART` | 81 |
  | `DESCRIBE_SOUND` | 58 |
  | `REPORT_KEY` | 47 |
  | `REPORT_CHART` | 44 |
  | `REPORT_TEMPO` | 41 |
  | `RESHUFFLE` | 58 |
  | `SET_QUIET` | 39 |
  | `SET_LOUD` | 19 |
  | `EXPLAIN_SELF` | 38 |
  | `LEAVE` | 36 |
  | `CLARIFY` -- must ask, not guess | 17 |
  | `NONE` -- must not answer at all | 103 |

  The test asserts the resolution of every line and reports three miss rates,
  because the three failures do not cost the same. A **fallback** is honest: it
  names what was recognised. A **clarify** asks which of two and names both. A
  **wrong** answer is the only one that actively misleads, so it carries the
  tightest bound.

  **Every fourth line of each section is held out from tuning**, and that split
  is the only reason the headline number means anything. Built without it, the
  engine read 74.4% correct; tuned against the whole corpus it would have
  reported 99.7%, while the held-out quarter said 92.8% -- and the gap between
  those two is exactly the amount by which the corpus had been memorised rather
  than understood.

  ```
  tune    467 of 469 (99.6%)   fallback 0.2%  clarify 0.0%  wrong 0.2%
  holdout 147 of 148 (99.3%)   fallback 0.0%  clarify 0.0%  wrong 0.7%
  ```

  The holdout has been read once and its misses repaired, which spends it: only
  lines added from here on restore an independent measurement, so add new
  phrasings to the END of a section.

  The `NONE` section is the other half and is the one that keeps the bots
  civil: greetings, courtesy, humans talking to each other, someone asking after
  Dave, a cat on a keyboard. None of it may fire an intent and none of it may be
  answered. It is deliberately the largest section.

  `CLARIFY` is worth its own section because a design with three outcomes needs
  a corpus with three: "tell me about your kick" is genuinely ambiguous and the
  right behaviour is to ask which.

  A message that asks for two things -- "whats the key and can you shake it",
  "tell me the tempo then be quiet" -- is read clause by clause, and the corpus
  cannot express that because every line in it carries exactly one intent. Those
  cases are asserted directly in `test/BotLanguageTests.cpp` instead, and the
  corpus guards the boundary from the other side: splitting must be a no-op on
  all 581 of its lines, so the two readings can only ever differ where a message
  really does ask twice.

  It is plain text so extending it needs no C++. When a real phrasing misses,
  add it, watch the test go red, then widen the lexicon -- and if widening would
  take more than a word or two, that is the signal the design was over-reaching
  rather than the corpus being short.
- **Each stage is testable alone**: the Porter stemmer against its published
  vectors, the edit distance against known pairs, the normaliser against
  contraction and vocative cases, the shape flags against negation.
- **No bot answers room chat that is not addressed to it**, which is the current
  behaviour and must survive.

---

## 12. Shape and cost

Two JUCE-light modules, split where the seam naturally is: understanding what
was said has nothing to do with deciding whether to speak, and each is much
easier to test alone.

**`src/BotLanguage.{h,cpp}`** -- text in, intent out, and nothing else. No
knowledge of bots, rooms or music beyond the slot parsers it borrows.

```cpp
enum class Intent { None, DescribePart, DescribeSound, ReportKey, ReportChart,
                    ReportTempo, Reshuffle, SetQuiet, SetLoud, ExplainSelf,
                    Leave };

struct Reading {
  Intent intent = Intent::None;
  Intent alsoConsidered = Intent::None; // set when it wants to clarify
  double margin = 0.0;
  bool looksLikeRequest = false;        // courtesy gets silence, not a fallback
  std::vector<Concept> recognised;      // what to name when it gives up
  MusicalKey::Key key;                  // slots, when present
  Harmony::Chart chart;
};

Reading read(const juce::String &text, const Reading &previousTurn);
```

**`src/BotChat.{h,cpp}`** -- cues, guards, budgets and templates, deciding what
to say and whether to say it at all.

```cpp
struct Observation { /* what the bot knows, as plain data */ };
struct Utterance { bool isPrivate; juce::String to, text; };

std::vector<Utterance> respond(const Event &, const Observation &,
                               BudgetState &, std::uint32_t seed);
```

`PracticeBot` calls `respond` from `onChatMessage` and once per interval, and
sends back whatever it returns. Everything decidable is decided in functions
with no socket, no clock and no state beyond what they are handed, so the tests
are ordinary unit tests and the same modules serve a standalone bot runner.

The tutor is a `PracticeBot` with no voice and no channel -- `setRender` is
already optional, and "silence unless a render is set" is documented as
deliberate -- plus its own cue table and a `part()` at the end of the thread.

Rough size:

| | lines |
|---|---|
| `BotLanguage`: normaliser, stemmer, edit distance, shape flags, scorer | ~400 |
| the lexicon and the intent table (plain data) | ~200 |
| `BotChat`: cues, guards, budgets, templates | ~300 |
| the tutor's thread | ~80 |
| tests, including the two corpora | ~500 |

---

## 13. Open questions

1. ~~Does teaching live on the instrument bots or a fifth bot?~~ **Decided: a
   fifth bot, which leaves when it is finished.** See §7. The players play the
   changes.
2. ~~Is the presence subscription worth building first?~~ **Decided: no, and it
   is not what was needed anyway.** The four players do not listen. The tutor
   does, for the owner alone and for the one check in §7, and that needs real
   decoded audio rather than presence -- so the presence-only idea is dropped
   rather than deferred. Musical listening beyond that is §14.
3. ~~What are the bots actually called?~~ **Decided: `Mirn`, `Delvo`, `Pundo`
   and `Quado`, with the tutor called `Tutor`.** Thirty candidates searched and
   the least occupied kept; see §5 for the criteria, including the one that
   only emerged from reading them aloud -- a familiar rime matters more than
   syllable count.

   Still open underneath it: **the pool has no spares**, so a name colliding
   with a player already in the room falls straight to the instrument fallback
   rather than being skipped at join. Another handful wants vetting.

4. ~~Do bots take room chat, or private messages only?~~ **Decided: room chat
   with an explicit address is the primary path, and a private message is an
   equal alternative.** Private-message-only was implemented once and was wrong
   three ways: no client can reach a username with a space in it, so it did not
   work at all; a private exchange is invisible, so the feature could never be
   discovered by anyone watching; and the evidence actually gathered was against
   BARE KEYWORDS in room chat, not against room chat. See §5.

5. **How much should bots know about each other?** Today they share nothing and
   converge only by hearing the same chat. Letting the drummer say "the bass is
   on the offbeat too" needs shared state and I suspect it is not worth it.

   Narrowed by §5: they now know each other's NAMES, told to them by whatever
   spawned them, because the loop invariant needs an exact list rather than a
   marker that can be spoofed. That is the smallest possible amount of shared
   state -- a list of strings fixed at startup, never updated, never agreed
   about -- and it is worth noticing that it is not nothing, since the previous
   answer was.
6. **Should `quiet` persist across a rejoin?** It cannot, since a bot that parts
   is gone forever, but the room could remember it.
7. **Anything in the room, or practice only?** I have assumed unprompted speech
   is practice-only and replies work anywhere. The alternative -- fully silent
   outside practice, even when asked -- is more conservative and I could be
   argued into it.
8. ~~Is the flat fallback too cold?~~ **Decided: yes, and it is replaced.** §5
   is now three outcomes rather than two -- answer, clarify, or report what was
   recognised -- with courtesy getting silence and the fallback rate treated as
   a defect to measure and drive down.

   Still open underneath it: **how far the lexicon should reach before it is
   over-engineered.** The corpus exists now, so this is answerable by
   measurement rather than by argument: build the pipeline, run it, and let the
   fallback rate say when to stop widening.

---

## 14. Beyond chat: the responsive partner

Not proposed, not scheduled, and written down because the shape of it is
peculiar to this program and easy to lose.

**A bot can be more responsive than a human, and the interval is why.**

Follow one phrase through. You play during interval N. Your audio is complete at
the boundary into N+1, and everyone plays it back through N+1. A human listening
hears it *unfold* across N+1 -- they learn how your phrase ended only at the end
of N+1 -- while simultaneously playing their own material, which others will
hear in N+2. So a human's N+1 performance can answer only the part of your
phrase they have heard so far. Your ending reaches them too late to answer
before N+2.

A bot renders a whole interval in one go, before that interval is transmitted.
At the start of N+1 it holds your **complete** interval N, ending and all, and
what it renders is heard in N+2 -- the same slot as the human's reply. It is
answering the whole phrase in the interval where the human is still hearing it.

That is not a trick or a latency cheat. It is a consequence of two facts already
true here: audio arrives a whole interval at a time, and a generative bot
composes a whole interval at a time. It means a bot could do things a human
player in the same room cannot -- answer your ending, match your phrase length,
land its own cadence against yours -- while staying exactly inside the form and
the wire protocol.

**The architecture that keeps it honest** is to leave the generator in charge
and let analysis *bias* it:

- analysis of the received interval produces a handful of plain numbers --
  density, register, how active, how syncopated, where the energy sits, a pitch
  histogram;
- those bias existing decisions rather than replacing them: the Euclidean pulse
  count, the register the bass sits in, whether to rest through a bar, dynamics,
  how busy the lead is;
- with no analysis available, every bias is zero and the band plays exactly as
  it does today.

That last property is what makes it safe to build incrementally, and it is the
same shape as the character system: a small vector of influences over a
generator that already works.

**What to be careful of, when it comes:**

- *Mimicry reads as mockery.* A bot that plays back your rhythm is not
  responsive, it is a parrot, and it is unpleasant within about four bars. The
  interesting responses are complementary -- it thins out when you get busy,
  drops to the root when you go outside.
- *Feedback loops.* Bots are deaf to each other, and that should stay true, or
  four responsive bots will converge on each other and leave you out of it.
- *Key detection from audio is a real project* -- `Harmony::inferKey` infers from
  a chart, which is a very different problem from inferring from a signal.
  Rhythm and density are much cheaper and would buy most of the effect.
- *The tutor's check in §7 is the first stone of this path*, and worth building
  well for that reason: "is somebody playing, and does it have a shape" is the
  simplest question in this family, and its answer is already useful.

   What is still open underneath it: **how far the lexicon should reach before
   it is over-engineered.** 150 words and nine intents is my estimate, and the
   corpus is what would tell us. My instinct is that the first fifty words buy
   most of it and the last fifty buy very little, so the honest plan is to build
   the pipeline, write two hundred phrasings the way a person would actually
   type them, and let the fallback rate say when to stop.
7. **Does the tutor need to know you are there?** Step 2 of its thread -- "what
   just happened when you played" -- is weak without ears. It could be reworded
   to fire on a timer instead, at the cost of telling you something that might
   not have happened. This is the only place in the design where the deafness
   actually hurts.

---

## 15. Being present without playing

A jam is not one continuous take. You play a song, you stop, you argue about the
next key, somebody suggests a tempo, and you start again. The band has no state
for any of that: it plays from the moment it connects until it is evicted, and
the only way to make it stop is to make it leave.

That is the gap this section closes. It also fixes two lifecycle bugs that turn
out to be the same bug.

### Three states, and one boundary

```
Silent --start--> Playing --stop--> Wrapping --[1 interval]--> Resolving
   ^                     ^              |                          |
   |                     \--start-------/           [1 interval]   |
   \--------------------------------------------------------------/
```

`Wrapping` and `Resolving` advance on their own, exactly one interval each;
every other arrow is somebody asking.

`start` during `Wrapping` **cancels the ending** and goes back to playing, which
is a real thing to want -- "no, keep going" is said in rehearsals constantly.
There is deliberately no such escape from `Resolving`: by then the wrap-up has
been heard and the final chord is the only musical way out.

**The state is sampled once per interval, at the top of the render, and held for
that whole interval.** Reading it again part-way through would tear an interval
across two states, and interval delivery is all-or-nothing -- a half-ended
interval is not a thing the protocol can carry.

`PracticeBot::playing` already exists for this and is currently dead weight: set
once in `playAs`, never cleared, and passed to `BotChat` as `Self::playing`
where nothing reads it. This gives it its meaning.

### Stopping cannot be immediate, and the reply must say so

The conductor renders interval N at the top of N, and Ninjam delivers it a whole
interval late, so you hear it during N+1. An ending therefore lands **one to two
intervals after you ask** -- four to eight seconds at 120 bpm and 8 bpi.

This is not a defect to hide behind a hopeful reply. It is the same delay every
player in the room is subject to (`PRINCIPLES §9`), and a bandleader says the
same thing anyway: *"ending after this one."* A reply that implied it stops now
would be wrong twice a minute and would teach players to distrust the band.

### What an ending is

**An ending is two intervals: wrap it up, then land.**

```
      you type "stop"
            |
   [ in flight ]  [ wrapping up ]  [ resolve ]  [ silent ...
    unchanged      full interval    downbeat
                   lead lays out    chord,
                   kit fills        ring,
                                    silence
```

An earlier draft made it one interval that opened on the resolution. Two things
were wrong with that, and they are the same thing seen twice.

A resolution lands on a **downbeat** -- so the final chord does belong on the
first beat of an interval, and that part stands. But a chord arriving on a
downbeat with nothing leading into it is not an ending, it is a dropout with a
note on the front. What makes an ending sound intended is the bar *before* it.

That draft also argued the fill was impossible: a drummer fills into an ending
because they know it is coming, and a bot told to stop part-way through an
interval does not, because the interval that would carry the fill is already
encoded and on the wire. True -- and the answer is not to give up the fill, it
is to give it an interval of its own. **The wrap-up interval is that interval.**

So:

- **The wrap-up interval** is a complete interval of music, played from the same
  chart, with the arrangement saying what is about to happen. It is a **taper
  rather than a switch**: the first half plays, and the second half winds down.
  The lead lays out at the halfway point, the texture thins behind it, and the
  kit fills through the last bar. Nobody drops out all at once, because that is
  not what winding down sounds like -- and the halfway point is a clean boundary
  to test against, since `layoutChart` already counts the interval in steps.
- **The resolving interval** opens on the chord the loop resolves to, lets it
  ring, and is quiet for the remainder.

**It costs nothing extra.** The band renders an interval every slot regardless,
so a wrap-up and a resolve cost exactly two normal intervals of CPU and
bandwidth. The extra interval is *full of music*, not empty; the near-empty one
is the resolve, and it existed in the one-interval design too. What is actually
spent here is time, not resource.

**The wrap-up invents no harmony.** No turnaround, no borrowed ii-V, nothing the
room did not write: the chart belongs to the room (section 5), and a bot adding
a cadence of its own is a bot deciding something nobody agreed. The signal is
arrangement -- laying out, filling, thinning -- which every musician reads and
which needs no new chords. The kit already fills every fourth interval, so the
machinery exists.

**The delay is about three intervals from typing to silence**: one because the
in-flight interval cannot be recalled, one to wrap up, one to resolve. Twelve
seconds at 120 bpm and 8 bpi. That is not a cost to apologise for -- a band told
to wrap it up and stopping instantly would be the strange behaviour. It scales
sensibly too: the fill lives in the last bar whatever the interval length, so a
long interval simply means one more interval of playing before the end.

### Which chord the resolve lands on

The one part of this that is theory rather than taste, and the one that would be
silently wrong if it were guessed. Three plausible rules disagree constantly.
Take `| Am | F | C | G |`:

| Rule | In C major | In A minor |
|---|---|---|
| Last chord of the chart | G -- the V, unresolved | G -- wrong |
| First chord of the chart | Am -- the vi | Am -- right |
| Tonic of the key | C -- right | Am -- right |

The chart's last chord is the tempting one and it is wrong: a loop often ends on
the V *precisely so that it loops*, and landing there is how you get an ending
that sounds like a mistake.

So it is the tonic -- but not blindly the tonic triad, because a blues has a
dominant seventh on the I and ending a blues on a plain `C` triad is as wrong as
ending it unresolved. The rule:

> **The room's own tonic chord if the chart contains one, otherwise the mode's
> tonic triad.**
>
> Scan `Harmony::flatten(chart)` for a chord rooted on the tonic. Found, use it
> whole -- `C7` stays `C7`, `Dm7` stays `Dm7`. Not found,
> `Harmony::diatonicTriad(key, 0)`.

One rule covers blues, modal vamps and plain diatonic, and it **minimises
invention**: it only introduces a chord when the chart never said what the tonic
sounds like in this tune. That keeps faith with the wrap-up inventing no harmony
at all -- the wrap-up plays the chart, and the resolve prefers the chart's own
answer whenever there is one.

**A consequence to accept rather than fix.** If nobody set the key, the band is
in the default C major, so a tune that is really in A minor gets a C ending and
sounds wrong. The temptation is to reach for `Harmony::inferKey`. Don't: a key
guess is offered and never acted on (section 5), and a bot quietly ending in a
key nobody announced is deciding something the room did not. The wrong ending is
a *symptom* of an unset key, and the fix is to set it. The key already drives
the bass roots and the lead lines, so the ending is not introducing the problem
-- it is making an existing one audible, which is useful.

### What is taste, and belongs in the lab

None of these can be argued from first principles, and all of them want ears:

- how long the final chord rings, and whether it is gated or left to decay;
- whether the kit's last hit is a crash alone or a crash with the kick;
- whether the resolve is voiced by `voiceLead` from where the wrap-up left off,
  or dropped to root position for finality;
- how far the texture drops across the wrap-up's second half;
- whether the lead is silent on the resolve or plays the tonic once.

How the two intervals actually *sound* is a tuning job for `AntiphonVoiceLab`,
measured the way every other voice was, and not something to settle in prose.

Two further variants are worth naming because they are different musical devices
rather than different tunings of this one, and both are future work:

- **A fade across the wrap-up**, or a velocity taper. This is what you reach for
  when there is no cadence to land on -- it ends a groove rather than a song,
  and it is the honest choice for a loop that resolves nowhere.
- **A ritardando.** Ruled out rather than deferred: the interval grid is the one
  thing every client in the room agrees on, and a bot that slowed down would be
  a bot leaving the grid (`PRINCIPLES` 9).

A bot told to stop on its own plays its own ending and drops out. That is
"laying out", and it is ordinary musical behaviour rather than a special case.

### Individually or as a band, for free

`BotAddress::Address::Collective` already sits beside `Named`, so `band, stop`
and `Ravo, stop` need no work in the addressing layer at all. `PartAll` is
simply the destructive member of a family that already exists.

### `stop` means stop playing

It currently means **leave** -- in `kPartCommands`, in
`BotAddress::isPartCommand`, and in the `[LEAVE]` corpus, which contains
`stop playing` and `you can stop now` in as many words. The scoring rule states
the assumption outright: *"to stop playing is to leave."*

That assumption is what this section overturns, and it is the `part` footgun
again in a worse place. To a musician `stop` is the least destructive thing you
can say, and it was wired to the most destructive thing a bot can do.

So: `stop`, `halt`, `enough`, `that's enough` and `we're done` all mean **stop
playing**. Leaving requires a word that can only mean leaving -- `leave`,
`exit`, `go away`. The reversible action gets the natural phrase and the
irreversible one stays deliberate, which is the rule the roster line has
followed since it was written.

### They arrive silent

The band connects before you do, so a band that plays on connect plays to an
empty room -- encoding and transmitting a full interval every few seconds to
nobody, for as long as it takes you to arrive.

They arrive, they wait, and the roster line -- which already re-arms so that it
lands when the first human joins rather than into the empty room -- says how to
start them. Arrival stops being a special case and becomes the first turn of the
same stop/start loop you use between songs. It also disposes of the
wait-forever problem completely: a band nobody ever joins now costs nothing, so
it needs no arrival timeout.

The cost is real and has to be carried by that one line: a room where nothing
happens looks broken. The roster earns its place by being the thing that tells
you it isn't.

### Any human, every command

**There is one tier.** Anybody in the room can start the band, stop it, shake
it, hush it or send it home. There is no owner-only class of command.

The argument is short: **eviction is already open to everyone**, deliberately --
"a bot in somebody else's jam should be removable by the people it is
bothering, not only by whoever brought it". Gating something strictly *less*
destructive than eviction behind ownership would be incoherent. A room of
musicians is also simply what this is modelling: anyone in a band can call a
halt.

Bots still take no orders from bots. That is enforced already and stays.

The owner is not a permission at all -- it is **who the cleanup rule watches**,
and nothing else.

### Leaving, and the blip that should not be fatal

Today a `PART` naming the owner calls `part()` at once, which sets `active`
false; `onDisconnected` refuses to reconnect by design, and
`PracticeRoom::reapPartedBots` then deletes the objects. A thirty-second network
blip does not lose the band for thirty seconds. It destroys it, and the room
process runs on with no bots in it.

The rule turns on a question the code already asks, in `onRoomMembershipChange`,
to decide whether to re-arm the roster: **is anyone else still here?**

Today the practice room is solo, so the first branch below is unexercised there
and only begins to matter once the band can be brought onto a shared server. It
is written now anyway, for the same reason the eviction rule it inherits from
was written before there was anybody to evict: the moment it *is* reachable is
the worst possible moment to be deciding what it should do.

- **Others are still in the room -- keep playing, and start no timer.** The band
  plays for the *room*; the owner is only who summoned it. Stopping four voices
  because one person's router hiccuped is a disruption to everybody who did not
  drop. Nothing is leaking here, because anyone present can dismiss them.
- **The room is empty of humans -- go Silent, and start a three-minute timer.**
  Nobody is listening, so playing on is waste. Come back inside it and the band
  is still there. Let it expire and they leave for good: at three minutes it was
  either deliberate, or something bigger than a blip.

Returning inside the window does **not** restart them. You dropped mid-song, and
rejoining a groove already in progress -- whose beginning you could not hear --
is worse than a quiet band waiting for you to say go.

**Speak only when the state changed.** A return to a band that never stopped
needs no announcement at all; a return to a silent band gets one line saying
they are still here and how to start. Four bots saying "welcome back" is the
chorus this whole design exists to prevent.

### What this deliberately does not include

- **A count-in.** A drummer counting in is natural and would answer "when does
  it actually begin", but the interval grid already answers that and everything
  is phase-locked to it. The state machine leaves room for a `Counting` state
  between `Silent` and `Playing`; it does not need one yet.
- **Ownership transfer** when the owner leaves a populated room. It reads
  plausible and it builds a chain by which a band outlives everybody who wanted
  it, which is the "bot nobody can get rid of" failure in a new coat. Anyone
  present can already dismiss them, which covers the real need.
- **Per-voice stop scheduling** -- "drop the keys for this section". That is
  arrangement, and it belongs with staggered rests in `ROADMAP.md`, not here.
