#include <JuceHeader.h>

#include "ClipsortLog.h"

namespace {

using namespace ClipsortLog;

const char *kGuidA = "0123456789abcdef0123456789abcdef";
const char *kGuidB = "fedcba9876543210fedcba9876543210";

class ClipsortLogTests : public juce::UnitTest {
public:
  ClipsortLogTests() : juce::UnitTest("ClipsortLog", "ClipsortLog") {}

  void runTest() override {
    beginTest("a clip belongs to the interval line above it");
    {
      // The one structural rule of the format: the interval number is carried
      // forward while scanning, so a run of user lines all belong to the most
      // recent interval.
      const juce::String log =
          juce::String("interval 0 120 16\n") + "user " + kGuidA +
          " \"daniel\" 0 \"Guitar\"\n" + "user " + kGuidB +
          " \"sam\" 1 \"Kit\"\n" + "interval 1 120 16\n" + "user " + kGuidA +
          " \"daniel\" 0 \"Guitar\"\n";

      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 3);
      expectEquals(s.clips[0].interval, 0);
      expectEquals(s.clips[1].interval, 0);
      expectEquals(s.clips[2].interval, 1);
      expectEquals(s.intervalCount, 2);
      expectEquals(s.malformedLines, 0);
    }

    beginTest("every field of a user line is read");
    {
      const juce::String log = juce::String("interval 7 137 11\n") + "user " +
                               kGuidA + " \"da niel\" 3 \"Vox mic\"\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 1);
      const auto &c = s.clips[0];
      expectEquals(c.guid, juce::String(kGuidA));
      expectEquals(c.username, juce::String("da niel"),
                   "a quoted name may contain spaces");
      expectEquals(c.channelIndex, 3);
      expectEquals(c.channelName, juce::String("Vox mic"));
      expectEquals(c.interval, 7);
      expectEquals(c.bpm, 137);
      expectEquals(c.bpi, 11);
    }

    beginTest("tempo is recorded per clip, because it can change mid-session");
    {
      // Interval lengths are not constant across a session, so a stem laid out
      // with one length throughout would drift after any vote.
      const juce::String log =
          juce::String("interval 0 120 16\n") + "user " + kGuidA +
          " \"a\" 0 \"c\"\n" + "interval 1 137 11\n" + "user " + kGuidA +
          " \"a\" 0 \"c\"\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 2);
      expectEquals(s.clips[0].bpm, 120);
      expectEquals(s.clips[0].bpi, 16);
      expectEquals(s.clips[1].bpm, 137);
      expectEquals(s.clips[1].bpi, 11);
    }

    beginTest("a malformed line is counted and skipped, not guessed at");
    {
      const juce::String log =
          juce::String("interval 0 120 16\n")
          + "user notahexguid \"a\" 0 \"c\"\n"     // guid wrong length
          + "user " + kGuidA + " noquotes 0 \"c\"\n" // unquoted name
          + "user " + kGuidA + " \"a\" notanumber \"c\"\n"
          + "interval x 120 16\n"
          + "banana 1 2 3\n"
          + "user " + kGuidA + " \"a\" 0 \"c\"\n";  // this one is fine

      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 1, "only the well-formed clip survives");
      expectEquals(s.malformedLines, 5);
    }

    beginTest("a clip before the first interval line belongs to interval 0");
    {
      // Every real server log opens this way: the session directory is created
      // on a periodic check that can land part-way through an interval, so the
      // upload already in progress is logged before the first boundary.
      // Rejecting it -- which is what this module did until a real archive
      // showed otherwise -- silently drops audio from the front of a session.
      const juce::String log =
          juce::String("user ") + kGuidA + " \"a\" 0 \"c\"\n" +
          "interval 1 120 16\n" + "user " + kGuidB + " \"b\" 0 \"c\"\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 2);
      expectEquals(s.clips[0].guid, juce::String(kGuidA));
      expectEquals(s.clips[0].interval, 0);
      expectEquals(s.clips[1].interval, 1);
      expectEquals(s.malformedLines, 0);
      expectEquals(s.firstInterval, 0);
      expectEquals(s.lastInterval, 1);
      expectEquals(s.intervalCount, 2);

      // And it must carry a tempo, or it has no length and is written as
      // nothing -- the same clip lost by a different route. The first tempo the
      // log states is the best evidence for what was in force a moment before.
      expectEquals(s.clips[0].bpm, 120);
      expectEquals(s.clips[0].bpi, 16);
      expect(intervalSamples(s.clips[0].bpm, s.clips[0].bpi, 48000.0) > 0,
             "a clip with no tempo occupies no time and disappears");
    }

    beginTest("a server log numbering from 1 spans what it actually covers");
    {
      // The server restarts numbering at 1 in every half-hour directory, so a
      // log that starts at 1 must not be given a phantom interval 0 -- that
      // would prepend silence to a segment and push everything after it late.
      const juce::String log = juce::String("interval 1 120 16\n") + "user " +
                               kGuidA + " \"a\" 0 \"c\"\n" +
                               "interval 2 120 16\n" + "user " + kGuidA +
                               " \"a\" 0 \"c\"\n";
      const auto s = parse(log);
      expectEquals(s.firstInterval, 1);
      expectEquals(s.lastInterval, 2);
      expectEquals(s.intervalCount, 2, "two intervals, not three");
    }

    beginTest("a missing channel name is allowed, not malformed");
    {
      const juce::String log =
          juce::String("interval 0 120 16\n") + "user " + kGuidA + " \"a\" 2\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 1);
      expectEquals(s.clips[0].channelIndex, 2);
      expect(s.clips[0].channelName.isEmpty());
      expectEquals(s.malformedLines, 0);
    }

    beginTest("blank lines and stray whitespace are ignored");
    {
      const juce::String log = juce::String("\n  interval 0 120 16  \n\n  user ") +
                               kGuidA + " \"a\" 0 \"c\"  \n\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 1);
      expectEquals(s.malformedLines, 0);
    }

    beginTest("an empty or clipless manifest yields nothing rather than failing");
    {
      expectEquals((int)parse("").clips.size(), 0);
      expectEquals((int)parse("interval 0 120 16\n").clips.size(), 0);
      expectEquals(parse("interval 0 120 16\n").intervalCount, 1);
      expectEquals(parse("").intervalCount, 0, "nothing spans nothing");
    }

    beginTest("intervals count from the highest seen, not from the clip count");
    {
      // A session where nobody played for a while still has those intervals in
      // its timeline, and the stems need silence for them.
      const juce::String log = juce::String("interval 0 120 16\n") + "user " +
                               kGuidA + " \"a\" 0 \"c\"\n" +
                               "interval 9 120 16\n" + "user " + kGuidA +
                               " \"a\" 0 \"c\"\n";
      const auto s = parse(log);
      expectEquals(s.firstInterval, 0);
      expectEquals(s.lastInterval, 9);
      expectEquals(s.intervalCount, 10);
      expectEquals((int)s.clips.size(), 2);
    }

    beginTest("a clip's path is its first hex character then its guid");
    {
      expectEquals(clipPath(kGuidA),
                   juce::String("0/0123456789abcdef0123456789abcdef.OGG"));
      expectEquals(clipPath(kGuidB),
                   juce::String("f/fedcba9876543210fedcba9876543210.OGG"));
      expect(clipPath("").isEmpty());
    }

    beginTest("what we write is what we parse");
    {
      // The round trip that lets the client save a session the converter can
      // read. Both halves live in this module so they cannot drift.
      Clip original;
      original.guid = kGuidA;
      original.username = "daniel";
      original.channelIndex = 2;
      original.channelName = "Guitar";

      const juce::String log =
          intervalLine(4, 137, 11) + "\n" + userLine(original) + "\n";
      const auto s = parse(log);
      expectEquals((int)s.clips.size(), 1);
      expectEquals(s.malformedLines, 0);

      const auto &back = s.clips[0];
      expectEquals(back.guid, original.guid);
      expectEquals(back.username, original.username);
      expectEquals(back.channelIndex, original.channelIndex);
      expectEquals(back.channelName, original.channelName);
      expectEquals(back.interval, 4);
      expectEquals(back.bpm, 137);
      expectEquals(back.bpi, 11);
    }

    beginTest("interval length matches the clock the clients used");
    {
      // Truncated, not rounded, reproducing njclient.cpp:806 -- the same
      // arithmetic IntervalClock uses. Stems laid out on a different grid would
      // not line up with each other.
      expectEquals(intervalSamples(120, 16, 48000.0), 384000);
      expectEquals(intervalSamples(120, 8, 48000.0), 192000);
      expectEquals(intervalSamples(137, 11, 48000.0), 231240);
      expectEquals(intervalSamples(137, 11, 44100.0), 212452);

      expectEquals(intervalSamples(0, 16, 48000.0), 0, "no tempo, no length");
      expectEquals(intervalSamples(120, 0, 48000.0), 0);
      expectEquals(intervalSamples(120, 16, 0.0), 0);
    }
  }
};

static ClipsortLogTests clipsortLogTests;

} // namespace
