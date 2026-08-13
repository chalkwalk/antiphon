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
  //
  // Tuned DOWN from 185 and 295, and the interesting part is that the body was
  // never the reason it read high. A 14-inch snare's lowest head mode really
  // does sit near 180 Hz. What the ear takes as the pitch of a snare is mostly
  // the wires and the stick, and those were at 4.2 kHz and 1.6 kHz -- a piccolo
  // snare, or a rim, rather than the drum in the middle of a kit. Lowering the
  // body alone would have left it sounding exactly as high; all three moved.
  head.addMode(155.0, 0.19, 1.0f);
  head.addMode(248.0, 0.11, 0.55f);

  BotDsp::Noise noise(seed);
  BotDsp::Svf wireTone;
  wireTone.set(3100.0, 0.8, sampleRate);
  BotDsp::Svf snapTone;
  snapTone.set(1150.0, 1.5, sampleRate);

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    const float strike = (i == 0 ? 1.0f : 0.0f) +
                         0.35f * noise.next() * decayAt(t, 0.002);
    const float body = head.process(strike);

    // The wires: bandpassed noise on a longer envelope than the head, which is
    // the whole trick.
    const float wires =
        wireTone.process(noise.next(), BotDsp::Svf::BandPass) * decayAt(t, 0.26);

    // And the crack of the stick, which is neither.
    const float snap =
        snapTone.process(noise.next(), BotDsp::Svf::BandPass) * decayAt(t, 0.006);

    // The body brought up and the wires brought down. A snare is a drum with
    // a rattle under it, and the balance had it the other way round.
    out[i] += velocity * (0.80f * body + 0.60f * wires + 0.42f * snap);
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

  // How far up the harmonic series the instrument lets anything through, as a
  // multiple of the note. See the tone control below.
  double toneFloor = 5.0, toneSpan = 3.0;

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
    toneFloor = 6.5;
    toneSpan = 4.5;
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
    toneFloor = 4.0;
    toneSpan = 2.5;
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

  // The tone control, and the only filter here that follows the note.
  //
  // Everything else in this signal path has a cutoff fixed in hertz -- the
  // body resonance is an air cavity and does not move, and the cabinet is a
  // speaker in a box and does not either. That is right for both of them and
  // wrong as the whole answer, because it means the instrument's brightness
  // depends on which note is being played: a fixed 2.2 kHz corner is the
  // fifth harmonic of a high note and the fiftieth of a low one, so the top of
  // the register comes out clean and the bottom comes out buzzing with
  // partials nothing on a real bass would pass.
  //
  // A real one is not a filter at all -- it is the mass of the string, the
  // pickup's own resonance, and a tone pot -- but all three scale with what is
  // being played, and a two-pole tracking the note is what that adds up to.
  // Two poles rather than four on purpose: this is meant to take the edge off
  // the upper partials, not to remove them, and at 24 dB/octave it stops being
  // a tone control and becomes a mute.
  //
  // It tracks VELOCITY as well as pitch, which is what keeps the articulation:
  // digging in opens it, exactly as it opens the excitation.
  BotDsp::Svf tone;
  tone.set(hz * (toneFloor + toneSpan * (double)v), 0.7, sampleRate);

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
    const float voiced =
        tone.process(withBody + attack, BotDsp::Svf::LowPass);
    out[i] += 0.55f * (float)techniqueGain * cabinet.process(voiced);
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

// What the keyboard player brought to the session.
//
// Three patches off the front panel of a stage polysynth, and the choice of
// what to model is a choice about the era rather than about the machine: a
// Prophet-5, a Juno-106, a Polysix and an OB-X differ in details a player
// cares about and a listener mostly does not. What they SHARE is the thing to
// build -- two oscillators a few cents apart, a four-pole lowpass with an
// envelope on it, a little noise in the mixer, and saturation everywhere the
// signal passes through a gain stage.
//
// Deliberately bread and butter. There is no ring modulator, no sync, no
// screaming self-oscillation, and no modulation matrix, because none of those
// is what a keyboard player is doing behind a jam.
enum class PadCharacter { Strings, Brass, Poly };

inline const char *padCharacterName(PadCharacter c) {
  switch (c) {
  case PadCharacter::Strings:
    return "strings";
  case PadCharacter::Brass:
    return "brass";
  case PadCharacter::Poly:
    return "poly";
  }
  return "poly";
}

// One patch: the front panel, as numbers.
//
// Every field has a range rather than a value, and the ranges are the whole
// point of the seed being allowed near this. A synth's controls are mostly not
// safe -- resonance at the top self-oscillates, a filter closed too far leaves
// silence, an attack longer than the chord means the chord never arrives. So
// the seed does not turn knobs; it picks one of three patches and then moves
// each control inside a span that was chosen by listening to both of its ends.
// The sweet spot is the range, and `padPatchFor` is what keeps you in it.
struct PadPatch {
  PadCharacter character = PadCharacter::Poly;

  double detuneCents = 7.0;   // between the two oscillators
  double driftCents = 3.0;    // how far each drifts, slowly, on its own
  bool secondIsPulse = true;  // saw + pulse, or saw + saw
  double pulseWidth = 0.4;

  // Where the second oscillator is TUNED, in semitones from the first.
  //
  // Two oscillators means two of them you can tune, which is the whole reason
  // these instruments have two -- not one plus a fixed sub-octave square, which
  // is a different and cheaper arrangement. Unison with a few cents between
  // them is the setting most patches use and the one that produces the beating
  // everybody means by "fat". An octave down is the other common one and is
  // where the weight comes from. A fifth is a real setting on a real panel and
  // people do use it, but every note of a four-part voicing gets it, so a
  // chord arrives with its own quintal harmony on top of what the keys were
  // asked to play -- it is left rare for that reason rather than for taste.
  int secondSemitones = 0;

  // And how loud it is, which is not independent of the above. An oscillator
  // at unison is an equal partner; one an octave down is doubling a register
  // four notes already occupy; one at a fifth is a colour and a colour that
  // loud is a chord change.
  double secondLevel = 1.0;
  double noiseLevel = 0.02;

  double cutoffPartials = 9.0; // filter cutoff, in harmonics of the note
  double resonance = 1.0;
  // The filter envelope: how far it opens, how long it takes to get there,
  // and how long it takes to settle back.
  //
  // The attack is what makes this an instrument rather than a blip, and it was
  // missing. Without it the filter is at its widest on the first sample and
  // only ever closes, which is the shape of something plucked -- so a brass
  // patch, whose whole identity is a swell INTO the note, arrived already
  // open and sounded like nothing in particular. A wind instrument's spectrum
  // grows as the player leans on it, and a subtractive synth imitates that
  // with a filter envelope that rises.
  double envAmount = 2.4;
  double envAttack = 0.12;
  double envDecay = 0.7;

  // Where it settles back to, as a fraction of how far it opened.
  //
  // Without this the envelope decays all the way to the closed cutoff, so
  // "closed" has to be a usable sustained tone and there is nowhere to sweep
  // from -- which is the corner the brass patch was painted into. Separating
  // the two lets the filter start genuinely shut, open a long way, and settle
  // somewhere in between, which is the ordinary ADSR shape and the reason a
  // real one has a sustain control at all.
  double envSustain = 0.35;

  double attackSeconds = 0.18;
  double releaseSeconds = 0.35;
  double drive = 1.0;          // into the filter
  double movementHz = 0.12;    // the slow wander that keeps a held chord alive

  // What the player left the volume on, so that changing patch is not changing
  // level.
  //
  // Not a taste control -- a correction, and it is needed because the patches
  // differ in things that all happen to affect loudness. A brass patch is a
  // near-square oscillator driven hard through a filter that opens on every
  // note; a strings patch is two saws barely driven through one that mostly
  // sits still. Measured across twelve seeds, that was 6 LU between the two,
  // which is a seed changing how loud the band is. A real player would have
  // reached for the output knob, and this is that knob.
  double level = 1.0;
};

inline PadPatch padPatchFor(std::uint32_t seed) {
  // Its own generator, so a patch can be asked for without disturbing whatever
  // sequence chose the notes (see BotBand::bassTechnique for the same rule).
  std::uint32_t state = seed | 1u;
  auto uni = [&state]() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (double)(state >> 8) / 16777216.0; // 0..1
  };
  auto between = [&uni](double lo, double hi) { return lo + (hi - lo) * uni(); };

  PadPatch p;
  p.character = (PadCharacter)(int)(uni() * 2.999);

  switch (p.character) {
  case PadCharacter::Strings:
    // Two saws, wide apart, filter well open and barely moving: the patch that
    // is on the front panel of every one of these machines and is the first
    // thing anybody plays through them.
    p.secondIsPulse = false;
    p.detuneCents = between(9.0, 16.0);
    p.driftCents = between(2.5, 5.0);
    p.noiseLevel = between(0.015, 0.035);
    p.cutoffPartials = between(10.0, 16.0);
    p.resonance = between(0.75, 1.00);
    p.envAmount = between(1.2, 2.0);
    p.envAttack = between(0.20, 0.45);
    p.envDecay = between(0.9, 1.6);
    p.envSustain = between(0.55, 0.80);
    p.attackSeconds = between(0.45, 0.85);
    p.releaseSeconds = between(0.70, 1.20);
    p.drive = between(0.5, 0.9);
    p.movementHz = between(0.07, 0.16);
    p.level = 1.63;
    break;

  case PadCharacter::Brass:
    // The other patch everybody plays: a filter envelope deep enough to hear
    // as a swell into each chord, which is what makes a subtractive synth
    // sound like it is being blown rather than switched on.
    p.secondIsPulse = true;
    p.pulseWidth = between(0.42, 0.50);
    p.detuneCents = between(5.0, 10.0);
    p.driftCents = between(1.5, 3.5);
    p.noiseLevel = between(0.020, 0.045);
    // Shut, and then a third of a second to open.
    //
    // Between one and two harmonics is the fundamental and almost nothing
    // else -- as closed as this filter goes while still passing the note --
    // and it is where the sweep has to start for the swell to be the sound of
    // the patch rather than a detail on the front of it. The sustain then
    // settles back to about a third of the way up, so the held chord is darker
    // than the note's arrival without being the muffled thing it started as.
    p.cutoffPartials = between(1.2, 2.0);
    p.resonance = between(1.00, 1.45);
    p.envAmount = between(8.0, 14.0);
    p.envAttack = between(0.28, 0.38);
    p.envDecay = between(0.45, 0.85);
    p.envSustain = between(0.25, 0.42);
    p.attackSeconds = between(0.12, 0.28);
    p.releaseSeconds = between(0.40, 0.70);
    p.drive = between(1.0, 1.6);
    p.movementHz = between(0.10, 0.22);
    p.level = 0.866;
    break;

  case PadCharacter::Poly:
    // The bread and butter one: a narrow pulse against a saw, and everything
    // else in the middle of its range.
    p.secondIsPulse = true;
    p.pulseWidth = between(0.28, 0.44);
    p.detuneCents = between(4.0, 9.0);
    p.driftCents = between(2.0, 4.0);
    p.noiseLevel = between(0.015, 0.035);
    p.cutoffPartials = between(7.0, 11.0);
    p.resonance = between(0.80, 1.20);
    p.envAmount = between(1.8, 3.0);
    p.envAttack = between(0.10, 0.24);
    p.envDecay = between(0.6, 1.1);
    p.envSustain = between(0.40, 0.65);
    p.attackSeconds = between(0.25, 0.50);
    p.releaseSeconds = between(0.55, 0.95);
    p.drive = between(0.7, 1.2);
    p.movementHz = between(0.08, 0.18);
    p.level = 1.10;
    break;
  }

  // Where the second oscillator sits. Weighted rather than uniform, because
  // these are not three equally likely settings on a real instrument: unison is
  // what most patches use, the octave is the next most common, and the fifth is
  // a thing people occasionally do.
  const double roll = uni();
  if (roll < 0.62) {
    p.secondSemitones = 0;
    p.secondLevel = 1.00;
  } else if (roll < 0.90) {
    p.secondSemitones = -12;
    p.secondLevel = 0.75;
  } else {
    p.secondSemitones = 7;
    p.secondLevel = 0.50;
  }

  return p;
}

// One voice of the polysynth, held for the whole of its slot.
//
// The signal path, in the order a panel lays it out, because every stage is
// there for a reason a player would recognise:
//
//   two oscillators, tuned against each other  ->  the beating that makes it
//                                                  wide, or the weight if the
//                                                  second one is an octave down
//   a little noise                       ->  air, and it keeps the filter alive
//   drive                                ->  an oscillator mixer overloading
//   a four-pole lowpass with an envelope ->  the instrument's actual voice
//   an amplifier envelope                ->  soft in, soft out
//
// Two things are doing most of the work of not sounding like a computer.
//
// The oscillators are BAND-LIMITED. A naive saw folds everything above Nyquist
// back down as inharmonic tones, and while that is inaudible as "aliasing" to
// most people, it is exactly what they mean when they say a synth sounds
// cheap. That is the whole reason BotDsp::polyBlepSaw exists.
//
// And nothing here is steady. Each oscillator drifts a few cents on its own
// slow path, the filter wanders, and both are seeded per NOTE, so the four
// notes of a chord are four independent instruments rather than one waveform
// played four times. On a real polysynth that is not a feature -- it is six
// separate boards that will never quite agree -- and it is most of the
// difference between a chord that breathes and one that sits.
// `holdSamples` is where the key comes up. The note keeps sounding after it,
// for as long as its release takes, and `numSamples` is only how much room the
// caller has -- so a chord can ring on over the one that follows it, which is
// what a keyboard player's hands actually do and what no amount of envelope
// tuning inside a single slot could imitate.
inline void renderPad(float *out, int numSamples, int holdSamples,
                      double sampleRate, double hz, float velocity,
                      const PadPatch &patch, std::uint32_t seed) {
  if (out == nullptr || numSamples <= 0 || sampleRate <= 0.0 || hz <= 0.0)
    return;

  if (holdSamples < 1)
    holdSamples = 1;
  if (holdSamples > numSamples)
    holdSamples = numSamples;

  const double holdTime = (double)holdSamples / sampleRate;
  // A slow attack is a real setting and a chord shorter than one is a real
  // situation -- two chords to a bar at a brisk tempo is under half a second
  // each -- so the attack gives way rather than swallowing the chord whole.
  const double attack = std::min(patch.attackSeconds, holdTime * 0.6);
  const double release = patch.releaseSeconds;

  // Filter cutoff, keyboard-tracked. Expressed in harmonics of the note so the
  // patch means the same thing wherever it is played -- the lesson the plucked
  // string cost us, where an absolute cutoff made one number a dull guitar and
  // a bass with its energy around the twelfth harmonic.
  //
  // Tracked at 70% rather than fully, which is what these instruments do: full
  // tracking makes a low chord as thin as a high one, and none tracked at all
  // makes it mud. Referred to middle C, so the patch's numbers describe the
  // register the keys actually play in.
  const double middleC = 261.6255653;
  const double baseCutoff =
      middleC * patch.cutoffPartials * std::pow(hz / middleC, 0.7);

  // Two oscillators, detuned in opposite directions so the pair stays centred
  // on the note. A synth whose detune pulls both oscillators sharp is a synth
  // that is out of tune.
  const double halfDetune = std::pow(2.0, patch.detuneCents / 2400.0);
  // And where the second one is tuned to, which is a front-panel decision
  // rather than a fine one: unison, an octave down, or a fifth up.
  const double interval = std::pow(2.0, (double)patch.secondSemitones / 12.0);
  double phaseA = 0.0, phaseB = 0.0;

  // Free-running phase, per note. Analogue oscillators are never reset by a
  // key, so no two notes of a chord start together -- and phase-coherent
  // oscillators are a large part of why a naive digital chord sounds like one
  // waveform at four pitches.
  Noise seeder(seed);
  phaseA = 0.5 * (double)seeder.next() + 0.5;
  phaseB = 0.5 * (double)seeder.next() + 0.5;

  // The slow disagreements: two drift paths for the oscillators, one for the
  // filter, at rates that share no common period.
  const double driftPhaseA = seeder.next() * kPi;
  const double driftPhaseB = seeder.next() * kPi;
  const double driftRateA = 0.21 + 0.13 * (0.5 * (double)seeder.next() + 0.5);
  const double driftRateB = 0.31 + 0.17 * (0.5 * (double)seeder.next() + 0.5);
  const double movePhase = seeder.next() * kPi;

  BotDsp::Noise noise(seed ^ 0xA511E9B3u);

  // Four poles. Two is not a synth filter: the whole character of these
  // machines is a 24 dB/octave slope, and at 12 the sound stays bright and
  // buzzy however far the cutoff comes down. Resonance sits on the first stage
  // only -- putting it on both squares the peak, which is how a bread-and-
  // butter patch turns into a whistle.
  BotDsp::Svf filterA, filterB;

  const double driftDepth = patch.driftCents / 1200.0;

  for (int i = 0; i < numSamples; ++i) {
    const double t = (double)i / sampleRate;

    // Amplifier envelope: attack, hold, release from the note-off. Squared, so
    // the corners are curves rather than the kinks a linear ramp leaves.
    double env = t < attack ? t / attack : 1.0;
    if (t > holdTime) {
      const double r = (t - holdTime) / release;
      env *= r >= 1.0 ? 0.0 : 1.0 - r;
    }
    env = env < 0.0 ? 0.0 : (env > 1.0 ? 1.0 : env);
    env *= env;

    // Filter envelope: open on the attack, settle back towards a sustain. This
    // is the one that is audible as an instrument being played.
    // Rise to the top, then settle back towards the sustain: attack, decay,
    // sustain, with the closed cutoff as the floor it all sits on.
    const double fenv =
        t < patch.envAttack
            ? t / patch.envAttack
            : patch.envSustain +
                  (1.0 - patch.envSustain) *
                      std::exp(-(t - patch.envAttack) / patch.envDecay);
    const double move =
        1.0 + 0.15 * std::sin(2.0 * kPi * patch.movementHz * t + movePhase);
    double cutoff = baseCutoff * (1.0 + patch.envAmount * fenv) * move;
    if (cutoff < 80.0)
      cutoff = 80.0;

    // Retuned in blocks: tan() at every sample of every note of every chord is
    // real money, and a filter cannot move audibly in two thirds of a
    // millisecond anyway.
    if (i % 32 == 0) {
      filterA.set(cutoff, patch.resonance, sampleRate);
      filterB.set(cutoff, 0.6, sampleRate);
    }

    const double detA =
        halfDetune *
        (1.0 + driftDepth * std::sin(2.0 * kPi * driftRateA * t + driftPhaseA));
    const double detB =
        (1.0 / halfDetune) *
        (1.0 + driftDepth * std::sin(2.0 * kPi * driftRateB * t + driftPhaseB));

    const double incA = hz * detA / sampleRate;
    const double incB = hz * interval * detB / sampleRate;

    phaseA += incA;
    if (phaseA >= 1.0)
      phaseA -= 1.0;
    phaseB += incB;
    if (phaseB >= 1.0)
      phaseB -= 1.0;

    float mixed = BotDsp::polyBlepSaw(phaseA, incA);
    mixed += (float)patch.secondLevel *
             (patch.secondIsPulse
                  ? BotDsp::polyBlepPulse(phaseB, incB, patch.pulseWidth)
                  : BotDsp::polyBlepSaw(phaseB, incB));
    mixed += (float)patch.noiseLevel * noise.next();

    // The oscillator mixer, pushed. On these instruments the summed
    // oscillators run into the filter hot enough to round their corners, and
    // that is where a subtractive synth stops sounding subtractive.
    mixed = saturate(0.34f * mixed, patch.drive);

    const float filtered = filterB.process(
        filterA.process(mixed, BotDsp::Svf::LowPass), BotDsp::Svf::LowPass);

    // Scaled for a CHORD rather than for a note. A polysynth's output amp sees
    // however many voices are held, and four of these summing incoherently is
    // about twice one of them -- so a note loud enough to be right on its own
    // drives the output stage of renderKeys into hard tanh clamping, where it
    // stops being warmth and becomes a limiter. Measured: every seed peaked at
    // exactly 1.198, which is 1/tanh(1.2) and therefore the ceiling of the
    // shaper rather than anything the music did.
    out[i] += velocity * 0.30f * (float)patch.level * (float)env * filtered;
  }
}

} // namespace BotVoice
