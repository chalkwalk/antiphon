#pragma once

#include <string>
#include <vector>

// What a message MEANS, once BotAddress has decided it is for us.
//
// The harder half of `docs/BOT-CHAT.md` section 5, and the half that decides
// whether a bot feels like a machine you talk to or a vending machine you
// operate. Exact-match command words fail flatly the moment you phrase
// something the way a person actually would, and one flat failure teaches you
// to stop trying.
//
// The goal is not conversation. It is that WITHIN THIS NARROW DOMAIN, indirect
// phrasing works -- and that a miss is rare enough to be measured as a defect
// rather than accepted as a limit. The number is the miss rate over
// `test/fixtures/bot-phrases.txt`, which is the specification for this file and
// is 617 lines of what people actually type, a quarter of them held out from
// tuning so the rate means something.
//
// No machine learning and no model. What this is, in the terms of the
// literature, is a CASCADED FINITE-STATE RECOGNISER of the kind Abney
// described for partial parsing and FASTUS used for information extraction: a
// short stack of levels, each doing one local, deterministic thing to the
// output of the level below, and none of them ever needing a complete parse.
// That is why it is robust rather than merely small -- an unparseable sentence
// degrades to the level that did understand it instead of failing outright.
//
//   0. segment clauses            "whats the key and can you shake it" is two
//   1. tokenise, and fuse idioms  "going on" -> situation, "sound like" -> sound
//   2. expand contractions        so "ill" and "i will" are one sentence
//   3. drop the address, then the grammar, keeping each word's left neighbour
//   4. word class from context    "the changes" is a noun, "change it" a verb
//   5. words -> concepts, repairing what is left if it is not a real word
//   6. clause shape               question, request, imperative, self-report
//   7. score, with a margin below which it answers nothing
//
// Level 6 is where the politeness lives, and it earns its place: "can you
// change your part" is a question in form and an instruction in force, and
// before that level existed the politer half of the room was answered with a
// description of the thing it had just asked us to change.
//
// The levels are deliberately shallow. A real chunker would give noun-phrase
// boundaries rather than a one-word window, and was tried against phrasings
// with a modifier between the determiner and its head ("the recent changes",
// "your main groove"); the window handled them, so the chunker was not built.
//
// The one data file is `BotDictionary.h`: a generated list of ordinary English
// words, used to refuse to "repair" one. That single test was worth more than
// every refinement of the edit metric put together -- see the comment above
// `editDistance` in the .cpp, which records the refinements that were tried and
// measured to do nothing.
//
// JUCE-free. The musical SLOTS -- a key, a chart, a tempo -- are pulled out by
// the caller, which already has `MusicalKey` and `Harmony` and needs the
// original capitals to do it, since `Am` is a chord and `am` is a verb.

namespace BotLanguage {

// The whole surface. Nine things a bot can be asked.
enum class Intent {
  None,
  DescribePart,
  DescribeSound,
  ReportKey,
  ReportChart,
  ReportTempo,
  // Asked to CHANGE the key, the tempo or the chart. The bots have no authority
  // over any of them -- a tempo is a server vote, a key and a chart are room
  // conventions announced in chat -- but
  // recognising the request is what lets them say so. Answering "the key is
  // Am" to "can you play in G minor" is the worst kind of miss: it looks like
  // an answer and it ignores what was asked.
  SetKey,
  SetTempo,
  SetChart,
  Reshuffle,
  SetQuiet,
  SetLoud,
  ExplainSelf,
  Leave,
};

const char *intentName(Intent i);

// What the words meant, before the sentence was scored. Kept because the
// failure path needs it: reporting the concepts we DID recognise turns a dead
// end into a hint, which is most of the difference between an honest bot and a
// shrug.
enum class Concept {
  Part,       // part, pattern, groove, figure, rhythm, line
  Tone,       // sound, tone, timbre, patch, voice
  Key,        // key, scale, tonic
  Chart,      // chords, changes, progression
  Tempo,      // tempo, bpm, speed, interval
  Change,     // shake, reroll, different, again
  Quiet,      // quiet, hush, mute, silence
  Loud,       // unmute, resume, go ahead
  Identity,   // who, what are you, help, commands
  Leave,      // leave, go, evict, goodbye
  Drum,       // kick, snare, hat -- a piece of the kit, which is ambiguous
  Instrument, // bass, guitar, keys -- likewise: could be part or sound
  Speak,      // tell, say, describe, explain -- the REQUEST, not the topic
  Chat,       // chat, talk, commentary -- talking as an activity, our topic
  Cease,      // stop, enough, less -- ceasing WHAT is decided by the object
  Hear,       // hear, listen, sounds like -- what we cannot do
};

struct Reading {
  Intent intent = Intent::None;

  // Set when two intents were too close to separate. The bot should ask which
  // of the two rather than guess -- it knows exactly what it was torn between,
  // so naming them is nearly free and is the single biggest difference between
  // feeling alive and feeling like a wall.
  bool ambiguous = false;
  Intent alternative = Intent::None;

  // What was recognised, whatever the outcome.
  std::vector<Concept> concepts;

  // The sentence shape, read off the tokens by rule. Not a part-of-speech
  // tagger: those need a tagged corpus to train on, and this domain has no such
  // corpus and would not repay one. What it does instead is decide word class
  // where -- and only where -- the class changes the answer, from the function
  // words either side. "the changes" is a noun and asks for the chart; "change
  // it" is a verb and asks for a reroll, and a determiner is the whole
  // difference. That is one Brill-style contextual rule, hand-written, and it
  // buys most of what a tagger would.
  bool question = false;
  bool imperative = false;
  // "can you change your part" is a question in form and an instruction in
  // force. Without this the politer half of the room is answered with a
  // description of what it politely asked us to change.
  bool request = false;
  bool negated = false;
  bool secondPerson = false;
  bool possessive = false;  // "your part", "yours" -- a thing OF ours
  bool proposal = false;    // "let us", "shall we" -- not an instruction to us
  bool continuation = false; // a leading "and"/"so" -- carries the last topic

  // Content words that matched nothing, even after typo repair. The strongest
  // single signal that a message is not about us at all: "what daw are you on"
  // is grammatical, addressed and entirely outside what a bot can answer.
  int unknownWords = 0;

  bool has(Concept c) const;
};

// The strongest single reading of the whole message.
Reading read(const std::string &text);

// One reading per clause, for a message that asks for more than one thing:
// "whats the key and can you shake it", "tell me the tempo then be quiet".
//
// The missing cascade level. A finite-state cascade segments clauses before it
// recognises anything inside them, and skipping that step is why the engine
// answered the first request in a message and silently dropped the second --
// which is the rudest thing a bot can do that is not actually a wrong answer.
//
// Conservative by construction: it returns more than one reading ONLY when the
// clauses resolve to different, definite intents. A conjunction inside a single
// request ("shake the bass and the drums", "tell me about your kick and snare")
// yields one clause that reads and one that does not, and falls back to reading
// the message whole -- so `read` and `readAll` can never disagree about a
// single-clause message, and the corpus measures both.
std::vector<Reading> readAll(const std::string &text);

// The stages, exposed because each is a rule in its own right and worth testing
// on its own terms.
std::vector<std::string> normalise(const std::string &text);
std::string stem(const std::string &word);

} // namespace BotLanguage
