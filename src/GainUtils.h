#pragma once
#include <JuceHeader.h>
#include <vector>

// Level controls are presented in decibels, but every gain in the audio model
// stays a linear multiplier. Only the slider mapping changes, so persisted
// state and the audio path are unaffected.
//
// A fader that is linear in amplitude is a bad control: unity lands at the
// midpoint, the whole of -inf..-12 dB is squeezed into the bottom eighth of
// travel, and half the range is spent above unity. Linear in dB puts the
// useful resolution where the ear is.

namespace GainUtils {

// Bottom of the fader, treated as silence rather than -60 dB.
static constexpr double kMinDb = -60.0;
// A little headroom above unity, matching typical mixer faders.
static constexpr double kMaxDb = 6.0;
static constexpr double kStepDb = 0.1;

inline float dbToGain(double db) {
  return db <= kMinDb ? 0.0f : (float)juce::Decibels::decibelsToGain(db);
}

inline double gainToDb(float gain) {
  if (gain <= 0.0f)
    return kMinDb;
  return juce::jlimit(kMinDb, kMaxDb,
                      (double)juce::Decibels::gainToDecibels(gain, (float)kMinDb));
}

// "-12.0 dB", or "-inf" at the bottom of the range.
inline juce::String formatDb(double db) {
  if (db <= kMinDb)
    return "-inf";
  return juce::String(db, 1) + " dB";
}

// ---------------------------------------------------------------------------
// Meters
// ---------------------------------------------------------------------------
//
// Meters are scaled in dBFS so they agree with the faders. A linear meter is
// close to useless: everything below -20 dBFS lives in the bottom tenth of the
// bar, so ordinary playing barely moves it and quiet signals look like silence.
//
// The meter range is not the fader range. A fader goes to +6 because gain can
// boost; a meter tops out at 0 dBFS, which is full scale, with anything above
// it flagged as over rather than drawn longer.

static constexpr double kMeterMinDb = -60.0;
static constexpr double kMeterMaxDb = 0.0;
// Below this is comfortable, above it is approaching full scale.
static constexpr double kMeterHotDb = -6.0;

// dB of a measured peak, without the fader's upper clamp -- a peak can legally
// exceed 0 dBFS and the meter needs to know.
inline double peakToDb(float peak) {
  if (peak <= 0.0f)
    return kMeterMinDb;
  return (double)juce::Decibels::gainToDecibels(peak, (float)kMeterMinDb);
}

// Fraction of the meter to fill, 0 at kMeterMinDb and 1 at kMeterMaxDb.
inline float meterFraction(float peak) {
  const double db = peakToDb(peak);
  return (float)juce::jlimit(
      0.0, 1.0, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
}

// Peak-hold release rate. Digital peak meters typically fall at 20-25 dB/s
// (IEC PPM types are slower, around 9-12 dB/s, which feels sticky for a jam
// where you want to see transients settle).
//
// Expressed as a rate rather than a per-tick multiplier so it does not silently
// change when the UI timer rate changes or ticks are dropped: the previous
// `peak *= 0.92` was 21.7 dB/s only as long as the timer really ran at 30 Hz,
// and stretched out whenever the editor was busy.
static constexpr double kMeterDecayDbPerSecond = 20.0;

// Applies the release over `secondsElapsed` and lets a new peak through
// instantly -- peak meters attack immediately and only the release is damped.
inline float decayMeterPeak(float currentPeak, float newPeak,
                            double secondsElapsed) {
  if (secondsElapsed <= 0.0)
    return juce::jmax(currentPeak, newPeak);

  const double db = -kMeterDecayDbPerSecond * secondsElapsed;
  // Past the bottom of the meter there is nothing left to show.
  const float factor =
      db <= kMeterMinDb ? 0.0f : (float)juce::Decibels::decibelsToGain(db);
  return juce::jmax(currentPeak * factor, newPeak);
}

// Values printed on the strip scale, top to bottom. The bottom entry is drawn
// as "-inf" rather than "-60".
inline const std::vector<double> &scaleTicksDb() {
  static const std::vector<double> ticks{6.0,   0.0,   -6.0,  -12.0,
                                         -24.0, -40.0, kMinDb};
  return ticks;
}

// Colour band, kept numeric so this header stays free of juce_graphics (the
// test target does not link it).
enum class MeterZone { Normal, Hot, Over };

inline MeterZone meterZone(float peak) {
  const double db = peakToDb(peak);
  if (db >= kMeterMaxDb)
    return MeterZone::Over;
  if (db >= kMeterHotDb)
    return MeterZone::Hot;
  return MeterZone::Normal;
}

// Deliberately no slider or colour helpers here: this header is compiled into
// the test target, which links neither juce_gui_basics nor juce_graphics. Call
// sites use setRange(kMinDb, kMaxDb, kStepDb) and map MeterZone to a colour
// themselves.

} // namespace GainUtils
