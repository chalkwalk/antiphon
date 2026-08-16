#include "BotChat.h"
#include "BotLanguage.h"
#include "ChatFormat.h"

namespace BotChat {

namespace {

// What this bot is playing, in its own terms. One line per voice because the
// interesting fact is a different one for each: the kit has no patch to name,
// and the lead's instrument is the thing a player most often wants changed.
juce::String describeSound(const Self &self) {
  switch (self.voice) {
  case BotBand::Voice::Drums:
    return self.name + " is playing the kit.";
  case BotBand::Voice::Bass:
    return self.name + " is playing " +
           BotVoice::bassTechniqueName(BotBand::bassTechnique(self.settings)) +
           " bass.";
  case BotBand::Voice::Keys:
    return self.name + " is playing a " +
           BotVoice::padCharacterName(
               BotBand::keysPatch(self.settings).character) +
           " patch.";
  case BotBand::Voice::Lead:
    return self.name + " is playing " +
           BotVoice::leadInstrumentName(BotBand::leadInstrument(self.settings)) +
           ".";
  }
  return self.name + " is playing.";
}

// What this bot is playing MUSICALLY, which is a different question from what
// it sounds like -- the corpus separates "whats your part" from "whats your
// sound" and the recogniser scores them apart, so collapsing them here would
// throw that away at the last step.
//
// Factual rather than atmospheric. The rhythm voices can state their actual
// figure, because `BotBand::figureFor` is the same thing the renderer reads;
// the harmony voices state what they are following, because that is what their
// part IS.
juce::String describePart(const Self &self) {
  const juce::String key = self.settings.key.valid
                               ? MusicalKey::displayName(self.settings.key)
                               : juce::String("no key yet");

  switch (self.voice) {
  case BotBand::Voice::Drums: {
    const auto f = BotBand::figureFor(self.voice, self.settings);
    return self.name + " is on the kit -- " + juce::String(f.pulses) +
           " hits over " + juce::String(f.steps) + ".";
  }
  case BotBand::Voice::Bass: {
    const auto f = BotBand::figureFor(self.voice, self.settings);
    return self.name + " is on the bass, roots on the changes -- " +
           juce::String(f.pulses) + " over " + juce::String(f.steps) + ".";
  }
  case BotBand::Voice::Keys:
    return self.name + " is on the keys, holding the chart in " + key + ".";
  case BotBand::Voice::Lead:
    return self.name + " is on the lead, a line over " + key + ".";
  }
  return self.name + " is playing.";
}

// The key somebody asked for, or an invalid Key when they named none.
//
// `MusicalKey::parseName` accepts a BARE tonic -- "a" is A major -- so scanning
// a sentence for the first thing that parses reads a key out of an article.
// Two rules keep that from happening, and both come from the SET_KEY corpus,
// which contains the trap in both directions:
//
//   "put it in a minor"   -> A minor.        A key.
//   "give me a minor key" -> some minor key. Not a key.
//
// So a tonic on its own is never enough -- the mode must be said -- and a
// "<tonic> <mode>" pair immediately followed by "key" is a description of a
// category rather than a name. Anything else unreadable is reported as
// unreadable, because a key put up wrongly is worse than one not put up at all
// (BotAnswer::answerSetKey carries that reply).
MusicalKey::Key keyAskedFor(const juce::String &text) {
  const auto words = juce::StringArray::fromTokens(
      text.removeCharacters(",.?!").toLowerCase(), " \t", "");

  for (int i = 0; i + 1 < words.size(); ++i) {
    const auto key = MusicalKey::parseName(words[i] + " " + words[i + 1]);
    if (!key.valid)
      continue;

    // "a minor key", "some major key" -- the pair is qualifying the word
    // "key", not naming one.
    if (i + 2 < words.size() && words[i + 2] == "key")
      continue;

    return key;
  }

  return {};
}

// The tempo somebody asked for, as (bpm, bpi); zero means "not this one",
// which is what `BotAnswer::answerSetTempo` reads.
//
// The two votable ranges OVERLAP between 40 and 64 bpm/bpi, so a bare number
// cannot be assigned by size alone. The rules, in order:
//
//   an explicit unit always wins  -- "16 bpi", "bpm 132"
//   a bare number is a bpm        -- "vote for 140", which is what it means
//   unless that reading is impossible and the bpi one is not -- "vote for 16"
//
// The last is not a guess about intent. It is the only reading under which the
// request can be satisfied at all, and the alternative is answering "the tempo
// vote only goes from 40 to 400 bpm" to somebody who asked for 16 bpi.
void tempoAskedFor(const juce::String &text, int &bpm, int &bpi) {
  bpm = 0;
  bpi = 0;

  const auto words = juce::StringArray::fromTokens(
      text.removeCharacters(",.?!").toLowerCase(), " \t", "");

  for (int i = 0; i < words.size(); ++i) {
    const auto &w = words[i];
    if (!w.containsOnly("0123456789") || w.isEmpty())
      continue;

    const int value = w.getIntValue();
    const juce::String before = i > 0 ? words[i - 1] : juce::String();
    const juce::String after = i + 1 < words.size() ? words[i + 1] : juce::String();

    if (after == "bpi" || before == "bpi") {
      bpi = value;
      continue;
    }
    if (after == "bpm" || before == "bpm") {
      bpm = value;
      continue;
    }

    if (!ChatFormat::isVotableBpm(value) && ChatFormat::isVotableBpi(value))
      bpi = value;
    else
      bpm = value;
  }
}

} // namespace

Response respond(const Context &ctx, const BotAddress::Incoming &in,
                 BotAddress::Attention &attention) {
  Response out;
  out.privately = in.isPrivate;

  const auto who =
      BotAddress::classify(ctx.room, ctx.self.name.toStdString(), in, attention);
  if (who == BotAddress::Address::Ignore)
    return {};

  const juce::String body = juce::String(BotAddress::withoutAddress(
      ctx.room, ctx.self.name.toStdString(), in.text));

  const auto reading = BotLanguage::read(body.toStdString());

  switch (reading.intent) {
  case BotLanguage::Intent::DescribeSound:
    out.speak = true;
    out.text = describeSound(ctx.self);
    return out;

  case BotLanguage::Intent::DescribePart:
    out.speak = true;
    out.text = describePart(ctx.self);
    return out;

  case BotLanguage::Intent::ReportKey:
    // `describeKey` is a noun phrase carrying its own provenance, so the
    // sentence is built around it rather than instead of it. Dropping the
    // provenance here would report a key nobody chose as though the room had
    // agreed on it, which is the failure BotAnswer is shaped to prevent.
    out.speak = true;
    out.text = "we are in " + BotAnswer::describeKey(ctx.music) + ".";
    return out;

  case BotLanguage::Intent::ReportChart:
    // The lead-in is load-bearing. `describeChart` begins with a bar line when
    // somebody put the chart up, and `Harmony::readChart` takes a leading `|`
    // as the whole signal -- so sending the fragment alone would not report the
    // chart, it would announce one.
    out.speak = true;
    out.text = "the chart is " + BotAnswer::describeChart(ctx.music) + ".";
    return out;

  case BotLanguage::Intent::SetKey:
    // Recognised precisely so it can be declined. Answering "the key is D
    // minor" to somebody asking for G minor looks like an answer and ignores
    // what was asked, which is the worst miss available here.
    out.speak = true;
    out.text = BotAnswer::answerSetKey(ctx.music, keyAskedFor(body));
    return out;

  case BotLanguage::Intent::SetTempo: {
    // A bot is an ordinary client: it cannot set a tempo and must not start a
    // vote, because four bots backing one person is that person having four
    // votes. It can say which command does work.
    int wantBpm = 0, wantBpi = 0;
    tempoAskedFor(body, wantBpm, wantBpi);
    out.speak = true;
    out.text = BotAnswer::answerSetTempo(ctx.music, wantBpm, wantBpi);
    return out;
  }

  case BotLanguage::Intent::SetChart:
    // Never acts and never reads a chart out of the request: a chart has to
    // lead its line, so a request for one essentially never carries one.
    out.speak = true;
    out.text = BotAnswer::answerSetChart(ctx.music);
    return out;

  case BotLanguage::Intent::ReportTempo:
    // Both numbers, always. The bpi is what decides how long you wait to hear
    // yourself, it is the part newcomers are surprised by, and it cannot be
    // worked out from the bpm.
    out.speak = true;
    out.text = "we are at " + juce::String(ctx.music.bpm) + " bpm, " +
               juce::String(ctx.music.bpi) + " beats to the interval.";
    return out;

  default:
    break;
  }

  return {};
}

} // namespace BotChat
