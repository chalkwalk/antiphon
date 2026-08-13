#pragma once

#include "BotDsp.h"

#include <array>
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

// Soft saturation, normalised so that an input of 1 comes out at 1.
//
// Used for two different jobs, and it is worth keeping them apart.
//
// On a single voice it is about AUDIBILITY: the harmonics it adds sit above the
// fundamental, so a bass note or a kick whose fundamental a small speaker
// cannot reproduce is still heard. Raising the gain instead spends headroom and
// does not help.
//
// On a bus it is about COHESION, and it does something no amount of per-voice
// shaping can. Because the sum is shaped, the loudest element momentarily pushes
// the others down -- when the kick lands, the hats duck a little. That
// intermodulation is what "glue" actually is.
//
// Drives above about 3 start eating transients before they add anything, which
// on drums is the wrong trade.
inline float saturate(float x, double drive) {
  if (drive <= 0.0)
    return x;
  return (float)(std::tanh(drive * (double)x) / std::tanh(drive));
}

// Exponential decay to about -60 dB over `seconds`.
inline float decayAt(double t, double seconds) {
  if (seconds <= 0.0)
    return 0.0f;
  return (float)std::exp(-6.9078 * t / seconds);
}

// A kick drum is a struck membrane, and modelling it as one is the difference
// between a drum and a low beep with an envelope.
//
// Three physical facts, and each is a few lines. A circular membrane has modes
// at INHARMONIC ratios -- 1, 1.59, 2.14, 2.30, 2.65, from the zeros of the
// Bessel functions -- not at multiples of a fundamental, which is why a drum
// does not sound like a pitched note. The high modes die far faster than the
// low one, so the sound darkens within its first tenth of a second. And the
// strike stretches the head, so the tension and with it the pitch fall as it
// relaxes; that drop is what the old exponential sweep was imitating without
// the modes underneath it.
//
// The beater is separate from the head. It contributes a burst of contact noise
// that does not ring, which is what makes a kick sound hit rather than played.
// It also does the job the old 1.4 kHz sine did: the body lands near 50 Hz,
// which a laptop does not reproduce at all, so without something up where the
// speaker works the drum is inaudible on most of the machines this reaches.
//
// Saturation stays, and for the same reason as before: a bare low sine is the
// least loud waveform there is for a given peak, and shaping it fills in
// harmonics a small speaker can actually pass.
inline constexpr double kKickDrive = 2.0;

// Bessel-zero ratios for a circular membrane, which is what makes this a drum.
inline constexpr int kKickModes = 4;
inline constexpr double kMembraneRatios[kKickModes] = {1.0, 1.593, 2.136,
                                                       2.653};

inline void renderKick(float *out, int numSamples, double sampleRate,
                       float velocity) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double baseHz = 50.0;
  // How far the head is stretched by the strike, and how fast it relaxes.
  const double bendDepth = 2.6, bendTime = 0.028;

  BotDsp::ModalBank head;
  head.prepare(sampleRate);
  // The fundamental carries the weight and rings; the upper modes are the
  // strike and are gone almost immediately.
  const double decays[kKickModes] = {0.34, 0.09, 0.05, 0.03};
  const float gains[kKickModes] = {1.0f, 0.30f, 0.18f, 0.10f};
  for (int m = 0; m < kKickModes; ++m)
    head.addMode(baseHz * kMembraneRatios[m] * (1.0 + bendDepth), decays[m],
                 gains[m]);

  BotDsp::Noise noise(0x9E3779B9u);
  BotDsp::Svf beaterTone;
  beaterTone.set(2200.0, 1.2, sampleRate);

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    // Tension falling back after the strike, applied to every mode at once so
    // the head stays one object rather than four detuning oscillators.
    if (i % 32 == 0) {
      const double bend = 1.0 + bendDepth * std::exp(-t / bendTime);
      for (int m = 0; m < kKickModes; ++m)
        head.setModeFrequency(m, baseHz * kMembraneRatios[m] * bend);
    }

    // The strike: an impulse into the head, plus a couple of milliseconds of
    // contact noise so it is a beater rather than a mathematical excitation.
    const float strike =
        (i == 0 ? 1.0f : 0.0f) + 0.5f * noise.next() * decayAt(t, 0.0025);
    const float body = head.process(strike);

    // The beater's own sound, which does not ring: bandpassed noise, gone in
    // four milliseconds, and the part of a kick a small speaker reproduces.
    const float beater = 0.5f * beaterTone.process(noise.next(), BotDsp::Svf::BandPass) *
                         decayAt(t, 0.004);

    // Shaped before the velocity rather than after it, so a quiet hit and an
    // accented one are the same drum at two levels instead of two drums.
    out[i] += velocity * saturate(0.62f * body + beater, kKickDrive);
  }
}

// A snare is two instruments in one shell, and the reason the old one sounded
// like a filtered click is that it treated them as one.
//
// The head is a struck membrane like the kick, tuned far higher and damped
// hard. The wires underneath rattle against it, and they have their OWN
// envelope -- they are shaken into life by the strike and keep going after the
// head has stopped, which is most of what makes a snare sound like a snare
// rather than a burst of noise with a tone under it.
inline void renderSnare(float *out, int numSamples, double sampleRate,
                        float velocity, std::uint32_t seed) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  BotDsp::ModalBank head;
  head.prepare(sampleRate);
  // Two body modes a little over a fifth apart: the shell's own pitch and its
  // first overtone, both damped hard by the hand-tightened head above them.
  head.addMode(185.0, 0.11, 1.0f);
  head.addMode(295.0, 0.06, 0.55f);

  BotDsp::Noise noise(seed);
  BotDsp::Svf wireTone;
  wireTone.set(4200.0, 0.8, sampleRate);
  BotDsp::Svf snapTone;
  snapTone.set(1600.0, 1.5, sampleRate);

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    const float strike = (i == 0 ? 1.0f : 0.0f) +
                         0.35f * noise.next() * decayAt(t, 0.002);
    const float body = head.process(strike);

    // The wires: bandpassed noise on a longer envelope than the head, which is
    // the whole trick.
    const float wires =
        wireTone.process(noise.next(), BotDsp::Svf::BandPass) * decayAt(t, 0.16);

    // And the crack of the stick, which is neither.
    const float snap =
        snapTone.process(noise.next(), BotDsp::Svf::BandPass) * decayAt(t, 0.006);

    out[i] += velocity * (0.55f * body + 0.75f * wires + 0.5f * snap);
  }
}

// A hi-hat is metal, and metal is inharmonic.
//
// Six square oscillators at ratios that are deliberately not whole numbers,
// which is the 808's answer and still the cheapest convincing one. Filtered
// noise alone -- what this used to be -- gives fizz with no pitch structure at
// all, and the ear hears that as a noise gate rather than as a cymbal.
//
// The ratio table is lifted from chalkwalk/seq_play src/machine/DrumMachine.cpp,
// whose Cymbal voice is the one part of that machine doing something a sine
// could not.
inline constexpr int kHatPartials = 6;
inline constexpr double kMetalRatios[kHatPartials] = {2.0, 3.0, 3.7,
                                                      5.3, 5.9, 6.4};

inline void renderHat(float *out, int numSamples, double sampleRate,
                      float velocity, std::uint32_t seed, bool open = false) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0)
    return;

  const double decay = open ? 0.30 : 0.055;
  const double baseHz = 2000.0;

  BotDsp::Noise noise(seed);
  BotDsp::Svf metal;
  metal.set(7000.0, 0.7, sampleRate);
  BotDsp::Svf sizzle;
  sizzle.set(9000.0, 0.7, sampleRate);

  std::array<double, (size_t)kHatPartials> phases{};

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    float sum = 0.0f;
    for (int p = 0; p < kHatPartials; ++p) {
      phases[(size_t)p] += baseHz * kMetalRatios[p] / sampleRate;
      if (phases[(size_t)p] >= 1.0)
        phases[(size_t)p] -= 1.0;
      sum += phases[(size_t)p] < 0.5 ? 1.0f : -1.0f;
    }
    const float clang = metal.process(sum / (float)kHatPartials,
                                      BotDsp::Svf::HighPass);

    // A little noise on a shorter envelope: the sound of the two cymbals
    // meeting, as opposed to the metal ringing afterwards.
    const float hiss = sizzle.process(noise.next(), BotDsp::Svf::HighPass) *
                       decayAt(t, decay * 0.4);

    out[i] += velocity * 0.55f * (clang * decayAt(t, decay) + 0.5f * hiss);
  }
}

// How the string is set in motion. A choice a player makes for a whole part,
// not something that changes note to note.
//
// This axis exists SEPARATELY from velocity, and the separation is the point.
// Playing harder does not turn a fingerstyle bassist into a plectrum player;
// it makes the same technique brighter and more percussive. So technique is
// picked once from the seed and velocity moves continuously inside it, which
// means no note can ever land on the wrong side of a threshold and arrive
// sounding like a different instrument.
enum class BassTechnique { Fingered, Picked, Muted };

inline const char *bassTechniqueName(BassTechnique t) {
  switch (t) {
  case BassTechnique::Fingered:
    return "fingered";
  case BassTechnique::Picked:
    return "picked";
  case BassTechnique::Muted:
    return "muted";
  }
  return "fingered";
}

// A plucked bass string.
//
// Karplus-Strong, which is a delay line the length of the period, a bridge
// that loses a little on every round trip and loses the top first, and an
// excitation injected at one point along the string. What that buys over the
// four summed sines it replaces is the thing no additive voice has: the timbre
// changes AS the note decays, bright for a tenth of a second and dark for the
// rest of its life. That shape is most of what makes a note sound played
// rather than switched on.
//
// It also reopens a decision. `f82d9ce` made this voice sustained rather than
// plucked, because the plucked version it replaced was a sine with a fast
// decay -- which is the definition of a kick drum, in the same octave as one.
// That commit's actual argument was that SHAPE AND TIMBRE have to separate a
// bass from a kick, since pitch cannot. A string that rings for seconds with a
// full set of harmonics satisfies it; a decaying sine never did.
//
// Velocity does three things at once here, and all three are what a real
// instrument does when you dig in: the note is louder, its excitation is
// brighter, and the contact noise of finger or plectrum is more prominent.
// None of them is a switch.
inline void renderBassString(float *out, int numSamples, double sampleRate,
                             double hz, float velocity, BassTechnique technique,
                             std::uint32_t seed) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  const float v = velocity < 0.0f ? 0.0f : (velocity > 1.0f ? 1.0f : velocity);

  // Each technique is a RANGE that velocity moves along, never a point.
  double pickPosition = 0.25, brightnessFloor = 0.18, brightnessSpan = 0.24;
  double decaySeconds = 3.0, contact = 0.15;

  // A technique that lets the string ring puts far more energy into the room
  // than one that stops it, so the same velocity is not the same loudness. A
  // player compensates by digging in, and so does this: without it a muted
  // part measures 6 LU under a fingered one and the bass drops out of the band
  // whenever the seed happens to choose it.
  double techniqueGain = 1.0;
  switch (technique) {
  case BassTechnique::Fingered:
    // The flesh of a finger, over the end of the neck: round, and it damps the
    // string a little as it leaves.
    break;
  case BassTechnique::Picked:
    // Nearer the bridge and much harder, so more of the upper modes survive
    // the pluck and the contact is a click rather than a thump.
    pickPosition = 0.11;
    brightnessFloor = 0.32;
    brightnessSpan = 0.30;
    decaySeconds = 2.4;
    contact = 0.40;
    techniqueGain = 1.15;
    break;
  case BassTechnique::Muted:
    // The heel of the hand resting on the bridge. Same pluck, far shorter
    // string life, which is the whole of what a palm mute is.
    //
    // Not as short as it wants to be, and the reason is level rather than
    // physics: at 0.45 s the note carries so little energy that the gain
    // needed to keep it in the band pushed single notes to 1.19, and a voice
    // that lives in the ceiling is a voice being limited rather than played.
    // 0.7 s is still unmistakably muted and needs half the compensation.
    pickPosition = 0.16;
    brightnessFloor = 0.14;
    brightnessSpan = 0.20;
    decaySeconds = 0.70;
    contact = 0.18;
    techniqueGain = 1.5;
    break;
  }

  const double brightness = brightnessFloor + brightnessSpan * (double)v;

  BotDsp::PluckedString string;
  string.pluck(hz, sampleRate, 0.85f * (0.18f + 0.82f * v), pickPosition,
               brightness, decaySeconds, seed);

  // The body: an instrument is not only its string. A bandpass around the
  // lowest air resonance, mixed under, is what stops the note sounding like a
  // synthesiser playing the right frequency.
  BotDsp::Svf body;
  body.set(95.0, 2.2, sampleRate);

  // The sound of the finger or plectrum meeting the string, which is not the
  // string and does not ring.
  BotDsp::Noise noise(seed ^ 0x5BD1E995u);
  BotDsp::Svf contactTone;
  contactTone.set(technique == BassTechnique::Picked ? 2600.0 : 1300.0, 1.1,
                  sampleRate);

  // A bass cabinet is a DARK box: a 15-inch driver in a sealed cab does
  // essentially nothing above two kilohertz, and that limit is most of why an
  // amplified bass sounds like one rather than like a very low guitar.
  BotDsp::Cabinet cabinet;
  cabinet.prepare(sampleRate, 2200.0, 0.25);

  // The note is damped rather than cut. A string stopped by a player dies over
  // a few tens of milliseconds with its highs going first, and gating it at the
  // buffer's end would be a click.
  const double total = (double)numSamples / sampleRate;
  const double release = std::min(0.06, total * 0.25);
  const int releaseAt = numSamples - (int)(release * sampleRate);

  for (int i = 0; i < numSamples; ++i) {
    if (i == releaseAt)
      string.mute(sampleRate, release);

    const double t = (double)i / sampleRate;
    const float s = string.next();

    // The contact is a detail on the front of the note, not a component of
    // it: audible as articulation, never as a second instrument sitting on
    // top of the string.
    const float attack = 0.35f * (float)contact * (0.4f + 0.6f * v) *
                         contactTone.process(noise.next(), BotDsp::Svf::BandPass) *
                         decayAt(t, 0.004);

    const float withBody = s + 0.35f * body.process(s, BotDsp::Svf::BandPass);
    out[i] += 0.55f * (float)techniqueGain * cabinet.process(withBody + attack);
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
