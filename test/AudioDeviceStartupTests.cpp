#include <JuceHeader.h>

#include "AudioDeviceStartup.h"

namespace {

using S = AudioDeviceStartup::State;

AudioDeviceStartup::Inputs in(bool finished, bool succeeded, int64_t elapsedMs) {
  AudioDeviceStartup::Inputs i;
  i.probeFinished = finished;
  i.probeSucceeded = succeeded;
  i.elapsedMs = elapsedMs;
  return i;
}

class AudioDeviceStartupTests : public juce::UnitTest {
public:
  AudioDeviceStartupTests()
      : juce::UnitTest("AudioDeviceStartup", "AudioDeviceStartup") {}

  void runTest() override {
    beginTest("the happy path");
    {
      AudioDeviceStartup s(5000);
      expect(s.get() == S::Opening, "must start out opening");
      expect(!s.isTerminal());

      expect(!s.update(in(false, false, 10)), "no change while still opening");
      expect(s.get() == S::Opening);

      expect(s.update(in(true, true, 120)), "opening -> ready is a change");
      expect(s.get() == S::Ready);
      expect(s.isTerminal());
    }

    beginTest("a probe that fails cleanly reports Failed, not TimedOut");
    {
      AudioDeviceStartup s(5000);
      expect(s.update(in(true, false, 40)));
      expect(s.get() == S::Failed,
             "a device that says no is a different story from one that hangs");
    }

    beginTest("exceeding the budget with no answer is a timeout");
    {
      AudioDeviceStartup s(5000);
      expect(!s.update(in(false, false, 4999)), "still inside the budget");
      expect(s.get() == S::Opening);

      expect(s.update(in(false, false, 5000)), "the boundary counts as expiry");
      expect(s.get() == S::TimedOut);
      expect(s.isTerminal());
    }

    beginTest("a wedged probe that finally answers must not resurrect startup");
    {
      // The whole point of the timeout is that we have already told the user
      // the device did not respond and handed them the settings dialog. A
      // probe thread completing minutes later must not yank the app back.
      AudioDeviceStartup s(5000);
      expect(s.update(in(false, false, 5000)));
      expect(s.get() == S::TimedOut);

      expect(!s.update(in(true, true, 90000)), "no change");
      expect(s.get() == S::TimedOut, "TimedOut is terminal");
    }

    beginTest("terminal states are sticky");
    {
      AudioDeviceStartup ready(5000);
      ready.update(in(true, true, 10));
      expect(!ready.update(in(false, false, 999999)));
      expect(ready.get() == S::Ready, "a Ready device does not time out");

      AudioDeviceStartup failed(5000);
      failed.update(in(true, false, 10));
      expect(!failed.update(in(true, true, 20)));
      expect(failed.get() == S::Failed);
    }

    beginTest("reset returns it to Opening for a second attempt");
    {
      AudioDeviceStartup s(5000);
      s.update(in(false, false, 5000));
      expect(s.get() == S::TimedOut);

      s.reset();
      expect(s.get() == S::Opening);
      expect(!s.isTerminal(), "the user picking another device starts over");
      expect(s.update(in(true, true, 30)));
      expect(s.get() == S::Ready);
    }

    beginTest("every state describes itself");
    {
      for (auto st : {S::Opening, S::Ready, S::Failed, S::TimedOut}) {
        const auto *d = AudioDeviceStartup::describe(st);
        expect(d != nullptr && juce::String(d).isNotEmpty(),
               "a state a user can reach needs words");
      }
    }
  }
};

static AudioDeviceStartupTests audioDeviceStartupTests;

} // namespace
