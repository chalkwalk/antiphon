#pragma once

#include <cmath>

// The tone that says "the DAW stopped, and the room did not".
//
// One state matters more than any other to someone who cannot see the screen:
// synced, the DAW stops, and the jam carries on without them on Antiphon's
// clock. A sentence is said once; this repeats, once an interval, until the
// state ends. G6 (1568 Hz) because nothing the metronome or a band plays sits
// there -- the click is 440/660/880 -- and it is short so it reads as a signal
// rather than a note. Never transmitted: it is mixed where the metronome is,
// after the input is captured.
//
// Header-only and allocation-free: render() writes only into what it is given.

class CueVoice {
public:
  static constexpr double kFrequencyHz = 1568.0;
  static constexpr double kDurationMs = 120.0;

  void prepare(double sr) {
    sampleRate = sr > 0.0 ? sr : 0.0;
    remaining = 0;
  }

  void trigger() {
    if (sampleRate <= 0.0)
      return;
    total = (int)(kDurationMs * 0.001 * sampleRate);
    remaining = total;
    phase = 0.0;
    attack = (int)(kAttackMs * 0.001 * sampleRate);
    // -60 dB at the end: e^(-total/tau) = 1e-3.
    decayPerSample = std::exp(-std::log(1000.0) / (double)total);
    level = 1.0;
  }

  void render(float *dst, int n, float gain) {
    if (remaining <= 0 || n <= 0)
      return;
    const double inc = kTwoPi * kFrequencyHz / sampleRate;
    const int count = n < remaining ? n : remaining;
    for (int i = 0; i < count; ++i) {
      const int t = total - remaining + i;
      const double env = t < attack ? (double)t / (double)attack : 1.0;
      dst[i] += (float)(std::sin(phase) * env * level * kAmplitude) * gain;
      phase += inc;
      if (phase >= kTwoPi)
        phase -= kTwoPi;
      level *= decayPerSample;
    }
    remaining -= count;
  }

  bool isActive() const { return remaining > 0; }
  void reset() { remaining = 0; }

private:
  static constexpr double kTwoPi = 6.283185307179586476925;
  static constexpr double kAttackMs = 2.0; // no click at the onset
  static constexpr double kAmplitude = 0.12;

  double sampleRate = 0.0;
  double phase = 0.0;
  double level = 0.0;
  double decayPerSample = 1.0;
  int total = 0;
  int remaining = 0;
  int attack = 1;
};
