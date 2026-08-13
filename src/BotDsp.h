#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

// Below this a decaying tail is snapped to zero rather than left to approach it
// forever.
//
// -180 dBFS: two hundred times below the quietest thing 24-bit audio can
// represent, so nothing audible is ever truncated. Denormals do not begin until
// around 1e-38, so a far smaller threshold would still avoid them -- this one is
// chosen so that tails actually END, within a second or so of becoming
// inaudible, rather than ringing at 1e-12 for a minute. That turns "silence" into
// a property a test can assert as an equality instead of a small number.
inline constexpr float kFlushLevel = 1.0e-9f;

inline float flush(float x) noexcept {
  return (x > -kFlushLevel && x < kFlushLevel) ? 0.0f : x;
}

// A state-variable filter: one 12 dB/octave pole pair, four outputs.
//
// Lifted essentially verbatim from chalkwalk/seq_play src/machine/SvfFilter.h,
// which is the Cytomic topology-preserving form. Worth taking rather than
// writing: it is stable at every cutoff up to Nyquist, its modes come from one
// pass, and the coefficients are three multiplies.
//
// It replaces two hand-rolled one-poles in BotVoice -- the snare's lowpass
// accumulator and the hat's highpass-by-subtraction -- neither of which had a
// controllable cutoff or any resonance at all.
struct Svf {
  enum Mode { LowPass = 0, HighPass = 1, BandPass = 2, Notch = 3 };

  float ic1eq = 0.0f, ic2eq = 0.0f;
  float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 0.0f;

  void set(double cutoffHz, double q, double sampleRate) noexcept {
    if (sampleRate <= 0.0)
      return;
    // tan() runs away at Nyquist, so the cutoff is clamped short of it. The
    // floor matters too: a cutoff of zero makes g zero and the filter a wire.
    const double nyquistish = 0.45 * sampleRate;
    const double fc = cutoffHz < 1.0 ? 1.0
                                     : (cutoffHz > nyquistish ? nyquistish
                                                              : cutoffHz);
    const double safeQ = q < 0.05 ? 0.05 : q;
    const double g = std::tan(kPi * fc / sampleRate);
    k = (float)(1.0 / safeQ);
    a1 = (float)(1.0 / (1.0 + g * (g + (double)k)));
    a2 = (float)(g * (double)a1);
    a3 = (float)(g * (double)a2);
  }

  float process(float v, Mode mode) noexcept {
    const float v3 = v - ic2eq;
    const float v1 = a1 * ic1eq + a2 * v3;
    const float v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = flush(2.0f * v1 - ic1eq);
    ic2eq = flush(2.0f * v2 - ic2eq);

    switch (mode) {
    case LowPass:
      return v2;
    case HighPass:
      return v - k * v1 - v2;
    case BandPass:
      return v1;
    default:
      return v - k * v1;
    }
  }

  void reset() noexcept { ic1eq = ic2eq = 0.0f; }
};

// 4-point, 3rd-order Hermite interpolation, from chalkwalk/seq_play
// src/deckcore/Interpolation.h. For fractional reads that are NOT inside a
// feedback loop -- see DelayLine::readLinear for why the loop uses something
// duller.
inline float hermite4(float ym1, float y0, float y1, float y2,
                      float t) noexcept {
  const float c0 = y0;
  const float c1 = 0.5f * (y1 - ym1);
  const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
  const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
  return ((c3 * t + c2) * t + c1) * t + c0;
}

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
    Noise noise(seed);
    Svf shaper;
    const double excitationCutoff = 400.0 + 7000.0 * brightness;
    shaper.set(excitationCutoff, 0.7, sampleRate);

    const int length = (int)period;
    std::array<float, (size_t)kStringCapacity> burst{};
    for (int i = 0; i < length; ++i)
      burst[(size_t)i] = shaper.process(noise.next(), Svf::LowPass);

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
// Ported from chalkwalk/seq_play src/machine/AnalogMachine.cpp. A naive saw
// steps by 2 once per cycle, and that discontinuity has infinite bandwidth, so
// everything above Nyquist folds back down as inharmonic tones -- the sound
// people mean by "cheap digital synth". This subtracts a polynomial
// approximation of the step's spectrum at the moment it happens.
//
// PORTED WITH A CORRECTION, and it is worth knowing about upstream. seq_play
// ADDS the correction to a rising saw and has the two signs of the pulse's
// edges the other way round as well. The polynomial itself carries a downward
// step, so adding it to a saw that already steps downward doubles the
// discontinuity instead of cancelling it. Measured here, aliasing below the
// fundamental of a 5 kHz saw at 48 kHz:
//
//   naive, no correction     0.071
//   seq_play's signs         0.129   -- 82% WORSE than no correction
//   as written below         0.033   -- 53% better
//
// So the upstream oscillators alias harder than if the feature were switched
// off. Found by porting it and testing the claim rather than the code.
inline float polyBlep(double t, double dt) noexcept {
  if (dt <= 0.0)
    return 0.0f;
  if (t < dt) {
    const double x = t / dt;
    return (float)(x + x - x * x - 1.0);
  }
  if (t > 1.0 - dt) {
    const double x = (t - 1.0) / dt;
    return (float)(x * x + x + x + 1.0);
  }
  return 0.0f;
}

// `phase` and `increment` are in cycles, 0..1.
inline float polyBlepSaw(double phase, double increment) noexcept {
  // Subtracted: the saw steps down once a cycle and so does the polynomial.
  return (float)(2.0 * phase - 1.0) - polyBlep(phase, increment);
}

inline float polyBlepPulse(double phase, double increment,
                           double width) noexcept {
  if (width < 0.05)
    width = 0.05;
  if (width > 0.95)
    width = 0.95;

  // Two edges, opposite directions, opposite signs: the pulse steps UP at the
  // start of the cycle and DOWN at the width.
  float s = phase < width ? 1.0f : -1.0f;
  s += polyBlep(phase, increment);
  double shifted = phase - width;
  if (shifted < 0.0)
    shifted += 1.0;
  s -= polyBlep(shifted, increment);
  return s;
}

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
  Svf lowpass;
  float dcX1 = 0.0f, dcY1 = 0.0f;
  float drive = 1.0f;

  void prepare(double sampleRate, double cutoffHz, double driveAmount) noexcept {
    lowpass.set(cutoffHz, 0.8, sampleRate);
    lowpass.reset();
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
    x = lowpass.process(x, Svf::LowPass);

    const float y = x - dcX1 + 0.995f * dcY1;
    dcX1 = x;
    dcY1 = flush(y);
    return y;
  }
};

// Enough for a 40 ms tap and a 90 ms comb at 96 kHz.
inline constexpr int kRoomCapacity = 16384;

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
