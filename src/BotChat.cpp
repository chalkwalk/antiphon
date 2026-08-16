#include "BotChat.h"
#include "BotLanguage.h"

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
