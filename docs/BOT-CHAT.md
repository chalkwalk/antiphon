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
`hollis, ridley, can you turn it up` names two people and should get two
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
`hollis`, `Hollis,` and `@hollis` are one thing and `hollisters` is not. Where
in the message the name falls changes only how strongly it counts:

| Signal | Example | Strength |
|---|---|---|
| private message | (any) | certain |
| name first, with a separator | `hollis: what are the changes`, `hollis, ...`, `@hollis ...` | very strong |
| name last | `what are the changes hollis` | very strong |
| the name alone | `hollis` | very strong -- see below |
| name anywhere | `what is hollis playing` | strong |
| several names | `hollis, ridley, turn it up` | each is addressed |
| instrument noun in the name position | `bass, what are you playing` | strong |
| near-miss on a name | `holis:`, `hollos` | strong, if unambiguous |
| continuation, from the person who opened it | `and the chords?` | moderate |
| nothing at all | `what are you playing` | none -- **nobody answers** |

The last row is the important one. **First contact has to be explicit.** An
unaddressed question in a room with eight participants is not a question for a
bot, and answering it is presumptuous.

#### The name on its own, and the attention window

Saying just `hollis` is the most natural way there is to start talking to
somebody, and it should work:

> `you: hollis`
> `Hollis[bass-bot]: here -- roots on the changes, D minor.`
> `you: what are the changes`
> `Hollis[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.`

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

The mechanism underneath it is the ` [bot]` marker in the username, which is
how bots recognise each other and is spoofable -- so it decides who talks and
nothing more, exactly as recorded for how the echo bot identifies the human.
When it fails, two further limits bound the damage: a bot answers a given
speaker at most once every few seconds, and a hard cap on lines per minute. A
spoofed name then costs one exchange rather than an afternoon.

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
message as `/msg <user> <text>` and splits on the first space, so a bot called
`Wren[keys-bot]` cannot be sent one at all: `/msg Wren[keys-bot] guitar` addresses a
user called `Keys`, who does not exist, and fails silently. Antiphon's own
client does this (`PluginEditor.cpp`), and so, being the same one-line parse,
will everyone else's.

Two separate fixes, and both are worth doing because they fail differently.
The names lose their spaces, which fixes every client including the ones we do
not ship. And Antiphon resolves `/msg` and `/kick` against the room's user list
by longest match rather than against whitespace, which additionally reaches
humans whose names have spaces in them. Tab completion over the same list is
the obvious companion and is tracked in `ROADMAP.md`.

#### The deliberate exception

`everyone`, `all`, `band`. Then they all answer, in a fixed order, one short
line each, because that is what was asked for.

These are matched **in the address position only**, unlike a name. "band" is an
ordinary word in a room full of musicians -- "nice band", "the band's tight" --
and a bot that answers those is the poltergeist this section is about. A name
like `hollis` is rare enough to be matched anywhere in a sentence; `band` is
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
you: hollis
Hollis[bass-bot]: here -- roots on the changes, D minor.
you: and your sound?
Hollis[bass-bot]: fingered, fairly dark.
you: dave what pedal is that
(nobody -- that is for dave)
dave: what are you playing
(nobody -- dave has not addressed anyone, and Hollis's window is yours)
you: hollis, ridley, can you turn it up
Hollis[bass-bot]: up 2 dB.
Ridley[kit-bot]: up 2 dB.
you: band, what are you playing
Ridley[kit-bot]: five over eight, accents on 1 and 4.
Hollis[bass-bot]: roots, on the changes and the kick.
Wren[keys-bot]: the chart, held, one chord a bar.
Sabine[lead-bot]: eighths over D minor, resting on the weak beats.
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

Nine, and they are the whole surface:

| Intent | Answers with |
|---|---|
| `DESCRIBE_PART` | its figure and how it sits: "five over eight, accents on 1 and 4" |
| `DESCRIBE_SOUND` | its character: "deep kick, soft beater" |
| `REPORT_KEY` | the key it follows, and whether it was told or defaulted |
| `REPORT_CHART` | the chart, in letters and degrees |
| `REPORT_TEMPO` | tempo and interval length, and that the server owns them |
| `RESHUFFLE` | rerolls, and says what changed |
| `SET_QUIET` / `SET_LOUD` | stops or resumes unprompted speech |
| `EXPLAIN_SELF` | what it is and how to remove it |
| `LEAVE` | parts, as now |

Slots ride along where they make sense: a key, a chord chart, a tempo, an
instrument name.

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
  > `Ridley[kit-bot]: the part or the sound? "what are you playing" or "what do you sound like".`
- **Nothing** -- below the floor. Even here it should not be a shrug. Report the
  concepts it *did* recognise, because that turns a dead end into a hint:
  > `you: is the snare a bit much on the turnaround`
  > `Ridley[kit-bot]: i caught "snare" but not the question. i can tell you my part, my sound, the key, the chords or the tempo -- and i cannot hear you, so i am no judge of what is too much.`

The last clause of that is worth keeping: it is honest, it is the answer to a
whole class of questions people will reasonably ask, and it says the limitation
once rather than pretending.

### One turn of memory

Elliptical follow-ups are most of what makes conversation feel connected, and
they cost almost nothing: remember the last intent and slot per conversant, for
a few turns.

> `you: what key are we in`
> `Hollis[bass-bot]: D minor, as announced in chat.`
> `you: and the chords?`
> `Hollis[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.`

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

- **On arriving**: ONE line for the whole band, once, from one bot -- not one
  line each, which is four lines of chat before anybody has said anything.

  > `The Understudies: Hollis (bass), Ridley (kit), Wren (keys), Sabine (lead).`
  > `Say a name to talk to one, or "part" to send us home.`

  Which bot says it is decided by a rule they can all evaluate without talking
  to each other, exactly as for the key change below. This is the only line I
  would make unconditional, because it carries both the eviction instruction and
  the answer to discoverability (§5): without it, a room full of people who do
  not know the bots can be spoken to is a room where they never are.
- **When the key changes**: at most one bot acknowledges, not all four. Which one
  is decided by a rule they can all evaluate without talking to each other --
  lowest instrument first, say -- so there is no coordination protocol.
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
`Hollis[bass-bot]`. The token `bass` is present, section 5 scores a name appearing
anywhere in a sentence as strong, and the bass bot answers "roots, on the
changes" into a conversation about mixing. That is precisely the failure this
document exists to prevent, and it is caused by the name being an ordinary
word. The same collision makes the near-miss row in that table unusable:
edit distance one from `hollis` is safe, and edit distance one from `bass`
covers `base`, `bas` and `bass` itself.

So the two sections were already in conflict before anybody proposed a change.
Section 5 needs names rare enough to match anywhere in a sentence; section 8
forbade exactly that. Rare names are what make the natural forms work --
`what are the changes hollis`, `hey hollis whats your part` -- and without them
addressing collapses back to a rigid `name:` prefix, which is command syntax
wearing a conversation's clothes.

What a name has to be, then, and none of these is about character:

- **not an ordinary English word**, so it can be matched anywhere safely;
- **one token, no spaces**, so `/msg` reaches it in every client (§5);
- **pronounceable**, because a screen reader will read it aloud and
  `bot_3` is not a thing anybody says;
- **paired with the instrument somewhere**, so the room stays legible.

`Hollis[bass-bot]` satisfies all four: `hollis` is the handle, `bass` says what
it plays, `bot` is the marker bots recognise each other by, and there is no
space anywhere. The alternative of a bare `Hollis` with a channel named `bass`
is cleaner to say and worse to read in a client that does not show channels.

**The cost, stated plainly.** A human name raises expectations of human
conversation, and rule 3 then has to disappoint them. That is real, and it is
the strongest argument for the position being abandoned here. What keeps it
tolerable is that everything else about the bot is machine-shaped: the username
is visibly a label rather than a person, the first thing it ever says is a
terse fact about its part, and it never claims to be anything else. Hollis has
a bass line. Hollis does not have a day.

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

> `The Understudies: Hollis (bass), Ridley (kit), Wren (keys), Sabine (lead).`
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
*** Ridley[kit-bot] joined
*** Hollis[bass-bot] joined
*** Wren[keys-bot] joined
*** Sabine[lead-bot] joined
*** Marlow[tutor-bot] joined
Marlow[tutor-bot]: practice room. four players, all bots. say "part" to any of them
             and it goes home.
you: /key Dm
~~ [key: D minor]
Hollis[bass-bot]: got it, D minor.
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
Ridley[kit-bot]: five over eight, accents on 1 and 4. fill every fourth interval.
you: could you tell me what key we're supposed to be in
Hollis[bass-bot]: D minor, as announced in chat.
you: and the chords?
Hollis[bass-bot]: | Dm | Bb | F | C | -- i VI III VII.
you: bass, giv me somthing else
Hollis[bass-bot]: shook. four onsets now, off the kick rather than on it.
```

Not one of those is a command word. Vocative and filler stripped, `ur` expanded,
`groove` and `whats` mapped, `giv`/`somthing` repaired, `and the chords?`
resolved from the previous turn.

**When it is not sure, and when it is lost**

```
you: tell me about your kick
Ridley[kit-bot]: the part or the sound? "what are you playing", or "what do you sound like".
you: the sound
Ridley[kit-bot]: deep kick, soft beater. tuned low, and the beater is soft enough to
           lose the click.
you: is the snare a bit much on the turnaround?
Ridley[kit-bot]: i caught "snare" but not the question. i can tell you my part, my
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
Marlow[tutor-bot]: that's the whole of it -- i'll get out of the way. the band will
             keep playing.
*** Marlow[tutor-bot] left
```

**In a real room, uninvited**

```
*** Ridley[kit-bot] joined
Ridley[kit-bot]: kit here. "part" sends me home.
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
  **`test/fixtures/bot-phrases.txt`**, 519 lines written the way people type in
  chat -- lowercase, unpunctuated, abbreviated, misspelled, padded with
  politeness, often not a question at all.

  | | |
  |---|---|
  | `DESCRIBE_PART` | 73 |
  | `DESCRIBE_SOUND` | 50 |
  | `REPORT_KEY` | 42 |
  | `REPORT_CHART` | 42 |
  | `REPORT_TEMPO` | 37 |
  | `RESHUFFLE` | 45 |
  | `SET_QUIET` | 35 |
  | `SET_LOUD` | 18 |
  | `EXPLAIN_SELF` | 37 |
  | `LEAVE` | 33 |
  | `CLARIFY` -- must ask, not guess | 15 |
  | `NONE` -- must not answer at all | 92 |

  The test asserts the resolution of every line and reports the **fallback
  rate**, which is the number to quote and drive down:

  ```
  519 phrasings, 9 intents
  resolved 498  clarified 15  fell back 6   (fallback 1.2%)
  ```

  The `NONE` section is the other half and is the one that keeps the bots
  civil: greetings, courtesy, humans talking to each other, someone asking after
  Dave, a cat on a keyboard. None of it may fire an intent and none of it may be
  answered. It is deliberately the largest section.

  `CLARIFY` is worth its own section because a design with three outcomes needs
  a corpus with three: "tell me about your kick" is genuinely ambiguous and the
  right behaviour is to ask which.

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
3. ~~Do bots take room chat, or private messages only?~~ **Decided: room chat
   with an explicit address is the primary path, and a private message is an
   equal alternative.** Private-message-only was implemented once and was wrong
   three ways: no client can reach a username with a space in it, so it did not
   work at all; a private exchange is invisible, so the feature could never be
   discovered by anyone watching; and the evidence actually gathered was against
   BARE KEYWORDS in room chat, not against room chat. See §5.

4. **How much should bots know about each other?** Today they share nothing and
   converge only by hearing the same chat. Letting the drummer say "the bass is
   on the offbeat too" needs shared state and I suspect it is not worth it.
5. **Should `quiet` persist across a rejoin?** It cannot, since a bot that parts
   is gone forever, but the room could remember it.
6. **Anything in the room, or practice only?** I have assumed unprompted speech
   is practice-only and replies work anywhere. The alternative -- fully silent
   outside practice, even when asked -- is more conservative and I could be
   argued into it.
7. ~~Is the flat fallback too cold?~~ **Decided: yes, and it is replaced.** §5
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
