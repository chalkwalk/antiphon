#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <chalkwalk/dsp/Denormal.h>
#include <chalkwalk/dsp/Interpolation.h>
#include <chalkwalk/dsp/PolyBlep.h>
#include <chalkwalk/dsp/SoftClip.h>
#include <chalkwalk/dsp/Svf.h>

// The band's DSP primitives: filters, delay lines, strings, resonators.
//
// Everything here is a building block rather than an instrument. BotVoice.h
// assembles these into a kick or a bass; this file knows nothing about music,
// nothing about Antiphon, and nothing about JUCE. That is deliberate on three
// counts: it keeps the band testable in the headless target, it lets each piece
// be tested against arithmetic rather than against a tune, and it means the
// whole file can be lifted into another project as a copy rather than a port.
//
// Allocation-free. Every buffer is a fixed std::array sized at compile time, so
// a voice can own one on the stack and nothing reaches for the heap mid-note.
//
// DENORMALS. Several things here are feedback loops that decay towards zero: a
// string that rings out, a resonator after the strike, a room tail. Left alone
// they spend their last seconds in denormal range, which on x86 costs a
// hundred-odd cycles per operation and -- worse for us -- can differ between
// machines. Each loop therefore flushes below a threshold, which also makes
// "silence is exactly zero" a property a test can assert.

namespace BotDsp {

inline constexpr double kPi = 3.14159265358979323846;

// Denormal flushing, adopted from chalkwalk-dsp.
//
// The threshold is the 1e-9 this file chose, and the reasoning is worth
// keeping: at -180 dBFS it is two hundred times below the quietest thing
// 24-bit audio can represent, and denormals do not begin until around 1e-38,
// so a far smaller number would still avoid the CPU cliff. This one is chosen
// so that tails actually END, within a second or so of becoming inaudible,
// rather than ringing at 1e-12 for a minute -- which is what turns "silence"
// into a property a test can assert as an equality rather than a small number.
using chalkwalk::dsp::kFlushLevel;
using chalkwalk::dsp::flush;

// A state-variable filter, adopted from chalkwalk-dsp.
//
// Lifted from a sibling project and then diverged: this copy grew a
// set(cutoffHz, q, sampleRate) with the Nyquist and zero-cutoff edges handled,
// and denormal flushing on the state, neither of which went back. The shared
// version has both, plus a raw setCoeffs(g, k) for callers that
// smooth their own coefficients per sample.
//
// It replaced two hand-rolled one-poles in BotVoice -- the snare's lowpass
// accumulator and the hat's highpass-by-subtraction -- neither of which had a
// controllable cutoff or any resonance at all.
using chalkwalk::dsp::Svf;

// 4-point Hermite interpolation, adopted from chalkwalk-dsp. For fractional
// reads that are NOT inside a feedback loop -- see DelayLine::readLinear for
// why the loop uses something duller.
using chalkwalk::dsp::hermite4;

// A circular delay line with a fixed, power-of-two capacity, so the wrap is a
// mask rather than a branch.
template <int Capacity> struct DelayLine {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "capacity must be a power of two");

  std::array<float, (size_t)Capacity> buffer{};
  int writeIndex = 0;

  void clear() noexcept {
    buffer.fill(0.0f);
    writeIndex = 0;
  }

  void push(float x) noexcept {
    buffer[(size_t)writeIndex] = x;
    writeIndex = (writeIndex + 1) & (Capacity - 1);
  }

  float readInt(int delaySamples) const noexcept {
    if (delaySamples < 1)
      delaySamples = 1;
    if (delaySamples >= Capacity)
      delaySamples = Capacity - 1;
    const int i = (writeIndex - delaySamples) & (Capacity - 1);
    return buffer[(size_t)i];
  }

  // Linear interpolation, used inside feedback loops on purpose.
  //
  // Hermite is the better interpolator and it is right there -- but its
  // magnitude response exceeds unity around a third of Nyquist, and a gain of
  // 1.0001 inside a string's feedback path is an oscillator rather than a
  // string. Linear can only ever attenuate, so the loop's stability depends on
  // the loop gain alone, which is the thing being controlled. The cost is a
  // little extra damping up high, which on a plucked string is what the
  // physical instrument does anyway.
  float readLinear(double delaySamples) const noexcept {
    if (delaySamples < 1.0)
      delaySamples = 1.0;
    if (delaySamples > (double)(Capacity - 2))
      delaySamples = (double)(Capacity - 2);

    const int whole = (int)delaySamples;
    const float frac = (float)(delaySamples - (double)whole);
    const float a = readInt(whole);
    const float b = readInt(whole + 1);
    return a + frac * (b - a);
  }

  // Hermite, for reads that are not fed back: room taps and the like.
  float readHermite(double delaySamples) const noexcept {
    if (delaySamples < 2.0)
      delaySamples = 2.0;
    if (delaySamples > (double)(Capacity - 3))
      delaySamples = (double)(Capacity - 3);

    const int whole = (int)delaySamples;
    const float frac = (float)(delaySamples - (double)whole);
    return hermite4(readInt(whole - 1), readInt(whole), readInt(whole + 1),
                    readInt(whole + 2), frac);
  }
};

// A small deterministic noise source, matching BotVoice::Noise so the two agree
// about what a given seed sounds like.
struct Noise {
  std::uint32_t state = 1u;

  explicit Noise(std::uint32_t seed = 1u) noexcept : state(seed | 1u) {}

  float next() noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return (float)((double)(state >> 8) / 8388608.0 - 1.0);
  }
};

// Enough delay for a string down to about 23 Hz at 96 kHz.
inline constexpr int kStringCapacity = 4096;

// A plucked string, by extended Karplus-Strong.
//
// The physical picture, and each part of it earns a line of code: a string is a
// delay line whose length is the period, a bridge that loses a little energy
// every round trip and loses the high frequencies fastest, and a pluck that
// injects a burst of energy at one point along its length.
//
// What that buys over the four summed sines it replaces is the thing no
// additive voice has: the timbre changes as the note decays, because the loop
// filter takes the harmonics down in order. A real bass note is bright for a
// tenth of a second and dark for the rest of its life, and that shape is most
// of what makes an instrument sound played rather than switched on.
struct PluckedString {
  DelayLine<kStringCapacity> line;
  double delaySamples = 100.0;
  float loopGain = 0.99f;
  float damping = 0.5f; // one-pole coefficient in the loop
  float loopState = 0.0f;
  bool active = false;

  // `brightness` 0..1 -- how much high end the pluck injects, which on a real
  // instrument is how hard and how close to the bridge you played.
  // `pickPosition` 0..0.5 -- along the string, as a fraction of its length.
  void pluck(double hz, double sampleRate, float velocity, double pickPosition,
             double brightness, double decaySeconds,
             std::uint32_t seed) noexcept {
    line.clear();
    loopState = 0.0f;
    active = false;
    if (hz <= 0.0 || sampleRate <= 0.0)
      return;

    const double period = sampleRate / hz;
    if (period < 4.0 || period > (double)(kStringCapacity - 4))
      return;

    // A darker pluck also loses its highs faster, which is one physical fact
    // rather than two parameters: a soft, fleshy attack damps the string.
    damping = (float)(0.5 - 0.45 * brightness);

    // The loop filter is part of the loop's length, so the delay line has to be
    // shorter by however much the filter delays -- otherwise every note plays
    // flat, most audibly at the top where a fraction of a sample is a bigger
    // share of the period.
    //
    // How much is not a constant: a one-pole with coefficient a delays by
    // a/(1-a) samples, which is 0.3 for a bright pluck and 1.0 for a dull one.
    // A hardcoded half sample was the first version here and it left every note
    // measurably sharp -- 0.05% at 110 Hz rising to 0.3% at 660 -- because it
    // over-corrected for the filter that was actually there.
    const double filterDelay = (double)damping / (1.0 - (double)damping);
    delaySamples = period - filterDelay;
    if (delaySamples < 2.0)
      delaySamples = 2.0;

    // Per round trip, to reach -60 dB after decaySeconds.
    const double trips = (decaySeconds * sampleRate) / period;
    loopGain = trips > 0.0 ? (float)std::exp(-6.9078 / trips) : 0.0f;
    if (loopGain > 0.9999f)
      loopGain = 0.9999f;

    // The excitation. Noise through a lowpass set by brightness, so a hard
    // pick is a wideband burst and a thumb is a dull one.
    //
    // Scaled by the note rather than fixed in Hz, which is the difference
    // between a model and a lookup table. A string's brightness is about WHICH
    // HARMONIC it reaches, not which frequency: a bass string at 65 Hz excited
    // to its twentieth partial is a bass, and a guitar string at 330 Hz excited
    // to its twentieth is a guitar. With an absolute cutoff the same number
    // gives a dull guitar and a bass with a spectral centroid of 1.7 kHz --
    // which is what it did, measured seven times brighter than the sustained
    // voice it replaced.
    // Two poles rather than one, because a real pluck's spectrum falls away
    // fast above the first few harmonics and a single lowpass leaves a burst
    // that is nearly flat up to its corner. With one pole the bass measured a
    // spectral centroid of 835 Hz on a 65 Hz note -- energy centred around the
    // twelfth harmonic, which is a guitar.
    Noise noise(seed);
    Svf shaperA, shaperB;
    const double partials = 4.0 + 18.0 * brightness;
    shaperA.set(hz * partials, 0.7, sampleRate);
    shaperB.set(hz * partials * 1.2, 0.6, sampleRate);

    const int length = (int)period;
    std::array<float, (size_t)kStringCapacity> burst{};
    for (int i = 0; i < length; ++i)
      burst[(size_t)i] = shaperB.process(
          shaperA.process(noise.next(), Svf::LowPass), Svf::LowPass);

    // Pick position, as a comb: plucking a string a fifth of the way along
    // cannot excite the harmonics with a node there, which is why a bridge
    // pickup is nasal and playing over the neck is round. One subtraction.
    const int pickDelay =
        (int)(pickPosition * period) < 1 ? 1 : (int)(pickPosition * period);
    for (int i = length - 1; i >= 0; --i) {
      const float earlier = i >= pickDelay ? burst[(size_t)(i - pickDelay)] : 0.0f;
      burst[(size_t)i] = burst[(size_t)i] - earlier;
    }

    // Normalise, so velocity means level rather than "whatever the noise did".
    float peak = 0.0f;
    for (int i = 0; i < length; ++i)
      peak = std::max(peak, std::abs(burst[(size_t)i]));
    const float scale = peak > 0.0f ? velocity / peak : 0.0f;

    for (int i = 0; i < length; ++i)
      line.push(burst[(size_t)i] * scale);

    active = true;
  }

  float next() noexcept {
    if (!active)
      return 0.0f;

    const float sample = line.readLinear(delaySamples);
    // One-pole lowpass in the loop: the bridge. This is what takes the
    // harmonics away in order and leaves the fundamental last.
    loopState = flush(sample + damping * (loopState - sample));
    line.push(flush(loopState * loopGain));
    return sample;
  }

  // Note off. A player stopping a string does not gate it: they mute it, and it
  // dies over a few tens of milliseconds with its highs going first.
  void mute(double sampleRate, double seconds) noexcept {
    if (sampleRate <= 0.0 || seconds <= 0.0 || delaySamples <= 0.0)
      return;
    const double trips = (seconds * sampleRate) / delaySamples;
    loopGain = trips > 0.0 ? (float)std::exp(-6.9078 / trips) : 0.0f;
    damping = 0.7f;
  }
};

inline constexpr int kMaxModes = 6;

// A bank of two-pole resonators: the modal picture of something struck.
//
// A drum head is not a sine with an envelope. It is a membrane with a set of
// modes at inharmonic ratios, each decaying at its own rate -- the high ones
// fast, the fundamental slowly -- excited by whatever hits it. Model those
// three facts and the result is a drum; model the fundamental alone and the
// result is a low beep with a decay on it, which is what the kick was.
//
// The resonator is the standard two-pole form. Feeding it a beater signal
// rather than an impulse is what makes the strike sound like contact with a
// surface instead of a click.
struct ModalBank {
  struct Mode {
    double hz = 0.0;
    float gain = 0.0f;
    float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
  };

  std::array<Mode, (size_t)kMaxModes> modes{};
  int count = 0;
  double rate = 48000.0;

  void reset() noexcept {
    for (auto &m : modes)
      m.y1 = m.y2 = 0.0f;
  }

  void clear() noexcept {
    count = 0;
    reset();
  }

  void prepare(double sampleRate) noexcept {
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    clear();
  }

  void addMode(double hz, double decaySeconds, float gain) noexcept {
    if (count >= kMaxModes || hz <= 0.0 || hz >= 0.5 * rate)
      return;
    auto &m = modes[(size_t)count++];
    m.hz = hz;
    m.gain = gain;
    m.y1 = m.y2 = 0.0f;
    setModeCoefficients(m, hz, decaySeconds);
  }

  // Retune a mode while it rings. A drum head's pitch falls as the strike
  // stretches it and the tension relaxes, and that drop is most of what
  // separates a kick from a tom.
  void setModeFrequency(int index, double hz) noexcept {
    if (index < 0 || index >= count || hz <= 0.0 || hz >= 0.5 * rate)
      return;
    auto &m = modes[(size_t)index];
    const double r = std::sqrt((double)-m.a2);
    const double w = 2.0 * kPi * hz / rate;
    m.a1 = (float)(2.0 * r * std::cos(w));
    m.hz = hz;
  }

  float process(float excitation) noexcept {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) {
      auto &m = modes[(size_t)i];
      const float y = m.b0 * excitation + m.a1 * m.y1 + m.a2 * m.y2;
      m.y2 = m.y1;
      m.y1 = flush(y);
      sum += m.gain * y;
    }
    return sum;
  }

private:
  void setModeCoefficients(Mode &m, double hz, double decaySeconds) noexcept {
    const double w = 2.0 * kPi * hz / rate;
    // -60 dB over decaySeconds.
    const double r = decaySeconds > 0.0
                         ? std::exp(-6.9078 / (decaySeconds * rate))
                         : 0.0;
    m.a1 = (float)(2.0 * r * std::cos(w));
    m.a2 = (float)(-(r * r));

    // Peak-normalised: the impulse response of this form is
    // b0 * r^n * sin((n+1)w) / sin(w), so its peak is about b0 / sin(w) and
    // b0 = sin(w) makes every mode reach about 1 whatever its frequency and
    // whatever its decay.
    //
    // That matters for tuning by ear rather than for correctness. With the
    // obvious (1 - r) instead, a mode's level falls as its decay lengthens --
    // energy normalisation -- so lengthening a drum's tail quietens it and
    // every gain in the bank has to be found again.
    m.b0 = (float)std::sin(w);
  }
};

// The band-limiting correction that makes a digital saw or pulse sound like a
// saw or a pulse rather than like aliasing.
//
// Ported from a sibling project. A naive saw
// steps by 2 once per cycle, and that discontinuity has infinite bandwidth, so
// everything above Nyquist folds back down as inharmonic tones -- the sound
// people mean by "cheap digital synth". This subtracts a polynomial
// approximation of the step's spectrum at the moment it happens.
//
// THE SIGN WAS THE BUG, and it has since been fixed at both ends. This file
// once carried a note saying the original ADDED the correction where it should
// subtract: measured, its 5 kHz saw aliased 82% worse than no correction at
// all. That was fixed there independently, and both are now the same code in
// chalkwalk-dsp -- whose tests assert that the inverted version is worse than
// a naive oscillator, so it cannot come back quietly.
//
// ONE BEHAVIOUR CHANGE came with the move. This file clamped pulse width to a
// fixed [0.05, 0.95]; the shared version clamps to a multiple of the phase
// INCREMENT, because a pulse has two discontinuities and each correction spans
// a sample either side of its own. Measured, the fixed clamp was wrong at both
// ends: at 110 Hz it is twenty-one increments, forbidding narrow pulses that
// would have been clean, and at 3520 Hz it is two thirds of one, so it did not
// protect in the case it existed for.
using chalkwalk::dsp::polyBlep;
using chalkwalk::dsp::polyBlepSaw;
using chalkwalk::dsp::polyBlepPulse;

using chalkwalk::dsp::softClip;

// This band's ceiling, which is NOT the shared default.
//
// The shared default is the transparent master-bus pair -- knee 0.71, ceiling
// 0.99 -- for catching peaks on a mix that is already staged. These are for
// shaping a voice: a lower ceiling, because the level here is set by gain and
// this is what makes that gain safe.
//
// Named and passed explicitly because both projects that had this function
// baked in different constants AND called it bare, so "the default" silently
// meant two things. Whichever is right, it belongs at the call site.
inline constexpr float kBandKnee = 0.70f;
inline constexpr float kBandCeiling = 0.95f;

// A speaker cabinet, close-miked.
//
// Two things and no more. A lowpass, because a guitar or bass cabinet does
// almost nothing above 4 or 5 kHz and that limit is a large part of why an
// amplified instrument sounds amplified. And a gentle asymmetric shaping,
// because a valve stage clips its halves differently and that is what "warm"
// means when people say it about an amp.
//
// The DC blocker is not decoration: asymmetric shaping produces a DC offset,
// and a DC offset eats headroom in a mix that has none to spare.
struct Cabinet {
  // Two pole pairs, because one is not a cabinet.
  //
  // A speaker in a box is a fourth-order rolloff or steeper, and the
  // difference is audible rather than academic: at 12 dB per octave a bass amp
  // still passes enough two-kilohertz content to sound like a very low guitar,
  // which is exactly what the first version of the plucked bass did -- a
  // spectral centroid of 1 kHz against a pad's 336.
  Svf lowpassA, lowpassB;
  float dcX1 = 0.0f, dcY1 = 0.0f;
  float drive = 1.0f;

  void prepare(double sampleRate, double cutoffHz, double driveAmount) noexcept {
    // Staggered slightly so the pair does not resonate as one.
    lowpassA.set(cutoffHz, 0.8, sampleRate);
    lowpassB.set(cutoffHz * 1.15, 0.6, sampleRate);
    lowpassA.reset();
    lowpassB.reset();
    dcX1 = dcY1 = 0.0f;
    drive = (float)(driveAmount < 0.0 ? 0.0 : driveAmount);
  }

  float process(float x) noexcept {
    if (drive > 0.0f) {
      // Asymmetric on purpose: the positive half is shaped harder, which makes
      // even harmonics as well as odd ones.
      const float g = 1.0f + drive;
      x = x > 0.0f ? std::tanh(g * x) / std::tanh(g)
                   : std::tanh(0.7f * g * x) / std::tanh(0.7f * g);
    }
    x = lowpassB.process(lowpassA.process(x, Svf::LowPass), Svf::LowPass);

    const float y = x - dcX1 + 0.995f * dcY1;
    dcX1 = x;
    dcY1 = flush(y);
    return y;
  }
};

// Enough for a 30 ms delay at 96 kHz.
inline constexpr int kChorusCapacity = 4096;

// A stereo chorus, of the kind bolted to the output of every stage polysynth
// of the period.
//
// Worth having as a primitive rather than as a general effect, because on a
// Juno or a Polysix it is not an effect at all -- it is part of the instrument,
// switched on for most of the factory patches, and a large share of what people
// are remembering when they call that sound lush. Underneath it is one short
// delay, modulated, added back to the dry signal: the delay's movement detunes
// the copy slightly and the two beat against each other.
//
// The two sides read the SAME delay line at points a quarter cycle apart. In
// quadrature rather than in antiphase, which is the choice worth explaining:
// antiphase is what the hardware does and is wider, but it also means the two
// sides are always moving in opposite directions, so a mono fold-down cancels
// whatever the modulation has separated. Quadrature is nearly as wide and folds
// down without a comb filter in it -- and a Ninjam room is full of people
// listening on one speaker.
//
// The read is Hermite rather than linear because this delay is swept
// continuously. Linear interpolation's error changes with the fractional part,
// so a slowly moving tap modulates its own high end and the result is a faint
// warble on top of the intended one. It is not in a feedback loop, so the
// stability argument that keeps DelayLine::readLinear inside the string does
// not apply here.
struct Chorus {
  DelayLine<kChorusCapacity> line;
  double phase = 0.0, increment = 0.0;
  double baseSamples = 0.0, depthSamples = 0.0;
  float mix = 0.5f;

  void prepare(double sampleRate, double rateHz, double baseMs, double depthMs,
               float wetMix) noexcept {
    line.clear();
    phase = 0.0;
    increment = sampleRate > 0.0 ? rateHz / sampleRate : 0.0;
    baseSamples = baseMs * sampleRate / 1000.0;
    depthSamples = depthMs * sampleRate / 1000.0;
    // The tap must never reach the write head: Hermite needs two samples
    // either side of it, and a delay of zero is a comb filter at DC.
    if (baseSamples - depthSamples < 4.0)
      depthSamples = std::max(0.0, baseSamples - 4.0);
    mix = wetMix;
  }

  void process(float in, float &outL, float &outR) noexcept {
    line.push(in);

    const double angle = 2.0 * kPi * phase;
    phase += increment;
    if (phase >= 1.0)
      phase -= 1.0;

    outL = in + mix * line.readHermite(baseSamples +
                                       depthSamples * std::sin(angle));
    outR = in + mix * line.readHermite(baseSamples +
                                       depthSamples * std::cos(angle));
  }
};

// Enough for a 37 ms tap and a 47 ms comb at 96 kHz, and small enough that a
// voice can hold one on the stack: three lines at 8192 floats is 98 KB.
inline constexpr int kRoomCapacity = 8192;

// A room, as overheads hear it.
//
// Not a reverb send. The thing that tells you how big a room is, and where you
// are standing in it, is the pattern of the first few reflections -- the floor,
// the walls, the ceiling -- arriving in the first 40 milliseconds. A smooth
// tail with no early pattern reads as an effect; early reflections with barely
// any tail read as a room. So this is mostly taps, with just enough diffusion
// behind them that the taps do not sound like a delay pedal.
//
// Left and right taps differ, which is the whole of the stereo image: two mics
// over a kit are not in the same place, and nothing else here needs to know
// about stereo at all.
struct Room {
  DelayLine<kRoomCapacity> line;
  std::array<float, 4> tapsL{}, tapsR{};
  std::array<float, 4> gainsL{}, gainsR{};

  DelayLine<kRoomCapacity> combL, combR;
  double combDelayL = 0.0, combDelayR = 0.0;
  float combFeedback = 0.0f;
  float dampL = 0.0f, dampR = 0.0f;
  float damping = 0.4f;
  float mix = 0.12f;

  void prepare(double sampleRate, double sizeMetres, float wetMix) noexcept {
    line.clear();
    combL.clear();
    combR.clear();
    dampL = dampR = 0.0f;
    mix = wetMix;

    // Prime-ish millisecond taps so their echoes do not reinforce each other
    // into a pitch, and different on each side so the image is wide.
    const double scale = sizeMetres <= 0.0 ? 1.0 : sizeMetres / 4.0;
    const double msL[4] = {11.0, 17.0, 23.0, 31.0};
    const double msR[4] = {13.0, 19.0, 29.0, 37.0};
    for (int i = 0; i < 4; ++i) {
      tapsL[(size_t)i] = (float)(msL[i] * scale * sampleRate / 1000.0);
      tapsR[(size_t)i] = (float)(msR[i] * scale * sampleRate / 1000.0);
      // Later reflections have travelled further and lost more.
      gainsL[(size_t)i] = (float)(0.7 / (1.0 + 0.9 * (double)i));
      gainsR[(size_t)i] = (float)(0.66 / (1.0 + 0.9 * (double)i));
    }

    combDelayL = 41.0 * scale * sampleRate / 1000.0;
    combDelayR = 47.0 * scale * sampleRate / 1000.0;
    combFeedback = 0.55f;
    damping = 0.4f;
  }

  void process(float in, float &outL, float &outR) noexcept {
    line.push(in);

    float wetL = 0.0f, wetR = 0.0f;
    for (int i = 0; i < 4; ++i) {
      wetL += gainsL[(size_t)i] * line.readHermite((double)tapsL[(size_t)i]);
      wetR += gainsR[(size_t)i] * line.readHermite((double)tapsR[(size_t)i]);
    }

    // A damped comb each side for the tail. Two is not a reverb; it is enough
    // smear that the taps stop sounding like discrete echoes, which is all a
    // short room needs.
    const float tailL = combL.readLinear(combDelayL);
    const float tailR = combR.readLinear(combDelayR);
    dampL = flush(tailL + damping * (dampL - tailL));
    dampR = flush(tailR + damping * (dampR - tailR));
    combL.push(flush(in * 0.5f + dampL * combFeedback));
    combR.push(flush(in * 0.5f + dampR * combFeedback));

    wetL += 0.5f * tailL;
    wetR += 0.5f * tailR;

    outL = in + mix * wetL;
    outR = in + mix * wetR;
  }
};

} // namespace BotDsp
