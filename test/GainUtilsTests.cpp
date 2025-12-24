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

    beginTest("meter scale endpoints");
    expectWithinAbsoluteError((double)GainUtils::meterFraction(1.0f), 1.0,
                              0.001, "0 dBFS should fill the meter");
    expectEquals(GainUtils::meterFraction(0.0f), 0.0f,
                 "silence should show nothing");
    // Half amplitude is -6 dBFS, which on a -60..0 scale is 90% of the way up.
    expectWithinAbsoluteError((double)GainUtils::meterFraction(0.5f), 0.9,
                              0.005);

    beginTest("meter gives quiet signals visible travel");
    {
      // The point of the change: on the old linear meter, -40 dBFS filled 1%
      // of the bar and was indistinguishable from silence. On a dB scale it
      // sits a third of the way up.
      const float quiet = GainUtils::dbToGain(-40.0);
      const float frac = GainUtils::meterFraction(quiet);
      expectWithinAbsoluteError((double)frac, 1.0 / 3.0, 0.01);
      expect(frac > (double)quiet * 10.0,
             "dB meter should give a quiet signal far more travel than linear");
    }

    beginTest("meter is monotonic and clamped");
    {
      float previous = -1.0f;
      for (double db = -80.0; db <= 12.0; db += 0.5) {
        const float f = GainUtils::meterFraction(GainUtils::dbToGain(db));
        expect(f >= previous, "meter fraction decreased as level rose");
        expect(f >= 0.0f && f <= 1.0f, "meter fraction left 0..1");
        previous = f;
      }
      // Above full scale the meter pins rather than overflowing.
      expectEquals(GainUtils::meterFraction(4.0f), 1.0f);
    }

    beginTest("meter zones");
    expect(GainUtils::meterZone(GainUtils::dbToGain(-20.0)) ==
           GainUtils::MeterZone::Normal);
    expect(GainUtils::meterZone(GainUtils::dbToGain(-3.0)) ==
           GainUtils::MeterZone::Hot);
    expect(GainUtils::meterZone(1.0f) == GainUtils::MeterZone::Over);
    expect(GainUtils::meterZone(2.0f) == GainUtils::MeterZone::Over);
    expect(GainUtils::meterZone(0.0f) == GainUtils::MeterZone::Normal);

    beginTest("formatting");
    expectEquals(GainUtils::formatDb(GainUtils::kMinDb), juce::String("-inf"));
    expectEquals(GainUtils::formatDb(0.0), juce::String("0.0 dB"));
    expectEquals(GainUtils::formatDb(-12.04), juce::String("-12.0 dB"));
  }
};

static GainUtilsTests gainUtilsTests;

} // namespace
