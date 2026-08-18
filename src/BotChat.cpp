#include "BotChat.h"
#include "BotLanguage.h"
#include "ChatFormat.h"

namespace BotChat {

namespace {

// What this bot is playing, in its own terms. One line per voice because the
// interesting fact is a different one for each: the kit has no patch to name,
// and the lead's instrument is the thing a player most often wants changed.
//
// FIRST PERSON, like everything else a bot says about itself. Every chat line
// already carries the sender's name, so "Ravo is playing the kit" arrives as
// "Ravo[keys-bot] Ravo is playing the kit" -- the name twice, and a bot that
// sounds like it is describing somebody else.
juce::String describeSound(const Self &self) {
  switch (self.voice) {
  case BotBand::Voice::Drums:
    return "i am playing the kit.";
  case BotBand::Voice::Bass:
    return juce::String("i am playing ") +
           BotVoice::bassTechniqueName(BotBand::bassTechnique(self.settings)) +
           " bass.";
  case BotBand::Voice::Keys:
    return juce::String("i am playing a ") +
           BotVoice::padCharacterName(
               BotBand::keysPatch(self.settings).character) +
           " patch.";
  case BotBand::Voice::Lead:
    return juce::String("i am playing ") +
           BotVoice::leadInstrumentName(BotBand::leadInstrument(self.settings)) +
           ".";
  }
  return "i am playing.";
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
    return "i am on the kit -- " + juce::String(f.pulses) + " hits over " +
           juce::String(f.steps) + ".";
  }
  case BotBand::Voice::Bass: {
    const auto f = BotBand::figureFor(self.voice, self.settings);
    return "i am on the bass, roots on the changes -- " +
           juce::String(f.pulses) + " over " + juce::String(f.steps) + ".";
  }
  case BotBand::Voice::Keys:
    return "i am on the keys, holding the chart in " + key + ".";
  case BotBand::Voice::Lead:
    return "i am on the lead, a line over " + key + ".";
  }
  return "i am playing.";
}

// First contact, and the answer to "what are you".
//
// An acknowledgement that teaches nothing is a promise the design cannot keep,
// so this doubles as a menu. It names the way OUT before anything else it can
// do, because somebody who did not want a bot in their room needs that more
// than they need to know what it plays.
//
// The one place the bot's own name belongs in what it says, and only inside the
// quotes: that is not the bot referring to itself, it is text to TYPE, and
// typing it needs the name.
juce::String explainSelf(const Self &self) {
  return juce::String("i am a bot playing the ") +
         juce::String(BotBand::voiceName(self.voice)).toLowerCase() +
         ". say \"" + self.name +
         " leave\" and i go. ask me about my part, my sound, the key, the "
         "chords or the tempo.";
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

// An intent as a player would say it. `BotLanguage::intentName` is the
// recogniser's own tag -- "DESCRIBE_PART" -- which is fine in a corpus file and
// is an internal identifier read out loud in a room.
const char *spokenIntent(BotLanguage::Intent i) {
  switch (i) {
  case BotLanguage::Intent::DescribePart: return "my part";
  case BotLanguage::Intent::DescribeSound: return "my sound";
  case BotLanguage::Intent::ReportKey: return "the key";
  case BotLanguage::Intent::ReportChart: return "the chords";
  case BotLanguage::Intent::ReportTempo: return "the tempo";
  case BotLanguage::Intent::SetKey: return "a key change";
  case BotLanguage::Intent::SetTempo: return "a tempo change";
  case BotLanguage::Intent::SetChart: return "different chords";
  case BotLanguage::Intent::ResetChart: return "the default chords";
  case BotLanguage::Intent::Reshuffle: return "something else played";
  case BotLanguage::Intent::StopPlaying: return "me to stop playing";
  case BotLanguage::Intent::StartPlaying: return "me to start playing";
  case BotLanguage::Intent::SetQuiet: return "me to be quiet";
  case BotLanguage::Intent::SetLoud: return "me talking again";
  case BotLanguage::Intent::ExplainSelf: return "to know what i am";
  case BotLanguage::Intent::Leave: return "me to leave";
  case BotLanguage::Intent::None: break;
  }
  return "something else";
}

// The whole decision, before the quiet rule is applied to it. Separate so the
// rule is applied in ONE place: a gate at each of a dozen returns is a gate
// somebody forgets when they add the thirteenth.
Response decide(const Context &ctx, const BotAddress::Incoming &in,
                BotAddress::Attention &attention) {
  Response out;
  out.privately = in.isPrivate;

  const auto who =
      BotAddress::classify(ctx.room, ctx.self.name.toStdString(), in, attention);
  if (who == BotAddress::Address::Ignore)
    return {};

  // Decided by the address rather than the sentence. Anyone may evict a bot --
  // a bot in somebody else's jam should be removable by the people it is
  // bothering, not only by whoever brought it.
  if (who == BotAddress::Address::PartAll ||
      who == BotAddress::Address::PartMe) {
    out.speak = true;
    out.act = Act::Part;
    out.text = "leaving. bye.";
    return out;
  }

  // The name alone. A greeting that teaches nothing would be a dead end, so it
  // is the same line as "what are you".
  if (who == BotAddress::Address::Opener) {
    out.speak = true;
    out.text = explainSelf(ctx.self);
    return out;
  }

  const juce::String body = juce::String(BotAddress::withoutAddress(
      ctx.room, ctx.self.name.toStdString(), in.text));

  // Naming an instrument, which is a setting rather than a question and so is
  // matched before the sentence is read. Only the soloist has one to change;
  // the rest say so rather than accept a value they will never read.
  const auto wanted = body.trim().toLowerCase();
  if (wanted == "epiano" || wanted == "piano" || wanted == "rhodes" ||
      wanted == "guitar" || wanted == "synth") {
    out.speak = true;
    if (ctx.self.voice != BotBand::Voice::Lead) {
      out.text = juce::String("i play the ") +
                 juce::String(BotBand::voiceName(ctx.self.voice)).toLowerCase() +
                 ". ask the lead.";
      return out;
    }

    auto pick = BotVoice::LeadInstrument::Synth;
    if (wanted == "epiano" || wanted == "piano" || wanted == "rhodes")
      pick = BotVoice::LeadInstrument::EPiano;
    else if (wanted == "guitar")
      pick = BotVoice::LeadInstrument::Guitar;

    out.act = Act::SetLeadInstrument;
    out.value = (int)pick;
    out.text = juce::String("now on ") + BotVoice::leadInstrumentName(pick) + ".";
    return out;
  }

  const auto reading = BotLanguage::read(body.toStdString());

  // Torn between two readings, and ASKING rather than picking. This has to
  // come before the switch: `intent` still holds the winner when `ambiguous`
  // is set, so acting on it answers one of two questions the recogniser has
  // just said it cannot separate -- confidently, and half the time wrongly.
  //
  // Naming the two is what makes the question useful rather than a shrug, and
  // it is nearly free: the recogniser knows exactly what they were.
  if (reading.ambiguous && reading.alternative != BotLanguage::Intent::None) {
    out.speak = true;
    out.text = juce::String("not sure whether you want ") +
               spokenIntent(reading.intent) + " or " +
               spokenIntent(reading.alternative) + " -- which?";
    return out;
  }

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

  case BotLanguage::Intent::Reshuffle:
    // Acting collectively is the point -- one "shake" rerolls the whole band --
    // so every addressed bot acts. Only the LINE about it is rationed, and that
    // rationing belongs to whoever owns the room, not here.
    out.speak = true;
    out.act = Act::Reshuffle;
    out.text = "ok, something else.";
    return out;

  case BotLanguage::Intent::Leave:
    out.speak = true;
    out.act = Act::Part;
    out.text = "leaving. bye.";
    return out;

  case BotLanguage::Intent::ExplainSelf:
    out.speak = true;
    out.text = explainSelf(ctx.self);
    return out;

  case BotLanguage::Intent::StopPlaying:
    // Future tense, always. An ending is two intervals and Ninjam delivers
    // them a whole interval late, so it lands four to eight seconds from here
    // -- a reply claiming to have stopped would be wrong twice a minute and
    // would teach the room to distrust the band.
    out.speak = true;
    switch (ctx.self.phase) {
    case BandPlayState::State::Playing:
      out.act = Act::StopPlaying;
      out.text = "wrapping it up -- ending on the downbeat after this one.";
      break;
    case BandPlayState::State::Wrapping:
    case BandPlayState::State::Resolving:
      out.text = "already bringing it to an end.";
      break;
    case BandPlayState::State::Silent:
      out.text = "already stopped. say \"" + ctx.self.name +
                 " play\" when you want me back in.";
      break;
    }
    return out;

  case BotLanguage::Intent::StartPlaying:
    out.speak = true;
    switch (ctx.self.phase) {
    case BandPlayState::State::Silent:
      out.act = Act::StartPlaying;
      out.text = "coming in on the next interval.";
      break;
    case BandPlayState::State::Wrapping:
      // The cancel. Worth its own line rather than the "already playing" one:
      // an ending was under way and is not any more, which is a change the
      // room should hear about.
      out.act = Act::StartPlaying;
      out.text = "right, keeping it going.";
      break;
    case BandPlayState::State::Resolving:
      // Nothing escapes the resolve. Saying so is better than silently doing
      // nothing, and the wait is one interval.
      out.text = "too late, i'm on the last chord -- ask me again after it.";
      break;
    case BandPlayState::State::Playing:
      out.text = "already playing.";
      break;
    }
    return out;

  case BotLanguage::Intent::SetQuiet:
    // The last thing it says, so it has to carry the way back. Everything
    // else about a quiet bot is invisible by design, including the fact that
    // it is quiet rather than broken.
    out.speak = true;
    out.act = Act::SetChatMuted;
    out.value = 1;
    out.text = "going quiet. say \"" + ctx.self.name +
               " talk\" to bring me back. still playing.";
    return out;

  case BotLanguage::Intent::SetLoud:
    // Answered whether or not it was quiet: "you can talk" to a bot that
    // already can is a harmless thing to say, and explaining that it was
    // never muted is the sort of pedantry the room does not need.
    out.speak = true;
    out.act = Act::SetChatMuted;
    out.value = 0;
    out.text = "talking again.";
    return out;

  case BotLanguage::Intent::SetChart:
    // Never acts and never reads a chart out of the request: a chart has to
    // lead its line, so a request for one essentially never carries one.
    out.speak = true;
    out.text = BotAnswer::answerSetChart(ctx.music);
    return out;

  case BotLanguage::Intent::ResetChart:
    // The one place a bot hands over a chart to paste. It does not act: a key
    // change stopped discarding the chart, which is why this is askable at
    // all, and a bot that quietly reverted its own would be playing something
    // nobody else in the room could see.
    out.speak = true;
    out.text = BotAnswer::answerResetChart(ctx.music);
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

  // Addressed, and not understood. One honest, visibly limited reply rather
  // than a plausible guess (docs/BOT-CHAT.md rule 3). Ambiguity is a different
  // thing from incomprehension and was answered above.
  out.speak = true;
  out.text = "i can tell you my part, my sound, the key, the chords or the "
             "tempo.";
  return out;
}

} // namespace

Response respond(const Context &ctx, const BotAddress::Incoming &in,
                 BotAddress::Attention &attention) {
  auto out = decide(ctx, in, attention);

  // "be quiet" means quiet. A bot that went on answering direct questions
  // would be arguing with the request, and the answer to "why is it still
  // talking" cannot be "because you asked it something".
  //
  // Two things still speak, and both are confirmations of an ACTION rather
  // than commentary on one: coming back -- without which there is no way out
  // of the mute at all -- and leaving. Everything else it was asked to do it
  // still does; only the talking stopped.
  if (ctx.self.chatMuted && out.act != Act::SetChatMuted &&
      out.act != Act::Part)
    out.speak = false;

  return out;
}

} // namespace BotChat
