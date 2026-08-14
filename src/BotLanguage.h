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
// phrasing works -- and that hitting the fallback is rare enough to be measured
// as a defect rather than accepted as a limit. The number is the fallback rate
// over `test/fixtures/bot-phrases.txt`, which is the specification for this
// file and is 519 lines of what people actually type.
//
// No machine learning and no data file. Seven cheap stages, each independently
// testable: normalise, stem, repair typos, map words to concepts, read four
// flags off the sentence shape, score, and require a margin before answering.
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
  Part,     // part, pattern, groove, figure, rhythm, line
  Tone,     // sound, tone, timbre, patch, voice
  Key,      // key, scale, tonic
  Chart,    // chords, changes, progression
  Tempo,    // tempo, bpm, speed, interval
  Change,   // shake, reroll, different, again
  Quiet,    // quiet, hush, shut up, stop talking
  Loud,     // speak, talk, unmute
  Identity, // who, what are you, help
  Leave,    // leave, go, part, evict
  Drum,     // kick, snare, hat -- a piece of the kit, which is ambiguous
  Speak,    // tell, say, describe, explain
  Hear,     // hear, listen, sounds like -- what we cannot do
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

  // The four flags the cheap grammar produces. Not a part-of-speech tagger --
  // that needs a lexicon or a model -- but these carry most of the same
  // information for a couple of dozen lines.
  bool question = false;
  bool imperative = false;
  bool negated = false;
  bool secondPerson = false;

  bool has(Concept c) const;
};

Reading read(const std::string &text);

// The stages, exposed because each is a rule in its own right and worth testing
// on its own terms.
std::vector<std::string> normalise(const std::string &text);
std::string stem(const std::string &word);

} // namespace BotLanguage
