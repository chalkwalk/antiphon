#include <JuceHeader.h>

#include "GainUtils.h"
#include "NinjamClient.h"

namespace {

class GainUtilsTests : public juce::UnitTest {
public:
  GainUtilsTests() : juce::UnitTest("GainUtils", "GainUtils") {}

  void runTest() override {
    beginTest("a meter only redraws when it would land on different pixels");
    {
      // 2% of the bar. On a 200 px meter that is 4 px; below it the repaint
      // would produce an identical image, which at 30 Hz across every strip is
      // most of what the UI was doing.
      expect(!GainUtils::meterNeedsRepaint(0.50f, 0.505f), "0.5% is invisible");
      expect(!GainUtils::meterNeedsRepaint(0.50f, 0.51f), "1% is invisible");
      expect(GainUtils::meterNeedsRepaint(0.50f, 0.53f), "3% shows");
      expect(GainUtils::meterNeedsRepaint(0.50f, 0.40f), "falling shows");

      // Silence is the one exception, and only the final sub-threshold step of
      // a decay can reach it -- any larger fall already passed the gate.
      expect(GainUtils::meterNeedsRepaint(0.01f, 0.0f),
             "the last step to silence redraws even though it is under 2%");
      expect(!GainUtils::meterNeedsRepaint(0.0f, 0.0f),
             "staying silent does not");

      // The exception must not become a general "silence always redraws" rule
      // applied at any magnitude: a big fall to zero is the ordinary gate
      // doing its job, not the exception.
      expect(GainUtils::meterNeedsRepaint(0.80f, 0.0f),
             "a large fall to silence redraws via the normal threshold");

      // And it is strictly one-directional: rising off zero by less than the
      // threshold is still invisible, so it is still gated.
      expect(!GainUtils::meterNeedsRepaint(0.0f, 0.005f),
             "a sub-threshold rise from silence stays gated");

      // The initial sentinel must draw, or a strip starts blank until the
      // signal happens to jump 2%.
      expect(GainUtils::meterNeedsRepaint(-1.0f, 0.0f) ||
                 GainUtils::meterNeedsRepaint(-1.0f, 0.001f),
             "the first update always draws");
    }

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

    beginTest("meter release runs at the stated dB per second");
    {
      // One second of release from full scale must land exactly
      // kMeterDecayDbPerSecond below it.
      const float after = GainUtils::decayMeterPeak(1.0f, 0.0f, 1.0);
      expectWithinAbsoluteError(GainUtils::peakToDb(after),
                                -GainUtils::kMeterDecayDbPerSecond, 0.01);

      const float half = GainUtils::decayMeterPeak(1.0f, 0.0f, 0.5);
      expectWithinAbsoluteError(GainUtils::peakToDb(half),
                                -GainUtils::kMeterDecayDbPerSecond / 2.0, 0.01);
    }

    beginTest("meter release is frame-rate independent");
    {
      // The reason this is a rate and not a per-tick multiplier: the old
      // `peak *= 0.92` only meant 21.7 dB/s while the timer really ran at
      // 30 Hz, and stretched out whenever ticks were dropped. Applying the
      // same total time in different numbers of steps must now agree.
      const double total = 0.5;
      const float oneStep = GainUtils::decayMeterPeak(1.0f, 0.0f, total);

      for (int steps : {2, 15, 30, 60, 200}) {
        float p = 1.0f;
        for (int i = 0; i < steps; ++i)
          p = GainUtils::decayMeterPeak(p, 0.0f, total / (double)steps);
        expectWithinAbsoluteError(
            GainUtils::peakToDb(p), GainUtils::peakToDb(oneStep), 0.05,
            "release differed when applied in " + juce::String(steps) +
                " steps");
      }
    }

    beginTest("meter attack is instant");
    {
      // Peak meters must catch a transient on the tick it arrives, however
      // long the gap was.
      expectEquals(GainUtils::decayMeterPeak(0.0f, 0.8f, 0.033), 0.8f);
      expectEquals(GainUtils::decayMeterPeak(0.1f, 0.9f, 1.0), 0.9f);
      // A quieter new peak must not raise a still-falling meter.
      const float held = GainUtils::decayMeterPeak(1.0f, 0.01f, 0.01);
      expect(held > 0.01f, "release should hold above a quieter new peak");
    }

    beginTest("meter release reaches silence and stays there");
    {
      // Crossing the bottom of the scale should read as nothing, not a tiny
      // residue that keeps the meter faintly lit forever.
      const double secondsToFall =
          -GainUtils::kMeterMinDb / GainUtils::kMeterDecayDbPerSecond;
      expectEquals(GainUtils::decayMeterPeak(1.0f, 0.0f, secondsToFall + 0.1),
                   0.0f);
      expectEquals(GainUtils::decayMeterPeak(0.0f, 0.0f, 0.033), 0.0f);
    }

    beginTest("formatting");
    expectEquals(GainUtils::formatDb(GainUtils::kMinDb), juce::String("-inf"));
    expectEquals(GainUtils::formatDb(0.0), juce::String("0.0 dB"));
    expectEquals(GainUtils::formatDb(-12.04), juce::String("-12.0 dB"));
  }
};

static GainUtilsTests gainUtilsTests;

} // namespace
