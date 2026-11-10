#include <JuceHeader.h>

#include "IntervalBudget.h"

namespace {

class IntervalBudgetTests : public juce::UnitTest {
public:
  IntervalBudgetTests() : juce::UnitTest("IntervalBudget", "IntervalBudget") {}

  void runTest() override {
    using namespace IntervalBudget;

    beginTest("the usual room costs what the roadmap says it does");
    {
      // 4 s of interval, half as much again, stereo float: 2.3 MB.
      expectEquals(samplesPerRemoteInterval(48000.0, 120, 8), 288000);
      expectEquals(bytesPerRemoteInterval(48000.0, 120, 8),
                   (std::int64_t)288000 * 8);
      expect(affordable(48000.0, 120, 8));
    }

    beginTest("long but playable intervals fit; the accidents do not");
    {
      expect(affordable(48000.0, 20, 32), "96 s of interval");
      expect(affordable(44100.0, 60, 64), "64 s at 44.1 kHz");
      expect(!affordable(48000.0, 120, 1000), "1000 BPI, seen live");
      expect(!affordable(48000.0, 40, 1024), "885 MB");
      expect(!affordable(96000.0, 60, 64), "the rate counts too");
    }

    beginTest("no tempo overflows the arithmetic");
    {
      expect(!affordable(192000.0, 1, 2147483647));
      expect(bytesPerRemoteInterval(192000.0, 1, 2147483647) > 0);
      expect(affordable(48000.0, 120, 0), "a zero BPI reserves nothing");
    }

    beginTest("what is said, word for word");
    {
      expectEquals(
          juce::String(receiveNotice(
              true, 120, 1000, bytesPerRemoteInterval(48000.0, 120, 1000))),
          juce::String("This room's interval (1000 BPI at 120 BPM) needs 275 "
                       "MB for each player, more than Antiphon will hold (64 "
                       "MB). You are still connected and still sending, but "
                       "you will not hear the others until the interval is "
                       "shorter."));
      expectEquals(juce::String(receiveNotice(false, 120, 8, 0)),
                   juce::String("The interval fits again (8 BPI at 120 BPM). "
                                "You will hear the others from the next one."));
    }
  }
};

static IntervalBudgetTests intervalBudgetTests;

} // namespace
