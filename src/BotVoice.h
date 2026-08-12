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
//
// The beater click on top of that is not decoration. The body lands at 50 Hz,
// which a laptop or a small monitor does not reproduce at all, so without
// something up where the speaker works the kick is inaudible on most of the
// machines this will be played on.
inline void renderKick(float *out, int numSamples, double sampleRate,
                       float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double startHz = 190.0, endHz = 50.0;
  const double sweep = 0.030, decay = 0.30;
  const double clickDecay = 0.004;
  double phase = 0.0, clickPhase = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    const double hz = endHz + (startHz - endHz) * std::exp(-t / sweep);
    phase += 2.0 * kPi * hz / sampleRate;
    const float body = (float)std::sin(phase) * decayAt(t, decay);

    clickPhase += 2.0 * kPi * 1400.0 / sampleRate;
    const float click =
        0.28f * (float)std::sin(clickPhase) * decayAt(t, clickDecay);

    out[i] += velocity * (body + click);
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

// A sustained, harmonically rich bass -- deliberately NOT a plucked one.
//
// The first version was a sine with a fast exponential decay, which is very
// nearly the definition of a kick drum: same register, same envelope, and the
// two were indistinguishable in the mix. Pitch alone does not separate them,
// because a bass note and a kick occupy the same octave by design.
//
// What separates them is shape and timbre. A bass note holds -- attack, a long
// body at nearly full level, then a release -- where a kick is gone in a third
// of a second. And it carries strong upper harmonics, so it reads as a pitched
// instrument on a speaker that cannot reproduce its fundamental at all. Most of
// what a listener hears as "the bass note" on a laptop is the second and third
// harmonic; the fundamental only fills it in on something that can go low.
// Saturation, for loudness rather than for grit.
//
// A bass part has to be heard in a mix that has headroom to respect, and
// turning the gain up spends the headroom without helping: the peak rises and
// the perceived level barely does. tanh flattens the peaks and fills in the
// harmonics instead, so the note reads louder while its peak goes DOWN -- and
// the added harmonics are what a small speaker actually reproduces.
inline constexpr double kBassDrive = 1.7;
// tanh of the drive times the tone's own peak (0.75+0.55+0.30+0.14), so a note
// still tops out near 1.0 before the gain below.
inline constexpr double kBassNormalise = 0.994;

inline void renderBass(float *out, int numSamples, double sampleRate, double hz,
                       float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  const double total = (double)numSamples / sampleRate;
  const double attack = std::min(0.012, total * 0.1);
  const double release = std::min(0.10, total * 0.3);

  // Enough of a droop to sound played rather than held by a machine, but
  // nothing like the decay of a drum.
  const double bodyDecay = 1.8;

  double p1 = 0.0, p2 = 0.0, p3 = 0.0, p4 = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    float env = 1.0f;
    if (t < attack)
      env = (float)(t / attack);
    else if (t > total - release)
      env = (float)((total - t) / release);
    if (env < 0.0f)
      env = 0.0f;
    if (env > 1.0f)
      env = 1.0f;
    env *= decayAt(t, bodyDecay);

    p1 += 2.0 * kPi * hz / sampleRate;
    p2 += 2.0 * kPi * hz * 2.0 / sampleRate;
    p3 += 2.0 * kPi * hz * 3.0 / sampleRate;
    p4 += 2.0 * kPi * hz * 4.0 / sampleRate;

    // Weighted towards the harmonics rather than the fundamental, which is
    // what makes the note audible on a small speaker.
    const double tone = 0.75 * std::sin(p1) + 0.55 * std::sin(p2) +
                        0.30 * std::sin(p3) + 0.14 * std::sin(p4);

    const float shaped = (float)(std::tanh(kBassDrive * tone) / kBassNormalise);
    out[i] += velocity * 0.52f * shaped * env;
  }
}

// A lead voice: bright enough to sit above the chords and articulate enough to
// hear as a line rather than a texture.
//
// Distinct from the pad by attack (fast, not soft) and from the bass by
// register and by having odd harmonics rather than a full stack -- closer to a
// clarinet or a square-ish synth than to either. It has to be recognisable as
// "the part someone would otherwise be playing", because the point of the lead
// bot is that you can mute it and play that part yourself.
inline void renderLead(float *out, int numSamples, double sampleRate, double hz,
                       float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  const double total = (double)numSamples / sampleRate;
  const double attack = std::min(0.006, total * 0.1);
  const double release = std::min(0.08, total * 0.4);
  double p1 = 0.0, p3 = 0.0, p5 = 0.0;

  // A little vibrato, late in the note. Nothing says "played" like a pitch
  // that is not perfectly steady, and it costs one oscillator.
  double vib = 0.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    float env = 1.0f;
    if (t < attack)
      env = (float)(t / attack);
    else if (t > total - release)
      env = (float)((total - t) / release);
    if (env < 0.0f)
      env = 0.0f;
    if (env > 1.0f)
      env = 1.0f;

    vib += 2.0 * kPi * 5.2 / sampleRate;
    const double depth = std::min(1.0, t / 0.25) * 0.004;
    const double f = hz * (1.0 + depth * std::sin(vib));

    p1 += 2.0 * kPi * f / sampleRate;
    p3 += 2.0 * kPi * f * 3.0 / sampleRate;
    p5 += 2.0 * kPi * f * 5.0 / sampleRate;

    // Odd harmonics only: hollow rather than buzzy, and it keeps the lead from
    // masking the keys, whose triads are full of even-harmonic content.
    const double tone =
        std::sin(p1) + 0.32 * std::sin(p3) + 0.12 * std::sin(p5);

    out[i] += velocity * 0.30f * (float)tone * env;
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
