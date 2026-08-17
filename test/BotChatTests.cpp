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

    beginTest("the chart is reported without announcing itself");
    {
      // `describeChart` returns text that BEGINS with a bar line when somebody
      // put the chart up, and `Harmony::readChart` treats a leading `|` as the
      // whole signal -- so a bot answering "what are the chords" with the
      // fragment alone would be read by every client as somebody announcing a
      // new chart. The lead-in is the protection, not decoration.
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      ctx.music.chartSource = BotAnswer::Source::Chat;
      BotAddress::Attention att;

      const auto r =
          BotChat::respond(ctx, from("tester", "Ravo: what are the chords"), att);

      expect(r.speak, "a question about the chords got no answer at all");
      expect(!r.text.trim().startsWithChar('|'),
             "the reply leads with a bar line and is itself a chart: " + r.text);

      const juce::String bars = Harmony::chartText(
          ctx.music.chart, MusicalKey::usesFlats(ctx.music.key.tonic,
                                                 ctx.music.key.mode));
      expect(r.text.contains(bars),
             "the reply does not contain the chart (" + bars + "): " + r.text);

      // A chart nobody put up is the default for the key, and saying so is the
      // same honesty rule the key answer follows.
      auto fallback = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      BotAddress::Attention att2;
      const auto d = BotChat::respond(
          fallback, from("tester", "Ravo: whats the progression"), att2);
      expect(d.speak, "a question about a defaulted chart got no answer");
      expect(d.text.containsIgnoreCase("default"),
             "a chart nobody chose is reported as though somebody put it up: " +
                 d.text);
    }

    beginTest("the tempo is reported as both of the numbers that set it");
    {
      // Ninjam's tempo is two numbers and a player needs both: the bpi decides
      // how long you wait to hear yourself, which is the thing newcomers find
      // surprising, and it is not derivable from the bpm.
      auto ctx = contextWith(BotBand::Voice::Drums, "Quado", "tester");
      ctx.music.bpm = 132;
      ctx.music.bpi = 16;
      BotAddress::Attention att;

      const auto r =
          BotChat::respond(ctx, from("tester", "Quado: how fast are we going"), att);

      expect(r.speak, "a question about the tempo got no answer at all");
      expect(r.text.contains("132"),
             "the reply does not give the tempo: " + r.text);
      expect(r.text.contains("16"),
             "the reply gives the bpm but not the bpi: " + r.text);
    }

    beginTest("asked to change the key, a bot says whose decision it is");
    {
      // The bots have no authority over the key -- it is whatever the room
      // agrees -- so recognising the ask is what lets them say so instead of
      // reciting the current key at somebody who just asked for a different
      // one. That miss is the worst kind: it looks like an answer.
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      BotAddress::Attention att;

      const auto r = BotChat::respond(
          ctx, from("tester", "Ravo: can you play in g minor"), att);

      expect(r.speak, "a request to change the key got no answer at all");
      expect(r.text.containsIgnoreCase("G minor"),
             "the reply does not name the key that was asked for: " + r.text);
      expect(r.text.containsIgnoreCase("not mine"),
             "the reply does not say whose decision the key is: " + r.text);
      expect(r.text.containsIgnoreCase("/key"),
             "the reply does not say how to actually change it: " + r.text);
    }

    beginTest("a key is read out of the sentence, or admitted to be unreadable");
    {
      // `MusicalKey::parseName` takes a BARE letter -- "a" is A major -- so
      // scanning a sentence for something that parses will read a key out of an
      // article. The corpus has both halves of the trap under SET_KEY: "put it
      // in a minor" IS A minor, and "give me a minor key" is somebody asking
      // for some minor key and naming none. Guessing wrong here puts a key up
      // that nobody asked for, so the rule is to say so instead.
      struct Case {
        const char *said;
        const char *wanted; // empty: must not claim to have read a key
      };

      const Case cases[] = {
          {"Ravo: put it in a minor", "A minor"},
          {"Ravo: switch to g major", "G major"},
          {"Ravo: can you try d dorian", "D Dorian"},
          {"Ravo: lets play in e minor", "E minor"},
          {"Ravo: give me a minor key", ""},
          {"Ravo: can we change the key", ""},
          {"Ravo: play something in dorian", ""},
      };

      for (const auto &c : cases) {
        auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
        BotAddress::Attention att;
        const auto r = BotChat::respond(ctx, from("tester", c.said), att);

        expect(r.speak, juce::String(c.said) + " got no answer at all");

        if (*c.wanted != 0) {
          expect(r.text.containsIgnoreCase(c.wanted),
                 juce::String(c.said) + " did not read the key (wanted " +
                     c.wanted + "): " + r.text);
        } else {
          expect(r.text.containsIgnoreCase("could not tell"),
                 juce::String(c.said) +
                     " claimed to read a key nobody named: " + r.text);
        }
      }
    }

    beginTest("asked to change the tempo, a bot points at the vote");
    {
      // A tempo is a server vote, and a bot is an ordinary client -- so it can
      // neither set one nor start one. Saying so, with the command that does
      // work, is the answer.
      auto ctx = contextWith(BotBand::Voice::Drums, "Quado", "tester");
      BotAddress::Attention att;

      const auto r = BotChat::respond(
          ctx, from("tester", "Quado: can you vote for 132 bpm"), att);

      expect(r.speak, "a request to change the tempo got no answer at all");
      expect(r.text.containsIgnoreCase("not mine"),
             "the reply does not say whose decision the tempo is: " + r.text);
      expect(r.text.contains("!vote bpm 132"),
             "the reply does not carry the vote that was asked for: " + r.text);

      // No number named: still answerable, with the command and a blank to
      // fill in rather than a number nobody asked for.
      BotAddress::Attention att2;
      const auto vague =
          BotChat::respond(ctx, from("tester", "Quado: can we go faster"), att2);
      expect(vague.speak, "'can we go faster' got no answer at all");
      expect(vague.text.contains("!vote bpm"),
             "the reply does not say how to change the tempo: " + vague.text);
      expect(!vague.text.contains("!vote bpm 0"),
             "the reply invented a tempo nobody named: " + vague.text);
    }

    beginTest("a tempo number goes to the unit it was given with");
    {
      // The two ranges overlap between 40 and 64, so a bare number cannot be
      // assigned by size alone. An explicit unit always wins; a bare number is
      // a bpm, which is what "vote for 140" means -- except where that reading
      // is impossible and the bpi one is not, since answering "the tempo vote
      // only goes from 40 to 400" to somebody asking for 16 bpi is a confident
      // answer to a question they did not ask.
      struct Case {
        const char *said;
        const char *wanted;
      };

      const Case cases[] = {
          {"Quado: vote for 140", "!vote bpm 140"},
          {"Quado: can you vote 100", "!vote bpm 100"},
          {"Quado: can we do 16 bpi", "!vote bpi 16"},
          {"Quado: vote for 16", "!vote bpi 16"},
          {"Quado: can you vote for 50", "!vote bpm 50"},
      };

      for (const auto &c : cases) {
        auto ctx = contextWith(BotBand::Voice::Drums, "Quado", "tester");
        BotAddress::Attention att;
        const auto r = BotChat::respond(ctx, from("tester", c.said), att);

        expect(r.speak, juce::String(c.said) + " got no answer at all");
        expect(r.text.contains(c.wanted),
               juce::String(c.said) + " did not offer " + c.wanted + ": " +
                   r.text);
      }
    }

    beginTest("asked to change the chords, a bot says what it is on");
    {
      // Never acts, and never needs to read a chart out of the request: a chart
      // has to lead its line, so a request for one essentially never carries
      // one to echo.
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      ctx.music.chartSource = BotAnswer::Source::Chat;
      BotAddress::Attention att;

      const auto r = BotChat::respond(
          ctx, from("tester", "Ravo: can we change the chords"), att);

      expect(r.speak, "a request to change the chart got no answer at all");
      expect(!r.text.trim().startsWithChar('|'),
             "the reply leads with a bar line and is itself a chart: " + r.text);
      expect(r.text.containsIgnoreCase("room"),
             "the reply does not say whose decision the chart is: " + r.text);

      // The example it gives is the chart it is ACTUALLY on, which is both the
      // honest answer and the safe one -- a generic example pasted into a room
      // in another key would silently move the harmony.
      const juce::String bars = Harmony::chartText(
          ctx.music.chart,
          MusicalKey::usesFlats(ctx.music.key.tonic, ctx.music.key.mode));
      expect(r.text.contains(bars),
             "the reply does not say what it is on (" + bars + "): " + r.text);
    }

    beginTest("a command produces an action, not just a sentence");
    {
      // The half of Response that is not words. A command both acts and speaks,
      // and only the action touches state that outlives the message -- which is
      // exactly why they are separate fields and why this can be asserted
      // without a band, a socket or a room.
      struct Case {
        const char *said;
        BotChat::Act act;
      };

      const Case cases[] = {
          {"Ravo: shake", BotChat::Act::Reshuffle},
          {"Ravo: mix it up", BotChat::Act::Reshuffle},
          {"Ravo: leave", BotChat::Act::Part},
          {"Ravo: help", BotChat::Act::None},
          {"Ravo: what key are we in", BotChat::Act::None},
      };

      for (const auto &c : cases) {
        auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
        BotAddress::Attention att;
        const auto r = BotChat::respond(ctx, from("tester", c.said), att);

        expect(r.act == c.act,
               juce::String(c.said) + " produced the wrong action");
        expect(r.speak,
               juce::String(c.said) + " acted silently, so nobody can tell it "
                                      "worked");
      }
    }

    beginTest("only the soloist answers to an instrument, and says so if not");
    {
      // The one thing about the band a player may pin, and it survives a shake:
      // somebody who asked for a guitar because they came to practise keyboards
      // has not changed their mind by asking for a different tune.
      auto lead = contextWith(BotBand::Voice::Lead, "Pemo", "tester");
      BotAddress::Attention att;
      const auto r = BotChat::respond(lead, from("tester", "Pemo: guitar"), att);

      expect(r.act == BotChat::Act::SetLeadInstrument,
             "the lead did not take the instrument: " + r.text);
      expect(r.value == (int)BotVoice::LeadInstrument::Guitar,
             "the lead took the wrong instrument");
      expect(r.speak && r.text.containsIgnoreCase("guitar"),
             "the lead did not say what it picked up: " + r.text);

      // A drummer asked to play the guitar should say so rather than silently
      // accepting a setting it will never read.
      auto kit = contextWith(BotBand::Voice::Drums, "Quado", "tester");
      BotAddress::Attention att2;
      const auto no =
          BotChat::respond(kit, from("tester", "Quado: guitar"), att2);

      expect(no.act == BotChat::Act::None,
             "a drummer accepted a guitar setting it will never read");
      expect(no.speak && no.text.containsIgnoreCase("lead"),
             "the drummer did not point at the bot that can: " + no.text);
    }

    beginTest("asked what it is, a bot says so and offers a way out");
    {
      // First contact. An acknowledgement that teaches nothing is a promise the
      // design cannot keep, so the answer doubles as a menu -- and it must name
      // how to remove the bot, because somebody who does not want it needs that
      // more than anything else in the sentence.
      auto ctx = contextWith(BotBand::Voice::Lead, "Pemo", "tester");
      BotAddress::Attention att;

      const auto r = BotChat::respond(ctx, from("tester", "Pemo: what are you"), att);

      expect(r.speak, "'what are you' got no answer at all");
      expect(r.text.containsIgnoreCase("bot"),
             "the reply does not say it is a bot: " + r.text);
      expect(r.text.containsIgnoreCase("leave"),
             "the reply does not say how to be rid of it: " + r.text);
      // "part" may appear as the ordinary noun it now is -- "ask it about its
      // part" -- but never offered as the command it no longer is.
      expect(!r.text.containsIgnoreCase("\"" + ctx.self.name + " part\"") &&
                 !r.text.containsIgnoreCase("say \"part\""),
             "the reply offers a command that was withdrawn: " + r.text);
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

      // Both provenances are swept, not just the key's. A chart put up in chat
      // makes `describeChart` return the bars ALONE, which is the only case
      // where the reply can parse as a chart -- sweeping the key's provenance
      // while leaving the chart defaulted never produced one, so this guard
      // agreed with the dedicated test without being able to catch anything.
      for (const auto source : sources) {
        for (const auto *line : asked) {
          auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
          ctx.music.keySource = source;
          ctx.music.chartSource = source;
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

    beginTest("a bot told to be quiet says how to bring it back, then stops");
    {
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      BotAddress::Attention att;

      const auto hush = BotChat::respond(ctx, from("tester", "Ravo: be quiet"), att);
      expect(hush.speak, "going quiet was not acknowledged at all");
      expect(hush.act == BotChat::Act::SetChatMuted, hush.text);
      expectEquals(hush.value, 1);
      // The acknowledgement is the ONLY place the way back is offered: after
      // it, by construction, the bot says nothing. A silent mute is a bot that
      // looks broken and cannot be fixed.
      expect(hush.text.containsIgnoreCase("talk"),
             "no way back was offered: " + hush.text);

      ctx.self.chatMuted = true;
      const juce::String questions[] = {"Ravo: what key are we in",
                                        "Ravo: whats your part", "Ravo",
                                        "Ravo: flurble"};
      for (const auto &q : questions) {
        BotAddress::Attention quiet;
        const auto r = BotChat::respond(ctx, from("tester", q), quiet);
        expect(!r.speak, "a quiet bot answered '" + q + "': " + r.text);
      }
    }

    beginTest("a quiet bot still acts, and still says the two things it must");
    {
      auto ctx = contextWith(BotBand::Voice::Keys, "Ravo", "tester");
      ctx.self.chatMuted = true;

      // Coming back has to be audible or there is no way out of the mute.
      BotAddress::Attention att;
      const auto back = BotChat::respond(ctx, from("tester", "Ravo: you can talk now"), att);
      expect(back.speak, "a quiet bot could not be brought back");
      expect(back.act == BotChat::Act::SetChatMuted, back.text);
      expectEquals(back.value, 0);

      // Leaving is an action, not commentary: going silently would read as
      // having ignored the request.
      BotAddress::Attention att2;
      const auto bye = BotChat::respond(ctx, from("tester", "Ravo: leave"), att2);
      expect(bye.act == BotChat::Act::Part, bye.text);
      expect(bye.speak, "a quiet bot left without saying so");

      // Everything else still happens; only the talking stopped.
      BotAddress::Attention att3;
      const auto shake = BotChat::respond(ctx, from("tester", "Ravo: shake"), att3);
      expect(shake.act == BotChat::Act::Reshuffle, shake.text);
      expect(!shake.speak, "a quiet bot narrated a shake: " + shake.text);
    }
  }
};

static BotChatTests botChatTests;

} // namespace
