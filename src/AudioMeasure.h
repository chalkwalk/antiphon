#pragma once

#include <ebur128.h>

#include <algorithm>
#include <cmath>
#include <vector>

// The instruments the band is measured with.
//
// This file exists because of a rule and a scar. The rule is `PRINCIPLES §5`:
// a number needs a method, and the method needs to be calibrated. The scar is
// that three of this project's "bugs" turned out to be measurement error, and
// one of them was a pitch detector living in a test file's anonymous namespace
// where nothing could check it against a signal of known pitch.
//
// So the detectors live here, they are tested against synthetic signals whose
// answers are known in advance, and everything that needs a number -- the unit
// tests, and the voice lab used to tune the synthesis by ear -- asks the same
// code. A tuning session and a test threshold that disagree about what "bright"
// means would be worse than having neither (`PRINCIPLES §8`).
//
// JUCE-free and allocation-light, so it compiles into the headless test target
// and into a console tool without dragging anything behind it.

namespace AudioMeasure {

inline constexpr double kPi = 3.14159265358979323846;

inline float peak(const float *data, int numSamples) {
  if (data == nullptr || numSamples <= 0)
    return 0.0f;
  float p = 0.0f;
  for (int i = 0; i < numSamples; ++i)
    p = std::max(p, std::abs(data[i]));
  return p;
}

inline float rms(const float *data, int numSamples) {
  if (data == nullptr || numSamples <= 0)
    return 0.0f;
  double sum = 0.0;
  for (int i = 0; i < numSamples; ++i)
    sum += (double)data[i] * (double)data[i];
  return (float)std::sqrt(sum / (double)numSamples);
}

// Peak over RMS: how spiky a signal is, independent of how loud it is.
//
// A pure sine is 1.414 and a decaying sine is much higher. It is the number
// that says whether a drum has a transient or is merely a tone with an
// envelope on it, which is why the kick is measured with it.
inline float crest(const float *data, int numSamples) {
  const float level = rms(data, numSamples);
  if (level <= 0.0f)
    return 0.0f;
  return peak(data, numSamples) / level;
}

inline double toDb(double linear) {
  return 20.0 * std::log10(std::max(linear, 1e-12));
}

// Where the energy sits, as a single frequency: an energy-weighted mean, not a
// pitch.
//
// Derived from the signal's own slope rather than from a spectrum. For any
// waveform, the ratio of the derivative's RMS to the signal's RMS is 2*pi times
// the energy-weighted mean frequency; discretely, the first difference of a
// sine has a gain of 2*sin(pi*f/fs), so inverting that sine makes this exact
// for a pure tone at any frequency below Nyquist and monotonic in brightness
// for everything else. No FFT, no window, no allocation, one pass.
//
// It exists to be a SECOND opinion. `crossingRateHz` below is threshold-based
// and was once fooled by an asymmetric waveform into reading three semitones
// flat; this is fooled by different things, which is the whole point of having
// both (`PRINCIPLES §5`).
inline double brightnessHz(const float *data, int numSamples,
                           double sampleRate) {
  if (data == nullptr || numSamples < 2 || sampleRate <= 0.0)
    return 0.0;

  double mean = 0.0;
  for (int i = 0; i < numSamples; ++i)
    mean += (double)data[i];
  mean /= (double)numSamples;

  double signalEnergy = 0.0, slopeEnergy = 0.0;
  double previous = (double)data[0] - mean;
  signalEnergy += previous * previous;
  for (int i = 1; i < numSamples; ++i) {
    const double x = (double)data[i] - mean;
    const double d = x - previous;
    signalEnergy += x * x;
    slopeEnergy += d * d;
    previous = x;
  }
  if (signalEnergy <= 0.0)
    return 0.0;

  const double ratio = std::sqrt(slopeEnergy / signalEnergy);
  const double argument = std::min(1.0, ratio / 2.0);
  return sampleRate * std::asin(argument) / kPi;
}

// Zero crossings per second, halved: crude, and kept because it answers "is
// this an octave apart" cheaply.
//
// Not a pitch tracker and not to be used as one. It counts crossings caused by
// any harmonic, and it is the instrument that read a B2 bass as three semitones
// flat because a strong second harmonic made the waveform asymmetric.
inline double crossingRateHz(const float *data, int numSamples,
                             double sampleRate) {
  if (data == nullptr || numSamples < 2 || sampleRate <= 0.0)
    return 0.0;
  int crossings = 0;
  for (int i = 1; i < numSamples; ++i)
    if ((data[i - 1] <= 0.0f) != (data[i] <= 0.0f))
      ++crossings;
  return 0.5 * (double)crossings * sampleRate / (double)numSamples;
}

// The true peak between samples, by fitting a parabola through the correlation
// either side of the best lag.
//
// Without this the finest answer available is `sampleRate / lag` for an integer
// lag, and lags get short as pitch rises: at 660 Hz and 48 kHz the period is
// 72.7 samples and the two nearest answers are 666.7 and 657.5 Hz, so the
// instrument cannot resolve better than about 1.4% however good the signal is.
// That is coarse enough to hide a real half-sample tuning error in a string,
// which is exactly what it did hide.
template <typename ScoreFn>
inline double refinedHz(ScoreFn scoreAt, int lag, int minLag, int maxLag,
                        double sampleRate) {
  if (lag <= minLag || lag >= maxLag)
    return sampleRate / (double)lag;

  const double before = scoreAt(lag - 1);
  const double here = scoreAt(lag);
  const double after = scoreAt(lag + 1);

  const double denom = before - 2.0 * here + after;
  if (denom == 0.0)
    return sampleRate / (double)lag;

  double offset = 0.5 * (before - after) / denom;
  // A parabola through three points near a broad maximum can suggest a vertex
  // some way off; beyond half a sample it is extrapolating rather than
  // refining, so it is clamped to the interval it was fitted over.
  if (offset > 0.5)
    offset = 0.5;
  if (offset < -0.5)
    offset = -0.5;

  return sampleRate / ((double)lag + offset);
}

// The fundamental, by normalised autocorrelation. Returns 0 when it is not
// confident rather than guessing.
inline double fundamentalHz(const float *data, int numSamples,
                            double sampleRate, double lowHz = 40.0,
                            double highHz = 500.0) {
  if (data == nullptr || numSamples < 64 || sampleRate <= 0.0)
    return 0.0;

  // Mean removal, so a DC offset cannot dominate the correlation.
  double mean = 0.0;
  for (int i = 0; i < numSamples; ++i)
    mean += (double)data[i];
  mean /= (double)numSamples;

  std::vector<double> x((size_t)numSamples);
  for (int i = 0; i < numSamples; ++i)
    x[(size_t)i] = (double)data[i] - mean;

  double energy = 0.0;
  for (double v : x)
    energy += v * v;
  if (energy <= 0.0)
    return 0.0;

  const int minLag = std::max(2, (int)(sampleRate / highHz));
  int maxLag = std::min(numSamples / 2, (int)(sampleRate / lowHz));
  if (maxLag <= minLag)
    return 0.0;

  // Only as much signal as the question needs.
  //
  // This is O(samples x lags), so measuring half a second at 96 kHz costs a
  // hundred million multiply-adds per call -- and it buys nothing, because a
  // correlation is settled by a handful of periods of the lowest candidate.
  // Six of them is generous. Without this bound the string tests alone took
  // 43 seconds and pushed the whole suite past its ctest timeout.
  const int enough = 6 * maxLag;
  if (numSamples > enough) {
    numSamples = enough;
    maxLag = std::min(numSamples / 2, maxLag);
    if (maxLag <= minLag)
      return 0.0;
  }

  auto scoreAt = [&](int lag) {
    double sum = 0.0, normA = 0.0, normB = 0.0;
    for (int i = 0; i + lag < numSamples; ++i) {
      sum += x[(size_t)i] * x[(size_t)(i + lag)];
      normA += x[(size_t)i] * x[(size_t)i];
      normB += x[(size_t)(i + lag)] * x[(size_t)(i + lag)];
    }
    const double denom = std::sqrt(normA * normB);
    return denom > 0.0 ? sum / denom : 0.0;
  };

  std::vector<double> scores((size_t)(maxLag - minLag + 1), 0.0);
  double bestScore = 0.0;
  for (int lag = minLag; lag <= maxLag; ++lag) {
    const double score = scoreAt(lag);
    scores[(size_t)(lag - minLag)] = score;
    bestScore = std::max(bestScore, score);
  }

  if (bestScore < 0.3)
    return 0.0;

  // The SHORTEST period that explains the signal as well as the best one does,
  // chosen among the correlation's PEAKS.
  //
  // Taking the global maximum is wrong whenever a whole multiple of the period
  // also fits the analysis window, because every multiple of a periodic
  // signal's period correlates just as well and which one wins is then decided
  // by floating-point noise. Calibrating against a 440 Hz sine found exactly
  // that: a quarter-second window is 11 periods to the sample, lag 1200 tied
  // with lag 109, and the detector reported 40 Hz with complete confidence.
  // Nothing in the band had shown it, because the bass sits at 60-140 Hz where
  // the longest lag considered is under three periods and the tie cannot
  // happen.
  //
  // It has to be peaks rather than lags, and that is the second thing
  // calibration caught: the correlation is broad around each peak, so the
  // first lag scoring within a couple of percent of the best sits several
  // samples BEFORE the true period, and every reading came out about 3% sharp.
  int bestLag = minLag;
  double globalBest = -1.0;
  for (int lag = minLag; lag <= maxLag; ++lag)
    if (scores[(size_t)(lag - minLag)] > globalBest) {
      globalBest = scores[(size_t)(lag - minLag)];
      bestLag = lag;
    }

  for (int lag = minLag + 1; lag < maxLag; ++lag) {
    const double here = scores[(size_t)(lag - minLag)];
    const bool isPeak = here >= scores[(size_t)(lag - minLag - 1)] &&
                        here > scores[(size_t)(lag - minLag + 1)];
    if (isPeak && here >= 0.98 * bestScore) {
      bestLag = lag;
      break;
    }
  }

  // Then reject subharmonics, but only at INTEGER divisions.
  //
  // A period of 3T correlates about as well as T, so a tie looser than the 2%
  // above still has to be caught -- which is how this instrument once claimed a
  // B2 bass was sounding at 41 Hz, convincingly enough to look like a bug in
  // the synthesis. Scanning for any shorter lag that scores nearly as well
  // overcorrects the other way and lands between semitones, so only bestLag/2,
  // /3, /4... are considered.
  for (int divisor = 8; divisor >= 2; --divisor) {
    const int lag = bestLag / divisor;
    if (lag < minLag)
      continue;
    if (scoreAt(lag) >= 0.85 * bestScore)
      return refinedHz(scoreAt, lag, minLag, maxLag, sampleRate);
  }
  return refinedHz(scoreAt, bestLag, minLag, maxLag, sampleRate);
}

// The pitch of the first note in a buffer, wherever it starts.
//
// Finding the onset matters: a figure's rotation can move the first note off
// the downbeat, and measuring a fixed window from the start then reads silence
// and reports nothing. `windowSamples` bounds the analysis so it does not run
// into the note after.
inline double firstNoteHz(const float *data, int numSamples, double sampleRate,
                          int windowSamples, double lowHz = 40.0,
                          double highHz = 500.0) {
  if (data == nullptr || numSamples <= 0)
    return 0.0;

  const float loudest = peak(data, numSamples);
  if (loudest <= 0.0f)
    return 0.0;

  int onset = 0;
  while (onset < numSamples && std::abs(data[onset]) < 0.2f * loudest)
    ++onset;
  if (onset >= numSamples)
    return 0.0;

  const int span = std::min(windowSamples, numSamples - onset);
  return fundamentalHz(data + onset, span, sampleRate, lowHz, highHz);
}

// Loudness, as ITU-R BS.1770 / EBU R128 hears it.
//
// RMS is not loudness, and the difference matters most for exactly the
// comparison the band needs: a kick and a hi-hat at the same RMS are nowhere
// near the same loudness, because the ear is far less sensitive at 50 Hz than
// at 8 kHz. Balancing a band by RMS therefore flatters whatever is lowest, and
// the drums were the thing being balanced.
//
// MEASURED BY libebur128 (MIT), not here. There was a K-weighting pair and a
// two-stage gate in this file, and they were correct -- validated against
// ffmpeg's ebur128 to inside 0.05 LU on all five cases below, which is why the
// swap could be checked rather than trusted. They were deleted anyway, on the
// standing rule: take the dependency when the thing has a
// SPECIFICATION you could fail to meet.
//
// The failure being avoided is not today's. It is the momentary and
// short-term measures, the loudness range, the true peak, and whatever the
// next revision of BS.1770 says -- each of which is a further piece of a
// standard to track by hand, each correct only until it silently is not.
// Being right once is not the same as staying right, and a reimplementation
// gives you no way to tell the difference.
//
// What is kept is the interface. `integratedLufs` still takes two channel
// pointers and a sample rate and returns LUFS, so that peak, rms, crest,
// brightness, pitch and loudness continue to come from ONE place -- which is
// the whole argument for this header, and the one shim the dependency rule
// defends by name.

inline constexpr double kSilenceLufs = -70.0;

// Integrated loudness in LUFS. `right` may be null for a single channel.
//
// Needs at least one 400 ms block; anything shorter returns the silence floor,
// because the standard has nothing to say about a shorter measurement and
// inventing an answer would be worse than admitting there is not one.
inline double integratedLufs(const float *left, const float *right,
                             int numSamples, double sampleRate) {
  if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return kSilenceLufs;

  // Shorter than one gating block is refused here rather than deeper down.
  // libebur128 answers -HUGE_VAL, which is indistinguishable from silence;
  // "there was not enough audio to measure" and "the audio was silent" are
  // different facts, and only one of them is about the signal.
  const int blockSamples = (int)(0.4 * sampleRate);
  if (blockSamples <= 0 || numSamples < blockSamples)
    return kSilenceLufs;

  const unsigned channels = right != nullptr ? 2u : 1u;

  ebur128_state *st =
      ebur128_init(channels, (unsigned long)sampleRate, EBUR128_MODE_I);
  if (st == nullptr)
    return kSilenceLufs;

  // libebur128 takes interleaved frames, and this header takes a pointer per
  // channel, so one copy is unavoidable. It is a measurement path -- offline,
  // over whole takes -- so the copy costs nothing that matters.
  std::vector<float> interleaved((size_t)numSamples * channels);
  if (channels == 2) {
    for (int i = 0; i < numSamples; ++i) {
      interleaved[(size_t)i * 2] = left[i];
      interleaved[(size_t)i * 2 + 1] = right[i];
    }
  } else {
    std::copy(left, left + numSamples, interleaved.begin());
  }

  double lufs = kSilenceLufs;
  if (ebur128_add_frames_float(st, interleaved.data(), (size_t)numSamples) ==
      EBUR128_SUCCESS) {
    double measured = 0.0;
    if (ebur128_loudness_global(st, &measured) == EBUR128_SUCCESS &&
        measured > kSilenceLufs)
      lufs = measured;
  }

  ebur128_destroy(&st);
  return lufs;
}

inline double integratedLufs(const float *data, int numSamples,
                             double sampleRate) {
  return integratedLufs(data, nullptr, numSamples, sampleRate);
}

// The gain that moves a measured loudness onto a target one.
inline double gainForLufs(double measuredLufs, double targetLufs) {
  if (measuredLufs <= kSilenceLufs)
    return 1.0;
  return std::pow(10.0, (targetLufs - measuredLufs) / 20.0);
}

// MIDI note number for a frequency, and its pitch class. Handy wherever a
// measured frequency has to be compared with a chord root.
inline double midiForHz(double hz) {
  if (hz <= 0.0)
    return -1.0;
  return 69.0 + 12.0 * std::log2(hz / 440.0);
}

inline int pitchClassForHz(double hz) {
  if (hz <= 0.0)
    return -1;
  const int midi = (int)std::lround(midiForHz(hz));
  return ((midi % 12) + 12) % 12;
}

} // namespace AudioMeasure
