#include "../src/BotAddress.h"
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
    beginTest("part is the whole message, or it is an ordinary word");
    {
      // By far the commonest use of "part" in a jam is not the command.
      expect(BotAddress::isPartCommand("part"));
      expect(BotAddress::isPartCommand("  PART  "));
      for (const char *ordinary :
           {"whats your part", "the bass part is tricky", "im learning my part",
            "can you play that part again", "part of the chart is wrong"})
        expect(!BotAddress::isPartCommand(ordinary),
               juce::String(ordinary) + " was taken for the command");
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
      if (line.isEmpty())
        continue;

      if (line.startsWithChar('[') && line.endsWithChar(']')) {
        context = line.substring(1, line.length() - 1).trim();
        continue;
      }

      const int split = line.indexOfAnyOf(" \t");
      if (split <= 0)
        continue;
      const auto expected = line.substring(0, split).trim();
      const auto message = line.substring(split).trim();
      if (message.isEmpty())
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
