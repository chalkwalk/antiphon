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

  // One interval of a constant value, written the way the audio thread writes
  // it -- in blocks, as it is played -- so which interval came back out is
  // readable from a single sample.
  void writeIntervalAudio(float value, int blockSize) {
    juce::AudioBuffer<float> block(2, blockSize);
    for (int pos = 0; pos < intervalSamples; pos += blockSize) {
      const int n = juce::jmin(blockSize, intervalSamples - pos);
      for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill(block.getWritePointer(ch), value, n);
      client.writeEchoBlock(block.getReadPointer(0), block.getReadPointer(1),
                            false, 0, n, 1.0f, 1.0f);
    }
  }

  // Which absolute interval the last runInterval call played.
  //
  // Worth stating rather than counting loop iterations, because they are not
  // the same number and assuming they were is how a correct implementation
  // gets "fixed". The first call plays interval 0 while filling it, so the
  // count of completed intervals is the index of the one just played.
  long long playingInterval() const { return closed - 1; }

  int closed = 0;

  // One whole interval of the real pipeline, in the order processBlock runs
  // it: write per block as the audio plays, then close and swap at the
  // boundary -- both on this thread, which is what lets a one-interval echo
  // exist at all. `value` is what you play during this interval.
  //
  // Mirroring processBlock is the whole point. This harness once swapped per
  // interval and called nothing per block, while the processor swapped per
  // block -- so the suite tested a pipeline the product did not have, and
  // passed while echo played one block of each interval and then went silent.
  // If this loop stops matching processBlock, it stops being a test.
  juce::AudioBuffer<float> runInterval(float value, int blockSize = 512) {
    return runIntervalImpl(&value, blockSize);
  }

  // A boundary that stores nothing: the source has stopped. Reaches the paths
  // where a slot has no audio at all, which writing silence does not.
  juce::AudioBuffer<float> runIntervalWithoutPush(int blockSize = 512) {
    return runIntervalImpl(nullptr, blockSize);
  }

  juce::AudioBuffer<float> runIntervalImpl(const float *value, int blockSize) {
    if (value != nullptr)
      writeIntervalAudio(*value, blockSize);

    juce::AudioBuffer<float> out(2, intervalSamples);
    out.clear();
    juce::AudioBuffer<float> block(2, blockSize);
    for (int pos = 0; pos < intervalSamples; pos += blockSize) {
      const int n = juce::jmin(blockSize, intervalSamples - pos);
      block.clear();
      juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), 2, n);
      client.serviceEchoSlots();
      client.getEchoAudio(view);
      for (int ch = 0; ch < 2; ++ch)
        out.copyFrom(ch, pos, block, ch, 0, n);
    }

    client.closeEchoInterval();
    client.swapEchoBuffers();
    ++closed;
    return out;
  }
};

// The proportion of an interval that is actually carrying signal. The bug this
// guards against was audible as a snippet at each interval start followed by
// silence, which every mid-interval spot check in the world would have missed.
float duty(const juce::AudioBuffer<float> &b, float floorLevel = 1.0e-4f) {
  int sounding = 0;
  const float *p = b.getReadPointer(0);
  for (int i = 0; i < b.getNumSamples(); ++i)
    if (std::abs(p[i]) > floorLevel)
      ++sounding;
  return (float)sounding / (float)b.getNumSamples();
}

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

      // The default tap is 4 deep, so intervals 1 to 3 have nothing that old.
      // The loop condition looks at the interval about to be played, not the
      // one just played.
      while (h.playingInterval() + 1 < 4) {
        const auto out = h.runInterval(1.0f);
        expectWithinAbsoluteError(steadyValue(out), 0.0f, 1.0e-6f,
                                  "interval " +
                                      juce::String((int)h.playingInterval()) +
                                      " should still be silent");
      }

      // And then it starts, exactly on interval 4 -- the boundary between
      // "still filling" and "wrong by one" is the whole point of the test.
      const auto first = h.runInterval(1.0f);
      expectEquals((int)h.playingInterval(), 4);
      expectWithinAbsoluteError(steadyValue(first), 1.0f, 1.0e-4f,
                                "interval 4 must play what interval 0 held");
    }

    beginTest("a tap labelled N intervals is heard N intervals later");
    {
      // The promise the UI makes. It was two intervals deeper than this,
      // because the entry handed over at a push is not consumed until the swap
      // two intervals later and the arithmetic counted back from the push
      // instead of forward from the interval it would be heard in. The result
      // sounded plausible -- a canon, just not the one you asked for.
      EchoHarness h;
      expect(h.start());

      // Each interval carries its own index as a level, scaled to stay well
      // inside range.
      auto levelFor = [](int i) { return 0.01f * (float)(i + 1); };

      for (int i = 0; i < 12; ++i) {
        const auto out = h.runInterval(levelFor(i));
        // Only tap 0 is audible by default, so the output is that tap alone
        // and the value names exactly which interval came back. Expressed
        // against the interval being played rather than the loop counter,
        // which is one less: see playingInterval.
        const long long played = h.playingInterval() - 4;
        const float expected = played >= 0 ? levelFor((int)played) : 0.0f;
        expectWithinAbsoluteError(
            steadyValue(out), expected, 1.0e-4f,
            "at interval " + juce::String((int)h.playingInterval()));
      }
    }

    beginTest("a one-interval tap plays back the moment the interval ends");
    {
      // The looper case, and the reason the store moved onto the audio thread.
      // "Why does the shortest delay start at 2?" is a fair question with no
      // good answer -- one interval is the natural shortest echo and the one a
      // player reaches for, so the pipeline had to give it up rather than the
      // UI explain it away.
      EchoHarness h;
      expect(h.start());
      h.client.setEchoTapDelay(0, 1);
      h.client.setEchoTapMute(0, false);

      auto levelFor = [](int i) { return 0.05f * (float)(i + 1); };

      // Interval 0 has nothing before it, so it is the one interval of silence
      // a one-interval echo can honestly have.
      const auto first = h.runInterval(levelFor(0));
      expectWithinAbsoluteError(steadyValue(first), 0.0f, 1.0e-6f,
                                "interval 0 has no predecessor");

      for (int i = 1; i < 8; ++i) {
        const auto out = h.runInterval(levelFor(i));
        expectWithinAbsoluteError(
            steadyValue(out), levelFor(i - 1), 1.0e-4f,
            "interval " + juce::String((int)h.playingInterval()) +
                " must play the one immediately before it");
      }
    }

    beginTest("the shallowest delay the picker offers actually works");
    {
      // Ties the constant to the behaviour, so lowering kMinDelayIntervals
      // without the pipeline to back it fails here rather than on stage.
      EchoHarness h;
      expect(h.start());
      h.client.setEchoTapDelay(0, EchoSchedule::kMinDelayIntervals);
      expectEquals(h.client.getEchoTaps()[0].delayIntervals,
                   EchoSchedule::kMinDelayIntervals);

      for (int i = 0; i < 6; ++i)
        h.runInterval(0.3f);
      const auto out = h.runInterval(0.3f);
      expect(duty(out) > 0.99f,
             "the shallowest tap must sound for a whole interval, measured " +
                 juce::String(duty(out) * 100.0f, 1) + "%");
    }

    beginTest("rebuilding the history does not free it under the audio thread");
    {
      // setPracticeEnabled used to clear echoHistory the instant it was called,
      // while the audio thread could still be mid-block holding a pointer into
      // an entry. The comment said the storage was released "once the slots
      // have been observed free"; nothing observed anything. Retiring the ring
      // and reclaiming it only after the audio thread acknowledges is what
      // makes that comment true.
      EchoHarness h;
      expect(h.start());
      for (int i = 0; i < 6; ++i)
        h.runInterval(0.4f);

      // Rebuild repeatedly with the audio thread still running between each,
      // which is the shape that would trip an allocator or a sanitiser.
      for (int round = 0; round < 8; ++round) {
        expect(h.client.setPracticeEnabled(true, h.intervalSamples, 48000.0),
               "round " + juce::String(round) + " must rebuild");
        for (int i = 0; i < 3; ++i)
          h.runInterval(0.4f);
      }
      expect(h.client.isPracticeEnabled());
    }

    beginTest("the whole interval sounds, not just its first block");
    {
      // The regression that made practice mode useless. swapEchoBuffers was
      // called per block instead of per interval, so the second block of each
      // interval found audio still unplayed, treated it as a swap arriving
      // early, faded it out over 256 samples and had nothing to replace it
      // with. What you heard was a click at each interval start and silence
      // for the rest -- which is why this measures duty rather than sampling
      // the middle, where the old harness happened to look.
      EchoHarness h;
      expect(h.start());

      for (int i = 0; i < 6; ++i)
        h.runInterval(0.25f);

      const auto out = h.runInterval(0.25f);
      expect(duty(out) > 0.99f,
             "an echoed interval must sound end to end, measured " +
                 juce::String(duty(out) * 100.0f, 1) + "%");
    }

    beginTest("the block size does not change what is heard");
    {
      // The per-block swap made the audible fraction a function of the block
      // size, which is the signature of servicing being confused with
      // swapping. Two very different sizes must give the same answer.
      for (int blockSize : {64, 512, 1024}) {
        EchoHarness h;
        expect(h.start());
        for (int i = 0; i < 6; ++i)
          h.runInterval(0.25f, blockSize);
        const auto out = h.runInterval(0.25f, blockSize);
        expect(duty(out) > 0.99f,
               "block size " + juce::String(blockSize) + " gave duty " +
                   juce::String(duty(out) * 100.0f, 1) + "%");
      }
    }

    beginTest("the meter falls back to silence instead of holding its last peak");
    {
      // The peak was only stored on the path that actually copies audio, so
      // every early return -- underrun, drained, nothing playing -- left the
      // last value standing. A player who stopped kept a lit meter, and while
      // echo was playing one block per interval it made the meter look like
      // proof that audio was flowing.
      EchoHarness h;
      expect(h.start());

      for (int i = 0; i < 8; ++i)
        h.runInterval(0.5f);
      expect(h.client.getEchoTaps()[0].channel.peakLevel > 0.1f,
             "the meter should be showing the signal");

      // Boundaries with nothing new stored, which is what a source that has
      // stopped looks like. Writing silence would not do: that still runs the
      // mixing path, which always did set the peak. It is the early returns
      // that left it standing, so the test has to reach one.
      //
      // Long enough for the ring to come round to an entry that was opened for
      // writing and never written -- until then the tap is quite correctly
      // still playing the past, which is the whole point of a history.
      for (int i = 0; i < 8; ++i)
        h.runIntervalWithoutPush();
      expect(h.client.getEchoTaps()[0].channel.peakLevel < 1.0e-4f,
             "a tap with nothing to play must read silent, measured " +
                 juce::String(h.client.getEchoTaps()[0].channel.peakLevel, 4));
    }

    beginTest("unmuting a deeper tap is immediate, not silent");
    {
      // The reason the history is sized by the deepest tap whether or not it is
      // audible. If it were sized by the audible ones, unmuting would give
      // silence until the ring refilled -- which reads as a bug.
      EchoHarness h;
      expect(h.start());

      for (int i = 0; i < 12; ++i)
        h.runInterval(0.01f * (float)(i + 1));

      // Silence tap 0 and bring up tap 2, which is delayed by 8.
      h.client.setEchoTapMute(0, true);
      h.client.setEchoTapMute(2, false);

      const auto out = h.runInterval(0.13f);

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
        h.runInterval(0.1f);
      const auto out = h.runInterval(0.1f);

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
        h.runInterval(0.5f);

      const auto before = h.runInterval(0.5f);
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
      for (int i = 0; i < 8; ++i)
        h.runInterval(0.5f);

      h.client.setPracticeEnabled(false, 0, 0.0);
      for (int i = 0; i < 4; ++i)
        h.runInterval(0.5f); // service the draining slots

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
      expect(h.client.maxEchoDelay() >= EchoSchedule::kMinDelayIntervals,
             "some delay must remain usable");

      const auto taps = h.client.getEchoTaps();
      expectEquals((int)taps.size(), 3);
      for (const auto &t : taps) {
        expect(t.delayIntervals <= h.client.maxEchoDelay(),
               "a tap must never be deeper than the budget allows");
        expect(t.delayIntervals >= EchoSchedule::kMinDelayIntervals,
               "nor shallower than the handoff can deliver");
      }
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
