#pragma once
#include <JuceHeader.h>

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

// Deliberately no slider helper here: this header is compiled into the test
// target, which does not link juce_gui_basics. Call sites use
// setRange(kMinDb, kMaxDb, kStepDb) directly.

} // namespace GainUtils
