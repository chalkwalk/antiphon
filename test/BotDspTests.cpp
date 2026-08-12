#include "../src/AudioMeasure.h"
#include "../src/BotDsp.h"
#include <JuceHeader.h>

// The primitives are arithmetic, so these are exact tests wherever the answer
// is knowable in advance -- a filter's gain at DC, a delay line's contents, an
// interpolator on a straight line -- and measured ones where the claim is about
// sound: that a string plays the pitch it was asked for, that its highs die
// before its fundamental, that a resonator decays in the time it was given.
//
// Measurements come from src/AudioMeasure.h, the same instrument the unit
// suite and the voice lab use, so a number here means the same thing it means
// there.

namespace {

constexpr double kSr = 48000.0;

std::vector<float> sine(double hz, double seconds, double sampleRate = kSr) {
  const int n = (int)(seconds * sampleRate);
  std::vector<float> v((size_t)n);
  for (int i = 0; i < n; ++i)
    v[(size_t)i] =
        (float)std::sin(2.0 * BotDsp::kPi * hz * (double)i / sampleRate);
  return v;
}

// Gain of a filter at one frequency, measured rather than derived: run a sine
// through it, ignore the settling time, compare rms in to rms out.
float gainAt(double hz, BotDsp::Svf filter, BotDsp::Svf::Mode mode,
             double sampleRate = kSr) {
  const auto in = sine(hz, 0.25, sampleRate);
  std::vector<float> out((size_t)in.size());
  for (size_t i = 0; i < in.size(); ++i)
    out[i] = filter.process(in[i], mode);

  const int skip = (int)(0.05 * sampleRate);
  const int n = (int)in.size() - skip;
  if (n <= 0)
    return 0.0f;
  const float a = AudioMeasure::rms(in.data() + skip, n);
  const float b = AudioMeasure::rms(out.data() + skip, n);
  return a > 0.0f ? b / a : 0.0f;
}

bool allFinite(const std::vector<float> &v) {
  for (float x : v)
    if (!std::isfinite(x))
      return false;
  return true;
}

} // namespace

class BotDspTests : public juce::UnitTest {
public:
  BotDspTests() : juce::UnitTest("BotDsp", "music") {}

  void runTest() override {
    runFilterTests();
    runInterpolationTests();
    runDelayTests();
    runStringTests();
    runModalTests();
    runOscillatorTests();
    runCabinetTests();
    runRoomTests();
  }

  void runFilterTests() {
    beginTest("a lowpass passes what is below it and stops what is above");
    {
      BotDsp::Svf f;
      f.set(1000.0, 0.7, kSr);
      expectWithinAbsoluteError(gainAt(100.0, f, BotDsp::Svf::LowPass), 1.0f,
                                0.05f, "100 Hz through a 1 kHz lowpass");
      expect(gainAt(10000.0, f, BotDsp::Svf::LowPass) < 0.05f,
             "10 kHz got through a 1 kHz lowpass");
      // Two poles is 12 dB per octave, so an octave up should be about a
      // quarter. This is what says it is a real filter and not a one-pole.
      const float octaveUp = gainAt(2000.0, f, BotDsp::Svf::LowPass);
      expect(octaveUp > 0.15f && octaveUp < 0.45f,
             "an octave above cutoff measured " + juce::String(octaveUp));
    }

    beginTest("a highpass is the other way round");
    {
      BotDsp::Svf f;
      f.set(1000.0, 0.7, kSr);
      expectWithinAbsoluteError(gainAt(10000.0, f, BotDsp::Svf::HighPass), 1.0f,
                                0.06f);
      expect(gainAt(100.0, f, BotDsp::Svf::HighPass) < 0.05f,
             "100 Hz got through a 1 kHz highpass");
    }

    beginTest("a bandpass peaks where it was tuned");
    {
      BotDsp::Svf f;
      f.set(1000.0, 4.0, kSr);
      const float atCentre = gainAt(1000.0, f, BotDsp::Svf::BandPass);
      const float below = gainAt(200.0, f, BotDsp::Svf::BandPass);
      const float above = gainAt(5000.0, f, BotDsp::Svf::BandPass);
      expect(atCentre > below * 4.0f && atCentre > above * 4.0f,
             "centre " + juce::String(atCentre) + ", below " +
                 juce::String(below) + ", above " + juce::String(above));
    }

    beginTest("a notch removes what it is tuned to");
    {
      BotDsp::Svf f;
      f.set(1000.0, 4.0, kSr);
      expect(gainAt(1000.0, f, BotDsp::Svf::Notch) < 0.2f,
             "the notch did not notch");
      expectWithinAbsoluteError(gainAt(100.0, f, BotDsp::Svf::Notch), 1.0f,
                                0.1f);
    }

    beginTest("the filter is stable at every setting it will be given");
    {
      // Including the settings a caller has no business asking for. A filter
      // that explodes at an extreme cutoff is a filter that explodes when a
      // seed picks an extreme.
      BotDsp::Noise noise(7u);
      std::vector<float> input((size_t)4096);
      for (auto &x : input)
        x = noise.next();

      for (double cutoff : {0.0, 0.5, 1.0, 20.0, 1000.0, 20000.0, 24000.0,
                            48000.0, 1.0e6}) {
        for (double q : {0.05, 0.5, 0.707, 4.0, 20.0, 100.0}) {
          for (auto mode : {BotDsp::Svf::LowPass, BotDsp::Svf::HighPass,
                            BotDsp::Svf::BandPass, BotDsp::Svf::Notch}) {
            BotDsp::Svf f;
            f.set(cutoff, q, kSr);
            std::vector<float> out((size_t)input.size());
            for (size_t i = 0; i < input.size(); ++i)
              out[i] = f.process(input[i], mode);

            expect(allFinite(out), "not finite at cutoff " +
                                       juce::String(cutoff) + " q " +
                                       juce::String(q));
            expect(AudioMeasure::peak(out.data(), (int)out.size()) < 200.0f,
                   "ran away at cutoff " + juce::String(cutoff) + " q " +
                       juce::String(q));
          }
        }
      }
    }

    beginTest("a sample rate of zero leaves the filter alone");
    {
      BotDsp::Svf f;
      f.set(1000.0, 0.7, 0.0);
      expectEquals(f.process(1.0f, BotDsp::Svf::LowPass), 0.0f);
      expect(std::isfinite(f.process(1.0f, BotDsp::Svf::LowPass)));
    }
  }

  void runInterpolationTests() {
    beginTest("interpolating a straight line gives the straight line");
    {
      // Exact, because Hermite through collinear points is that line. If this
      // is ever off by a fraction, the interpolator is bent.
      for (int i = 0; i <= 10; ++i) {
        const float t = (float)i / 10.0f;
        expectWithinAbsoluteError(BotDsp::hermite4(0.0f, 1.0f, 2.0f, 3.0f, t),
                                  1.0f + t, 1.0e-5f);
      }
    }

    beginTest("the ends of an interpolation are the samples themselves");
    {
      expectWithinAbsoluteError(BotDsp::hermite4(3.0f, 7.0f, 11.0f, 2.0f, 0.0f),
                                7.0f, 1.0e-6f);
      expectWithinAbsoluteError(BotDsp::hermite4(3.0f, 7.0f, 11.0f, 2.0f, 1.0f),
                                11.0f, 1.0e-5f);
    }
  }

  void runDelayTests() {
    beginTest("a delay line gives back exactly what was put in");
    {
      BotDsp::DelayLine<64> line;
      line.clear();
      for (int i = 1; i <= 32; ++i)
        line.push((float)i);

      // The last thing pushed is one sample ago.
      expectEquals(line.readInt(1), 32.0f);
      expectEquals(line.readInt(2), 31.0f);
      expectEquals(line.readInt(32), 1.0f);
    }

    beginTest("a fractional read of a ramp is on the ramp");
    {
      // Linear interpolation of linear data is exact, so this is an equality
      // test rather than an approximation.
      BotDsp::DelayLine<64> line;
      line.clear();
      for (int i = 0; i < 40; ++i)
        line.push((float)i);

      expectWithinAbsoluteError(line.readLinear(1.5), 38.5f, 1.0e-4f);
      expectWithinAbsoluteError(line.readLinear(10.25), 29.75f, 1.0e-4f);
      expectWithinAbsoluteError(line.readHermite(10.5), 29.5f, 1.0e-3f);
    }

    beginTest("a delay line wraps");
    {
      BotDsp::DelayLine<8> line;
      line.clear();
      for (int i = 0; i < 100; ++i)
        line.push((float)i);
      expectEquals(line.readInt(1), 99.0f);
      expectEquals(line.readInt(7), 93.0f);
    }

    beginTest("a delay line is not read off its end");
    {
      // ASan is the real check here; this gives it something to look at.
      BotDsp::DelayLine<8> line;
      line.clear();
      for (int i = 0; i < 20; ++i)
        line.push((float)i);
      for (int d = -5; d < 40; ++d)
        expect(std::isfinite(line.readInt(d)));
      for (double d = -5.0; d < 40.0; d += 0.3) {
        expect(std::isfinite(line.readLinear(d)));
        expect(std::isfinite(line.readHermite(d)));
      }
    }
  }

  // A plucked note, rendered into a buffer.
  std::vector<float> pluck(double hz, double seconds, double sampleRate = kSr,
                           double brightness = 0.6, double pick = 0.2,
                           double decay = 2.0) {
    BotDsp::PluckedString s;
    s.pluck(hz, sampleRate, 0.9f, pick, brightness, decay, 12345u);
    std::vector<float> out((size_t)(seconds * sampleRate));
    for (auto &x : out)
      x = s.next();
    return out;
  }

  void runStringTests() {
    beginTest("a string plays the note it was asked for");
    {
      // Across the range the band uses, and at all three sample rates, because
      // every rate-dependent bug this project has had was invisible at one and
      // obvious at another.
      // A tenth of a percent, which is under two cents. That is tight enough
      // to catch the loop filter's own delay being mis-compensated -- the bug
      // that was here, worth up to a third of a percent at the top of the
      // range -- and the string measures an order of magnitude better than it.
      for (double sr : {44100.0, 48000.0, 96000.0}) {
        for (double hz : {41.2, 55.0, 82.4, 110.0, 164.8, 220.0, 330.0, 440.0,
                          660.0}) {
          const auto note = pluck(hz, 0.5, sr);
          const double measured = AudioMeasure::fundamentalHz(
              note.data(), (int)note.size(), sr, 30.0, 900.0);
          expectWithinAbsoluteError(measured, hz, hz * 0.001,
                                    juce::String(hz) + " Hz at " +
                                        juce::String(sr) + " measured " +
                                        juce::String(measured));
        }
      }
    }

    beginTest("a plucked string decays");
    {
      const auto note = pluck(110.0, 3.0);
      const int window = (int)(0.2 * kSr);
      const float early = AudioMeasure::rms(note.data(), window);
      const float late =
          AudioMeasure::rms(note.data() + (int)(2.5 * kSr), window);
      expect(early > 0.01f, "the pluck was silent");
      expect(late < early * 0.5f,
             "it did not decay: " + juce::String(early) + " then " +
                 juce::String(late));
    }

    beginTest("the highs go before the fundamental");
    {
      // The claim the whole model is for, and the one thing no additive voice
      // does: a real string is bright for a tenth of a second and dark for the
      // rest of its life. Measured with brightness, which is an
      // energy-weighted mean frequency and so falls as the harmonics die.
      const auto note = pluck(110.0, 2.0);
      const int window = (int)(0.15 * kSr);
      const double early =
          AudioMeasure::brightnessHz(note.data(), window, kSr);
      const double late = AudioMeasure::brightnessHz(
          note.data() + (int)(1.2 * kSr), window, kSr);
      expect(early > late * 1.3,
             "the timbre did not darken: " + juce::String(early, 1) +
                 " Hz then " + juce::String(late, 1) + " Hz");
    }

    beginTest("the bridge is what darkens the note, not the pluck");
    {
      // Isolating the loop filter, because the first version of this section
      // could not tell it from the excitation: the string darkens as it decays
      // even with no damping at all, since the interpolation in the loop
      // lowpasses a little by itself. Two strings plucked identically, damped
      // differently, so the only difference is the bridge.
      BotDsp::PluckedString soft, hard;
      soft.pluck(110.0, kSr, 0.9f, 0.2, 0.6, 3.0, 99u);
      hard.pluck(110.0, kSr, 0.9f, 0.2, 0.6, 3.0, 99u);
      soft.damping = 0.05f;
      hard.damping = 0.60f;

      std::vector<float> a((size_t)(1.0 * kSr)), b((size_t)(1.0 * kSr));
      for (size_t i = 0; i < a.size(); ++i) {
        a[i] = soft.next();
        b[i] = hard.next();
      }

      const int window = (int)(0.1 * kSr);
      const int late = (int)(0.6 * kSr);
      const double softLate =
          AudioMeasure::brightnessHz(a.data() + late, window, kSr);
      const double hardLate =
          AudioMeasure::brightnessHz(b.data() + late, window, kSr);
      expect(hardLate < softLate * 0.8,
             "damping changed nothing: lightly damped " +
                 juce::String(softLate, 1) + " Hz, heavily damped " +
                 juce::String(hardLate, 1) + " Hz");
    }

    beginTest("a brighter pluck is brighter");
    {
      const auto dull = pluck(110.0, 0.5, kSr, 0.05);
      const auto bright = pluck(110.0, 0.5, kSr, 0.95);
      const double a =
          AudioMeasure::brightnessHz(dull.data(), (int)dull.size(), kSr);
      const double b =
          AudioMeasure::brightnessHz(bright.data(), (int)bright.size(), kSr);
      expect(b > a * 1.2, "brightness did nothing: " + juce::String(a, 1) +
                              " against " + juce::String(b, 1));
    }

    beginTest("pick position changes the tone and not the pitch");
    {
      const auto neck = pluck(110.0, 0.5, kSr, 0.6, 0.45);
      const auto bridge = pluck(110.0, 0.5, kSr, 0.6, 0.05);
      const double pitchA = AudioMeasure::fundamentalHz(
          neck.data(), (int)neck.size(), kSr, 30.0, 600.0);
      const double pitchB = AudioMeasure::fundamentalHz(
          bridge.data(), (int)bridge.size(), kSr, 30.0, 600.0);
      expectWithinAbsoluteError(pitchA, 110.0, 3.0);
      expectWithinAbsoluteError(pitchB, 110.0, 3.0);
      expect(neck != bridge, "pick position changed nothing at all");
    }

    beginTest("muting a string shortens it");
    {
      BotDsp::PluckedString s;
      s.pluck(110.0, kSr, 0.9f, 0.2, 0.6, 3.0, 1u);
      std::vector<float> out((size_t)(1.0 * kSr));
      for (int i = 0; i < (int)out.size(); ++i) {
        if (i == (int)(0.2 * kSr))
          s.mute(kSr, 0.05);
        out[(size_t)i] = s.next();
      }
      const int window = (int)(0.05 * kSr);
      const float before = AudioMeasure::rms(out.data() + (int)(0.1 * kSr), window);
      const float after = AudioMeasure::rms(out.data() + (int)(0.4 * kSr), window);
      expect(after < before * 0.1f,
             "mute did not stop it: " + juce::String(before) + " then " +
                 juce::String(after));
    }

    beginTest("a string stays inside its bounds and goes properly silent");
    {
      // A one-second decay, so the flush is reached inside the buffer and
      // "silent" can be asserted as an equality rather than as a small number.
      const auto note = pluck(55.0, 5.0, kSr, 0.6, 0.2, 1.0);
      expect(allFinite(note), "not finite");
      expect(AudioMeasure::peak(note.data(), (int)note.size()) <= 1.2f,
             "a pluck at velocity 0.9 peaked at " +
                 juce::String(AudioMeasure::peak(note.data(), (int)note.size())));

      // Exactly zero, not merely small: the denormal flush is what makes this
      // an equality, and a decaying loop that never reaches zero is one that
      // spends its old age in denormal arithmetic.
      const int tail = (int)(4.0 * kSr);
      expectEquals(
          AudioMeasure::peak(note.data() + tail, (int)note.size() - tail), 0.0f,
          "the tail never reached zero");
    }

    beginTest("a string refuses what it cannot play");
    {
      for (double hz : {0.0, -100.0, 1.0, 40000.0}) {
        const auto note = pluck(hz, 0.1);
        expect(allFinite(note));
        expectEquals(AudioMeasure::peak(note.data(), (int)note.size()), 0.0f,
                     juce::String(hz) + " Hz should have made no sound");
      }
      const auto noRate = pluck(110.0, 0.1, 0.0);
      expectEquals(AudioMeasure::peak(noRate.data(), (int)noRate.size()), 0.0f);
    }
  }

  void runModalTests() {
    beginTest("a mode rings at the frequency it was given");
    {
      for (double hz : {60.0, 110.0, 220.0, 440.0}) {
        BotDsp::ModalBank bank;
        bank.prepare(kSr);
        bank.addMode(hz, 1.0, 1.0f);

        std::vector<float> out((size_t)(0.5 * kSr));
        for (int i = 0; i < (int)out.size(); ++i)
          out[(size_t)i] = bank.process(i == 0 ? 1.0f : 0.0f);

        const double measured = AudioMeasure::fundamentalHz(
            out.data(), (int)out.size(), kSr, 30.0, 600.0);
        expectWithinAbsoluteError(measured, hz, hz * 0.02,
                                  juce::String(hz) + " Hz mode measured " +
                                      juce::String(measured));
      }
    }

    beginTest("a mode decays in the time it was given");
    {
      // -60 dB after the decay time, which is a thousandth of the level it
      // started at. Measured against the envelope rather than asserted from
      // the coefficient, so a mistake in the coefficient shows up here.
      for (double decay : {0.25, 0.5, 1.0}) {
        BotDsp::ModalBank bank;
        bank.prepare(kSr);
        bank.addMode(200.0, decay, 1.0f);

        std::vector<float> out((size_t)(decay * 1.5 * kSr));
        for (int i = 0; i < (int)out.size(); ++i)
          out[(size_t)i] = bank.process(i == 0 ? 1.0f : 0.0f);

        const int window = (int)(0.02 * kSr);
        const float start = AudioMeasure::peak(out.data(), window);
        const int at = (int)(decay * kSr) - window;
        const float ended = AudioMeasure::peak(out.data() + at, window);
        const float ratio = start > 0.0f ? ended / start : 1.0f;
        expect(ratio > 0.0002f && ratio < 0.006f,
               "decay " + juce::String(decay) + " s ended at " +
                   juce::String(ratio) + " of where it started");
      }
    }

    beginTest("a bank sums its modes and keeps them apart");
    {
      BotDsp::ModalBank bank;
      bank.prepare(kSr);
      bank.addMode(100.0, 0.5, 1.0f);
      bank.addMode(159.3, 0.2, 0.6f); // a membrane ratio, not a harmonic
      bank.addMode(213.6, 0.1, 0.4f);

      std::vector<float> out((size_t)(0.5 * kSr));
      for (int i = 0; i < (int)out.size(); ++i)
        out[(size_t)i] = bank.process(i == 0 ? 1.0f : 0.0f);

      expect(allFinite(out));
      // The upper modes die first, so the sound darkens -- which is what makes
      // a struck membrane a drum rather than a chord.
      const int window = (int)(0.03 * kSr);
      const double early = AudioMeasure::brightnessHz(out.data(), window, kSr);
      const double late = AudioMeasure::brightnessHz(
          out.data() + (int)(0.3 * kSr), window, kSr);
      expect(early > late, "the strike did not darken: " +
                               juce::String(early, 1) + " then " +
                               juce::String(late, 1));
    }

    beginTest("a mode can be retuned while it rings");
    {
      // The kick's pitch drop. Retuning must move the pitch and must not make
      // the resonator unstable.
      BotDsp::ModalBank bank;
      bank.prepare(kSr);
      bank.addMode(190.0, 0.4, 1.0f);

      std::vector<float> out((size_t)(0.3 * kSr));
      for (int i = 0; i < (int)out.size(); ++i) {
        if (i % 32 == 0) {
          const double t = (double)i / kSr;
          bank.setModeFrequency(0, 50.0 + 140.0 * std::exp(-t / 0.03));
        }
        out[(size_t)i] = bank.process(i == 0 ? 1.0f : 0.0f);
      }

      expect(allFinite(out));
      const int window = (int)(0.08 * kSr);
      const double startHz =
          AudioMeasure::fundamentalHz(out.data(), window, kSr, 30.0, 600.0);
      const double endHz = AudioMeasure::fundamentalHz(
          out.data() + (int)(0.15 * kSr), window, kSr, 30.0, 600.0);
      expect(endHz > 0.0 && endHz < startHz,
             "the pitch did not fall: " + juce::String(startHz, 1) + " then " +
                 juce::String(endHz, 1));
    }

    beginTest("a bank refuses what it cannot hold");
    {
      BotDsp::ModalBank bank;
      bank.prepare(kSr);
      for (int i = 0; i < 20; ++i)
        bank.addMode(100.0 + 10.0 * i, 0.5, 1.0f);
      expectEquals(bank.count, BotDsp::kMaxModes, "it took more than it has");

      BotDsp::ModalBank other;
      other.prepare(kSr);
      other.addMode(30000.0, 0.5, 1.0f); // above Nyquist
      other.addMode(0.0, 0.5, 1.0f);
      other.addMode(-100.0, 0.5, 1.0f);
      expectEquals(other.count, 0, "it accepted an impossible mode");
      expectEquals(other.process(1.0f), 0.0f);
      other.setModeFrequency(5, 100.0); // out of range, must not write
      expect(true);
    }
  }

  void runOscillatorTests() {
    beginTest("a band-limited saw aliases far less than a naive one");
    {
      // Aliasing measured directly, with no reference waveform to argue about.
      //
      // A saw at 5 kHz has nothing below 5 kHz in it: its partials are at 5,
      // 10, 15 and 20 kHz and then they run out of room. Everything above
      // Nyquist folds back, and some of it lands underneath the fundamental --
      // the 9th partial at 45 kHz arrives at 3 kHz, the 10th at 2 kHz. So the
      // energy below 4 kHz is aliasing and nothing else, and that is the whole
      // measurement.
      //
      // The first version of this test compared both waveforms against an
      // additive saw truncated at Nyquist, and reported polyBLEP as twice as
      // BAD -- because at 5 kHz that sum has four terms and is mostly Gibbs
      // ringing, so it flattered whichever waveform happened to wobble like it.
      // Measuring the defect itself needs no reference and cannot be gamed
      // that way.
      const double f0 = 5000.0;
      const double inc = f0 / kSr;
      const int n = (int)(0.5 * kSr);

      auto belowFundamental = [&](const std::vector<float> &v) {
        // Three cascaded lowpasses well under f0, so what is left is only what
        // should not have been there.
        BotDsp::Svf a, b, c;
        a.set(3500.0, 0.7, kSr);
        b.set(3500.0, 0.7, kSr);
        c.set(3500.0, 0.7, kSr);
        std::vector<float> out((size_t)v.size());
        for (size_t i = 0; i < v.size(); ++i)
          out[i] = c.process(b.process(a.process(v[i], BotDsp::Svf::LowPass),
                                       BotDsp::Svf::LowPass),
                             BotDsp::Svf::LowPass);
        const int skip = (int)(0.05 * kSr);
        return AudioMeasure::rms(out.data() + skip, (int)out.size() - skip);
      };

      std::vector<float> naive((size_t)n), blep((size_t)n);
      double phase = 0.0;
      for (int i = 0; i < n; ++i) {
        naive[(size_t)i] = (float)(2.0 * phase - 1.0);
        blep[(size_t)i] = BotDsp::polyBlepSaw(phase, inc);
        phase += inc;
        if (phase >= 1.0)
          phase -= 1.0;
      }

      const float naiveAlias = belowFundamental(naive);
      const float blepAlias = belowFundamental(blep);
      expect(blepAlias < naiveAlias * 0.5f,
             "aliasing below the fundamental: naive " +
                 juce::String(naiveAlias, 5) + ", polyBLEP " +
                 juce::String(blepAlias, 5));
    }

    beginTest("a pulse has the width it was given");
    {
      for (double width : {0.25, 0.5, 0.75}) {
        const int n = 48000;
        const double inc = 100.0 / kSr;
        double phase = 0.0;
        int high = 0;
        for (int i = 0; i < n; ++i) {
          if (BotDsp::polyBlepPulse(phase, inc, width) > 0.0f)
            ++high;
          phase += inc;
          if (phase >= 1.0)
            phase -= 1.0;
        }
        expectWithinAbsoluteError((double)high / (double)n, width, 0.03,
                                  "width " + juce::String(width));
      }
    }

    beginTest("the oscillators stay in bounds at every frequency");
    {
      for (double hz : {20.0, 440.0, 5000.0, 15000.0, 23000.0}) {
        const double inc = hz / kSr;
        double phase = 0.0;
        float worst = 0.0f;
        for (int i = 0; i < 20000; ++i) {
          worst = std::max(worst, std::abs(BotDsp::polyBlepSaw(phase, inc)));
          worst = std::max(worst,
                           std::abs(BotDsp::polyBlepPulse(phase, inc, 0.5)));
          phase += inc;
          if (phase >= 1.0)
            phase -= 1.0;
        }
        expect(worst < 2.5f, juce::String(hz) + " Hz reached " +
                                 juce::String(worst));
      }
    }
  }

  void runCabinetTests() {
    beginTest("a cabinet takes the top off");
    {
      BotDsp::Cabinet cab;
      cab.prepare(kSr, 4000.0, 0.3);

      BotDsp::Noise noise(3u);
      std::vector<float> in((size_t)(0.3 * kSr)), out((size_t)(0.3 * kSr));
      for (size_t i = 0; i < in.size(); ++i) {
        in[i] = 0.5f * noise.next();
        out[i] = cab.process(in[i]);
      }

      const double before =
          AudioMeasure::brightnessHz(in.data(), (int)in.size(), kSr);
      const double after =
          AudioMeasure::brightnessHz(out.data(), (int)out.size(), kSr);
      expect(after < before * 0.5,
             "brightness went from " + juce::String(before, 1) + " to " +
                 juce::String(after, 1));
    }

    beginTest("a driven cabinet does not push out a DC offset");
    {
      // Asymmetric shaping makes DC by construction, and DC eats headroom in a
      // mix that has none. The blocker is why this is a test rather than a
      // known flaw.
      BotDsp::Cabinet cab;
      cab.prepare(kSr, 5000.0, 2.0);

      const auto in = sine(200.0, 0.5);
      std::vector<float> out((size_t)in.size());
      for (size_t i = 0; i < in.size(); ++i)
        out[i] = cab.process(0.9f * in[i]);

      const int skip = (int)(0.1 * kSr);
      double mean = 0.0;
      for (size_t i = (size_t)skip; i < out.size(); ++i)
        mean += (double)out[i];
      mean /= (double)((int)out.size() - skip);
      expect(std::abs(mean) < 0.01,
             "DC offset of " + juce::String(mean, 5));
      expect(allFinite(out));
    }
  }

  void runRoomTests() {
    beginTest("a room answers a click, in stereo, and then stops");
    {
      BotDsp::Room room;
      room.prepare(kSr, 4.0, 0.5f);

      const int n = (int)(2.0 * kSr);
      std::vector<float> left((size_t)n), right((size_t)n);
      for (int i = 0; i < n; ++i)
        room.process(i == 0 ? 1.0f : 0.0f, left[(size_t)i], right[(size_t)i]);

      expect(allFinite(left) && allFinite(right));

      // Reflections arrive after the dry click and before 50 ms.
      const int early = (int)(0.005 * kSr);
      const int window = (int)(0.045 * kSr);
      expect(AudioMeasure::peak(left.data() + early, window) > 0.01f,
             "no early reflections");

      // The two sides differ, which is the whole of the stereo image.
      expect(left != right, "both channels are identical");

      // And it is over. Exactly zero, thanks to the flush.
      const int tail = (int)(1.8 * kSr);
      expectEquals(AudioMeasure::peak(left.data() + tail, n - tail), 0.0f,
                   "the tail never ended");
    }

    beginTest("a dry room is the signal itself");
    {
      BotDsp::Room room;
      room.prepare(kSr, 4.0, 0.0f);
      const auto in = sine(220.0, 0.2);
      std::vector<float> l((size_t)in.size()), r((size_t)in.size());
      for (size_t i = 0; i < in.size(); ++i)
        room.process(in[i], l[i], r[i]);
      expect(l == in, "a zero mix changed the signal");
      expect(r == in, "a zero mix changed the signal");
    }
  }
};

static BotDspTests botDspTests;
