#include <JuceHeader.h>

#include "ChannelMix.h"

namespace {

class ChannelMixTests : public juce::UnitTest {
public:
  ChannelMixTests() : juce::UnitTest("ChannelMix", "ChannelMix") {}

  void runTest() override {
    // Distinguishable constants, so "took the left channel" and "summed both"
    // cannot produce the same number.
    const float L = 0.8f, R = 0.2f;
    const float expectedSum = 0.5f * (L + R); // 0.5
    std::vector<float> left(64, L), right(64, R);

    beginTest("mono sums both sides rather than discarding one");
    {
      const auto f =
          ChannelMix::sourceFrame(left.data(), right.data(), true, 0);
      expectWithinAbsoluteError(f.left, expectedSum, 1.0e-6f);
      expectWithinAbsoluteError(f.right, expectedSum, 1.0e-6f);
      // The bug this replaced: mono selected the left channel, throwing the
      // right half of a stereo source away. If that ever comes back, the two
      // assertions above still pass for a source where L == R, so pin it
      // against a source where they differ.
      expect(std::abs(f.left - L) > 0.01f,
             "mono must not simply be the left channel");
      expect(std::abs(f.right - R) > 0.01f,
             "mono must not simply be the right channel");
    }

    beginTest("stereo passes both sides through untouched");
    {
      const auto f =
          ChannelMix::sourceFrame(left.data(), right.data(), false, 0);
      expectWithinAbsoluteError(f.left, L, 1.0e-6f);
      expectWithinAbsoluteError(f.right, R, 1.0e-6f);
    }

    beginTest("a mono bus feeds both sides from its single channel");
    {
      // srcR is null when the assigned input bus has one channel. The mono flag
      // must not change the result: there is nothing to sum with.
      for (bool mono : {false, true}) {
        const auto f = ChannelMix::sourceFrame(left.data(), nullptr, mono, 0);
        expectWithinAbsoluteError(f.left, L, 1.0e-6f);
        expectWithinAbsoluteError(f.right, L, 1.0e-6f);
      }
    }

    beginTest("a missing source is silence, not a read of null");
    {
      const auto f = ChannelMix::sourceFrame(nullptr, nullptr, false, 0);
      expectEquals(f.left, 0.0f);
      expectEquals(f.right, 0.0f);
    }

    beginTest("summing a correlated source holds its level");
    {
      // Averaging rather than adding: an identical signal on both sides must
      // come out at its original amplitude, not doubled into clipping.
      std::vector<float> same(16, 0.9f);
      const auto f = ChannelMix::sourceFrame(same.data(), same.data(), true, 0);
      expectWithinAbsoluteError(f.left, 0.9f, 1.0e-6f);
    }

    beginTest("peaks are measured after mono summing");
    {
      // The meter bug: peaks were taken from the raw input and ignored mono, so
      // a mono channel displayed an independent stereo pair while transmitting
      // a single summed signal.
      const auto p = ChannelMix::peaks(left.data(), right.data(), true, 0, 64,
                                       {1.0f, 1.0f});
      expectWithinAbsoluteError(p.left, expectedSum, 1.0e-6f);
      expectWithinAbsoluteError(p.right, expectedSum, 1.0e-6f);
      expect(std::abs(p.right - R) > 0.01f,
             "a mono channel must not meter the raw right input");

      const auto ps = ChannelMix::peaks(left.data(), right.data(), false, 0, 64,
                                        {1.0f, 1.0f});
      expectWithinAbsoluteError(ps.left, L, 1.0e-6f);
      expectWithinAbsoluteError(ps.right, R, 1.0e-6f);
    }

    beginTest("peaks find the loudest frame and honour gain");
    {
      std::vector<float> ramp(32, 0.1f);
      ramp[17] = -0.75f; // negative, to prove the peak is on magnitude
      const auto p =
          ChannelMix::peaks(ramp.data(), nullptr, false, 0, 32, {0.5f, 0.5f});
      expectWithinAbsoluteError(p.left, 0.375f, 1.0e-6f);
    }

    beginTest("pan gains hold the centre and reach full on one side");
    {
      const auto c = ChannelMix::panGains(1.0f, 0.0f);
      expectWithinAbsoluteError(c.left, 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(c.right, 1.0f, 1.0e-6f);
      const auto hardLeft = ChannelMix::panGains(1.0f, -1.0f);
      expectWithinAbsoluteError(hardLeft.left, 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(hardLeft.right, 0.0f, 1.0e-6f);
      const auto hardRight = ChannelMix::panGains(1.0f, 1.0f);
      expectWithinAbsoluteError(hardRight.left, 0.0f, 1.0e-6f);
      expectWithinAbsoluteError(hardRight.right, 1.0f, 1.0e-6f);
    }

    beginTest("write fills a destination segment with gained frames");
    {
      std::vector<float> dl(8, -1.0f), dr(8, -1.0f);
      ChannelMix::write(dl.data(), dr.data(), left.data(), right.data(), true,
                        4, 8, {2.0f, 0.5f});
      for (int i = 0; i < 8; ++i) {
        expectWithinAbsoluteError(dl[(size_t)i], expectedSum * 2.0f, 1.0e-6f);
        expectWithinAbsoluteError(dr[(size_t)i], expectedSum * 0.5f, 1.0e-6f);
      }
    }

    beginTest("write reads from the requested source offset");
    {
      // The transmit ring is written in up to two segments, so the source
      // offset has to be honoured or the second segment repeats the first.
      std::vector<float> ramp(16);
      for (int i = 0; i < 16; ++i)
        ramp[(size_t)i] = (float)i;
      std::vector<float> dst(4, 0.0f);
      ChannelMix::write(dst.data(), nullptr, ramp.data(), nullptr, false, 12, 4,
                        {1.0f, 1.0f});
      expectWithinAbsoluteError(dst[0], 12.0f, 1.0e-6f);
      expectWithinAbsoluteError(dst[3], 15.0f, 1.0e-6f);
    }

    beginTest("addInto accumulates rather than overwriting");
    {
      std::vector<float> dl(4, 0.25f), dr(4, 0.25f);
      ChannelMix::addInto(dl.data(), dr.data(), left.data(), right.data(),
                          false, 4, {1.0f, 1.0f});
      for (int i = 0; i < 4; ++i) {
        expectWithinAbsoluteError(dl[(size_t)i], 0.25f + L, 1.0e-6f);
        expectWithinAbsoluteError(dr[(size_t)i], 0.25f + R, 1.0e-6f);
      }
    }
  }
};

static ChannelMixTests channelMixTests;

} // namespace
