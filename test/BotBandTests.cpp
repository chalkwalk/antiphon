#include "../src/BotBand.h"
#include "../src/BotVoice.h"
#include <JuceHeader.h>

// Two kinds of assertion here, and the split is the point (AGENTS.md).
//
// The pattern layer is exact -- a figure either has three pulses or it does
// not -- so it gets ordinary equality tests.
//
// The audio can only be measured statistically. RMS, where the energy sits in
// time, and pitch by zero crossings. Asserting sample values against the
// synthesis formula would only assert that the formula is the formula.

namespace {

MusicalKey::Key keyOf(const juce::String &name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

BotBand::Settings settingsFor(const juce::String &keyName, int bpm = 120,
                              int bpi = 8, std::uint32_t seed = 12345) {
  return BotBand::defaults(keyOf(keyName), bpm, bpi, 48000.0, seed);
}

int intervalSamplesFor(const BotBand::Settings &s) {
  // The same truncating arithmetic the interval clock uses.
  return (int)(s.sampleRate * 60.0 / s.bpm) * s.bpi;
}

float rms(const std::vector<float> &v, int from, int to) {
  from = juce::jmax(0, from);
  to = juce::jmin((int)v.size(), to);
  if (to <= from)
    return 0.0f;
  double sum = 0.0;
  for (int i = from; i < to; ++i)
    sum += (double)v[(size_t)i] * v[(size_t)i];
  return (float)std::sqrt(sum / (double)(to - from));
}

std::vector<float> render(BotBand::Voice voice, const BotBand::Settings &s,
                          int intervalIndex = 0) {
  const int n = intervalSamplesFor(s);
  std::vector<float> buf((size_t)n, 0.0f);
  BotBand::renderInterval(voice, s, intervalIndex, buf.data(), n);
  return buf;
}

} // namespace

class BotBandTests : public juce::UnitTest {
public:
  BotBandTests() : juce::UnitTest("BotBand", "music") {}

  void runTest() override {
    runSeedTests();
    runFigureTests();
    runAudioTests();
    runHarmonyFollowingTests();
    runRobustnessTests();
    writeAuditionIfAsked();
  }

  // Opt-in, like RealServerTests: set ANTIPHON_BAND_WAV to a path and the suite
  // writes eight intervals of the band there.
  //
  // Every other assertion in this file is statistical, and statistics cannot
  // tell you whether a groove is any good. This is how you check that by ear,
  // and it costs nothing when the variable is unset.
  void writeAuditionIfAsked() {
    const auto path = juce::SystemStats::getEnvironmentVariable(
        "ANTIPHON_BAND_WAV", juce::String());
    if (path.isEmpty())
      return;

    beginTest("writing an audition to " + path);

    const auto keyName = juce::SystemStats::getEnvironmentVariable(
        "ANTIPHON_BAND_KEY", "C major");
    const int bpm = juce::SystemStats::getEnvironmentVariable("ANTIPHON_BAND_BPM",
                                                              "120").getIntValue();
    const int bpi = juce::SystemStats::getEnvironmentVariable("ANTIPHON_BAND_BPI",
                                                              "8").getIntValue();
    const int seed = juce::SystemStats::getEnvironmentVariable(
        "ANTIPHON_BAND_SEED", "20260811").getIntValue();

    auto key = MusicalKey::parseName(keyName);
    if (!key.valid)
      key = MusicalKey::parseName("C major");

    const int intervals = 8;
    juce::AudioBuffer<float> mix(2, 0);

    for (int i = 0; i < intervals; ++i) {
      std::vector<float> acc;
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys}) {
        // A different base seed per voice, as PracticeRoom does.
        std::uint32_t s = (std::uint32_t)seed;
        for (int step = 0; step < (int)voice; ++step)
          s = s * 1664525u + 1013904223u;

        auto settings = BotBand::defaults(key, bpm, bpi, 48000.0, s);
        const int n = intervalSamplesFor(settings);
        if (acc.empty())
          acc.assign((size_t)n, 0.0f);

        // The far end applies kDefaultRemoteChannelVolume to every remote
        // channel, so mix at that level or the audition is 12 dB hotter than
        // the room.
        std::vector<float> one((size_t)n, 0.0f);
        BotBand::renderInterval(voice, settings, i, one.data(), n);
        for (int j = 0; j < n; ++j)
          acc[(size_t)j] += 0.25f * one[(size_t)j];
      }

      const int start = mix.getNumSamples();
      mix.setSize(2, start + (int)acc.size(), true, true, true);
      for (int ch = 0; ch < 2; ++ch)
        for (size_t j = 0; j < acc.size(); ++j)
          mix.setSample(ch, start + (int)j, acc[j]);
    }

    juce::File out(path);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(new juce::FileOutputStream(out), 48000.0, 2, 24,
                            {}, 0));
    if (writer == nullptr) {
      expect(false, "could not open " + path);
      return;
    }
    writer->writeFromAudioSampleBuffer(mix, 0, mix.getNumSamples());
    writer.reset();

    logMessage("wrote " + juce::String(intervals) + " intervals of " + keyName +
               " at " + juce::String(bpm) + " bpm, " + juce::String(bpi) +
               " bpi, seed " + juce::String(seed) + " to " + path);
    expect(true);
  }

  void runSeedTests() {
    beginTest("salting makes the voices differ from one seed");
    {
      // Without it, one seed gives the bass the kick's pattern note for note.
      const auto d = BotBand::saltedSeed(BotBand::Voice::Drums, 1);
      const auto b = BotBand::saltedSeed(BotBand::Voice::Bass, 1);
      const auto k = BotBand::saltedSeed(BotBand::Voice::Keys, 1);
      expect(d != b && b != k && d != k, "two voices share a salted seed");
    }

    beginTest("neighbouring seeds are not neighbouring patterns");
    {
      // A hash rather than an offset, so "shake" reliably changes something.
      const auto a = BotBand::saltedSeed(BotBand::Voice::Drums, 1);
      const auto b = BotBand::saltedSeed(BotBand::Voice::Drums, 2);
      expect(std::abs((long long)a - (long long)b) > 1000,
             "seeds 1 and 2 gave adjacent values");
    }

    beginTest("the same seed gives the same interval, every time");
    {
      const auto s = settingsFor("C major");
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys}) {
        const auto a = render(voice, s);
        const auto b = render(voice, s);
        expect(a == b, juce::String(BotBand::voiceName(voice)) +
                           " is not reproducible");
      }
    }

    beginTest("a different seed gives a different interval");
    {
      const auto a = render(BotBand::Voice::Drums, settingsFor("C major", 120, 8, 1));
      const auto b = render(BotBand::Voice::Drums, settingsFor("C major", 120, 8, 999));
      expect(a != b, "rerolling the seed changed nothing");
    }
  }

  void runFigureTests() {
    beginTest("a figure fits the interval and has onsets");
    {
      for (int bpi : {4, 8, 12, 16, 24}) {
        const auto s = settingsFor("C major", 120, bpi);
        for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                           BotBand::Voice::Keys}) {
          const auto f = BotBand::figureFor(voice, s);
          expect(f.steps > 0, "no steps");
          expect(f.pulses > 0, "no pulses at bpi " + juce::String(bpi));
          expect(f.pulses <= f.steps, "more pulses than steps");
        }
      }
    }

    beginTest("the bass is locked to the kick, not rolling its own");
    {
      // Two unrelated Euclidean patterns fight; a bass line that shares the
      // kick's density and differs only by displacement locks to it.
      for (std::uint32_t seed : {1u, 7u, 4242u}) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto kick = BotBand::figureFor(BotBand::Voice::Drums, s);
        const auto bass = BotBand::figureFor(BotBand::Voice::Bass, s);
        expectEquals(bass.pulses, kick.pulses,
                     "seed " + juce::String((int)seed) + " density");
        expectEquals(bass.steps, kick.steps);
      }
    }

    beginTest("the keys report one pulse per chord");
    {
      const auto s = settingsFor("C major");
      const auto f = BotBand::figureFor(BotBand::Voice::Keys, s);
      expectEquals(f.pulses, (int)s.progression.size());
    }
  }

  void runAudioTests() {
    beginTest("every voice makes a sound");
    {
      const auto s = settingsFor("C major");
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys}) {
        const auto buf = render(voice, s);
        const float level = rms(buf, 0, (int)buf.size());
        expect(level > 0.005f, juce::String(BotBand::voiceName(voice)) +
                                   " was silent, rms " + juce::String(level));
      }
    }

    beginTest("nothing clips");
    {
      // Three voices are summed by the room, so each must leave headroom.
      for (int bpi : {4, 8, 16})
        for (std::uint32_t seed : {1u, 55u, 900u}) {
          const auto s = settingsFor("C major", 120, bpi, seed);
          for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                             BotBand::Voice::Keys}) {
            const auto buf = render(voice, s);
            float peak = 0.0f;
            for (float x : buf)
              peak = juce::jmax(peak, std::abs(x));
            expect(peak <= 1.0f, juce::String(BotBand::voiceName(voice)) +
                                     " peaked at " + juce::String(peak) +
                                     " (bpi " + juce::String(bpi) + ")");
          }
        }
    }

    beginTest("the interval opens with a downbeat");
    {
      // Every interval is a complete musical unit, so the first beat has to
      // land -- that is what a listener syncs to.
      const auto s = settingsFor("C major");
      const auto buf = render(BotBand::Voice::Drums, s);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);
      const float onset = rms(buf, 0, beat / 4);
      expect(onset > 0.02f, "no downbeat, rms " + juce::String(onset));
    }

    beginTest("the drums put energy on more than one beat");
    {
      const auto s = settingsFor("C major", 120, 8);
      const auto buf = render(BotBand::Voice::Drums, s);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);

      int loudBeats = 0;
      for (int b = 0; b < s.bpi; ++b)
        if (rms(buf, b * beat, b * beat + beat / 4) > 0.01f)
          ++loudBeats;
      expect(loudBeats >= 3, "only " + juce::String(loudBeats) +
                                 " beats had any energy");
    }

    beginTest("the bass sits below the chords");
    {
      // The registers must not collide, or the band is mud. Compare where the
      // energy is rather than what the notes are.
      const auto s = settingsFor("C major");
      expect(dominantHz(render(BotBand::Voice::Bass, s), s.sampleRate) <
                 dominantHz(render(BotBand::Voice::Keys, s), s.sampleRate),
             "the bass is not below the keys");
    }

    beginTest("a fill lands every fourth interval and not otherwise");
    {
      const auto s = settingsFor("C major", 120, 8);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);
      const int lastBeat = (s.bpi - 1) * beat;

      const auto plain = render(BotBand::Voice::Drums, s, 0);
      const auto filled = render(BotBand::Voice::Drums, s, 3);

      const float plainEnd = rms(plain, lastBeat, lastBeat + beat);
      const float filledEnd = rms(filled, lastBeat, lastBeat + beat);
      expect(filledEnd > plainEnd,
             "the fill added nothing: " + juce::String(plainEnd) + " -> " +
                 juce::String(filledEnd));
    }

    beginTest("consecutive intervals are not bit-identical");
    {
      // A band repeats; a loop is identical. The hats carry the difference.
      const auto s = settingsFor("C major");
      expect(render(BotBand::Voice::Drums, s, 0) !=
                 render(BotBand::Voice::Drums, s, 1),
             "every interval was the same");
    }
  }

  void runHarmonyFollowingTests() {
    beginTest("changing the key changes what is played");
    {
      const auto c = settingsFor("C major");
      auto fSharp = settingsFor("F# major");
      fSharp.seed = c.seed;
      expect(render(BotBand::Voice::Keys, c) !=
                 render(BotBand::Voice::Keys, fSharp),
             "the band ignored the key");
    }

    beginTest("a minor key is played minor");
    {
      const auto s = settingsFor("A minor");
      expectEquals((int)s.progression.size(), 4);
      expectEquals(s.progression[0].root, 9);
      expect(s.progression[0].quality == Harmony::Quality::Minor);
    }

    beginTest("an announced progression is played instead of the default");
    {
      auto s = settingsFor("C major");
      s.progression = {Harmony::chordOn(2, Harmony::Quality::Minor),
                       Harmony::chordOn(7, Harmony::Quality::Dominant7)};

      const auto f = BotBand::figureFor(BotBand::Voice::Keys, s);
      expectEquals(f.pulses, 2, "the keys did not take the announced chords");

      auto def = settingsFor("C major");
      expect(render(BotBand::Voice::Keys, s) != render(BotBand::Voice::Keys, def),
             "the announced progression sounded like the default");
    }

    beginTest("tempo and BPI change the length, not the shape");
    {
      for (int bpm : {60, 120, 180})
        for (int bpi : {4, 8, 16}) {
          const auto s = settingsFor("C major", bpm, bpi);
          const auto buf = render(BotBand::Voice::Drums, s);
          expectEquals((int)buf.size(), intervalSamplesFor(s),
                       "wrong length at " + juce::String(bpm) + "/" +
                           juce::String(bpi));
          expect(rms(buf, 0, (int)buf.size()) > 0.005f,
                 "silent at " + juce::String(bpm) + "/" + juce::String(bpi));
        }
    }
  }

  void runRobustnessTests() {
    beginTest("nonsense settings render nothing rather than crashing");
    {
      std::vector<float> buf(4096, 0.0f);

      auto bad = settingsFor("C major");
      bad.bpi = 0;
      BotBand::renderInterval(BotBand::Voice::Drums, bad, 0, buf.data(),
                              (int)buf.size());

      bad = settingsFor("C major");
      bad.sampleRate = 0.0;
      BotBand::renderInterval(BotBand::Voice::Bass, bad, 0, buf.data(),
                              (int)buf.size());

      bad = settingsFor("C major");
      bad.progression.clear();
      BotBand::renderInterval(BotBand::Voice::Keys, bad, 0, buf.data(),
                              (int)buf.size());

      auto ok = settingsFor("C major");
      BotBand::renderInterval(BotBand::Voice::Drums, ok, 0, nullptr, 1024);
      BotBand::renderInterval(BotBand::Voice::Drums, ok, 0, buf.data(), 0);
      BotBand::renderInterval(BotBand::Voice::Drums, ok, -5, buf.data(),
                              (int)buf.size());

      expect(true, "survived");
    }

    beginTest("an invalid key still produces a playable band");
    {
      MusicalKey::Key none;
      auto s = BotBand::defaults(none, 120, 8, 48000.0, 7);
      const auto drums = render(BotBand::Voice::Drums, s);
      expect(rms(drums, 0, (int)drums.size()) > 0.005f,
             "the drums stopped for want of a key");
    }

    beginTest("a short buffer is not overrun");
    {
      // The renderers place hits by beat and must clip against the buffer, not
      // trust it to be interval-length. ASan is the real check; this provokes it.
      const auto s = settingsFor("C major");
      for (int n : {1, 17, 512, 5000}) {
        std::vector<float> small((size_t)n, 0.0f);
        for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                           BotBand::Voice::Keys})
          BotBand::renderInterval(voice, s, 0, small.data(), n);
      }
      expect(true, "survived");
    }
  }

private:
  // Zero crossings over the whole buffer: crude, but it answers "is this an
  // octave apart" without pretending to be a pitch tracker.
  static double dominantHz(const std::vector<float> &v, double sampleRate) {
    int crossings = 0;
    for (size_t i = 1; i < v.size(); ++i)
      if ((v[i - 1] <= 0.0f) != (v[i] <= 0.0f))
        ++crossings;
    if (v.empty())
      return 0.0;
    return 0.5 * (double)crossings * sampleRate / (double)v.size();
  }
};

static BotBandTests botBandTests;
