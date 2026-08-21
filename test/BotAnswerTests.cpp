#include "../src/BotAnswer.h"
#include <JuceHeader.h>

namespace {

BotAnswer::Room roomIn(const char *key, BotAnswer::Source keySource,
                       BotAnswer::Source chartSource) {
  BotAnswer::Room r;
  r.key = MusicalKey::parseName(key);
  r.keySource = keySource;
  r.chart = Harmony::defaultChart(r.key);
  r.chartSource = chartSource;
  return r;
}

class BotAnswerTests : public juce::UnitTest {
public:
  BotAnswerTests() : juce::UnitTest("BotAnswer", "music") {}

  void runTest() override {
    using namespace BotAnswer;

    beginTest("nothing a bot says can set the key by saying it");
    {
      // The rule this file exists to keep. `MusicalKey::parseTagged` matches
      // `[key:` anywhere in a line, so a reply that quoted the tag would set
      // the key -- in its own state and in every Antiphon client in the room.
      // The failure would be silent, and no corpus can catch it, so it is
      // asserted over every string this module can produce.
      const Room rooms[] = {
          roomIn("D minor", Source::Chat, Source::Chat),
          roomIn("C major", Source::Defaulted, Source::Defaulted),
          roomIn("G minor", Source::Topic, Source::Defaulted),
      };
      const auto wanted = MusicalKey::parseName("A major");

      for (const auto &r : rooms) {
        // Complete replies: neither hazard may appear.
        const juce::StringArray replies{answerSetKey(r, wanted),
                                        answerSetKey(r, {}),
                                        answerSetChart(r),
                                        answerResetChart(r),
                                        answerSetTempo(r, 130, 0),
                                        answerSetTempo(r, 0, 16),
                                        answerSetTempo(r, 0, 0),
                                        answerSetTempo(r, 500, 0),
                                        answerVoteRequest(r)};
        for (const auto &line : replies) {
          expect(!MusicalKey::parseAnnouncement(line.toStdString()).valid,
                 "this reply sets the key by saying it: " + line);
          // A reply beginning with a bar line would be read as somebody
          // announcing a chart. This nearly happened: dropping a provenance
          // suffix left describeChart returning bare chart text, and the only
          // thing that had been preventing it was the suffix.
          expect(!Harmony::looksLikeChart(line.toStdString()),
                 "this reply is itself a chart: " + line);
        }

        // Fragments carry only the key rule -- `describeChart` legitimately
        // begins with a bar line, which is exactly why the header forbids
        // sending one on its own.
        for (const auto &fragment : {describeKey(r), describeChart(r)})
          expect(!MusicalKey::parseAnnouncement(fragment.toStdString()).valid,
                 "this fragment sets the key: " + fragment);
      }
    }

    beginTest("a default is never reported as a decision");
    {
      const auto fresh = roomIn("C major", Source::Defaulted, Source::Defaulted);
      expect(describeKey(fresh).contains("which nobody chose"),
             describeKey(fresh));
      expect(describeChart(fresh).contains("the default for the key"),
             describeChart(fresh));
      expect(answerSetChart(fresh).contains("nobody has put a chart up"),
             answerSetChart(fresh));

      // Both describe* results are NOUN PHRASES, so they compose. This is the
      // assertion that would have caught "we are in nobody has named a key,
      // so i defaulted to C major".
      expect(answerSetKey(fresh, MusicalKey::parseName("A major"))
                 .contains("we are in C major, which nobody chose"),
             answerSetKey(fresh, MusicalKey::parseName("A major")));

      // ...and the chart it names is the one it is actually playing, which is
      // both the honest answer and the safe example: pasting it back is a
      // no-op, where a generic one would move the harmony.
      const auto text = Harmony::chartText(fresh.chart, false);
      expect(describeChart(fresh).contains(text),
             "the example is not what it is playing: " + describeChart(fresh));

      const auto told = roomIn("D minor", Source::Chat, Source::Chat);
      expect(describeKey(told).contains("said in the room"), describeKey(told));
    }

    beginTest("the default chords for a key are offered, never imposed");
    {
      // Askable because a key change no longer does it silently (DESIGN.md
      // 6.4). A bot has no more authority over a chart than over a key, so the
      // answer is the line to paste rather than the chart itself.
      Room r = roomIn("D minor", Source::Chat, Source::Chat);
      expect(Harmony::parseChart("| Dm | A7 | Dm | Gm |", r.chart));

      const auto reply = answerResetChart(r);
      const auto wanted =
          Harmony::chartText(Harmony::defaultChart(r.key), r.key);
      expect(reply.contains(wanted),
             "the default was not named: " + reply + " (wanted " + wanted + ")");
      // Naming it must not BE announcing it: a client reads a leading bar as
      // somebody putting a chart up, and the chart being offered is not the
      // one the room is on.
      expect(!Harmony::looksLikeChart(reply.toStdString()), reply);

      // A room already on the default has nothing to change, and saying so is
      // more useful than handing back a line that would do nothing.
      const auto already = roomIn("D minor", Source::Chat, Source::Defaulted);
      expect(answerResetChart(already).containsIgnoreCase("already"),
             answerResetChart(already));
    }

    beginTest("a chart is read out spelled against the key");
    {
      // A room reads its chart back so a player can paste it; that only works
      // if the reading is the notation. D major takes sharps and its lowered
      // second is still Eb, which one flag for a whole chart cannot say.
      Room r = roomIn("D major", Source::Chat, Source::Chat);
      expect(Harmony::parseChart("| D | Eb7 | D | A |", r.chart));
      expect(describeChart(r).contains("Eb7"), describeChart(r));
      expect(!describeChart(r).contains("D#"), describeChart(r));
    }

    beginTest("a topic key says it came from the topic");
    {
      auto r = roomIn("G minor", Source::Topic, Source::Defaulted);
      expect(describeKey(r).contains("topic"), describeKey(r));
      // The age is unknowable -- the topic reaches only a joining client -- so
      // the claim is bounded to what we can actually stand behind.
      expect(describeKey(r).contains("since i joined"), describeKey(r));

      auto named = roomIn("G minor", Source::Chat, Source::Chat);
      named.keySetBy = "Dave";
      expect(describeKey(named).contains("dave said so"), describeKey(named));
    }

    beginTest("an unreadable key is answered, not guessed");
    {
      const auto r = roomIn("D minor", Source::Chat, Source::Chat);
      const auto reply = answerSetKey(r, {});
      expect(reply.contains("could not tell"), reply);
      // Putting up the wrong key is worse than putting up none, so the reply
      // must not name one as though it had understood.
      expect(!reply.contains("A major"), reply);
    }

    beginTest("the tempo reply refuses what the server would refuse");
    {
      const auto r = roomIn("D minor", Source::Chat, Source::Chat);
      // An out-of-range vote is answered by the server with a complaint about
      // the command's parameters, which tells a player nothing. Refuse first.
      expect(answerSetTempo(r, 500, 0).contains("40 to 400"),
             answerSetTempo(r, 500, 0));
      expect(answerSetTempo(r, 0, 125).contains("2 to 64"),
             answerSetTempo(r, 0, 125));
      expect(answerSetTempo(r, 130, 0).contains("!vote bpm 130"),
             answerSetTempo(r, 130, 0));

      // Both numbers always, because either alone says almost nothing: 120 at
      // 8 and 120 at 32 are completely different rooms.
      for (const auto &reply : {answerSetTempo(r, 130, 0),
                                answerSetTempo(r, 0, 16),
                                answerSetTempo(r, 0, 0)}) {
        expect(reply.contains("120 bpm") && reply.contains("8 bpi"), reply);
      }
    }

    beginTest("what a bot actually says");
    {
      // Logged rather than asserted. The wording is the deliverable here and
      // the assertions above only pin its load-bearing parts, so this prints
      // every reply in full: a line that reads badly is a defect no `expect`
      // will catch, and it should be possible to notice one without starting
      // a room.
      auto told = roomIn("D minor", Source::Chat, Source::Chat);
      told.keySetBy = "Dave";
      const auto fresh = roomIn("C major", Source::Defaulted, Source::Defaulted);
      const auto topic = roomIn("G minor", Source::Topic, Source::Defaulted);

      const struct { const char *asked; juce::String said; } kLines[] = {
          {"[fresh room] can we play in a major",
           answerSetKey(fresh, MusicalKey::parseName("A major"))},
          {"[fresh room] can we change the chords", answerSetChart(fresh)},
          {"[fresh room] whats the key", "we are in " + describeKey(fresh) + "."},
          {"whats the key", "we are in " + describeKey(told) + "."},
          {"whats the chart", "the chart is " + describeChart(told) + "."},
          {"can we change the chords", answerSetChart(told)},
          {"can you slow down", answerSetTempo(told, 100, 0)},
          {"longer intervals", answerSetTempo(told, 0, 16)},
          {"can we change the tempo", answerSetTempo(told, 0, 0)},
          {"go to 500 bpm", answerSetTempo(told, 500, 0)},
          {"vote for 130", answerVoteRequest(told)},
          {"[from topic] whats the key", "we are in " + describeKey(topic) + "."},
          {"[from topic] play in something else", answerSetKey(topic, {})},
      };
      for (const auto &l : kLines) {
        logMessage(juce::String("  you: ") + l.asked);
        logMessage("  bot: " + l.said);
      }
      expect(true);
    }

    beginTest("a bot never starts a vote, even asked directly");
    {
      const auto r = roomIn("D minor", Source::Chat, Source::Chat);
      const auto reply = answerVoteRequest(r);
      expect(reply.contains("do not start votes"), reply);
      expect(reply.contains("back you"), reply);
    }
  }
};

static BotAnswerTests botAnswerTests;

} // namespace
