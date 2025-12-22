#include <JuceHeader.h>

#include "GainUtils.h"
#include "NinjamClient.h"

namespace {

class GainUtilsTests : public juce::UnitTest {
public:
  GainUtilsTests() : juce::UnitTest("GainUtils", "GainUtils") {}

  void runTest() override {
    beginTest("landmark conversions");
    expectWithinAbsoluteError(GainUtils::gainToDb(1.0f), 0.0, 0.01);
    expectWithinAbsoluteError((double)GainUtils::dbToGain(0.0), 1.0, 1.0e-6);
    expectWithinAbsoluteError(GainUtils::gainToDb(0.5f), -6.02, 0.02);
    expectWithinAbsoluteError((double)GainUtils::dbToGain(6.0), 1.995, 0.01);

    beginTest("the remote default is -12 dB");
    // The value that has to match the reference client, expressed the way the
    // fader shows it.
    expectWithinAbsoluteError(
        GainUtils::gainToDb(NinjamClient::kDefaultRemoteChannelVolume), -12.04,
        0.02);

    beginTest("silence maps to the bottom of the fader and back");
    expectEquals(GainUtils::gainToDb(0.0f), GainUtils::kMinDb);
    expectEquals(GainUtils::dbToGain(GainUtils::kMinDb), 0.0f);
    // Anything at or below the floor is silence, not a very small gain.
    expectEquals(GainUtils::dbToGain(GainUtils::kMinDb - 20.0), 0.0f);

    beginTest("round-trips are stable");
    // The UI writes gain from the fader, then reads it back on the next tick.
    // If that round trip drifted, faders would creep on their own.
    for (double db = GainUtils::kMinDb; db <= GainUtils::kMaxDb; db += 0.5) {
      const float gain = GainUtils::dbToGain(db);
      const double back = GainUtils::gainToDb(gain);
      expectWithinAbsoluteError(back, db, 0.01,
                                "round trip drifted at " +
                                    juce::String(db, 1) + " dB");
    }

    beginTest("out-of-range gains are clamped into the fader range");
    expectEquals(GainUtils::gainToDb(100.0f), GainUtils::kMaxDb);
    expect(GainUtils::gainToDb(1.0e-9f) <= GainUtils::kMinDb + 0.001);

    beginTest("dB is monotonic in gain");
    double previous = -1000.0;
    for (double db = GainUtils::kMinDb; db <= GainUtils::kMaxDb; db += 0.25) {
      const double g = (double)GainUtils::dbToGain(db);
      expect(g >= previous, "gain decreased while dB increased");
      previous = g;
    }

    beginTest("formatting");
    expectEquals(GainUtils::formatDb(GainUtils::kMinDb), juce::String("-inf"));
    expectEquals(GainUtils::formatDb(0.0), juce::String("0.0 dB"));
    expectEquals(GainUtils::formatDb(-12.04), juce::String("-12.0 dB"));
  }
};

static GainUtilsTests gainUtilsTests;

} // namespace
