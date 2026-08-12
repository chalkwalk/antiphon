#include "../src/AudioMeasure.h"
#include <JuceHeader.h>

// Calibrating the instruments, before anything is measured with them.
//
// Every signal here has an answer known in advance -- a sine at 220 Hz is at
// 220 Hz -- so a detector that is wrong is caught by arithmetic rather than by
// a synthesis test failing for reasons nobody can localise. That is not
// hypothetical: this project has three measurement errors on record, one of
// them chased all the way through a fix before the instrument was suspected
// (`PRINCIPLES §5`, `docs/COMPLETED.md` Withdrawn).

namespace {

constexpr double kSr = 48000.0;

std::vector<float> sine(double hz, double seconds, float amplitude = 1.0f,
                        double sampleRate = kSr) {
  const int n = (int)(seconds * sampleRate);
  std::vector<float> v((size_t)n);
  for (int i = 0; i < n; ++i)
    v[(size_t)i] = amplitude * (float)std::sin(2.0 * AudioMeasure::kPi * hz *
                                               (double)i / sampleRate);
  return v;
}

// A square wave, which has the same fundamental as a sine and much more energy
// up high -- so it separates "what note is this" from "how bright is this".
std::vector<float> square(double hz, double seconds, float amplitude = 1.0f) {
  const int n = (int)(seconds * kSr);
  std::vector<float> v((size_t)n);
  double phase = 0.0;
  for (int i = 0; i < n; ++i) {
    phase += hz / kSr;
    if (phase >= 1.0)
      phase -= 1.0;
    v[(size_t)i] = phase < 0.5 ? amplitude : -amplitude;
  }
  return v;
}

} // namespace

class AudioMeasureTests : public juce::UnitTest {
public:
  AudioMeasureTests() : juce::UnitTest("AudioMeasure", "music") {}

  void runTest() override {
    runLevelTests();
    runBrightnessTests();
    runPitchTests();
    runRobustnessTests();
  }

  void runLevelTests() {
    beginTest("level is measured the way the arithmetic says");
    {
      const auto s = sine(1000.0, 0.5, 0.5f);
      expectWithinAbsoluteError(AudioMeasure::peak(s.data(), (int)s.size()),
                                0.5f, 0.001f);
      // A sine's rms is its peak over root two.
      expectWithinAbsoluteError(AudioMeasure::rms(s.data(), (int)s.size()),
                                0.5f / (float)std::sqrt(2.0), 0.001f);
      expectWithinAbsoluteError(AudioMeasure::crest(s.data(), (int)s.size()),
                                (float)std::sqrt(2.0), 0.01f);
    }

    beginTest("crest factor tells a transient from a tone");
    {
      // The number the kick is held to. A steady sine sits at 1.41; the same
      // sine with a decay envelope on it is far spikier, and that difference is
      // the whole reason the measure is used.
      const auto steady = sine(60.0, 0.3);
      auto decaying = steady;
      for (size_t i = 0; i < decaying.size(); ++i)
        decaying[i] *= (float)std::exp(-6.9078 * (double)i / (double)decaying.size());

      const float flat = AudioMeasure::crest(steady.data(), (int)steady.size());
      const float spiky =
          AudioMeasure::crest(decaying.data(), (int)decaying.size());
      expect(spiky > flat * 1.8f,
             "a decaying sine should be much spikier than a steady one: " +
                 juce::String(spiky) + " against " + juce::String(flat));
    }

    beginTest("decibels");
    {
      expectWithinAbsoluteError(AudioMeasure::toDb(1.0), 0.0, 0.001);
      expectWithinAbsoluteError(AudioMeasure::toDb(0.5), -6.0206, 0.001);
      expect(AudioMeasure::toDb(0.0) < -200.0, "silence must not be infinite");
    }
  }

  void runBrightnessTests() {
    beginTest("brightness reads a pure tone as its own frequency");
    {
      // The calibration that makes the measure worth having: exact for a sine,
      // at any frequency, because the discrete difference's gain is inverted
      // rather than approximated.
      for (double hz : {50.0, 110.0, 440.0, 1000.0, 5000.0, 12000.0}) {
        const auto s = sine(hz, 0.25);
        const double measured =
            AudioMeasure::brightnessHz(s.data(), (int)s.size(), kSr);
        expectWithinAbsoluteError(measured, hz, hz * 0.02,
                                  "brightness of a " + juce::String(hz) +
                                      " Hz sine read " +
                                      juce::String(measured));
      }
    }

    beginTest("brightness rises with harmonic content at the same pitch");
    {
      // The property the bass and the pad are compared on. Both signals are at
      // 110 Hz; only one of them is bright, and a measure that could not tell
      // them apart would be measuring pitch under another name.
      const auto pure = sine(110.0, 0.5);
      const auto rich = square(110.0, 0.5);
      const double dull =
          AudioMeasure::brightnessHz(pure.data(), (int)pure.size(), kSr);
      const double bright =
          AudioMeasure::brightnessHz(rich.data(), (int)rich.size(), kSr);
      expect(bright > dull * 2.0,
             "a square at 110 Hz should read far brighter than a sine at 110: " +
                 juce::String(bright) + " against " + juce::String(dull));
    }

    beginTest("brightness ignores how loud the signal is");
    {
      const auto loud = sine(440.0, 0.25, 0.9f);
      const auto quiet = sine(440.0, 0.25, 0.02f);
      const double a =
          AudioMeasure::brightnessHz(loud.data(), (int)loud.size(), kSr);
      const double b =
          AudioMeasure::brightnessHz(quiet.data(), (int)quiet.size(), kSr);
      expectWithinAbsoluteError(a, b, 1.0, "level changed the brightness");
    }

    beginTest("brightness ignores a DC offset");
    {
      auto s = sine(440.0, 0.25, 0.4f);
      for (auto &v : s)
        v += 0.5f;
      expectWithinAbsoluteError(
          AudioMeasure::brightnessHz(s.data(), (int)s.size(), kSr), 440.0, 10.0);
    }

    beginTest("the two brightness instruments agree on a sine and may not elsewhere");
    {
      // Crossing rate and brightness are independent methods, which is why both
      // are kept. On a clean sine they must agree; the value of the pair is
      // that on a lopsided waveform they need not, and the disagreement is the
      // warning.
      const auto s = sine(300.0, 0.25);
      const double crossings =
          AudioMeasure::crossingRateHz(s.data(), (int)s.size(), kSr);
      const double slope =
          AudioMeasure::brightnessHz(s.data(), (int)s.size(), kSr);
      expectWithinAbsoluteError(crossings, 300.0, 5.0);
      expectWithinAbsoluteError(slope, 300.0, 5.0);
    }
  }

  void runPitchTests() {
    beginTest("the fundamental is found, and it is the fundamental");
    {
      for (double hz : {41.2, 55.0, 82.4, 110.0, 220.0, 440.0}) {
        const auto s = sine(hz, 0.5);
        const double measured =
            AudioMeasure::fundamentalHz(s.data(), (int)s.size(), kSr);
        expectWithinAbsoluteError(measured, hz, hz * 0.02,
                                  juce::String(hz) + " Hz sine read " +
                                      juce::String(measured));
      }
    }

    beginTest("a rich waveform does not read an octave or a twelfth out");
    {
      // The failure this detector was built for. A period of 3T correlates
      // nearly as well as T, so a naive peak-picker reports a third of the
      // pitch -- which is exactly what happened to a B2 bass, convincingly
      // enough to look like a synthesis bug.
      for (double hz : {55.0, 110.0, 220.0}) {
        const auto s = square(hz, 0.5);
        const double measured =
            AudioMeasure::fundamentalHz(s.data(), (int)s.size(), kSr);
        expectWithinAbsoluteError(measured, hz, hz * 0.03,
                                  "square at " + juce::String(hz) +
                                      " read " + juce::String(measured));
      }
    }

    beginTest("a sum of harmonics reads as its fundamental");
    {
      const int n = (int)(0.5 * kSr);
      std::vector<float> v((size_t)n, 0.0f);
      const double f0 = 98.0;
      for (int i = 0; i < n; ++i) {
        const double t = (double)i / kSr;
        v[(size_t)i] =
            (float)(0.6 * std::sin(2.0 * AudioMeasure::kPi * f0 * t) +
                    0.9 * std::sin(2.0 * AudioMeasure::kPi * f0 * 2.0 * t) +
                    0.5 * std::sin(2.0 * AudioMeasure::kPi * f0 * 3.0 * t));
      }
      // The second harmonic is the loudest partial, so a peak-picking detector
      // would say 196. The period is still 1/98.
      expectWithinAbsoluteError(
          AudioMeasure::fundamentalHz(v.data(), n, kSr), f0, 3.0);
    }

    beginTest("noise is refused rather than given a pitch");
    {
      std::uint32_t state = 12345u;
      std::vector<float> v((size_t)(0.3 * kSr));
      for (auto &x : v) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        x = (float)((double)(state >> 8) / 8388608.0 - 1.0);
      }
      expectEquals(AudioMeasure::fundamentalHz(v.data(), (int)v.size(), kSr),
                   0.0, "noise was given a pitch");
    }

    beginTest("a note is found wherever it starts");
    {
      // Half a second of silence, then the note. A fixed window from the start
      // would read the silence and report nothing.
      std::vector<float> v((size_t)(0.5 * kSr), 0.0f);
      const auto note = sine(147.0, 0.5, 0.8f);
      v.insert(v.end(), note.begin(), note.end());

      const int beat = (int)(kSr * 0.5);
      expectWithinAbsoluteError(
          AudioMeasure::firstNoteHz(v.data(), (int)v.size(), kSr, beat), 147.0,
          4.0);
    }

    beginTest("a frequency names a note");
    {
      expectWithinAbsoluteError(AudioMeasure::midiForHz(440.0), 69.0, 0.001);
      expectEquals(AudioMeasure::pitchClassForHz(440.0), 9); // A
      expectEquals(AudioMeasure::pitchClassForHz(261.63), 0); // middle C
      expectEquals(AudioMeasure::pitchClassForHz(65.41), 0);  // C2
      expectEquals(AudioMeasure::pitchClassForHz(0.0), -1);
    }
  }

  void runRobustnessTests() {
    beginTest("an instrument reading nothing says nothing");
    {
      std::vector<float> silence((size_t)1024, 0.0f);
      expectEquals(AudioMeasure::peak(silence.data(), 1024), 0.0f);
      expectEquals(AudioMeasure::rms(silence.data(), 1024), 0.0f);
      expectEquals(AudioMeasure::crest(silence.data(), 1024), 0.0f);
      expectEquals(AudioMeasure::brightnessHz(silence.data(), 1024, kSr), 0.0);
      expectEquals(AudioMeasure::fundamentalHz(silence.data(), 1024, kSr), 0.0);
      expectEquals(AudioMeasure::firstNoteHz(silence.data(), 1024, kSr, 512),
                   0.0);

      // A constant is not silence, but it has no pitch and no brightness.
      std::vector<float> dc((size_t)1024, 0.7f);
      expectEquals(AudioMeasure::brightnessHz(dc.data(), 1024, kSr), 0.0);
      expectEquals(AudioMeasure::fundamentalHz(dc.data(), 1024, kSr), 0.0);
    }

    beginTest("nothing is read off the end of a buffer");
    {
      // ASan is the real check; this is what gives it something to look at.
      const auto s = sine(200.0, 0.05);
      for (int n : {0, 1, 2, 63, 64, 65, 100}) {
        AudioMeasure::peak(s.data(), n);
        AudioMeasure::rms(s.data(), n);
        AudioMeasure::crest(s.data(), n);
        AudioMeasure::brightnessHz(s.data(), n, kSr);
        AudioMeasure::crossingRateHz(s.data(), n, kSr);
        AudioMeasure::fundamentalHz(s.data(), n, kSr);
        AudioMeasure::firstNoteHz(s.data(), n, kSr, 32);
      }
      AudioMeasure::peak(nullptr, 100);
      AudioMeasure::rms(nullptr, 100);
      AudioMeasure::brightnessHz(nullptr, 100, kSr);
      AudioMeasure::fundamentalHz(nullptr, 100, kSr);
      AudioMeasure::firstNoteHz(nullptr, 100, kSr, 32);
      expect(true);
    }

    beginTest("a sample rate of zero is not divided by");
    {
      const auto s = sine(200.0, 0.1);
      expectEquals(AudioMeasure::brightnessHz(s.data(), (int)s.size(), 0.0), 0.0);
      expectEquals(AudioMeasure::crossingRateHz(s.data(), (int)s.size(), 0.0),
                   0.0);
      expectEquals(AudioMeasure::fundamentalHz(s.data(), (int)s.size(), 0.0),
                   0.0);
    }

    beginTest("the detectors work at 44.1 kHz as well as 48");
    {
      // Every rate-dependent bug this project has had was invisible at one rate
      // and obvious at the other.
      for (double sr : {44100.0, 48000.0, 96000.0}) {
        const auto s = sine(220.0, 0.4, 1.0f, sr);
        expectWithinAbsoluteError(
            AudioMeasure::fundamentalHz(s.data(), (int)s.size(), sr), 220.0, 5.0,
            "pitch at " + juce::String(sr));
        expectWithinAbsoluteError(
            AudioMeasure::brightnessHz(s.data(), (int)s.size(), sr), 220.0, 6.0,
            "brightness at " + juce::String(sr));
      }
    }
  }
};

static AudioMeasureTests audioMeasureTests;
