#include "MetronomeVoice.h"

#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586476925;
}

void MetronomeVoice::prepare(double sr) {
  sampleRate = sr > 0.0 ? sr : 0.0;
  samplesTotal = (int)(kClickSeconds * sampleRate);
  samplesRemaining = 0;
  phase = 0.0;
}

void MetronomeVoice::setBeatsPerInterval(int bpi) {
  beatsPerBar = (bpi > 0 && bpi % 4 == 0) ? 4 : 0;
}

void MetronomeVoice::trigger(int beatIndex) {
  if (sampleRate <= 0.0)
    return;

  if (beatIndex == 0) {
    frequency = 880.0;
    amplitude = 0.10f;
  } else if (beatsPerBar > 0 && beatIndex % beatsPerBar == 0) {
    frequency = 660.0;
    amplitude = 0.06f;
  } else {
    frequency = 440.0;
    amplitude = 0.03f;
  }

  phase = 0.0;
  phaseIncrement = kTwoPi * frequency / sampleRate;
  samplesTotal = (int)(kClickSeconds * sampleRate);
  samplesRemaining = samplesTotal;
}

void MetronomeVoice::render(float *dst, int numSamples, float gain) {
  if (samplesRemaining <= 0 || numSamples <= 0 || samplesTotal <= 0)
    return;

  const int n = numSamples < samplesRemaining ? numSamples : samplesRemaining;
  for (int i = 0; i < n; ++i) {
    // Linear decay to zero across the click, so it never runs into the next.
    const double envelope =
        (double)(samplesRemaining - i) / (double)samplesTotal;
    dst[i] += (float)(std::sin(phase) * envelope) * amplitude * gain;
    phase += phaseIncrement;
    if (phase >= kTwoPi)
      phase -= kTwoPi;
  }
  samplesRemaining -= n;
}
