#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Signal helpers shared by the codec and loopback tests.
//
// Vorbis is lossy and introduces codec delay, so recovered audio can never be
// compared sample-by-sample against what went in. These helpers support the
// comparisons that are actually meaningful: energy (RMS) and pitch (measured by
// hysteresis-gated zero crossings, which tolerates the low-level noise a lossy
// codec leaves around the zero line).

namespace TestSignal {

constexpr double kPi = 3.14159265358979323846;

// Fills an interleaved buffer with a sine on every channel.
inline void fillSine(float *interleaved, int numFrames, int numChannels,
                     double freq, double sampleRate, float amplitude) {
  const double inc = 2.0 * kPi * freq / sampleRate;
  for (int i = 0; i < numFrames; ++i) {
    const float v = amplitude * (float)std::sin(inc * (double)i);
    for (int ch = 0; ch < numChannels; ++ch)
      interleaved[i * numChannels + ch] = v;
  }
}

inline std::vector<float> makeSine(int numFrames, int numChannels, double freq,
                                   double sampleRate, float amplitude) {
  std::vector<float> v((size_t)(numFrames * numChannels));
  fillSine(v.data(), numFrames, numChannels, freq, sampleRate, amplitude);
  return v;
}

inline double rms(const float *data, int numSamples, int stride = 1) {
  if (numSamples <= 0)
    return 0.0;
  double sum = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    const double v = data[i * stride];
    sum += v * v;
  }
  return std::sqrt(sum / (double)numSamples);
}

inline double peak(const float *data, int numSamples, int stride = 1) {
  double p = 0.0;
  for (int i = 0; i < numSamples; ++i)
    p = std::max(p, (double)std::fabs(data[i * stride]));
  return p;
}

inline double toDb(double linear) {
  return 20.0 * std::log10(std::max(linear, 1e-12));
}

// Estimates the dominant frequency by counting zero crossings, gated by a
// hysteresis band at +/- 20% of peak so that codec noise near zero does not
// register as extra crossings.
inline double dominantFrequency(const float *data, int numSamples,
                                double sampleRate, int stride = 1) {
  if (numSamples < 2 || sampleRate <= 0.0)
    return 0.0;

  const double threshold = 0.2 * peak(data, numSamples, stride);
  if (threshold <= 0.0)
    return 0.0;

  int crossings = 0;
  int state = 0; // -1 below -threshold, +1 above +threshold, 0 undecided
  int firstCrossing = -1, lastCrossing = -1;

  for (int i = 0; i < numSamples; ++i) {
    const double v = data[i * stride];
    int newState = state;
    if (v > threshold)
      newState = 1;
    else if (v < -threshold)
      newState = -1;

    if (state != 0 && newState != state) {
      ++crossings;
      if (firstCrossing < 0)
        firstCrossing = i;
      lastCrossing = i;
    }
    state = newState;
  }

  if (crossings < 2 || lastCrossing <= firstCrossing)
    return 0.0;

  // (crossings - 1) half-cycles span the time between first and last crossing.
  const double seconds = (double)(lastCrossing - firstCrossing) / sampleRate;
  return (double)(crossings - 1) / (2.0 * seconds);
}

// Longest run of exactly-zero samples strictly inside the buffer. Used to catch
// dropouts that a plain RMS check would average away.
inline int longestZeroRun(const float *data, int numSamples, int stride = 1) {
  int best = 0, run = 0;
  for (int i = 0; i < numSamples; ++i) {
    if (data[i * stride] == 0.0f) {
      ++run;
      best = std::max(best, run);
    } else {
      run = 0;
    }
  }
  return best;
}

} // namespace TestSignal
