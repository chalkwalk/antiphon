#pragma once

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
  const int maxLag = std::min(numSamples / 2, (int)(sampleRate / lowHz));
  if (maxLag <= minLag)
    return 0.0;

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
// Two pieces. K-weighting is a pair of biquads -- a high shelf for the head's
// effect on incoming sound, then a high-pass that discounts the very low end --
// and gating throws away the quiet parts so that a sparse part is measured by
// how loud it is when it plays rather than by how much silence surrounds it.
//
// Validated against ffmpeg's ebur128 rather than against itself; see
// AudioMeasureTests.

inline constexpr double kSilenceLufs = -70.0;

struct Biquad {
  double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
  double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

  double process(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

// The two stages of K-weighting, for any sample rate. The constants are the
// analogue prototype's, so 44.1 and 96 kHz are as right as 48.
inline void kWeighting(double sampleRate, Biquad &shelf, Biquad &highpass) {
  {
    const double f0 = 1681.974450955533;
    const double gain = 3.999843853973347;
    const double q = 0.7071752369554196;
    const double k = std::tan(kPi * f0 / sampleRate);
    const double vh = std::pow(10.0, gain / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;

    shelf.b0 = (vh + vb * k / q + k * k) / a0;
    shelf.b1 = 2.0 * (k * k - vh) / a0;
    shelf.b2 = (vh - vb * k / q + k * k) / a0;
    shelf.a1 = 2.0 * (k * k - 1.0) / a0;
    shelf.a2 = (1.0 - k / q + k * k) / a0;
  }
  {
    const double f0 = 38.13547087602444;
    const double q = 0.5003270373238773;
    const double k = std::tan(kPi * f0 / sampleRate);
    const double denom = 1.0 + k / q + k * k;

    highpass.b0 = 1.0;
    highpass.b1 = -2.0;
    highpass.b2 = 1.0;
    highpass.a1 = 2.0 * (k * k - 1.0) / denom;
    highpass.a2 = (1.0 - k / q + k * k) / denom;
  }
}

// Integrated loudness in LUFS. `right` may be null for a single channel.
//
// Needs at least one 400 ms block; anything shorter returns the silence floor,
// because the standard has nothing to say about a shorter measurement and
// inventing an answer would be worse than admitting there is not one.
inline double integratedLufs(const float *left, const float *right,
                             int numSamples, double sampleRate) {
  if (left == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return kSilenceLufs;

  const int blockSamples = (int)(0.4 * sampleRate);
  const int hopSamples = (int)(0.1 * sampleRate);
  if (blockSamples <= 0 || hopSamples <= 0 || numSamples < blockSamples)
    return kSilenceLufs;

  const int channels = right != nullptr ? 2 : 1;

  // K-weight the whole thing once, then square it: the blocks overlap by 75%,
  // so filtering per block would do the work four times and, worse, would
  // restart the filter state at every block boundary.
  std::vector<double> squared((size_t)numSamples, 0.0);
  for (int ch = 0; ch < channels; ++ch) {
    const float *in = ch == 0 ? left : right;
    Biquad shelf, highpass;
    kWeighting(sampleRate, shelf, highpass);
    for (int i = 0; i < numSamples; ++i) {
      const double y = highpass.process(shelf.process((double)in[i]));
      squared[(size_t)i] += y * y;
    }
  }

  // Mean square per block, which is the sum over channels already.
  std::vector<double> blocks;
  for (int start = 0; start + blockSamples <= numSamples; start += hopSamples) {
    double sum = 0.0;
    for (int i = start; i < start + blockSamples; ++i)
      sum += squared[(size_t)i];
    blocks.push_back(sum / (double)blockSamples);
  }
  if (blocks.empty())
    return kSilenceLufs;

  auto loudnessOf = [](double meanSquare) {
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare)
                            : kSilenceLufs;
  };

  // The absolute gate: anything under -70 LUFS is silence and is not part of
  // the programme.
  double sum = 0.0;
  int kept = 0;
  for (double z : blocks)
    if (loudnessOf(z) > kSilenceLufs) {
      sum += z;
      ++kept;
    }
  if (kept == 0)
    return kSilenceLufs;

  // The relative gate, which is what makes this a measure of the music rather
  // than of how much room was left around it: blocks more than 10 LU below the
  // ungated average are dropped and the average taken again.
  const double relative = loudnessOf(sum / (double)kept) - 10.0;

  double finalSum = 0.0;
  int finalKept = 0;
  for (double z : blocks)
    if (loudnessOf(z) > kSilenceLufs && loudnessOf(z) > relative) {
      finalSum += z;
      ++finalKept;
    }
  if (finalKept == 0)
    return kSilenceLufs;

  return loudnessOf(finalSum / (double)finalKept);
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
