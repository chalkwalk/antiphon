#pragma once

#include <cmath>
#include <vector>

// Deterministic interval probe: a bed tone with short enveloped bursts at
// fixed fractions of each interval.
//
// Shared deliberately. The plugin's Test Tone feature generates it, the tests
// look for it, and the reference-client harness transmits it. Two definitions
// would drift, and a timing test would then be comparing an implementation
// against itself.
//
// A single-sample impulse would be simpler but does not survive a perceptual
// codec: Vorbis smears a lone broadband click and does not preserve its
// amplitude relative to the tone. A few milliseconds of a pure tone is exactly
// what such a codec keeps, and it can be located by energy in a narrow band
// rather than by absolute level.

namespace ninjam {

constexpr double kProbePi = 3.14159265358979323846;

struct IntervalProbe {
  double burstHz = 3000.0; // well clear of the 440 Hz bed tone
  double burstSeconds = 0.008;
  float burstAmp = 0.9f;
  double bedHz = 440.0;
  float bedAmp = 0.25f;

  // Fractions of the interval at which bursts start.
  std::vector<double> positions{0.0, 0.25, 0.5, 0.75};

  // Value at `posInInterval` of an interval `intervalLen` samples long.
  // `globalSample` drives the continuous bed tone so it has no discontinuity.
  float sampleAt(int64_t posInInterval, int intervalLen, int64_t globalSample,
                 double sampleRate) const {
    const int burstLen = (int)(burstSeconds * sampleRate);
    for (double f : positions) {
      const int64_t start = (int64_t)(f * (double)intervalLen);
      if (posInInterval >= start && posInInterval < start + burstLen) {
        const int64_t k = posInInterval - start;
        // Raised-cosine envelope: no click, so the codec has an easy time and
        // the burst stays narrow-band.
        const double env =
            0.5 *
            (1.0 - std::cos(2.0 * kProbePi * (double)k / (double)burstLen));
        return (
            float)(burstAmp * env *
                   std::sin(2.0 * kProbePi * burstHz * (double)k / sampleRate));
      }
    }
    return (float)(bedAmp * std::sin(2.0 * kProbePi * bedHz *
                                     (double)globalSample / sampleRate));
  }
};

} // namespace ninjam
