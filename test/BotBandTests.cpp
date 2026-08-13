#include "../src/BotBand.h"
#include "../src/AudioMeasure.h"
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

        // The figure must span the interval rather than repeat inside it: a
        // bass line that comes round every four steps is doubling the kick by
        // another route. Exactly twice the kick's pulses always shares a
        // factor with twice its steps, so the count is nudged to the nearest
        // coprime one.
        expectEquals(Euclidean::patternPeriod(bass.steps, bass.pulses),
                     bass.steps,
                     "seed " + juce::String((int)seed) + ": E(" +
                         juce::String(bass.pulses) + "," +
                         juce::String(bass.steps) + ") repeats");
        // Near twice the kick, in either direction: the coprime nudge may
        // move the count down as readily as up, and the guarantee that the
        // bass is never sparser than the kick comes from the union below
        // rather than from the pulse count.
        expect(std::abs(bass.pulses - kick.pulses * 2) <= 2,
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

    beginTest("the bass figure spans the interval at every BPI");
    {
      // Odd is enough when the step count is a power of two, and not
      // otherwise: at BPI 12 and 24 -- both ordinary Ninjam values -- 9, 15
      // and 21 share a factor of three and still repeat. Coprimality is the
      // property, not oddness.
      for (int bpi : {4, 8, 12, 16, 20, 24, 32})
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
          const auto s = settingsFor("C major", 120, bpi, seed);
          const auto bass = BotBand::figureFor(BotBand::Voice::Bass, s);
          if (Euclidean::patternPeriod(bass.steps, bass.pulses) != bass.steps) {
            expect(false, "bpi " + juce::String(bpi) + " seed " +
                              juce::String((int)seed) + ": E(" +
                              juce::String(bass.pulses) + "," +
                              juce::String(bass.steps) + ") repeats every " +
                              juce::String(Euclidean::patternPeriod(
                                  bass.steps, bass.pulses)));
            return;
          }
        }
      expect(true);
    }

    beginTest("the kick is allowed to repeat, and often does");
    {
      // The counterpart: movement is what a bass wants and a kick does not, so
      // the coprime nudge is deliberately not applied to the drums.
      int repeating = 0;
      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto kick = BotBand::figureFor(BotBand::Voice::Drums, s);
        if (Euclidean::patternPeriod(kick.steps, kick.pulses) < kick.steps)
          ++repeating;
      }
      expect(repeating > 0,
             "the kick was never allowed a repeating figure, which suggests "
             "the coprime nudge leaked into the drums");
    }

    beginTest("the keys report one pulse per chord");
    {
      const auto s = settingsFor("C major");
      const auto f = BotBand::figureFor(BotBand::Voice::Keys, s);
      expectEquals(f.pulses, (int)Harmony::flatten(s.chart).size());
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

    beginTest("saturation shapes rather than trims");
    {
      // A drive of zero has to be exactly the input, because it is the way to
      // turn the stage off while measuring the other one.
      for (float x : {-1.0f, -0.3f, 0.0f, 0.25f, 1.0f})
        expectEquals(BotVoice::saturate(x, 0.0), x);

      // Normalised, odd, and never expanding past full scale -- which is what
      // lets it be applied to a bus without a limiter behind it.
      expectWithinAbsoluteError(BotVoice::saturate(1.0f, 2.0), 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(BotVoice::saturate(-1.0f, 2.0), -1.0f, 1.0e-6f);

      float previous = -2.0f;
      for (int i = -100; i <= 100; ++i) {
        const float x = (float)i / 100.0f;
        const float y = BotVoice::saturate(x, 2.0);
        expect(std::abs(y) <= 1.0f + 1.0e-6f,
               "saturate(" + juce::String(x) + ") left full scale at " +
                   juce::String(y));
        expect(y > previous, "saturate is not monotonic at " + juce::String(x));
        previous = y;
      }

      // The point of it: quiet material comes out louder, which is where the
      // audibility the kick needed comes from.
      expect(BotVoice::saturate(0.1f, 2.0) > 0.15f,
             "small signals were not lifted");
    }

    beginTest("shaping a sum ducks the quiet part under the loud one");
    {
      // The glue the kit's bus stage is there for, measured directly: a loud
      // low tone and a quiet high one, shaped together. Where the low tone is
      // at its peak the high one must come out smaller than where the low tone
      // is passing through zero. That intermodulation only exists in the sum,
      // and it is what makes three drums read as one kit.
      const double sr = 48000.0, low = 60.0, high = 4000.0;
      const int n = 1600;
      std::vector<float> both((size_t)n), lowOnly((size_t)n);
      for (int i = 0; i < n; ++i) {
        const double t = (double)i / sr;
        const float l = (float)(0.85 * std::sin(2.0 * juce::MathConstants<double>::pi * low * t));
        const float h = (float)(0.10 * std::sin(2.0 * juce::MathConstants<double>::pi * high * t));
        both[(size_t)i] = BotVoice::saturate(l + h, 1.1);
        lowOnly[(size_t)i] = BotVoice::saturate(l, 1.1);
      }

      // What survives of the high tone, over one of its cycles, at the low
      // tone's crest (sample 200) and at its zero crossing (sample 400).
      auto amplitudeAt = [&](int centre) {
        float lo = 1.0f, hi = -1.0f;
        for (int i = centre - 6; i <= centre + 6; ++i) {
          const float d = both[(size_t)i] - lowOnly[(size_t)i];
          lo = juce::jmin(lo, d);
          hi = juce::jmax(hi, d);
        }
        return 0.5f * (hi - lo);
      };

      const float atCrest = amplitudeAt(200), atZero = amplitudeAt(400);
      expect(atCrest < 0.7f * atZero,
             "no ducking: " + juce::String(atCrest, 4) + " at the crest against " +
                 juce::String(atZero, 4) + " at the zero crossing");
    }

    beginTest("the kick is shaped, not just loud");
    {
      // A drum that is only a resonating membrane is the least loud waveform
      // there is for a given peak, and that is why the kick was once the
      // quietest thing in the kit. Crest factor is what changes when it is
      // shaped: re-measured for the modal kick at 3.52 unshaped against 2.50
      // as it stands, so a limit of 3.0 still fails if the saturation is taken
      // out and passes with room as it is.
      //
      // Crest rather than brightness, and that is worth recording because the
      // obvious choice is wrong here: saturating a kick RAISES its low-order
      // harmonics, which pulls the energy-weighted mean frequency DOWN, from
      // 165 Hz to 150. Brightness would have read the shaped kick as the duller
      // one.
      std::vector<float> kick(7200, 0.0f);
      BotVoice::renderKick(kick.data(), (int)kick.size(), 48000.0, 1.0f);

      float peak = 0.0f;
      for (float x : kick)
        peak = juce::jmax(peak, std::abs(x));
      const float level = rms(kick, 0, (int)kick.size());

      expect(level > 0.0f, "the kick was silent");
      expect(peak / level < 3.0f,
             "the kick's crest factor is " + juce::String(peak / level, 3) +
                 ", which is an unshaped sine");
    }

    beginTest("the kit carries level and not only peaks");
    {
      // Both saturation stages together, in one number. Re-derived after the
      // balance pass, because the output trim lifted every figure here and the
      // old floor of 0.068 had stopped discriminating: 0.152 as it stands,
      // 0.096 with the kick's own shaping removed, 0.087 with the bus stage
      // removed. A floor of 0.12 still fails if either one goes.
      const auto buf = render(BotBand::Voice::Drums,
                              settingsFor("C major", 120, 8, 1u));
      const float level = rms(buf, 0, (int)buf.size());
      expect(level > 0.12f,
             "the kit came out at rms " + juce::String(level, 5));
    }

    beginTest("the kit is heard in a room, and the room has two sides");
    {
      const auto s = settingsFor("C major", 120, 8, 1u);
      const int n = intervalSamplesFor(s);
      std::vector<float> left((size_t)n, 0.0f), right((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Drums, s, 0, left.data(),
                              right.data(), n);

      expect(BotBand::isStereo(BotBand::Voice::Drums));
      expect(left != right, "both sides of the kit are identical");

      // Different, but the same drummer: the sides must not diverge in level,
      // or the kit is panned rather than in a room.
      const float l = rms(left, 0, n), r = rms(right, 0, n);
      expect(l > 0.01f && r > 0.01f, "a side was silent");
      expect(std::abs(l - r) < 0.25f * juce::jmax(l, r),
             "the sides are at different levels: " + juce::String(l, 4) +
                 " against " + juce::String(r, 4));

      // The room shows up as energy after the last drum has stopped. Rendered
      // dry, the tail of a bar is much quieter than it is with reflections in
      // it -- which is what "the kit is in a room" means, measurably.
      for (auto voice : {BotBand::Voice::Bass, BotBand::Voice::Keys,
                         BotBand::Voice::Lead})
        expect(!BotBand::isStereo(voice),
               juce::String(BotBand::voiceName(voice)) +
                   " should be a close-miked instrument, not a room");
    }

    beginTest("a mono caller gets the kit without touching a right channel");
    {
      // PracticeBot mirrors mono voices, so it has to be able to tell. A null
      // right channel must render the left exactly as the stereo call does.
      const auto s = settingsFor("C major", 120, 8, 1u);
      const int n = intervalSamplesFor(s);

      std::vector<float> stereoL((size_t)n, 0.0f), stereoR((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Drums, s, 0, stereoL.data(),
                              stereoR.data(), n);

      std::vector<float> monoL((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Drums, s, 0, monoL.data(), n);

      expect(monoL == stereoL,
             "the left channel depends on whether a right one was asked for");

      // And a mono voice must leave a right channel entirely alone.
      std::vector<float> untouched((size_t)n, 0.0f), bassL((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Bass, s, 0, bassL.data(),
                              untouched.data(), n);
      for (float x : untouched)
        if (x != 0.0f) {
          expect(false, "a mono voice wrote into the right channel");
          break;
        }
    }

    beginTest("the band is balanced against itself");
    {
      // The four voices were never levelled against each other and the pad sat
      // 10 dB ABOVE the drums, so a rebuilt kit could improve as much as it
      // liked and stay buried. This is the shape a backing band wants: bass
      // carrying, chords underneath, nothing more than a few dB from the kit.
      //
      // Asserted as an ordering plus a spread rather than as four numbers,
      // because the exact levels move with the seed and the ordering is the
      // part that matters.
      for (std::uint32_t seed : {1u, 55u, 900u}) {
        const auto s2 = settingsFor("C major", 120, 8, seed);
        const int n = intervalSamplesFor(s2);

        double kit = 0.0, bass = 0.0, keys = 0.0, lead = 0.0;
        for (int v = 0; v < 4; ++v) {
          const auto buf = render((BotBand::Voice)v, s2);
          const double db = AudioMeasure::toDb(rms(buf, 0, n));
          switch ((BotBand::Voice)v) {
          case BotBand::Voice::Drums: kit = db; break;
          case BotBand::Voice::Bass: bass = db; break;
          case BotBand::Voice::Keys: keys = db; break;
          case BotBand::Voice::Lead: lead = db; break;
          }
        }

        const juce::String at = " (seed " + juce::String((int)seed) + ")";
        expect(bass > kit, "the bass should carry, above the kit" + at);
        expect(keys < kit, "the chords should sit under the kit" + at);
        expect(keys < bass && keys < lead, "the chords should be the floor" + at);

        // And nothing buried: the old failure was a 10 dB spread the wrong way
        // round, so the width of the band is the thing to bound.
        const double loudest = juce::jmax(juce::jmax(kit, bass), juce::jmax(keys, lead));
        const double quietest = juce::jmin(juce::jmin(kit, bass), juce::jmin(keys, lead));
        expect(loudest - quietest < 8.0,
               "the band spans " + juce::String(loudest - quietest, 1) +
                   " dB, which is a mix rather than a balance" + at);
      }
    }

    beginTest("nothing clips");
    {
      // Guaranteed by the ceiling in renderInterval rather than by a measured
      // headroom constant, so this now checks that the ceiling is applied at
      // all -- and the assertion below checks it is not doing the job of a
      // fader.
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

    beginTest("the ceiling is a backstop, not a sound");
    {
      // A ceiling makes "nothing clips" true by construction, which would let a
      // trim be cranked to ten and still pass while sounding like a brick wall.
      // So: how much of the signal reaches it at all. Measured 1.6% of samples
      // above the knee for the kit, which is peak limiting; a fader doing the
      // job of a fader.
      const auto s2 = settingsFor("C major", 120, 8, 1u);
      const int n = intervalSamplesFor(s2);
      const auto buf = render(BotBand::Voice::Drums, s2);

      int aboveKnee = 0;
      for (float x : buf)
        if (std::abs(x) > 0.70f)
          ++aboveKnee;

      const double percent = 100.0 * (double)aboveKnee / (double)n;
      expect(percent < 5.0,
             "the kit spends " + juce::String(percent, 2) +
                 "% of its time in the limiter, which is a brick wall");
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
        s.chart = {s.chart[0]};

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
        expectEquals(pitchClass, s.chart[0].chords[0].root,
                     juce::String(keyName) + ": bass at " +
                         juce::String(hz, 1) + " Hz is pitch class " +
                         juce::String(pitchClass) + ", chord root is " +
                         juce::String(s.chart[0].chords[0].root));
      }
    }

    beginTest("every chord change gets a bass note, on the root");
    {
      // The stronger form of the test above: not just the first change, but
      // all of them, with a real progression underneath.
      auto s = settingsFor("C major", 120, 16);
      s.chart = Harmony::chartOf({Harmony::chordOn(0, Harmony::Quality::Major),
                                  Harmony::chordOn(5, Harmony::Quality::Major),
                                  Harmony::chordOn(9, Harmony::Quality::Minor),
                                  Harmony::chordOn(7, Harmony::Quality::Major)});

      const auto buf = render(BotBand::Voice::Bass, s);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);

      const auto layout = Harmony::layoutChart(s.chart, s.bpi);
      const auto chords = Harmony::flatten(s.chart);

      for (int chord = 0; chord < (int)chords.size(); ++chord) {
        // Where this chord starts, asked of the same layout the band played
        // from rather than re-derived here.
        int at = -1;
        for (int i = 0; i < layout.steps(); ++i)
          if (layout.stepToChord[(size_t)i] == chord) {
            at = i * beat / Harmony::kStepsPerBeat;
            break;
          }
        if (at < 0)
          continue;
        const int step = at / beat;
        const int span = juce::jmin(beat, (int)buf.size() - at);
        if (span <= 0)
          continue;

        const double hz = fundamentalHz(buf.data() + at, span, s.sampleRate);
        expect(hz > 0.0, "chord " + juce::String(chord) + ": no note");
        if (hz <= 0.0)
          continue;

        const double midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        const int pitchClass = ((int)std::lround(midi) % 12 + 12) % 12;
        expectEquals(pitchClass, chords[(size_t)chord].root,
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
        s.chart = {s.chart[0]};
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
        const auto layout = Harmony::layoutChart(s.chart, s.bpi);
        for (int interval = 0; interval < 4; ++interval) {
          const auto line = BotBand::leadLine(s, interval);

          for (size_t step = 0; step < line.size(); ++step) {
            if (line[step] < 0)
              continue;

            const int strength = BotBand::metricStrength((int)step, s.bpi);
            const auto &chord = Harmony::chordAtStep(layout, (int)step);
            const int tier = BotBand::noteTier(line[step], chord);
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
      const auto chords = Harmony::flatten(s.chart);
      expectEquals((int)chords.size(), 4);
      expectEquals(chords[0].root, 9);
      expect(chords[0].quality == Harmony::Quality::Minor);
    }

    beginTest("an announced progression is played instead of the default");
    {
      auto s = settingsFor("C major");
      s.chart = Harmony::chartOf({Harmony::chordOn(2, Harmony::Quality::Minor),
                                  Harmony::chordOn(7, Harmony::Quality::Dominant7)});

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
      bad.chart.clear();
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
  // The detectors themselves live in src/AudioMeasure.h, calibrated against
  // signals of known pitch in AudioMeasureTests and shared with the voice lab,
  // so tuning by ear and asserting a threshold use one instrument
  // (`PRINCIPLES §5`, `§8`). These are the shapes this file wants them in.
  static double fundamentalHz(const float *data, int numSamples,
                              double sampleRate) {
    return AudioMeasure::fundamentalHz(data, numSamples, sampleRate);
  }

  static double firstNoteHz(const std::vector<float> &buf, double sampleRate,
                            int bpm) {
    // One beat of analysis: long enough for many cycles at bass frequencies,
    // short enough not to run into the note after.
    const int beat = (int)(sampleRate * 60.0 / (double)bpm);
    return AudioMeasure::firstNoteHz(buf.data(), (int)buf.size(), sampleRate,
                                     beat);
  }

  static double dominantHz(const std::vector<float> &v, double sampleRate) {
    return AudioMeasure::crossingRateHz(v.data(), (int)v.size(), sampleRate);
  }
};

static BotBandTests botBandTests;
