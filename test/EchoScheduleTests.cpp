#include <JuceHeader.h>

#include <set>

#include "EchoSchedule.h"

namespace {

using namespace EchoSchedule;

class EchoScheduleTests : public juce::UnitTest {
public:
  EchoScheduleTests() : juce::UnitTest("EchoSchedule", "EchoSchedule") {}

  void runTest() override {
    beginTest("a tap plays the interval it is delayed by");
    {
      const int depth = historyDepth(4);
      // At interval 10, a 4-interval tap plays whatever was stored for 6.
      expectEquals(readSlotFor(10, 4, depth), writeSlotFor(6, depth));
      expectEquals(readSlotFor(11, 4, depth), writeSlotFor(7, depth));
    }

    beginTest("a tap is silent until it has something that old to play");
    {
      // The first few intervals of practice have no history yet. Silence is the
      // honest answer; playing whatever happens to be in the buffer would be
      // stale audio from a previous session.
      const int depth = historyDepth(4);
      for (int now = 0; now < 4; ++now)
        expectEquals(readSlotFor(now, 4, depth), -1,
                     "interval " + juce::String(now) + " cannot have a 4-old");
      expect(readSlotFor(4, 4, depth) >= 0, "by interval 4 it can");
    }

    beginTest("a tap labelled N intervals is heard N intervals later");
    {
      // The promise the UI makes, stated as arithmetic. The handoff cost is
      // real -- an entry stored at a push is not consumed until the swap two
      // intervals later -- but it belongs inside readSlotForPush, not added to
      // what the user asked for. Counting back from the push instead put every
      // tap two intervals deeper than its label, which sounds like a canon and
      // is not the one you chose.
      const int depth = historyDepth(8);
      for (int delay = kMinDelayIntervals; delay <= 8; ++delay) {
        for (long long push = 20; push < 40; ++push) {
          const int slot = readSlotForPush(push, delay, depth);
          // The entry handed here is consumed by the swap that begins interval
          // push + kHandoffIntervals, so that is when it is heard.
          const long long heardIn = push + kHandoffIntervals;
          const long long playedIn = heardIn - delay;
          expectEquals(slot, writeSlotFor(playedIn, depth),
                       "delay " + juce::String(delay) + " at push " +
                           juce::String((int)push));
          expectEquals(heardDuring(playedIn, delay), heardIn,
                       "the two definitions must agree");
        }
      }
    }

    beginTest("a delay shallower than the handoff has nothing to play");
    {
      // One interval would mean playing audio that has not been captured yet:
      // an interval is not complete until the boundary that ends it, and the
      // store happens after that. Refusing is the honest answer, and the
      // picker starts at kMinDelayIntervals for the same reason.
      const int depth = historyDepth(8);
      for (int delay = 0; delay < kMinDelayIntervals; ++delay)
        expectEquals(readSlotForPush(50, delay, depth), -1,
                     "delay " + juce::String(delay) + " cannot be delivered");
      expect(readSlotForPush(50, kMinDelayIntervals, depth) >= 0,
             "the shallowest deliverable delay must in fact deliver");
    }

    beginTest("a push cannot hand over an interval that has not happened");
    {
      // The pipeline filling, from the push side. At the shallowest delay the
      // entry handed over is the one just written, and every deeper tap counts
      // back from there -- so early pushes have nothing old enough.
      const int depth = historyDepth(8);
      expectEquals(readSlotForPush(0, 8, depth), -1, "no history at all yet");
      expectEquals(readSlotForPush(5, 8, depth), -1, "still filling");
      expect(readSlotForPush(6, 8, depth) >= 0,
             "by push 6 the 8-interval tap has interval 0 to play");
      // The shallowest tap is ready at once: it plays what just finished.
      expectEquals(readSlotForPush(0, kMinDelayIntervals, depth),
                   writeSlotFor(0, depth));
    }

    beginTest("the deepest delay sets the depth, not the sum of the taps");
    {
      // The whole reason the taps share one ring. Three taps at 4, 6 and 8 cost
      // what 8 costs, not what 4+6+8 costs.
      expectEquals(historyDepth(8), 10);
      expect(historyDepth(8) < 4 + 6 + 8,
             "sharing must be cheaper than not sharing");
    }

    beginTest("no tap is ever read from an entry being overwritten");
    {
      // The property the whole design rests on. Run the real tap set over far
      // more intervals than the ring holds, so the indices wrap many times, and
      // assert the writer never lands on an entry a reader could still want --
      // including the one still sounding its fade tail.
      const int depth = historyDepth(8);

      for (long long now = 0; now < 10000; ++now)
        for (int delay = kMinDelayIntervals; delay <= 8; ++delay)
          expect(!wouldOverwriteLiveEntry(now, delay, depth),
                 "interval " + juce::String((int)now) + ", tap " +
                     juce::String(delay) + " would be overwritten");
    }

    beginTest("one interval less of slack really would collide");
    {
      // Proves the slack is doing something rather than being superstition.
      // With depth = delay there is no room for the entry still sounding, and
      // the writer lands straight on it.
      bool everCollided = false;
      const int tooShallow = 8;
      for (long long now = 0; now < 100 && !everCollided; ++now)
        if (wouldOverwriteLiveEntry(now, 8, tooShallow))
          everCollided = true;
      expect(everCollided,
             "if this passes, the slack is not earning its place");
    }

    beginTest("write slots cycle and cover the whole ring");
    {
      const int depth = historyDepth(4); // 6
      std::set<int> seen;
      for (long long n = 0; n < depth; ++n)
        seen.insert(writeSlotFor(n, depth));
      expectEquals((int)seen.size(), depth, "every entry must get used");
      expectEquals(writeSlotFor(depth, depth), writeSlotFor(0, depth),
                   "and then it wraps");
    }

    beginTest("a negative interval index does not index backwards");
    {
      // Reachable if a counter is reset while a tap is mid-flight.
      const int depth = historyDepth(4);
      for (long long n = -20; n < 0; ++n) {
        const int slot = writeSlotFor(n, depth);
        expect(slot >= 0 && slot < depth,
               "slot " + juce::String(slot) + " is outside the ring");
      }
    }

    beginTest("the budget decides how deep a tap may go");
    {
      // Eight seconds of stereo float at 48 kHz is about 3 MB per interval, so
      // a 128 MB budget is worth roughly forty intervals.
      const int intervalSamples = 8 * 48000;
      const int perIntervalMB = intervalSamples * 2 * 4 / 1000000;
      expect(perIntervalMB >= 3, "sanity: an interval really is megabytes");

      const int deep = maxDelayForBudget(128000000LL, intervalSamples, 2);
      expect(deep > 8, "a normal jam must allow the default taps");
      expect(deep >= kMinDelayIntervals, "and at least the shallowest one");
      expect(deep < 100, "and the budget must actually bite");

      // A slow tempo with a long interval costs far more per entry, so the
      // allowance drops.
      const int slow = maxDelayForBudget(128000000LL, 32 * 48000, 2);
      expect(slow < deep, "a longer interval must allow fewer of them");

      // Degenerate inputs give nothing rather than something huge.
      expectEquals(maxDelayForBudget(0, intervalSamples, 2), 0);
      expectEquals(maxDelayForBudget(128000000LL, 0, 2), 0);
      expectEquals(maxDelayForBudget(1, intervalSamples, 2), 0,
                   "a budget too small for one entry allows no delay at all");
    }

    beginTest("a nonsense delay still gives a usable depth");
    {
      expect(historyDepth(0) >= 3);
      expect(historyDepth(-5) >= 3);
      expectEquals(writeSlotFor(5, 0), 0, "a zero-depth ring cannot be indexed");
      expectEquals(readSlotFor(5, 0, 6), -1, "a zero delay is not a tap");
      expectEquals(readSlotForPush(5, 0, 6), -1, "nor at the push");
      expectEquals(readSlotForPush(5, 4, 0), -1, "nor with no ring");
    }
  }
};

static EchoScheduleTests echoScheduleTests;

} // namespace
