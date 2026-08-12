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

**That is a boundary this proposal keeps rather than pushes against.** The bots
interact in chat; they do not listen. Musical interaction -- a bot that responds
to what you played -- is a different and much larger piece of work, and it is
not proposed here. The deafness is worth stating precisely all the same, because
it is *why* several tempting ideas are absent from the lists below: a bot cannot
tell you that you dropped out, cannot compliment a phrase, and must never sound
as though it could.

If it is ever wanted, the cheap version is a **presence-only subscription** --
subscribe to one player and teach `NinjamClient` to notice interval *arrivals*
without decoding them, since the memory cost is the decoded buffers and not the
subscription. That would tell a bot "somebody is playing, this interval, yes or
no" for nearly nothing, with no audio analysis at all. The place it would earn
its keep is a tutorial bot confirming it can hear you before telling you that
everyone else can. Not now, and not for the rest of this.

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

## 5. Being addressed

Keyword sets, not parsing. A message is "addressed" if it is a private message,
or if it is room chat containing the bot's name.

| You ask | It answers with |
|---|---|
| what / playing / doing | its figure and how it sits: "four on the beat, accents on 1 and 5" |
| sound / tone / kit | its character name: "deep kick, soft beater" |
| key | the key it is following, and whether it was told or defaulted |
| chords / chart | the chart it is following, in letters and degrees |
| tempo / bpm / bpi | what it has, and that it takes them from the server |
| shake / new / again | rerolls, and says what changed |
| quiet / hush | stops all unprompted speech until told otherwise |
| louder / talk | resumes |
| help | one line: how to make it leave, and that it takes `quiet` |
| part / leave / exit / stop | leaves, as now |
| anything else | one honest line, always the same one |

That last row is the design's spine. The fallback is something like:

> `Kit [bot]: i only know about the music. try "what are you playing", "key",
> "chords", "shake", "quiet", or "part".`

It is not clever, it does not pretend, and it teaches the vocabulary at the exact
moment somebody is looking for it.

---

## 6. Speaking unprompted

The short list. Each is `notice`-class, guarded, and on a topic cooldown.

- **On arriving**: one line, once. "Kit [bot] here -- deep kick, soft beater.
  Say `part` to send me home." This is the only line I would make unconditional,
  because it is also the eviction instruction.
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

## 7. The teaching thread

The tutorial bot, as a special case rather than a separate creature: a `teach`
cue is one that fires **once ever per room**, in a fixed order, gated on
something the learner has actually done.

1. On the first interval you transmit: what just happened, and why nobody heard
   it yet.
2. On the second: why you now hear the band a bar behind you, and that this is
   the form rather than a fault.
3. When you first set a key: that the band followed it, and that chords work the
   same way.
4. When you first shake: that the parts changed but the chart did not.

Staged, unskippable-in-order, and finished after four lines. A bot that teaches
the interval model in four lines and then shuts up forever is worth having; one
with twenty tips is a wizard nobody reads.

Open question, flagged below: whether teaching belongs on the instrument bots at
all, or on a fifth non-playing bot that leaves when it is done.

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
jokes, emoji, or names beyond their instrument. Every one of those is a thing
that is funny twice.

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
*** Kit [bot] joined
*** Bass [bot] joined
*** Keys [bot] joined
*** Lead [bot] joined
Kit [bot]: kit here -- deep, soft beater. "part" sends me home, "quiet" shuts me up.
you: /key Dm
~~ [key: D minor]
Bass [bot]: got it, D minor.
you: what are you playing
Kit [bot]: five pulses over eight, accents on 1 and 4. fill every fourth interval.
Bass [bot]: roots, mostly. i land on every change and follow the kick otherwise.
```

Four bots, four lines in the first minute, and every one of them either
instructional or asked for. Note that only the bass acknowledged the key.

**Twenty minutes later, playing**

```
(nothing)
```

That is the design working.

**Getting it wrong**

```
you: | Am | F | C | G
Keys [bot]: i can read "| Am | F | C | G |" -- that one is missing its last bar.
you: thanks!
Keys [bot]: i only know about the music. try "what are you playing", "key",
            "chords", "shake", "quiet", or "part".
```

The second reply is deliberately flat. It is the honest answer, it is the same
answer every time, and after you have seen it once you know exactly what the bot
is. I would rather that than a bot that says "you're welcome!" and thereby
claims to be something it is not.

**In a real room, uninvited**

```
*** Kit [bot] joined
Kit [bot]: kit here. "part" sends me home.
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
- **The fallback is an assertion**: a corpus of unmatched lines -- greetings,
  questions, insults, empty strings, other bots' names -- all produce exactly
  the one fallback line and never anything else.
- **No bot answers room chat that is not addressed to it**, which is the current
  behaviour and must survive.

---

## 12. Shape and cost

A new JUCE-light module, `src/BotChat.{h,cpp}`, holding the cue table, the
budget, the keyword matching and the templates as pure functions:

```cpp
struct Observation { /* what the bot knows, as plain data */ };
struct Utterance { bool isPrivate; juce::String to, text; };

std::vector<Utterance> respond(const Event &, const Observation &,
                               BudgetState &, std::uint32_t seed);
```

`PracticeBot` calls it from `onChatMessage` and once per interval, and sends
whatever comes back. Everything decidable is decided in a function with no
socket, no clock and no state beyond what it is handed -- so the tests above are
ordinary unit tests, and the same module is what a future standalone bot runner
would use.

Rough size: 200 lines of cue table and templates, 100 of matching and budget,
250 of tests. The presence-only subscription in §3, if wanted, is a separate and
smaller change to `NinjamClient`.

---

## 13. Open questions

1. **Does teaching live on the instrument bots or on a fifth bot that leaves
   when it is finished?** A dedicated tutor is cleaner and can be absent from a
   room where it is not wanted; four bots that occasionally teach is fewer
   moving parts. I lean to the fifth bot, because "it leaves when done" is a
   good property and instrument bots should stay about their instruments.
2. ~~Is the presence subscription worth building first?~~ **Decided: no.** The
   bots interact in chat and do not listen. Musical interaction is future work,
   and the only presence question worth reopening later is a tutorial bot
   confirming it can hear you before it explains that everyone else can.
3. **How much should bots know about each other?** Today they share nothing and
   converge only by hearing the same chat. Letting the drummer say "the bass is
   on the offbeat too" needs shared state and I suspect it is not worth it.
4. **Should `quiet` persist across a rejoin?** It cannot, since a bot that parts
   is gone forever, but the room could remember it.
5. **Anything in the room, or practice only?** I have assumed unprompted speech
   is practice-only and replies work anywhere. The alternative -- fully silent
   outside practice, even when asked -- is more conservative and I could be
   argued into it.
6. **Is the flat fallback too cold?** It is the deliberate choice in §5 and the
   one most likely to be wrong. A warmer single line would still be honest; I
   just do not want two.
