#include <JuceHeader.h>

#include "GainRamp.h"

#include <vector>

namespace {

// The largest jump between consecutive samples of a signal. This is the thing
// that makes a click audible, so it is what the tests measure -- not the gain
// curve itself, which is an implementation detail.
double largestStep(const std::vector<float> &x) {
  double worst = 0.0;
  for (size_t i = 1; i < x.size(); ++i)
    worst = std::max(worst, (double)std::abs(x[i] - x[i - 1]));
  return worst;
}

class GainRampTests : public juce::UnitTest {
public:
  GainRampTests() : juce::UnitTest("GainRamp", "GainRamp") {}

  void runTest() override {
    beginTest("a ramp reaches its target and stays there");
    {
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(0.0f);
      r.setTarget(1.0f);
      for (int i = 0; i < r.rampLengthSamples(); ++i)
        r.next();
      expect(!r.isRamping());
      expectWithinAbsoluteError(r.next(), 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(r.next(), 1.0f, 1.0e-6f);
    }

    beginTest("5 ms at the session rate");
    {
      GainRamp r;
      r.prepare(48000.0);
      expectEquals(r.rampLengthSamples(), 240);
      r.prepare(44100.0);
      expectEquals(r.rampLengthSamples(), 220);
      r.prepare(96000.0);
      expectEquals(r.rampLengthSamples(), 480);
    }

    beginTest("a degenerate sample rate still gives a usable ramp");
    {
      GainRamp r;
      r.prepare(0.0);
      expect(r.rampLengthSamples() >= 1, "must never be zero-length");
      r.jumpTo(0.0f);
      r.setTarget(1.0f);
      r.next();
      expectWithinAbsoluteError(r.current(), 1.0f, 1.0e-6f);
    }

    beginTest("muting a full-scale tone never steps more than a hair");
    {
      // The point of the whole module. A hard mute of a full-scale sine steps
      // by up to 2.0 in one sample; ramped, no single step may be large enough
      // to hear. The bound is generous -- the ramped worst case is around
      // 0.02 -- because what matters is the order of magnitude, not the curve.
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(1.0f);

      std::vector<float> out;
      const double freq = 440.0, sr = 48000.0;
      int n = 0;
      for (; n < 480; ++n)
        out.push_back((float)std::sin(2.0 * 3.14159265358979 * freq * n / sr) *
                      r.next());
      r.setTarget(0.0f);
      for (; n < 1440; ++n)
        out.push_back((float)std::sin(2.0 * 3.14159265358979 * freq * n / sr) *
                      r.next());

      const double stepped = largestStep(out);
      expect(stepped < 0.1, "largest step was " + juce::String(stepped, 4) +
                                " -- that is a click");

      // ...and it really did mute.
      expectWithinAbsoluteError(out.back(), 0.0f, 1.0e-6f);
    }

    beginTest("an unramped mute is measurably worse, which is why this exists");
    {
      // Proves the measurement has teeth: the same signal cut without a ramp
      // must fail the bound the ramped one passes.
      std::vector<float> out;
      const double freq = 440.0, sr = 48000.0;
      for (int n = 0; n < 1440; ++n) {
        const float g = n < 480 ? 1.0f : 0.0f;
        out.push_back((float)std::sin(2.0 * 3.14159265358979 * freq * n / sr) *
                      g);
      }
      expect(largestStep(out) > 0.1, "a hard cut should step hard; measured " +
                                         juce::String(largestStep(out), 4));
    }

    beginTest("the ramp is monotonic and stays within its endpoints");
    {
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(0.0f);
      r.setTarget(1.0f);
      float previous = 0.0f;
      for (int i = 0; i < r.rampLengthSamples(); ++i) {
        const float g = r.next();
        expect(g >= previous - 1.0e-6f, "gain went backwards");
        expect(g >= -1.0e-6f && g <= 1.0f + 1.0e-6f, "gain left its endpoints");
        previous = g;
      }
    }

    beginTest("retargeting mid-ramp starts from where it actually is");
    {
      // Mute then immediately unmute is a real gesture, and it must not jump
      // back to the old start value.
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(1.0f);
      r.setTarget(0.0f);
      for (int i = 0; i < 100; ++i)
        r.next();
      const float mid = r.current();
      expect(mid > 0.0f && mid < 1.0f, "should be part-way down");

      r.setTarget(1.0f);
      expectWithinAbsoluteError(r.next(), mid, 0.05f,
                                "must continue from the current gain");
    }

    beginTest("setting the target it already has does not restart the ramp");
    {
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(1.0f);
      r.setTarget(1.0f);
      expect(!r.isRamping());
    }

    beginTest("jumpTo does not ramp");
    {
      GainRamp r;
      r.prepare(48000.0);
      r.jumpTo(0.0f);
      r.jumpTo(1.0f);
      expect(!r.isRamping());
      expectWithinAbsoluteError(r.current(), 1.0f, 1.0e-6f);
    }

    beginTest("gainAt matches next without moving the ramp");
    {
      // The mix samples gains at scattered offsets in a block and advances once
      // at the end. If these two disagreed, the crossfade region and the main
      // region would apply different gains to adjacent samples -- which is the
      // very discontinuity the ramp exists to remove.
      GainRamp probe, walker;
      probe.prepare(48000.0);
      walker.prepare(48000.0);
      probe.jumpTo(0.0f);
      walker.jumpTo(0.0f);
      probe.setTarget(1.0f);
      walker.setTarget(1.0f);

      for (int k = 0; k < 300; ++k) {
        const float expected = walker.next();
        expectWithinAbsoluteError(probe.gainAt(k), expected, 1.0e-6f,
                                  "offset " + juce::String(k));
      }
      // ...and the probe never moved.
      expectWithinAbsoluteError(probe.current(), 0.0f, 1.0e-6f);
    }

    beginTest("advance skips the ramp forward exactly like next would");
    {
      GainRamp a, b;
      a.prepare(48000.0);
      b.prepare(48000.0);
      a.jumpTo(0.0f);
      b.jumpTo(0.0f);
      a.setTarget(1.0f);
      b.setTarget(1.0f);
      for (int i = 0; i < 137; ++i)
        a.next();
      b.advance(137);
      expectWithinAbsoluteError(b.current(), a.current(), 1.0e-6f);
    }
  }
};

static GainRampTests gainRampTests;

} // namespace
