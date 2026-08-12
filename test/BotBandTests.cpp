#include "../src/BotBand.h"
#include "../src/BotVoice.h"
#include "../src/Euclidean.h"
#include "TestSignal.h"
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
    runLeadTests();
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
                         BotBand::Voice::Keys, BotBand::Voice::Lead}) {
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
      std::set<std::uint32_t> seen;
      for (int v = 0; v < BotBand::kNumVoices; ++v)
        seen.insert(BotBand::saltedSeed((BotBand::Voice)v, 1));
      expectEquals((int)seen.size(), BotBand::kNumVoices,
                   "two voices share a salted seed");
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
                         BotBand::Voice::Keys, BotBand::Voice::Lead}) {
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
                           BotBand::Voice::Keys, BotBand::Voice::Lead}) {
          const auto f = BotBand::figureFor(voice, s);
          expect(f.steps > 0, "no steps");
          expect(f.pulses > 0, "no pulses at bpi " + juce::String(bpi));
          expect(f.pulses <= f.steps, "more pulses than steps");
        }
      }
    }

    beginTest("the bass is denser than the kick but lands on every one");
    {
      // A bass part has far more notes than there are kicks -- matching one
      // for one made it sound like a second kick drum. Doubling both the
      // pulses and the resolution is what allows both at once: E(2p, 2s)
      // contains E(p, s) exactly, so the bass hits every kick and fills in
      // between. That containment is the property worth asserting.
      for (std::uint32_t seed : {1u, 7u, 4242u, 99u}) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto kick = BotBand::figureFor(BotBand::Voice::Drums, s);
        const auto bass = BotBand::figureFor(BotBand::Voice::Bass, s);

        expectEquals(bass.steps, kick.steps * 2,
                     "seed " + juce::String((int)seed) + " resolution");
        expect(bass.pulses >= kick.pulses * 2,
               "seed " + juce::String((int)seed) + ": bass has " +
                   juce::String(bass.pulses) + " pulses to the kick's " +
                   juce::String(kick.pulses));

        // The doubled figure alone does NOT contain the kick -- E(2p,2s) at
        // step 2j reduces to (2jp) mod s < p, not the kick's (jp) mod s < p --
        // so renderBass takes the union. Check that in the audio, which is
        // where the property has to hold.
        const auto buf = render(BotBand::Voice::Bass, s);
        const int beat = (int)(s.sampleRate * 60.0 / s.bpm);
        for (int step = 0; step < kick.steps; ++step) {
          if (!Euclidean::hit(step, kick.steps, kick.pulses, kick.rotation))
            continue;
          const int at = step * beat;
          if (at + 256 >= (int)buf.size())
            continue;
          // A note starting here means energy rising out of near-silence.
          float peak = 0.0f;
          for (int i = at; i < at + 256; ++i)
            peak = juce::jmax(peak, std::abs(buf[(size_t)i]));
          expect(peak > 0.01f, "seed " + juce::String((int)seed) +
                                   ": no bass note on kick step " +
                                   juce::String(step));
        }
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
                         BotBand::Voice::Keys, BotBand::Voice::Lead}) {
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
                             BotBand::Voice::Keys, BotBand::Voice::Lead}) {
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

    beginTest("the bass plays the root of the chord, in the right key");
    {
      // The test this replaces only compared the bass against the keys and so
      // passed while the bass was a major third sharp of everything: the
      // anchor was MIDI 28, which is E1 rather than C1, and a chord root is a
      // pitch class where 0 means C. Comparing two things that move together
      // proves nothing (PRINCIPLES 5) -- this asserts the absolute note.
      for (const char *keyName : {"C major", "D minor", "F# major", "A minor",
                                  "Bb major", "E Dorian"}) {
        auto s = settingsFor(keyName);
        // One chord for the whole interval, so the first note is unambiguous.
        s.progression = {s.progression[0]};

        // A chord change always gets a note and that note is always the root,
        // so beat 0 is exactly measurable. Later notes may be the octave or
        // the fifth, which is why this asks about the change rather than about
        // whatever happens to sound first.
        const auto buf = render(BotBand::Voice::Bass, s);
        const double hz = firstNoteHz(buf, s.sampleRate, s.bpm);
        if (hz <= 0.0) {
          expect(false, juce::String(keyName) + ": no bass note found");
          continue;
        }

        const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        const int pitchClass = ((int)std::lround(midi) % 12 + 12) % 12;
        expectEquals(pitchClass, s.progression[0].root,
                     juce::String(keyName) + ": bass at " +
                         juce::String(hz, 1) + " Hz is pitch class " +
                         juce::String(pitchClass) + ", chord root is " +
                         juce::String(s.progression[0].root));
      }
    }

    beginTest("every chord change gets a bass note, on the root");
    {
      // The stronger form of the test above: not just the first change, but
      // all of them, with a real progression underneath.
      auto s = settingsFor("C major", 120, 16);
      s.progression = {Harmony::chordOn(0, Harmony::Quality::Major),
                       Harmony::chordOn(5, Harmony::Quality::Major),
                       Harmony::chordOn(9, Harmony::Quality::Minor),
                       Harmony::chordOn(7, Harmony::Quality::Major)};

      const auto buf = render(BotBand::Voice::Bass, s);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);

      for (int chord = 0; chord < (int)s.progression.size(); ++chord) {
        // Where this chord starts.
        int step = 0;
        while (step < s.bpi &&
               Harmony::chordIndexForBeat(step, s.bpi,
                                          (int)s.progression.size()) != chord)
          ++step;

        const int at = step * beat;
        const int span = juce::jmin(beat, (int)buf.size() - at);
        if (span <= 0)
          continue;

        const double hz = fundamentalHz(buf.data() + at, span, s.sampleRate);
        expect(hz > 0.0, "chord " + juce::String(chord) + ": no note");
        if (hz <= 0.0)
          continue;

        const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        const int pitchClass = ((int)std::lround(midi) % 12 + 12) % 12;
        expectEquals(pitchClass, s.progression[(size_t)chord].root,
                     "chord " + juce::String(chord) + " at beat " +
                         juce::String(step) + ", " + juce::String(hz, 1) +
                         " Hz");
      }
    }

    beginTest("the bass is where a speaker can reproduce it");
    {
      // 41-78 Hz is below what most laptop and monitor speakers do at all,
      // which is how a wrong bass part went unnoticed as a missing one.
      for (const char *keyName : {"C major", "B major", "F# major"}) {
        auto s = settingsFor(keyName);
        s.progression = {s.progression[0]};
        const auto buf = render(BotBand::Voice::Bass, s);
        const double hz = firstNoteHz(buf, s.sampleRate, s.bpm);
        expect(hz >= 60.0 && hz <= 140.0,
               juce::String(keyName) + ": bass fundamental at " +
                   juce::String(hz, 1) + " Hz");
      }
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

  void runLeadTests() {
    beginTest("metric strength ranks the metre");
    {
      // Step is in eighths. The interval downbeat outranks a bar head, which
      // outranks a half bar, which outranks a beat, which outranks an off-beat.
      expectEquals(BotBand::metricStrength(0, 16), 4, "interval downbeat");
      expectEquals(BotBand::metricStrength(8, 16), 3, "beat 4, a bar head");
      expectEquals(BotBand::metricStrength(4, 16), 2, "beat 2, a half bar");
      expectEquals(BotBand::metricStrength(2, 16), 1, "beat 1");
      expectEquals(BotBand::metricStrength(1, 16), 0, "an off-beat eighth");
      expectEquals(BotBand::metricStrength(7, 16), 0, "an off-beat eighth");

      // It repeats each interval, and negative steps do not fall off the end.
      for (int step = 0; step < 32; ++step)
        expectEquals(BotBand::metricStrength(step, 16),
                     BotBand::metricStrength(step + 32, 16));
      expectEquals(BotBand::metricStrength(-32, 16), 4);
    }

    beginTest("the lead plays a line, with rests in it");
    {
      const auto s = settingsFor("C major", 120, 16);
      const auto line = BotBand::leadLine(s, 0);
      expectEquals((int)line.size(), s.bpi * 2);

      int notes = 0, rests = 0;
      for (int n : line)
        (n >= 0 ? notes : rests)++;
      expect(notes >= 4, "only " + juce::String(notes) + " notes");
      expect(rests >= 2, "a line with no rests is a drone");
    }

    beginTest("beat strength and note strength are coupled");
    {
      // The whole point of the melodic writing, and the half that was missing
      // when this sounded fine in major and wrong in minor. A strong beat may
      // only take a chord tone; an ordinary beat a comfortable scale tone; and
      // only an off-beat may touch a semitone above a chord tone.
      for (const char *keyName : {"C major", "D minor", "A minor", "F Lydian",
                                  "E Phrygian", "G Mixolydian"}) {
        auto s = settingsFor(keyName, 120, 16);
        for (int interval = 0; interval < 4; ++interval) {
          const auto line = BotBand::leadLine(s, interval);

          for (size_t step = 0; step < line.size(); ++step) {
            if (line[step] < 0)
              continue;

            const int strength = BotBand::metricStrength((int)step, s.bpi);
            const int idx = Harmony::chordIndexForBeat(
                (int)step / 2, s.bpi, (int)s.progression.size());
            const int tier =
                BotBand::noteTier(line[step], s.progression[(size_t)idx]);
            const int worst = strength >= 3 ? 0 : (strength >= 1 ? 1 : 2);

            expect(tier <= worst,
                   juce::String(keyName) + " interval " +
                       juce::String(interval) + ": step " +
                       juce::String((int)step) + " strength " +
                       juce::String(strength) + " played MIDI " +
                       juce::String(line[step]) + " of tier " +
                       juce::String(tier));
          }
        }
      }
    }

    beginTest("the avoid note is the one a semitone above a chord tone");
    {
      // Derived from the chord rather than listed per mode, which is what
      // makes it right in all seven.
      const auto cMajor = Harmony::chordOn(0, Harmony::Quality::Major);
      expectEquals(BotBand::noteTier(60, cMajor), 0, "C over C is the root");
      expectEquals(BotBand::noteTier(64, cMajor), 0, "E over C is the third");
      expectEquals(BotBand::noteTier(65, cMajor), 2, "F sits above the third");
      expectEquals(BotBand::noteTier(62, cMajor), 1, "D is comfortable");

      const auto aMinor = Harmony::chordOn(9, Harmony::Quality::Minor);
      expectEquals(BotBand::noteTier(65, aMinor), 2,
                   "the flat sixth sits above the fifth -- the minor problem");
      expectEquals(BotBand::noteTier(62, aMinor), 1, "the fourth is fine");

      // Lydian's sharp fourth is a whole tone above the third, so it is the
      // characteristic note rather than one to handle carefully.
      const auto fMajor = Harmony::chordOn(5, Harmony::Quality::Major);
      expectEquals(BotBand::noteTier(71, fMajor), 1, "B over F is Lydian");
    }

    beginTest("every note is in the key");
    {
      for (const char *keyName : {"C major", "D minor", "E Phrygian",
                                  "Bb Mixolydian"}) {
        auto s = settingsFor(keyName, 120, 16);
        // A diatonic progression, so chord tones are scale tones too.
        const auto line = BotBand::leadLine(s, 0);

        std::set<int> inKey;
        for (int degree = 0; degree < MusicalKey::kScaleDegrees; ++degree)
          inKey.insert(
              ((MusicalKey::degreeToMidi(s.key, degree, 4) % 12) + 12) % 12);

        for (int n : line) {
          if (n < 0)
            continue;
          expect(inKey.count(((n % 12) + 12) % 12) > 0,
                 juce::String(keyName) + ": MIDI " + juce::String(n) +
                     " is out of key");
        }
      }
    }

    beginTest("the lead sits above the chords");
    {
      const auto s = settingsFor("C major", 120, 16);
      const auto line = BotBand::leadLine(s, 0);
      for (int n : line)
        if (n >= 0)
          expect(n >= 60 && n <= 96,
                 "MIDI " + juce::String(n) + " is outside the lead register");
    }

    beginTest("the line develops across a phrase rather than repeating");
    {
      const auto s = settingsFor("C major", 120, 16);
      expect(BotBand::leadLine(s, 0) != BotBand::leadLine(s, 1),
             "two consecutive intervals gave the same line");
      // Still reproducible, which is what makes a seed worth having.
      expect(BotBand::leadLine(s, 3) == BotBand::leadLine(s, 3));
    }

    beginTest("an invalid key gives no line rather than a wrong one");
    {
      MusicalKey::Key none;
      auto s = BotBand::defaults(none, 120, 8, 48000.0, 3);
      for (int n : BotBand::leadLine(s, 0))
        expectEquals(n, -1);
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
                           BotBand::Voice::Keys, BotBand::Voice::Lead})
          BotBand::renderInterval(voice, s, 0, small.data(), n);
      }
      expect(true, "survived");
    }
  }

private:
  // Fundamental by autocorrelation.
  //
  // TestSignal::dominantFrequency counts threshold crossings, which is right
  // for the pure tones the rest of the suite uses and wrong here. The bass
  // carries a strong second harmonic, so its waveform is asymmetric: the
  // negative lobe does not always reach the hysteresis threshold, crossings
  // are missed, and the estimate comes out about three semitones flat --
  // consistently enough to look like a transposition bug in the synthesis
  // rather than an artefact of the instrument (PRINCIPLES 5).
  //
  // Autocorrelation finds the period rather than the crossings, so harmonics
  // reinforce the answer instead of confusing it.
  static double fundamentalHz(const float *data, int numSamples,
                              double sampleRate, double lowHz = 40.0,
                              double highHz = 500.0) {
    if (data == nullptr || numSamples < 64 || sampleRate <= 0.0)
      return 0.0;

    // Mean removal, so a DC offset cannot dominate the correlation.
    double mean = 0.0;
    for (int i = 0; i < numSamples; ++i)
      mean += data[i];
    mean /= (double)numSamples;

    std::vector<double> x((size_t)numSamples);
    for (int i = 0; i < numSamples; ++i)
      x[(size_t)i] = (double)data[i] - mean;

    double energy = 0.0;
    for (double v : x)
      energy += v * v;
    if (energy <= 0.0)
      return 0.0;

    const int minLag = juce::jmax(2, (int)(sampleRate / highHz));
    const int maxLag = juce::jmin(numSamples / 2, (int)(sampleRate / lowHz));
    if (maxLag <= minLag)
      return 0.0;

    double bestScore = 0.0;
    int bestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
      double sum = 0.0, normA = 0.0, normB = 0.0;
      for (int i = 0; i + lag < numSamples; ++i) {
        sum += x[(size_t)i] * x[(size_t)(i + lag)];
        normA += x[(size_t)i] * x[(size_t)i];
        normB += x[(size_t)(i + lag)] * x[(size_t)(i + lag)];
      }
      const double denom = std::sqrt(normA * normB);
      if (denom <= 0.0)
        continue;
      const double score = sum / denom;
      if (score > bestScore) {
        bestScore = score;
        bestLag = lag;
      }
    }

    if (bestLag <= 0 || bestScore < 0.3)
      return 0.0;

    // Reject subharmonics, but only at INTEGER divisions of the best lag.
    //
    // A period of 3T correlates about as well as T, so taking the maximum can
    // report a third of the true pitch -- which is how this instrument once
    // claimed a B2 bass was sounding at 41 Hz, convincingly enough to look
    // like a bug in the synthesis. Scanning for any shorter lag that scores
    // nearly as well overcorrects the other way and lands between semitones,
    // so only bestLag/2, /3, /4... are considered.
    auto scoreAt = [&](int lag) {
      double sum = 0.0, normA = 0.0, normB = 0.0;
      for (int i = 0; i + lag < numSamples; ++i) {
        sum += x[(size_t)i] * x[(size_t)(i + lag)];
        normA += x[(size_t)i] * x[(size_t)i];
        normB += x[(size_t)(i + lag)] * x[(size_t)(i + lag)];
      }
      const double denom = std::sqrt(normA * normB);
      return denom > 0.0 ? sum / denom : 0.0;
    };

    for (int divisor = 8; divisor >= 2; --divisor) {
      const int lag = bestLag / divisor;
      if (lag < minLag)
        continue;
      if (scoreAt(lag) >= 0.85 * bestScore)
        return sampleRate / (double)lag;
    }
    return sampleRate / (double)bestLag;
  }

  // The pitch of the first note in the buffer, wherever it starts.
  //
  // Finding the onset matters: a bass figure's rotation can move the first
  // note off beat 0, and measuring a fixed window from the start then reads
  // silence and reports nothing.
  static double firstNoteHz(const std::vector<float> &buf, double sampleRate,
                            int bpm) {
    float peak = 0.0f;
    for (float x : buf)
      peak = juce::jmax(peak, std::abs(x));
    if (peak <= 0.0f)
      return 0.0;

    size_t onset = 0;
    while (onset < buf.size() && std::abs(buf[onset]) < 0.2f * peak)
      ++onset;
    if (onset >= buf.size())
      return 0.0;

    // One beat from the onset, or whatever is left. Long enough for many
    // cycles at bass frequencies, short enough not to run into the next note.
    const int beat = (int)(sampleRate * 60.0 / (double)bpm);
    const int span = juce::jmin(beat, (int)(buf.size() - onset));
    return fundamentalHz(buf.data() + onset, span, sampleRate);
  }

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
