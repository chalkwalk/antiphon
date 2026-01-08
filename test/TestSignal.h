#pragma once

#include "IntervalProbe.h"

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
// hysteresis band so codec noise near zero does not register as extra
// crossings.
//
// The gate is derived from RMS, not peak: a signal carrying a transient (such
// as the interval-marker impulse in the interop tests) has a peak far above
// the steady tone, and a peak-relative gate then sits above the tone entirely
// and counts almost nothing, reporting a confidently wrong frequency.
inline double dominantFrequency(const float *data, int numSamples,
                                double sampleRate, int stride = 1) {
  if (numSamples < 2 || sampleRate <= 0.0)
    return 0.0;

  const double threshold = 0.5 * rms(data, numSamples, stride);
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

// ---------------------------------------------------------------------------
// Interval timing probe
// ---------------------------------------------------------------------------
//
// Timing is the thing Ninjam gets wrong most visibly, so it needs a marker
// that survives the round trip. A single-sample impulse does not: Vorbis
// smears a lone broadband click, and its amplitude relative to the tone is not
// preserved, which makes threshold detection unreliable.
//
// Instead the interval is marked with SHORT BURSTS of a distinct high
// frequency at known fractional positions. A few milliseconds of a pure tone
// is exactly what a perceptual codec preserves well, and it can be located by
// energy in a narrow band rather than by absolute amplitude. Placing bursts at
// several positions (not just sample 0) catches errors that a single marker at
// the boundary cannot -- a stretched or shifted interval moves the later
// bursts more than the first.

using IntervalProbe = ninjam::IntervalProbe;

// Energy of `data` in a narrow band around `hz`, computed by a single-bin
// Goertzel. Used to find the probe bursts without an FFT dependency.
inline double bandEnergy(const float *data, int numSamples, double hz,
                         double sampleRate, int stride = 1) {
  if (numSamples <= 0 || sampleRate <= 0.0)
    return 0.0;
  const double w = 2.0 * kPi * hz / sampleRate;
  const double coeff = 2.0 * std::cos(w);
  double s0 = 0.0, s1 = 0.0, s2 = 0.0;
  for (int i = 0; i < numSamples; ++i) {
    s0 = (double)data[i * stride] + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2) / (double)numSamples;
}

// Positions where burst energy peaks, found by sliding a window of the burst
// length. Returns the start sample of each detected burst.
inline std::vector<int> findBursts(const float *data, int numSamples,
                                   double burstHz, double burstSeconds,
                                   double sampleRate, int stride = 1) {
  const int win = std::max(8, (int)(burstSeconds * sampleRate));
  const int hop = std::max(1, win / 4);

  std::vector<double> energy;
  std::vector<int> at;
  for (int i = 0; i + win <= numSamples; i += hop) {
    energy.push_back(bandEnergy(data + (size_t)i * stride, win, burstHz,
                                sampleRate, stride));
    at.push_back(i);
  }
  if (energy.empty())
    return {};

  double maxE = 0.0;
  for (double e : energy)
    maxE = std::max(maxE, e);
  if (maxE <= 0.0)
    return {};

  // Peak-pick above half the maximum, merging adjacent windows.
  std::vector<int> out;
  const double gate = 0.5 * maxE;
  int bestIdx = -1;
  for (size_t i = 0; i < energy.size(); ++i) {
    if (energy[i] >= gate) {
      if (bestIdx < 0 || energy[i] > energy[(size_t)bestIdx])
        bestIdx = (int)i;
    } else if (bestIdx >= 0) {
      out.push_back(at[(size_t)bestIdx]);
      bestIdx = -1;
    }
  }
  if (bestIdx >= 0)
    out.push_back(at[(size_t)bestIdx]);
  return out;
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
