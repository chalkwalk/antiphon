#include "../src/BotChat.h"
#include <JuceHeader.h>

namespace {

// A room with one bot and one person in it, which is the shape almost every
// question arrives in.
BotChat::Context contextWith(BotBand::Voice voice, const juce::String &botName,
                             const juce::String &human) {
  BotChat::Context ctx;

  BotAddress::Participant bot;
  bot.username = botName.toStdString();
  bot.handle = botName.toLowerCase().toStdString();
  bot.instrument = BotBand::voiceName(voice);
  bot.isBot = true;

  BotAddress::Participant person;
  person.username = human.toStdString();
  person.handle = human.toLowerCase().toStdString();

  ctx.room.participants = {bot, person};
  ctx.room.resolveHandles();

  ctx.music.key = MusicalKey::parseName("D minor");
  ctx.music.keySource = BotAnswer::Source::Chat;
  ctx.music.keySetBy = human;
  ctx.music.chart = Harmony::defaultChart(ctx.music.key);

  ctx.self.name = botName;
  ctx.self.voice = voice;
  ctx.self.playing = true;
  // A real band's settings rather than a hand-built one, so the figures a bot
  // quotes are the figures the renderer would actually play.
  ctx.self.settings = BotBand::defaults(ctx.music.key, 120, 8, 48000.0, 20260811);

  return ctx;
}

BotAddress::Incoming from(const juce::String &who, const juce::String &text) {
  BotAddress::Incoming in;
  in.sender = who.toStdString();
  in.text = text.toStdString();
  in.at = 100.0;
  return in;
}

class BotChatTests : public juce::UnitTest {
public:
  BotChatTests() : juce::UnitTest("BotChat", "bots") {}

  void runTest() override {
    beginTest("an addressed question about the sound is answered, not deflected");
    {
      // The phrasing is the point. `PracticeBot` matches this question by exact
      // string equality -- `t == "sound"`, `t == "kit"` -- so anything a person
      // would actually type falls through to the catch-all that lists what the
      // bot could have answered. "what do you sound like" is in the corpus as
      // DESCRIBE_SOUND and is exactly the kind of phrasing the recogniser was
      // built for and the exact-match path cannot see.
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      BotAddress::Attention attention;

      const auto r = BotChat::respond(
          ctx, from("tester", "Ravo: what do you sound like"), attention);

      expect(r.speak, "an addressed question got no answer at all");

      const juce::String patch =
          BotVoice::padCharacterName(BotBand::keysPatch(ctx.self.settings).character);
      expect(r.text.containsIgnoreCase(patch),
             "the reply does not say what it is playing (wanted '" + patch +
                 "'), it said: " + r.text);

      expect(!r.text.containsIgnoreCase("i can tell you"),
             "the reply is the catch-all menu rather than an answer: " + r.text);
    }

    beginTest("the part and the sound are different questions");
    {
      // Discovered by getting it wrong: "what are you playing" reads as
      // DESCRIBE_PART, not DESCRIBE_SOUND. `PracticeBot` answers `t == "what"`
      // with the patch name, which is a timbre answer to a question about the
      // music. The corpus separates them -- "whats your part" against "whats
      // your sound" -- so the replies must differ too, or the recogniser's
      // distinction is thrown away at the last step.
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      BotAddress::Attention attention;

      const auto part =
          BotChat::respond(ctx, from("tester", "Ravo: whats your part"), attention);
      const auto sound = BotChat::respond(
          ctx, from("tester", "Ravo: what do you sound like"), attention);

      expect(part.speak, "a question about the part got no answer at all");
      expect(part.text != sound.text,
             "the part and the sound got the same answer: " + part.text);

      // The keys bot's part IS the chart -- it holds the changes. Naming the
      // patch here would be answering the other question.
      expect(part.text.containsIgnoreCase("chart"),
             "the part reply does not say what it is playing: " + part.text);
    }

    beginTest("the key is reported with where it came from");
    {
      // The rule `BotAnswer` exists to keep, now that something composes around
      // it. A key always HAS a value -- the room starts in C major -- so
      // reporting one flatly tells the room it agreed on something it never
      // discussed. The provenance is the difference between an answer and a
      // fabrication, and it is the caller that can throw it away.
      BotAddress::Attention att;

      auto said = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      const auto chosen =
          BotChat::respond(said, from("tester", "Ravo: what key are we in"), att);

      expect(chosen.speak, "a question about the key got no answer at all");
      expect(chosen.text.containsIgnoreCase("D minor"),
             "the reply does not name the key: " + chosen.text);
      expect(chosen.text.containsIgnoreCase("tester"),
             "the reply drops who chose the key: " + chosen.text);

      // Nobody chose this one, and saying so is the whole point.
      auto defaulted = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      defaulted.music.keySource = BotAnswer::Source::Defaulted;
      defaulted.music.keySetBy = {};
      BotAddress::Attention att2;
      const auto guessed = BotChat::respond(
          defaulted, from("tester", "Ravo: whats the key"), att2);

      expect(guessed.speak, "a question about a defaulted key got no answer");
      expect(guessed.text.containsIgnoreCase("nobody chose"),
             "a key nobody chose is reported as though somebody did: " +
                 guessed.text);
    }

    beginTest("nothing a bot composes can set the key by saying it");
    {
      // `MusicalKey::parseTagged` matches `[key:` ANYWHERE in a line, and
      // PracticeBot acts on it wherever it appears -- so a bot explaining the
      // syntax would set the key in its own state and in every Antiphon client
      // in the room. `BotAnswer` asserts this over its own strings; nothing
      // asserted it over what BotChat wraps around them, which is where a
      // "type [key: Dm]" would be added.
      const char *asked[] = {
          "Ravo: what key are we in", "Ravo: whats the key",
          "Ravo: can you play in g minor", "Ravo: what are the chords",
          "Ravo: what do you sound like", "Ravo: whats your part",
          "Ravo: how do i change the key",
      };

      const BotAnswer::Source sources[] = {BotAnswer::Source::Chat,
                                           BotAnswer::Source::Topic,
                                           BotAnswer::Source::Defaulted};

      for (const auto source : sources) {
        for (const auto *line : asked) {
          auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
          ctx.music.keySource = source;
          BotAddress::Attention att;

          const auto r = BotChat::respond(ctx, from("tester", line), att);
          if (!r.speak)
            continue;

          expect(!MusicalKey::parseAnnouncement(r.text).valid,
                 "this reply sets the key by saying it: " + r.text);
          expect(!MusicalKey::parseTagged(r.text).valid,
                 "this reply carries a key tag: " + r.text);
          expect(!Harmony::looksLikeChart(r.text),
                 "this reply is itself a chart: " + r.text);
        }
      }
    }

    beginTest("every voice answers about itself, in its own terms");
    {
      // Four voices, and a generic answer from any of them would be a bot that
      // does not know what it is doing. The rhythm voices quote the figure
      // `BotBand::figureFor` gives the renderer, so the number is checked
      // against the source rather than against a transcript.
      // Both columns matter. Asserting only "the two answers differ" let a
      // generic "Pemo is playing." pass as a part answer, because it differed
      // from the sound answer and carried the name -- caught by breaking the
      // lead branch on purpose and watching this test stay green.
      struct Case {
        BotBand::Voice voice;
        const char *name;
        juce::String wantedInSound;
        juce::String wantedInPart;
      };

      const Case cases[] = {
          {BotBand::Voice::Drums, "Quado", "kit", "kit"},
          {BotBand::Voice::Bass, "Vessa", "bass", "roots"},
          {BotBand::Voice::Keys, "Ravo", "patch", "chart"},
          {BotBand::Voice::Lead, "Pemo", "", "D minor"},
      };

      for (const auto &c : cases) {
        auto ctx = contextWith(c.voice, c.name, "tester");
        BotAddress::Attention att;

        const auto sound = BotChat::respond(
            ctx, from("tester", juce::String(c.name) + ": whats your sound"),
            att);
        const auto part = BotChat::respond(
            ctx, from("tester", juce::String(c.name) + ": whats your part"),
            att);

        expect(sound.speak && part.speak,
               juce::String(c.name) + " did not answer both questions");
        expect(sound.text.contains(c.name) && part.text.contains(c.name),
               juce::String(c.name) + " did not say which bot was speaking");
        expect(sound.text != part.text,
               juce::String(c.name) + " gave one answer to two questions: " +
                   sound.text);

        if (c.wantedInSound.isNotEmpty())
          expect(sound.text.containsIgnoreCase(c.wantedInSound),
                 juce::String(c.name) + " sound reply missing '" +
                     c.wantedInSound + "': " + sound.text);

        expect(part.text.containsIgnoreCase(c.wantedInPart),
               juce::String(c.name) + " part reply missing '" + c.wantedInPart +
                   "': " + part.text);

        // The rhythm voices state a real figure. Read it from the same place
        // the renderer does, so a wrong number cannot pass by agreeing with a
        // hardcoded expectation.
        if (c.voice == BotBand::Voice::Drums || c.voice == BotBand::Voice::Bass) {
          const auto f = BotBand::figureFor(c.voice, ctx.self.settings);
          expect(part.text.contains(juce::String(f.pulses)) &&
                     part.text.contains(juce::String(f.steps)),
                 juce::String(c.name) + " did not quote its figure (" +
                     juce::String(f.pulses) + " over " +
                     juce::String(f.steps) + "): " + part.text);
        }
      }
    }
  }
};

static BotChatTests botChatTests;

} // namespace
