#include "../src/jambot/BotAddress.h"
#include <JuceHeader.h>

// The addressing corpus IS the specification, so this file is mostly a reader
// for it. `test/fixtures/bot-addressing.txt` states, for 150 messages arriving
// in a stated conversational context, exactly which bots may answer -- and the
// commonest correct answer is none of them.
//
// Written this way on purpose. Assertions inline in C++ would have been easier
// to write and impossible to read as a body of behaviour, and the question this
// answers ("would a room full of these be tolerable?") is one you have to be
// able to skim the whole of to judge.

namespace {

BotAddress::Room fixtureRoom(bool humanCalledDelvo = false) {
  BotAddress::Room room;

  auto bot = [&](const char *name, const char *instrument) {
    BotAddress::Participant p;
    p.username = std::string(name) + "[" + instrument + "-bot]";
    p.handle = juce::String(name).toLowerCase().toStdString();
    p.instrument = instrument;
    p.channel = instrument;
    p.isBot = true;
    room.participants.push_back(p);
  };
  auto human = [&](const char *name, const char *channel) {
    BotAddress::Participant p;
    p.username = name;
    p.handle = name;
    p.channel = channel;
    room.participants.push_back(p);
  };

  bot("Mirn", "kit");
  bot("Delvo", "bass");
  bot("Pundo", "keys");
  bot("Quado", "lead");

  BotAddress::Participant tutor;
  tutor.username = "Tutor[bot]";
  tutor.handle = "tutor";
  tutor.instrument = "tutor";
  tutor.isBot = true;
  room.participants.push_back(tutor);

  human("you", "guitar");
  human("dave", "guitar");
  human("sam", "vocals");
  if (humanCalledDelvo)
    human("delvo", "drums");

  room.resolveHandles();
  return room;
}

juce::String labelFor(const juce::String &instrument) {
  return instrument.toUpperCase();
}

} // namespace

class BotAddressTests : public juce::UnitTest {
public:
  BotAddressTests() : juce::UnitTest("BotAddress", "music") {}

  void runTest() override {
    runUnitTests();
    runCorpus();
  }

  void runUnitTests() {
    beginTest("leaving is the whole message, and part is never it");
    {
      // By far the commonest use of "part" in a jam is not the command.
      expect(BotAddress::isPartCommand("leave"));
      expect(BotAddress::isPartCommand("  LEAVE  "));
      // Withdrawn as a command: it is the most ordinary word in the room.
      expect(!BotAddress::isPartCommand("part"));
      for (const char *ordinary :
           {"whats your part", "the bass part is tricky", "im learning my part",
            "can you play that part again", "part of the chart is wrong"})
        expect(!BotAddress::isPartCommand(ordinary),
               juce::String(ordinary) + " was taken for the command");

      // `stop` is withdrawn for the same reason `part` was, and it is the
      // worse of the two: to a musician it is the LEAST destructive thing you
      // can say, and it was wired to the most destructive act a bot can do.
      // Stopping and leaving are different states now (docs/BOT-CHAT.md 15).
      for (const char *playing : {"stop", "STOP", " stop ", "halt", "enough"})
        expect(!BotAddress::isPartCommand(playing),
               juce::String(playing) + " still sends the band home");

      // `go` goes with it: on its own it is as likely to mean start as leave.
      // Leaving needs a phrase that can only mean leaving.
      expect(!BotAddress::isPartCommand("go"));
      for (const char *leaving : {"go away", "go home", "GO AWAY", "exit"})
        expect(BotAddress::isPartCommand(leaving),
               juce::String(leaving) + " no longer sends the band home");
    }

    beginTest("naming a bot does not turn an ordinary sentence into a command");
    {
      // Found by a player, whose "Ravo: what's your part" made the bot LEAVE.
      //
      // `isPartCommand` gets this right and says why, but `classify` had a
      // SECOND rule of its own -- the message merely had to END with the word
      // -- so every one of these went to PartMe. The corpus could not see it:
      // it records WHO answers, and PartMe and Named are both "the kit bot",
      // so "hey kit whats your part" passed while doing the wrong thing.
      auto room = fixtureRoom();
      const std::string me = "Mirn[kit-bot]";

      for (const char *ordinary :
           {"mirn whats your part", "mirn: what is your part",
            "mirn can you play that part again", "kit hows your part going",
            "mirn what did you leave out"}) {
        BotAddress::Attention attention;
        BotAddress::Incoming in;
        in.sender = "you";
        in.text = ordinary;
        in.at = 10.0;

        const auto verdict = BotAddress::classify(room, me, in, attention);
        expect(verdict != BotAddress::Address::PartMe &&
                   verdict != BotAddress::Address::PartAll,
               juce::String(ordinary) + " sent the bot home");
      }

      // The command itself still works when it is the whole message.
      for (const char *command : {"mirn leave", "mirn: leave"}) {
        BotAddress::Attention attention;
        BotAddress::Incoming in;
        in.sender = "you";
        in.text = command;
        in.at = 10.0;
        expect(BotAddress::classify(room, me, in, attention) ==
                   BotAddress::Address::PartMe,
               juce::String(command) + " did not send the bot home");
      }
    }

    beginTest("an address to somebody else closes the window, known or not");
    {
      // "name: something" is aimed at that name. If it is not mine it is not
      // for me -- and that has to hold even when the name means nothing to me,
      // because the reasons it might are all ordinary: a player who has just
      // joined and is not in my list yet, a bot that has left, or a typo. The
      // window exists so a follow-up needs no address, and an explicit address
      // to somebody else is the clearest possible signal it has ended.
      auto room = fixtureRoom();
      const std::string me = "Mirn[kit-bot]";

      for (const char *elsewhere :
           {"zorp: what are you playing", "ravo: whats your part",
            "dave: how was that", "delvo: shake"}) {
        // Open a window by addressing me, the way a real conversation starts.
        BotAddress::Attention attention;
        BotAddress::Incoming opener;
        opener.sender = "you";
        opener.text = "mirn whats your part";
        opener.at = 10.0;
        expect(BotAddress::classify(room, me, opener, attention) !=
                   BotAddress::Address::Ignore,
               "the opener was not addressed to me");

        BotAddress::Incoming next;
        next.sender = "you";
        next.text = elsewhere;
        next.at = 11.0;

        expect(BotAddress::classify(room, me, next, attention) ==
                   BotAddress::Address::Ignore,
               juce::String(elsewhere) + " was answered by the wrong bot");
      }
    }

    beginTest("courtesy is a whole message, not a word inside one");
    {
      for (const char *c : {"thanks", "cheers", "nice one", "ok", "got it"})
        expect(BotAddress::isCourtesy(c), juce::String(c) + " is courtesy");
      for (const char *notCourtesy :
           {"thanks what about your accents", "ok now shake", "nice key choice"})
        expect(!BotAddress::isCourtesy(notCourtesy),
               juce::String(notCourtesy) + " is not just courtesy");
    }

    beginTest("the address comes off before a command is matched");
    {
      // The bug this exists to stop coming back: commands are matched exactly,
      // and "Ravo: shake" is not "shake". Naming the bot you wanted -- the
      // documented way to address one -- defeated every command in the room,
      // and the bot answered with its fallback, so it looked like a bot that
      // did not understand rather than one that never saw the word.
      auto room = fixtureRoom();
      const std::string me = "Mirn[kit-bot]";
      const struct { const char *in; const char *out; } kCases[] = {
          {"mirn: shake", "shake"},
          {"Mirn, shake", "shake"},
          {"mirn shake", "shake"},
          {"Mirn[kit-bot]: shake", "shake"},
          {"kit: shake", "shake"},
          {"  mirn:   shake", "shake"},
      };
      for (const auto &c : kCases)
        expectEquals(juce::String(BotAddress::withoutAddress(room, me, c.in)),
                     juce::String(c.out), juce::String(c.in));

      // A name that is only a prefix of a word is not an address.
      expectEquals(
          juce::String(BotAddress::withoutAddress(room, me, "mirnly shake")),
          juce::String("mirnly shake"));

      // The name ALONE is an opener, not a command with an empty body --
      // returning "" there would turn "mirn" into an unrecognised command.
      expectEquals(juce::String(BotAddress::withoutAddress(room, me, "mirn")),
                   juce::String("mirn"));

      // Somebody else's name is left alone: it is not our address to strip.
      expectEquals(
          juce::String(BotAddress::withoutAddress(room, me, "delvo: shake")),
          juce::String("delvo: shake"));

      // Nothing to strip.
      expectEquals(juce::String(BotAddress::withoutAddress(room, me, "shake")),
                   juce::String("shake"));
    }

    beginTest("a handle colliding with a player is withdrawn");
    {
      // Silence beats a wrong answer: the bot answers to its full username and
      // its instrument instead.
      auto room = fixtureRoom(true);
      const auto *delvoBot = room.find("Delvo[bass-bot]");
      expect(delvoBot != nullptr);
      expect(!delvoBot->handleUsable,
             "the handle survived a player of the same name");

      const auto *mirn = room.find("Mirn[kit-bot]");
      expect(mirn != nullptr && mirn->handleUsable,
             "an uncontested handle was withdrawn anyway");
    }
  }

  void runCorpus() {
    const auto file = fixtureFile();
    if (!file.existsAsFile()) {
      beginTest("the addressing corpus is present");
      expect(false, "not found: " + file.getFullPathName());
      return;
    }

    beginTest("every case in the addressing corpus");

    auto lines = juce::StringArray::fromLines(file.loadFileAsString());
    juce::String context = "COLD";
    int checked = 0, failed = 0;

    for (const auto &raw : lines) {
      auto line = raw.upToFirstOccurrenceOf("#", false, false).trim();
      if (juce::String(line).isEmpty())
        continue;

      if (juce::String(line).startsWithChar('[') && juce::String(line).endsWithChar(']')) {
        context = line.substring(1, line.length() - 1).trim();
        continue;
      }

      const int split = line.indexOfAnyOf(" \t");
      if (split <= 0)
        continue;
      const auto expected = line.substring(0, split).trim();
      const auto message = line.substring(split).trim();
      if (juce::String(message).isEmpty())
        continue;

      ++checked;
      const auto got = answerersFor(context, message);
      const auto want = juce::StringArray::fromTokens(expected, ",", "");

      juce::StringArray wantSorted(want), gotSorted(got);
      wantSorted.sort(true);
      gotSorted.sort(true);
      if (wantSorted.joinIntoString(",") == gotSorted.joinIntoString(",") ||
          (wantSorted[0] == "NOBODY" && gotSorted.isEmpty()))
        continue;

      ++failed;
      if (failed <= 25)
        logMessage("  [" + context + "] \"" + message + "\"  want " +
                   wantSorted.joinIntoString(",") + "  got " +
                   (gotSorted.isEmpty() ? juce::String("NOBODY")
                                        : gotSorted.joinIntoString(",")));
    }

    logMessage("corpus: " + juce::String(checked - failed) + " of " +
               juce::String(checked) + " cases");
    expect(failed == 0, juce::String(failed) + " of " + juce::String(checked) +
                            " corpus cases disagree");
  }

private:
  static juce::File fixtureFile() {
    auto dir = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile).getParentDirectory();
    for (int i = 0; i < 8; ++i) {
      const auto candidate =
          dir.getChildFile("test/fixtures/bot-addressing.txt");
      if (candidate.existsAsFile())
        return candidate;
      dir = dir.getParentDirectory();
    }
    return {};
  }

  // Which bots answer this message, in this context.
  juce::StringArray answerersFor(const juce::String &context,
                                 const juce::String &message) {
    const bool humanDelvo = context.startsWith("ROOM") && context.contains("delvo");
    auto room = fixtureRoom(humanDelvo);

    juce::StringArray out;
    const double now = 1000.0;

    for (const auto &p : room.participants) {
      if (!p.isBot)
        continue;

      BotAddress::Attention attention;
      juce::String speaker = "you";

      // The contexts the corpus uses, each setting up a prior turn.
      if (context.startsWith("AFTER_KIT")) {
        if (p.instrument == "kit") {
          attention.owner = "you";
          attention.turnsLeft = BotAddress::kWindowTurns;
          attention.openedAt = context.contains("EXPIRED")
                                   ? now - BotAddress::kWindowSeconds - 10.0
                                   : now - 5.0;
        }
      } else if (context.startsWith("AFTER")) {
        // "AFTER you: delvo" -- that speaker opened a window on that bot.
        const auto who = context.fromFirstOccurrenceOf(" ", false, false)
                             .upToFirstOccurrenceOf(":", false, false)
                             .trim();
        const auto opened = context.fromFirstOccurrenceOf(":", false, false).trim();
        if (juce::String(p.handle).equalsIgnoreCase(opened)) {
          attention.owner = who.toStdString();
          attention.turnsLeft = BotAddress::kWindowTurns;
          attention.openedAt = now - 5.0;
        }
      }

      // A speaker is written in angle brackets; a trailing colon is an
      // address. The corpus says so, because using the colon for both made
      // three cases ambiguous.
      juce::String text = message;
      if (text.startsWithChar('<')) {
        const int close = text.indexOfChar('>');
        if (close > 0) {
          speaker = text.substring(1, close).trim();
          text = text.substring(close + 1).trim();
        }
      }

      BotAddress::Incoming in;
      in.sender = speaker.toStdString();
      in.text = text.toStdString();
      in.at = now;

      const auto verdict =
          BotAddress::classify(room, p.username, in, attention);
      switch (verdict) {
      case BotAddress::Address::Ignore:
        break;
      case BotAddress::Address::PartAll:
      case BotAddress::Address::Collective:
        out.add(labelFor(p.instrument));
        break;
      default:
        out.add(labelFor(p.instrument));
        break;
      }
    }

    // The corpus writes "ALL" rather than listing five labels.
    if (out.size() >= 4)
      return {"ALL"};
    return out;
  }
};

static BotAddressTests botAddressTests;
