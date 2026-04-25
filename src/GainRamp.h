#pragma once

// A gain that moves to its target over a few milliseconds instead of jumping.
//
// Every gain change in this project used to be a step applied at a block
// boundary: mute, solo, the transmit gate, and a fader drag. A step in gain is
// a discontinuity in the signal, and a discontinuity is a click -- on the
// transmit path it is a click baked into what everyone else hears.
//
// Deliberately not zero-crossing detection, which is the intuitive answer and
// the wrong one:
//   - left and right cross at different times, so the two sides would switch at
//     different moments and the stereo image would lurch;
//   - silence and DC never cross at all, so the switch could wait forever --
//     exactly when you most want mute to be immediate;
//   - low frequencies cross rarely: 10 ms at 50 Hz, and worse below;
//   - and a crossing only removes the discontinuity in value, not in slope,
//     which is still audible for anything but a pure sine.
//
// A short ramp has none of those problems and is what mixing consoles do.
//
// JUCE-free and header-only so it can be unit-tested headlessly, and safe on
// the audio thread: no allocation, no locks, no libm.

#include <cmath>

class GainRamp {
public:
  // Long enough that nothing clicks, short enough to feel instant.
  static constexpr double kDefaultRampSeconds = 0.005; // 5 ms

  void prepare(double sampleRate, double rampSeconds = kDefaultRampSeconds) {
    lengthSamples = sampleRate > 0.0 ? (int)(sampleRate * rampSeconds) : 0;
    if (lengthSamples < 1)
      lengthSamples = 1;
    position = lengthSamples; // not ramping
  }

  // Jump with no ramp. For initialisation and for resets, never for a change
  // the user can hear.
  void jumpTo(float value) {
    startValue = targetValue = currentValue = value;
    position = lengthSamples;
  }

  void setTarget(float value) {
    if (value == targetValue)
      return;
    startValue = currentValue;
    targetValue = value;
    position = 0;
  }

  bool isRamping() const { return position < lengthSamples; }
  float target() const { return targetValue; }
  float current() const { return currentValue; }
  int rampLengthSamples() const { return lengthSamples; }

  // One sample of gain, advancing the ramp.
  float next() {
    if (position >= lengthSamples) {
      currentValue = targetValue;
      return currentValue;
    }
    ++position;
    const float t = (float)position / (float)lengthSamples;
    // Smoothstep: continuous in value and in slope at both ends, which is what
    // stops the click. A raised cosine has the same property; this one costs no
    // libm call per sample.
    const float w = t * t * (3.0f - 2.0f * t);
    currentValue = startValue + (targetValue - startValue) * w;
    return currentValue;
  }

  // The gain this ramp will have `offset` samples from now, without moving it.
  //
  // The mix reads gains at scattered offsets within a block -- the crossfade
  // region and the main region are separate passes over the same block -- and
  // then advances the ramp once for the whole block. Sampling without mutating
  // is what lets those two agree, and what keeps the ramp in step with the
  // clock even on the paths that mix nothing at all.
  //
  // gainAt(k) equals the value of the (k+1)-th call to next().
  float gainAt(int offset) const {
    const int p = position + offset + 1;
    if (p >= lengthSamples)
      return targetValue;
    const float t = (float)p / (float)lengthSamples;
    const float w = t * t * (3.0f - 2.0f * t);
    return startValue + (targetValue - startValue) * w;
  }

  // Skips ahead when the caller knows the gain is not needed per sample -- a
  // block that is entirely at the target, for instance.
  void advance(int numSamples) {
    for (int i = 0; i < numSamples && isRamping(); ++i)
      next();
    if (!isRamping())
      currentValue = targetValue;
  }

private:
  float startValue = 0.0f;
  float targetValue = 0.0f;
  float currentValue = 0.0f;
  int lengthSamples = 1;
  int position = 1;
};
