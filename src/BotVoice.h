#pragma once

#include <cmath>
#include <cstdint>

// The band's synthesis: three drums and two pitched voices, in about as few
// lines as will still sound like instruments.
//
// Deliberately small. The reference for a drum voice here is
// chalkwalk/seq_play src/machine/DrumMachine.cpp, which is 660 lines welded to
// a machine interface, a parameter frame and a MIDI buffer. What Antiphon needs
// from it is the voice design -- a pitch-swept sine is a kick, filtered noise
// is a hat -- not the framework, so the design was read and the framework left
// behind.
//
// Everything here ADDS into its output so overlapping notes mix, is
// deterministic given its arguments, and allocates nothing. It runs on the
// conductor thread rather than the audio thread, so the last of those is a
// convenience rather than a requirement -- but it makes the voices reusable if
// that ever changes.
//
// No JUCE at all: this is float arithmetic, and staying free of it keeps the
// whole band testable in the headless target.

namespace BotVoice {

inline constexpr double kPi = 3.14159265358979323846;

// A small deterministic noise source. std::rand would make the drums differ
// between runs and between platforms, which would make them untestable.
class Noise {
public:
  explicit Noise(std::uint32_t seed) : state(seed | 1u) {}

  float next() noexcept {
    // xorshift32
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)((double)(state >> 8) / 8388608.0 - 1.0);
  }

private:
  std::uint32_t state;
};

inline double midiToHz(double midiNote) {
  return 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
}

// Exponential decay to about -60 dB over `seconds`.
inline float decayAt(double t, double seconds) {
  if (seconds <= 0.0)
    return 0.0f;
  return (float)std::exp(-6.9078 * t / seconds);
}

// A pitch sweep is what separates a kick drum from a low beep: the click at the
// front is the first few milliseconds of a much higher pitch.
inline void renderKick(float *out, int numSamples, double sampleRate,
                       float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double startHz = 150.0, endHz = 45.0;
  const double sweep = 0.035, decay = 0.32;
  double phase = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;
    const double hz = endHz + (startHz - endHz) * std::exp(-t / sweep);
    phase += 2.0 * kPi * hz / sampleRate;
    out[i] += velocity * decayAt(t, decay) * (float)std::sin(phase);
  }
}

inline void renderSnare(float *out, int numSamples, double sampleRate,
                        float velocity, std::uint32_t seed) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  Noise noise(seed);
  const double decay = 0.18, toneDecay = 0.09;
  double phase = 0.0;
  float lowpassed = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    // A one-pole lowpass takes the fizz off white noise and leaves something
    // closer to a drum head.
    const float n = noise.next();
    lowpassed += 0.45f * (n - lowpassed);

    phase += 2.0 * kPi * 185.0 / sampleRate;
    const float body = 0.5f * (float)std::sin(phase) * decayAt(t, toneDecay);

    out[i] += velocity * (0.7f * lowpassed * decayAt(t, decay) + body);
  }
}

inline void renderHat(float *out, int numSamples, double sampleRate,
                      float velocity, std::uint32_t seed, bool open = false) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  Noise noise(seed);
  const double decay = open ? 0.22 : 0.045;
  float previous = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    // A one-pole highpass, by subtraction: the opposite of the snare's filter,
    // and what makes this read as metal rather than as a snare.
    const float n = noise.next();
    const float highpassed = n - previous;
    previous = n;

    out[i] += velocity * 0.35f * highpassed * decayAt(t, decay);
  }
}

// A plucked bass: a couple of harmonics and a fast-ish decay, which sits under
// a mix without needing a filter envelope.
inline void renderBass(float *out, int numSamples, double sampleRate,
                       double hz, float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  const double decay = 0.55;
  double p1 = 0.0, p2 = 0.0, p3 = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;
    p1 += 2.0 * kPi * hz / sampleRate;
    p2 += 2.0 * kPi * hz * 2.0 / sampleRate;
    p3 += 2.0 * kPi * hz * 3.0 / sampleRate;

    const float tone = (float)(std::sin(p1) + 0.30 * std::sin(p2) +
                               0.12 * std::sin(p3));
    out[i] += velocity * 0.5f * tone * decayAt(t, decay);
  }
}

// A sustained voice with a soft attack and release, for chords. Held for the
// whole of its slot rather than plucked, because a pad that stabs is not a pad.
inline void renderPad(float *out, int numSamples, double sampleRate, double hz,
                      float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  const double total = (double)numSamples / sampleRate;
  const double attack = std::min(0.08, total * 0.25);
  const double release = std::min(0.20, total * 0.35);
  double p1 = 0.0, p2 = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    float env = 1.0f;
    if (t < attack)
      env = (float)(t / attack);
    else if (t > total - release)
      env = (float)((total - t) / release);
    env = env < 0.0f ? 0.0f : (env > 1.0f ? 1.0f : env);

    p1 += 2.0 * kPi * hz / sampleRate;
    // A slightly detuned second oscillator, which is most of what makes a pad
    // sound wide rather than thin.
    p2 += 2.0 * kPi * hz * 1.005 / sampleRate;

    out[i] += velocity * 0.22f * env *
              (float)(std::sin(p1) + 0.8 * std::sin(p2));
  }
}

} // namespace BotVoice
