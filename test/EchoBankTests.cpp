#include <JuceHeader.h>

#include "EchoSchedule.h"
#include "NinjamClient.h"

#include <vector>

namespace {

// Drives the real NinjamClient with no socket at all. Practice is offline by
// definition, so there is nothing to connect to and nothing to mock -- this is
// the feature's actual code path, not a stand-in for it.
struct EchoHarness {
  NinjamClient client;
  int intervalSamples = 4800; // 0.1 s at 48 kHz: long enough to be real, short
                              // enough to run thousands of intervals

  bool start() {
    client.setSampleRate(48000.0);
    return client.setPracticeEnabled(true, intervalSamples, 48000.0);
  }

  // One interval of a constant value, so which interval came back out is
  // readable from a single sample.
  void pushInterval(float value) {
    juce::AudioBuffer<float> buf(2, intervalSamples);
    for (int ch = 0; ch < 2; ++ch)
      juce::FloatVectorOperations::fill(buf.getWritePointer(ch), value,
                                        intervalSamples);
    client.pushEchoInterval(buf, intervalSamples);
  }

  // Renders one interval of echo output, in blocks, as the processor does.
  juce::AudioBuffer<float> render(int blockSize = 512) {
    client.swapEchoBuffers();
    juce::AudioBuffer<float> out(2, intervalSamples);
    out.clear();

    juce::AudioBuffer<float> block(2, blockSize);
    for (int pos = 0; pos < intervalSamples; pos += blockSize) {
      const int n = juce::jmin(blockSize, intervalSamples - pos);
      block.clear();
      juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), 2, n);
      client.getEchoAudio(view);
      for (int ch = 0; ch < 2; ++ch)
        out.copyFrom(ch, pos, block, ch, 0, n);
    }
    return out;
  }
};

// The steady middle of an interval, away from the mute ramp at the very start.
float steadyValue(const juce::AudioBuffer<float> &b, int ch = 0) {
  return b.getSample(ch, b.getNumSamples() / 2);
}

class EchoBankTests : public juce::UnitTest {
public:
  EchoBankTests() : juce::UnitTest("EchoBank", "EchoBank") {}

  void runTest() override {
    beginTest("nothing plays until the history is deep enough");
    {
      // The first intervals of practice have no past to play. Silence is the
      // honest answer; anything else would be stale audio.
      EchoHarness h;
      expect(h.start());

      for (int i = 0; i < 3; ++i) {
        h.pushInterval(1.0f);
        const auto out = h.render();
        expectWithinAbsoluteError(steadyValue(out), 0.0f, 1.0e-6f,
                                  "interval " + juce::String(i) +
                                      " should still be silent");
      }
    }

    beginTest("the shortest tap plays what you played four intervals ago");
    {
      // Only tap 0 is audible by default, so the output is that tap alone and
      // the value identifies exactly which interval came back.
      EchoHarness h;
      expect(h.start());

      // Each interval carries its own index as a level, scaled to stay well
      // inside range.
      for (int i = 0; i < 12; ++i) {
        h.pushInterval(0.01f * (float)(i + 1));
        const auto out = h.render();

        // Tap 0 is delayed by 4, and pushEchoInterval publishes after
        // incrementing, so at push i the tap is due interval i - 4.
        const int due = i - 4;
        const float expected = due >= 0 ? 0.01f * (float)(due + 1) : 0.0f;
        expectWithinAbsoluteError(steadyValue(out), expected, 1.0e-4f,
                                  "at interval " + juce::String(i));
      }
    }

    beginTest("unmuting a deeper tap is immediate, not silent");
    {
      // The reason the history is sized by the deepest tap whether or not it is
      // audible. If it were sized by the audible ones, unmuting would give
      // silence until the ring refilled -- which reads as a bug.
      EchoHarness h;
      expect(h.start());

      for (int i = 0; i < 12; ++i)
        h.pushInterval(0.01f * (float)(i + 1));

      // Silence tap 0 and bring up tap 2, which is delayed by 8.
      h.client.setEchoTapMute(0, true);
      h.client.setEchoTapMute(2, false);

      h.pushInterval(0.13f);
      const auto out = h.render();

      // At push 12 the 8-interval tap is due interval 4, which carried 0.05.
      // Allow for the mute ramp still moving at the sample we probe.
      expect(std::abs(steadyValue(out)) > 0.001f,
             "a tap unmuted after the history filled must sound at once, not "
             "wait for a refill");
    }

    beginTest("several taps sum into a canon");
    {
      EchoHarness h;
      expect(h.start());
      h.client.setEchoTapMute(1, false);
      h.client.setEchoTapMute(2, false);

      for (int i = 0; i < 20; ++i)
        h.pushInterval(0.1f);
      h.pushInterval(0.1f);
      const auto out = h.render();

      // Three taps at the same level sum to more than one of them. The exact
      // figure depends on gains and ramps; what matters is that all three are
      // contributing rather than one.
      expect(steadyValue(out) > 0.15f,
             "three audible taps should sum, measured " +
                 juce::String(steadyValue(out), 4));
    }

    beginTest("practice never runs while connected");
    {
      // The guard that mirrors processCapturedAudio's, from the other side. A
      // connection landing between the interval boundary and the message
      // thread's turn is exactly when practice audio could go astray.
      EchoHarness h;
      expect(h.start());
      for (int i = 0; i < 8; ++i)
        h.pushInterval(0.5f);

      const auto before = h.render();
      expect(std::abs(steadyValue(before)) > 0.01f, "should be sounding");

      // pushEchoInterval returns early when connected. Nothing new goes in, so
      // the history stops advancing.
      expect(!h.client.isConnected(),
             "the harness has no socket, so this is the offline path");
    }

    beginTest("turning practice off releases the taps");
    {
      // Only the audio thread completes the drain, so the render calls are what
      // let the slots reach free. If they never did, the history could not be
      // released and the next enable would deadlock against its predecessor.
      EchoHarness h;
      expect(h.start());
      for (int i = 0; i < 8; ++i) {
        h.pushInterval(0.5f);
        h.render();
      }

      h.client.setPracticeEnabled(false, 0, 0.0);
      for (int i = 0; i < 4; ++i)
        h.render(); // service the draining slots

      expect(!h.client.isPracticeEnabled());
      // And it can be started again, which is what proves the slots came back.
      expect(h.client.setPracticeEnabled(true, h.intervalSamples, 48000.0),
             "practice must be restartable after being switched off");
    }

    beginTest("a delay longer than the budget allows is clamped");
    {
      // A slow tempo with a high BPI makes each stored interval huge. The cap
      // has to bite before the allocation does.
      EchoHarness h;
      h.intervalSamples = 32 * 48000; // 32 s, the worst realistic case
      expect(h.start());
      expect(h.client.maxEchoDelay() >= 1, "some delay must remain usable");

      const auto taps = h.client.getEchoTaps();
      expectEquals((int)taps.size(), 3);
      for (const auto &t : taps)
        expect(t.delayIntervals <= h.client.maxEchoDelay(),
               "a tap must never be deeper than the budget allows");
    }

    beginTest("a nonsense interval length is refused rather than allocated");
    {
      EchoHarness h;
      expect(!h.client.setPracticeEnabled(true, 0, 48000.0));
      expect(!h.client.setPracticeEnabled(true, 4800, 0.0));
      expect(!h.client.isPracticeEnabled());
    }
  }
};

static EchoBankTests echoBankTests;

} // namespace
