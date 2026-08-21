#include "../src/BandPatch.h"
#include "../src/BotBand.h"
#include "../src/AudioMeasure.h"
#include "../src/BotVoice.h"
#include <chalkwalk/music/Euclidean.h>
#include "TestSignal.h"
#include <map>
#include <JuceHeader.h>

namespace {

// An INDEPENDENT reading of "how strong is this note against this chord",
// kept deliberately separate from the one the band actually uses.
//
// This was BotBand's own model, and the generator has since moved to
// chalkwalk-music's tier order. Rather than delete it, it stays here as a
// second opinion: the tests below check the shared gate against this, so a
// change to either has to be argued for rather than merely compiling. Two
// implementations that agree is evidence; one implementation checked against
// itself is a tautology.
//
// The tiers are derived from the chord rather than listed per mode, using the
// avoid-note rule: a scale tone a semitone above a chord tone is the one that
// clashes. That gives the flat sixth in Aeolian over i, the fourth in Ionian
// over I, and the flat second in Phrygian -- and correctly leaves Lydian's
// sharp fourth alone, since it is a whole tone above the third and is the
// characteristic note of the mode rather than a note to handle carefully.
//
//   0  a chord tone
//   1  a scale tone that sits comfortably
//   2  a semitone above a chord tone: colour, and only in passing
int noteTier(int midiNote, const Harmony::Chord &chord) {
  const int pc = ((midiNote % 12) + 12) % 12;

  for (int t = 0; t < chord.toneCount; ++t) {
    const int tone = (((chord.root + chord.tones[(size_t)t]) % 12) + 12) % 12;
    if (pc == tone)
      return 0;
  }

  for (int t = 0; t < chord.toneCount; ++t) {
    const int tone = (((chord.root + chord.tones[(size_t)t]) % 12) + 12) % 12;
    if (pc == (tone + 1) % 12)
      return 2;
  }

  return 1;
}

}  // namespace


// Two kinds of assertion here, and the split is the point (AGENTS.md).
//
// The pattern layer is exact -- a figure either has three pulses or it does
// not -- so it gets ordinary equality tests.
//
// The audio can only be measured statistically. RMS, where the energy sits in
// time, and pitch by zero crossings. Asserting sample values against the
// synthesis formula would only assert that the formula is the formula.

namespace {

MusicalKey::Key keyOf(const std::string &name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

BotBand::Settings settingsFor(const std::string &keyName, int bpm = 120,
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
                          int intervalIndex = 0,
                          BotBand::Phase phase = BotBand::Phase::Groove) {
  const int n = intervalSamplesFor(s);
  std::vector<float> buf((size_t)n, 0.0f);
  BotBand::renderInterval(voice, s, intervalIndex, phase, buf.data(), nullptr, n);
  return buf;
}

} // namespace

class BotBandTests : public juce::UnitTest {
public:
  BotBandTests() : juce::UnitTest("BotBand", "music") {}

  void runTest() override {
    runSeedTests();
    runFigureTests();
    runEndingTests();
    runAudioTests();
    runKeysTests();
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

    auto key = MusicalKey::parseName(keyName.toStdString());
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

  void runEndingTests() {
    // An ending is two intervals: one that winds down and one that lands. The
    // STATES and their timing are BandPlayState's business; this is what they
    // sound like (docs/BOT-CHAT.md section 15).
    const auto s = settingsFor("C major");
    const int n = intervalSamplesFor(s);

    beginTest("the resolve lands on the downbeat and then gets out of the way");
    {
      // The shape that makes it an ending rather than a dropout: everything
      // arrives together on beat one, rings, and the rest of the interval is
      // quiet. Measured as a ratio between the two halves rather than against
      // an absolute, because the voices differ in level by design.
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys, BotBand::Voice::Lead}) {
        const auto out = render(voice, s, 0, BotBand::Phase::Resolving);
        const juce::String who = BotBand::voiceName(voice);

        // The lead is silent on the resolve: a soloist who hears the band
        // ending does not start another phrase.
        if (voice == BotBand::Voice::Lead) {
          expect(AudioMeasure::peak(out.data(), n) < 0.001f,
                 who + " played over the final chord");
          continue;
        }

        const float opening = rms(out, 0, n / 8);
        const float tail = rms(out, n / 2, n);
        expect(opening > 0.002f, who + " did not land on the downbeat");
        expect(tail < opening * 0.25f,
               who + " is still going in the second half of the resolve: "
                   + juce::String(opening) + " then " + juce::String(tail));
      }
    }

    beginTest("the wrap-up plays through, and thins in its second half");
    {
      // A taper rather than a switch: the first half is the tune, the second
      // half winds down. A wrap-up that went quiet immediately would be an
      // ending one interval early.
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys}) {
        const auto out = render(voice, s, 0, BotBand::Phase::Wrapping);
        const juce::String who = BotBand::voiceName(voice);
        expect(rms(out, 0, n / 2) > 0.002f,
               who + " stopped playing during the wrap-up");
        expect(rms(out, n / 2, n) > 0.0005f,
               who + " dropped out entirely instead of thinning: " + who);
      }

      // The lead lays out at the halfway point. It is the clearest "we are
      // ending" signal there is, and it is why the fill has room to be heard.
      const auto lead = render(BotBand::Voice::Lead, s, 0, BotBand::Phase::Wrapping);
      const float first = rms(lead, 0, n / 2);
      const float last = rms(lead, 3 * n / 4, n);
      expect(first > 0.001f, "the lead never played in the wrap-up at all");
      // The LAST QUARTER, not the second half. A note already under way when
      // the lead lays out rings on and finishes -- that is deliberate, and it
      // is what a player does -- so the half straight after the cutoff is
      // still full of tail. By the last quarter the ring-out has gone and only
      // a lead that kept playing would show up.
      expect(last < first * 0.15f,
             "the lead did not lay out: " + juce::String(first) +
                 " over the first half, " + juce::String(last) + " at the end");
    }

    beginTest("an ending is not an ordinary interval");
    {
      // The whole feature, stated as the difference a listener hears. If any
      // of these matched, the states would be real and inaudible.
      for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                         BotBand::Voice::Keys, BotBand::Voice::Lead}) {
        const auto groove = render(voice, s, 0, BotBand::Phase::Groove);
        const auto wrap = render(voice, s, 0, BotBand::Phase::Wrapping);
        const auto land = render(voice, s, 0, BotBand::Phase::Resolving);
        const juce::String who = BotBand::voiceName(voice);

        // The BASS is deliberately unchanged in the wrap-up. Winding down is a
        // taper and not everybody drops at once: the bass and the kit carry
        // the time into the final downbeat, and a rhythm section that thinned
        // out too would leave the landing with nothing to land from. The kit
        // still differs because it gains a fill.
        if (voice != BotBand::Voice::Bass)
          expect(groove != wrap, who + " wraps up exactly as it grooves");
        else
          expect(groove == wrap, "the bass stopped keeping time in the wrap-up");

        expect(groove != land, who + " resolves exactly as it grooves");
        expect(wrap != land, who + " cannot tell the two ending intervals apart");
      }
    }

    beginTest("the resolve lands on the chord the chart resolves to");
    {
      // The theory, heard rather than asserted about: the bass plays the root
      // of `Harmony::resolutionChord`, which for a blues is the chart's own
      // seventh chord and not a derived triad.
      struct Case { const char *key; const char *chart; int wantedPc; };
      const Case cases[] = {
          {"C major", "| Am | F | C | G |", 0},  // C, not the G it loops on
          {"A minor", "| Am | F | C | G |", 9},  // the same chart, A minor
          {"E minor", "| Em | C | G | D |", 4},
      };

      for (const auto &c : cases) {
        auto st = settingsFor(c.key);
        expect(Harmony::parseChart(c.chart, st.chart), c.chart);
        const auto out = render(BotBand::Voice::Bass, st, 0,
                                BotBand::Phase::Resolving);
        const double hz = AudioMeasure::fundamentalHz(out.data(), n / 8,
                                                      st.sampleRate);
        expect(hz > 20.0, juce::String(c.chart) + ": no pitch on the resolve");
        // Semitones above C0, folded into a pitch class.
        const int pc = ((int)std::lround(12.0 * std::log2(hz / 16.3516)) % 12 + 12) % 12;
        expectEquals(pc, c.wantedPc,
                     juce::String(c.chart) + " in " + c.key +
                         " resolved to the wrong root (" + juce::String(hz) + " Hz)");
      }
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
        expectEquals(chalkwalk::music::patternPeriod(bass.steps, bass.pulses),
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
          if (!chalkwalk::music::hit(step, kick.steps, kick.pulses, kick.rotation))
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
          if (chalkwalk::music::patternPeriod(bass.steps, bass.pulses) != bass.steps) {
            expect(false, "bpi " + juce::String(bpi) + " seed " +
                              juce::String((int)seed) + ": E(" +
                              juce::String(bass.pulses) + "," +
                              juce::String(bass.steps) + ") repeats every " +
                              juce::String(chalkwalk::music::patternPeriod(
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
        if (chalkwalk::music::patternPeriod(kick.steps, kick.pulses) < kick.steps)
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

    beginTest("the snare is a drum with a rattle under it");
    {
      // It read as a piccolo snare, or a rim. The body was never the reason --
      // 185 Hz is about right for a 14-inch drum -- because what the ear takes
      // as the pitch of a snare is mostly the wires and the stick, and those
      // sat at 4.2 kHz and 1.6 kHz. All three moved down, and the balance moved
      // off the wires and onto the body.
      //
      // The pitch assertion is the one with teeth: before the change,
      // autocorrelation found no fundamental at all, because the body was too
      // far under the noise to be one. A drum that has a pitch you can measure
      // is a drum rather than a burst.
      const int n = (int)(1.0 * 48000.0);
      std::vector<float> buf((size_t)n, 0.0f);
      BotVoice::renderSnare(buf.data(), n, 48000.0, 0.8f, 5u);

      // Searched between 120 and 320 Hz, which is where a snare body is, and
      // that narrowing is part of the question rather than a way of getting
      // the answer. The two body modes are INHARMONIC -- a shell pitch and an
      // overtone a little over a fifth above it -- so the pair has no single
      // period, and over a longer decay autocorrelation will happily report a
      // slow beat between them as the fundamental. It found 81 Hz that way.
      // What is being asked here is not "what is the pitch of this signal", to
      // which the honest answer is that a drum does not have one; it is
      // whether there is a body ringing where a snare's body rings.
      const double f0 =
          AudioMeasure::fundamentalHz(buf.data(), n, 48000.0, 120.0, 320.0);
      expect(f0 > 130.0 && f0 < 200.0,
             "the snare's body reads at " + juce::String(f0, 1) + " Hz");

      const double centroid =
          AudioMeasure::brightnessHz(buf.data(), n, 48000.0);
      expect(centroid < 4500.0,
             "the snare's energy centres at " + juce::String(centroid, 0) +
                 " Hz, which is a rim rather than a drum");
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

      // Bass and lead are one player standing in one spot, so they stay mono
      // and the listener's pan control decides where they are. The keys are
      // stereo for a reason of their own -- the chorus on the instrument's
      // output -- and are checked separately below.
      for (auto voice : {BotBand::Voice::Bass, BotBand::Voice::Lead})
        expect(!BotBand::isStereo(voice),
               juce::String(BotBand::voiceName(voice)) +
                   " should be a close-miked instrument, not a room");
    }

    beginTest("the keyboard is heard through its chorus, and it is stereo");
    {
      const auto s = settingsFor("C major", 120, 8, 1u);
      const int n = intervalSamplesFor(s);
      std::vector<float> left((size_t)n, 0.0f), right((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Keys, s, 0, left.data(),
                              right.data(), n);

      expect(BotBand::isStereo(BotBand::Voice::Keys));
      expect(left != right, "both sides of the keyboard are identical");

      const float l = rms(left, 0, n), r = rms(right, 0, n);
      expect(l > 0.01f && r > 0.01f, "a side was silent");

      // Within a decibel of each other, and much tighter than the kit's room
      // is allowed to be. The two sides read the same delay line a quarter
      // cycle apart, so they carry the same energy by construction -- a real
      // difference in level would mean the modulation had reached a point
      // where one tap was interpolating badly, or that the chorus had turned
      // into a pan.
      expect(std::abs(l - r) < 0.12f * juce::jmax(l, r),
             "the sides are at different levels: " + juce::String(l, 4) +
                 " against " + juce::String(r, 4));

      // The sides must DIFFER in a way that a fixed offset cannot explain,
      // which is what separates a chorus from a delay. Correlate them at zero
      // lag: identical signals give 1, and a moving comb between them takes it
      // down. Measured 0.87 with the chorus and 1.000 with it bypassed.
      double num = 0.0, dl = 0.0, dr = 0.0;
      for (int i = 0; i < n; ++i) {
        num += (double)left[(size_t)i] * right[(size_t)i];
        dl += (double)left[(size_t)i] * left[(size_t)i];
        dr += (double)right[(size_t)i] * right[(size_t)i];
      }
      const double correlation = num / std::sqrt(juce::jmax(1.0e-12, dl * dr));
      expect(correlation < 0.97 && correlation > 0.2,
             "the sides correlate at " + juce::String(correlation, 3) +
                 ", which is a copy rather than a chorus");
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
      // Averaged over seeds rather than asserted per seed, and that is forced
      // by the material rather than chosen for convenience. A voice's level
      // depends on how busy its figure is -- the kit varies by 3.7 LU across
      // seeds and the bass by 3.1, since a muted bass with few notes puts far
      // less energy in the air than a ringing one with many. At an unlucky
      // seed the bass lands a quarter of a decibel under the kit, and no trim
      // fixes that without making every other seed wrong.
      //
      // Making a seed stop changing the volume is real work and is on the
      // roadmap; until it lands, the balance is a property of the design and
      // not of any single roll of it.
      const std::uint32_t seeds[] = {1u, 7u, 55u, 900u, 4242u, 12345u};
      double kit = 0.0, bass = 0.0, keys = 0.0, lead = 0.0;

      for (std::uint32_t seed : seeds) {
        const auto s2 = settingsFor("C major", 120, 8, seed);
        const int n = intervalSamplesFor(s2);
        for (int v = 0; v < 4; ++v) {
          const auto buf = render((BotBand::Voice)v, s2);
          const double db = AudioMeasure::toDb(rms(buf, 0, n));
          switch ((BotBand::Voice)v) {
          case BotBand::Voice::Drums: kit += db; break;
          case BotBand::Voice::Bass: bass += db; break;
          case BotBand::Voice::Keys: keys += db; break;
          case BotBand::Voice::Lead: lead += db; break;
          }
        }
      }

      const double n = (double)(sizeof(seeds) / sizeof(seeds[0]));
      kit /= n; bass /= n; keys /= n; lead /= n;

      const juce::String at = " (kit " + juce::String(kit, 1) + ", bass " +
                              juce::String(bass, 1) + ", keys " +
                              juce::String(keys, 1) + ", lead " +
                              juce::String(lead, 1) + ")";

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

    beginTest("every corner of every character stays inside the ceiling");
    {
      // The test that makes "the seed may only pick inside this range" mean
      // something.
      //
      // Everything else here renders a default patch, which is the middle of
      // every range and therefore the case least likely to fail. A range is a
      // promise about its ENDS: that a seed drawing the highest resonance and
      // the lowest cutoff it is allowed to draw still produces an instrument
      // rather than a fault. Nothing checked that, so a character that clipped
      // only at the top of one knob would have shipped, and would have shown up
      // as one player in ten reporting a crackle nobody could reproduce.
      //
      // So: every knob of every selection of every voice, at both ends of its
      // range with the rest centred, plus the two corners where everything is
      // at its lowest and everything at its highest. Rendered short -- two
      // seconds at bpi 4 -- because this is a sweep over hundreds of patches
      // and the fault it looks for shows up in the first note if it shows up at
      // all.
      auto lab = BandPatch::defaults();
      int checked = 0;

      for (int v = 0; v < BotBand::kNumVoices; ++v) {
        const auto voice = (BotBand::Voice)v;

        for (int selection = 0; selection < BandPatch::Band::kSelections;
             ++selection) {
          if (voice == BotBand::Voice::Drums)
            break; // no knobs yet

          lab.keysCharacter = (BotVoice::PadCharacter)selection;
          lab.bassTechnique = (BotVoice::BassTechnique)selection;
          lab.lead.instrument = (BotVoice::LeadInstrument)selection;

          const auto knobs = BandPatch::knobsFor(lab, voice);
          if (knobs.empty())
            continue;

          // -1 and -2 are the all-low and all-high corners; 0.. are the
          // individual knobs, low then high.
          for (int k = -2; k < (int)knobs.size() * 2; ++k) {
            auto probe = BandPatch::defaults();
            probe.keysCharacter = lab.keysCharacter;
            probe.bassTechnique = lab.bassTechnique;
            probe.lead.instrument = lab.lead.instrument;

            auto probeKnobs = BandPatch::knobsFor(probe, voice);
            juce::String what;

            if (k < 0) {
              const bool high = (k == -1);
              for (auto &knob : probeKnobs)
                *knob.value = high ? knob.range->hi : knob.range->lo;
              what = high ? "everything at its highest"
                          : "everything at its lowest";
            } else {
              const int index = k / 2;
              const bool high = (k % 2) == 1;
              auto &knob = probeKnobs[(size_t)index];
              *knob.value = high ? knob.range->hi : knob.range->lo;
              what = juce::String(knob.name) + (high ? " at its highest"
                                                     : " at its lowest");
            }

            auto s2 = settingsFor("C major", 120, 4, 1u);
            s2.usePatchOverrides = true;
            s2.keysPatchOverride = probe.keysPatch();
            s2.bassPatchOverride = probe.bassPatch();
            s2.leadPatchOverride = probe.lead;

            const auto buf = render(voice, s2);
            const int n = (int)buf.size();
            const float peak = AudioMeasure::peak(buf.data(), n);
            ++checked;

            const juce::String at =
                juce::String(BotBand::voiceName(voice)) + " (" +
                BandPatch::selectionName(probe, voice) + ") with " + what;

            expect(peak <= 1.0f, at + " peaked at " + juce::String(peak, 4));
            expect(std::isfinite(peak), at + " produced something that is not a number");

            // And it must still be an instrument rather than silence. A knob
            // whose bottom end mutes the voice is a range with a hole in it,
            // which is exactly as much of a defect as one that clips.
            expect(AudioMeasure::rms(buf.data(), n) > 1.0e-4f,
                   at + " rendered essentially nothing");
          }
        }
      }

      expect(checked > 200, "the sweep only covered " + juce::String(checked) +
                                " patches");
      logMessage("swept " + juce::String(checked) + " corner patches");
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

    beginTest("velocity articulates the bass, and does so continuously");
    {
      // The feature, and the worry that shaped it: articulation should follow
      // how hard the note is played, and it must never SWITCH. A threshold
      // anywhere in the velocity range would make two notes either side of it
      // sound like different instruments, which is why technique is a property
      // of the player (chosen once from the seed) and velocity only moves
      // continuously inside it.
      //
      // Measured as brightness against velocity: it must rise, and no single
      // step may jump.
      std::vector<double> brightness;
      const int n = (int)(1.2 * 48000.0);
      for (int i = 0; i <= 8; ++i) {
        const float v = 0.2f + 0.1f * (float)i;
        std::vector<float> buf((size_t)n, 0.0f);
        BotVoice::renderBassString(buf.data(), n, 48000.0, 65.4, v,
                                   BotVoice::bassPatchFor(BotVoice::BassTechnique::Fingered), 4242u);
        brightness.push_back(
            AudioMeasure::brightnessHz(buf.data(), n, 48000.0));
      }

      expect(brightness.back() > brightness.front() * 1.15,
             "playing harder did not brighten the note: " +
                 juce::String(brightness.front(), 1) + " Hz to " +
                 juce::String(brightness.back(), 1) + " Hz");

      const double range = brightness.back() - brightness.front();
      for (size_t i = 1; i < brightness.size(); ++i) {
        const double step = brightness[i] - brightness[i - 1];
        expect(step > -2.0, "brightness went backwards at step " +
                                juce::String((int)i));
        expect(step < range * 0.45,
               "a jump of " + juce::String(step, 1) +
                   " Hz in one velocity step, out of a total range of " +
                   juce::String(range, 1) +
                   " -- that is a switch, not an articulation");
      }
    }

    beginTest("the bass is played rather than typed");
    {
      // Every note used to be velocity 0.7, so the part had no dynamics at all
      // and there was nothing for articulation to follow. A bass player lands
      // hardest on the chord change, then on the kick, and lightest in between.
      //
      // Measured at the downbeat, which is always a chord change and so always
      // the hardest note, against the average of every other onset. This test
      // is the reason the dynamics are as wide as they are: the first version
      // put a passing note only 2.2 dB under an accent, and the ratio here came
      // out at 1.34 against 1.28 for a part with no dynamics at all -- too
      // small to measure, which means too small to hear. Widened, it is 1.50
      // against 1.27.
      const auto s = settingsFor("C major", 120, 8, 1u);
      const auto buf = render(BotBand::Voice::Bass, s);
      const int beat = (int)(s.sampleRate * 60.0 / s.bpm);
      const int window = (int)(0.02 * s.sampleRate);

      double others = 0.0;
      int count = 0;
      for (int step = 1; step * beat / 2 + window < (int)buf.size(); ++step) {
        const float level =
            AudioMeasure::peak(buf.data() + step * beat / 2, window);
        if (level < 0.02f)
          continue; // a rest, not a quiet note
        others += level;
        ++count;
      }

      expect(count > 2, "too few onsets to judge dynamics");
      const double mean = count > 0 ? others / (double)count : 0.0;
      const float downbeat = AudioMeasure::peak(buf.data(), window);
      expect(downbeat > mean * 1.40,
             "the chord change is not landed on: downbeat " +
                 juce::String(downbeat, 4) + " against a mean of " +
                 juce::String(mean, 4));
    }

    beginTest("the three techniques are three different instruments");
    {
      // Not a switch within a part, but they must be distinguishable across
      // parts, or the character is decorative.
      const int n = (int)(1.5 * 48000.0);
      auto render1 = [&](BotVoice::BassTechnique t) {
        std::vector<float> buf((size_t)n, 0.0f);
        BotVoice::renderBassString(buf.data(), n, 48000.0, 65.4, 0.8f,
                                   BotVoice::bassPatchFor(t), 7u);
        return buf;
      };

      const auto fingered = render1(BotVoice::BassTechnique::Fingered);
      const auto picked = render1(BotVoice::BassTechnique::Picked);
      const auto muted = render1(BotVoice::BassTechnique::Muted);

      // A pick is brighter than a finger.
      expect(AudioMeasure::brightnessHz(picked.data(), n, 48000.0) >
                 AudioMeasure::brightnessHz(fingered.data(), n, 48000.0) * 1.15,
             "a plectrum should be brighter than a finger");

      // A mute is shorter, which is the whole of what a mute is.
      const int late = (int)(0.9 * 48000.0);
      expect(rms(muted, late, n) < rms(fingered, late, n) * 0.5f,
             "a muted note should be gone while a fingered one still rings");
    }

    beginTest("the bass has a tone control, and it follows the note");
    {
      // Every other filter in the bass has a cutoff fixed in hertz, and rightly
      // so: a body resonance is an air cavity and a cabinet is a speaker in a
      // box, and neither moves when you play a different note. But that cannot
      // be the whole answer, because it makes the instrument's brightness
      // depend on which note it is playing -- a 2.2 kHz corner is the fifth
      // harmonic of a high note and the fiftieth of a low one, so the bottom of
      // the register came out buzzing with partials no bass would pass.
      //
      // So one filter tracks the note. This is what says so: an octave up must
      // bring the energy up with it. With the fixed filters alone the two notes
      // land within a few percent of each other, since the corner they are both
      // hitting is the same one.
      for (auto technique : {BotVoice::BassTechnique::Fingered,
                             BotVoice::BassTechnique::Picked,
                             BotVoice::BassTechnique::Muted}) {
        const int n = (int)(2.0 * 48000.0);
        double centroid[2] = {0.0, 0.0};
        for (int k = 0; k < 2; ++k) {
          std::vector<float> buf((size_t)n, 0.0f);
          BotVoice::renderBassString(buf.data(), n, 48000.0,
                                     k == 0 ? 41.20 : 82.41, 0.8f,
                                     BotVoice::bassPatchFor(technique), 7u);
          centroid[k] = AudioMeasure::brightnessHz(buf.data(), n, 48000.0);
        }

        const double ratio = centroid[1] / centroid[0];
        expect(ratio > 1.4,
               juce::String(BotVoice::bassTechniqueName(technique)) +
                   ": E1 centres at " + juce::String(centroid[0], 0) +
                   " Hz and E2 at " + juce::String(centroid[1], 0) +
                   " Hz, a ratio of " + juce::String(ratio, 2) +
                   " over an octave");
      }
    }

    beginTest("the bass is a bass and not a low guitar");
    {
      // This used to compare the bass's brightness against the pad's, and the
      // plucked string broke it -- so the question was which of the two was
      // wrong. Both instruments agreed the bass really had got brighter (835 Hz
      // against the pad's 336, measured by slope AND by crossing rate), so it
      // was not a measurement artefact. But the pad is still two detuned sines
      // and is the least realistic thing in the band; it will be brighter than
      // this bass the moment it is rebuilt, and a test that depends on the
      // current state of an unrelated voice breaks for the wrong reason.
      //
      // So the claim is made about the bass alone: its energy must sit within
      // a few harmonics of its own fundamental, which is what separates a bass
      // from an instrument that merely plays low notes. The first version of
      // the plucked string centred on the twelfth harmonic and would fail this
      // by a factor of two.
      //
      // The pad is now a real subtractive voice and the ordering has been
      // restored, as "the keyboard sits above the bass" below. This assertion
      // stays as well, because the two say different things: that one is about
      // the mix, and this one is about the instrument.
      for (const char *keyName : {"C major", "D minor", "F# major"}) {
        for (std::uint32_t seed : {1u, 55u, 900u}) {
          const auto s = settingsFor(keyName, 120, 8, seed);
          const auto buf = render(BotBand::Voice::Bass, s);
          const int n = (int)buf.size();

          const double fundamental = firstNoteHz(buf, s.sampleRate, s.bpm);
          const double centroid =
              AudioMeasure::brightnessHz(buf.data(), n, s.sampleRate);
          if (fundamental <= 0.0) {
            expect(false, juce::String(keyName) + ": no bass note found");
            continue;
          }

          expect(centroid < fundamental * 10.0,
                 juce::String(keyName) + " seed " + juce::String((int)seed) +
                     ": energy centred at " + juce::String(centroid, 0) +
                     " Hz over a " + juce::String(fundamental, 1) +
                     " Hz note, which is " +
                     juce::String(centroid / fundamental, 1) +
                     " harmonics up");
        }
      }
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

  void runKeysTests() {
    beginTest("the seed picks a patch, and every patch is one somebody made");
    {
      // The point of the whole arrangement: the seed is allowed near the front
      // panel, and this is what stops that being a lottery. Every control has
      // a floor and a ceiling that were chosen by listening to both ends, and
      // no seed may produce a setting outside them.
      //
      // These are the OUTER bounds across all three characters, not the
      // per-character ranges, so the test does not simply restate the table it
      // is checking -- it says what a keyboard is allowed to be at all.
      int strings = 0, brass = 0, poly = 0;

      for (std::uint32_t seed = 1; seed <= 400; ++seed) {
        const auto p = BotVoice::padPatchFor(seed * 2654435761u);
        const juce::String at = " at seed " + juce::String((int)seed);

        switch (p.character) {
        case BotVoice::PadCharacter::Strings: ++strings; break;
        case BotVoice::PadCharacter::Brass: ++brass; break;
        case BotVoice::PadCharacter::Poly: ++poly; break;
        }

        expect(p.detuneCents >= 4.0 && p.detuneCents <= 16.0,
               "detune " + juce::String(p.detuneCents, 2) + at);
        expect(p.driftCents >= 1.0 && p.driftCents <= 5.0,
               "drift " + juce::String(p.driftCents, 2) + at);
        expect(p.pulseWidth >= 0.20 && p.pulseWidth <= 0.55,
               "pulse width " + juce::String(p.pulseWidth, 3) + at);
        expect(p.noiseLevel >= 0.0 && p.noiseLevel <= 0.06,
               "noise " + juce::String(p.noiseLevel, 3) + at);
        expect(p.cutoffPartials >= 1.0 && p.cutoffPartials <= 18.0,
               "cutoff " + juce::String(p.cutoffPartials, 2) + at);

        // The one that would be audible as a mistake rather than as a taste.
        // A four-pole lowpass self-oscillates as Q climbs, and a pad that
        // whistles is not a pad. 1.5 is well short of it.
        expect(p.resonance >= 0.5 && p.resonance <= 1.5,
               "resonance " + juce::String(p.resonance, 2) + at);

        expect(p.envAmount >= 1.0 && p.envAmount <= 14.5,
               "filter envelope " + juce::String(p.envAmount, 2) + at);
        // The filter envelope has to arrive within the note or the swell that
        // is the whole point of it happens after the chord has gone.
        expect(p.envAttack >= 0.05 && p.envAttack <= 0.50,
               "filter attack " + juce::String(p.envAttack, 3) + at);
        expect(p.envDecay >= 0.3 && p.envDecay <= 2.0,
               "envelope decay " + juce::String(p.envDecay, 2) + at);

        // An attack longer than a chord would mean the chord never arrives.
        // renderPad clamps it to a fraction of the note rather than letting
        // that happen, so what this bounds is the setting itself: slow enough
        // to be a pad, quick enough that the chord is stated in the bar it
        // belongs to.
        expect(p.attackSeconds >= 0.10 && p.attackSeconds <= 0.90,
               "attack " + juce::String(p.attackSeconds, 3) + at);
        // The release runs on PAST the note-off and overlaps the next chord,
        // so it is not bounded by the slot the way the attack is. What bounds
        // it is the tail renderKeys reserves, which is two seconds.
        expect(p.releaseSeconds >= 0.35 && p.releaseSeconds <= 1.30,
               "release " + juce::String(p.releaseSeconds, 3) + at);
        expect(p.drive >= 0.4 && p.drive <= 1.7,
               "drive " + juce::String(p.drive, 2) + at);
        expect(p.movementHz > 0.0 && p.movementHz <= 0.25,
               "movement " + juce::String(p.movementHz, 3) + at);
        expect(p.level > 0.5 && p.level < 2.0,
               "level " + juce::String(p.level, 2) + at);

        // Two oscillators means two you can tune. The second sits at unison, an
        // octave below, or a fifth above -- and nowhere else, because anything
        // else is an interval the keyboard player did not agree to add to
        // every chord.
        expect(p.secondSemitones == 0 || p.secondSemitones == -12 ||
                   p.secondSemitones == 7,
               "second oscillator at " + juce::String(p.secondSemitones) +
                   " semitones" + at);
        expect(p.secondLevel > 0.3 && p.secondLevel <= 1.0,
               "second oscillator level " + juce::String(p.secondLevel, 2) +
                   at);
      }

      // And all three are reachable. A character that no seed produces is dead
      // code wearing a name.
      expect(strings > 40 && brass > 40 && poly > 40,
             "the characters came up " + juce::String(strings) + " / " +
                 juce::String(brass) + " / " + juce::String(poly) +
                 " times in 400 seeds");
    }

    beginTest("a pad plays the note it was asked for");
    {
      // Detune is a detune and not an instrument that is out of tune: the two
      // oscillators sit either side of the note, so the pair stays centred on
      // it. Pulling both sharp is the easy mistake and this is what catches it.
      //
      // Checked at unison only. When the seed puts the second oscillator an
      // octave down or a fifth up, the "pitch" of the pair is a chord rather
      // than a note and autocorrelation is the wrong instrument for it.
      //
      // The drift is stilled first, and that is the whole reason this test is
      // shaped the way it is. Each oscillator wanders a few cents on its own
      // slow path -- by design, since that is what an analogue polysynth does
      // and most of why a held chord breathes -- so the INSTANTANEOUS pitch of
      // a note is several cents off wherever you sample it. Measured against
      // its own detune, a strings patch read 7.6 cents flat at one moment and
      // would have read sharp at another. Averaging that out needs twenty
      // seconds of audio per note; setting driftCents to zero says the same
      // thing in one line, and leaves the tolerance tight enough to matter.
      //
      // Which it has to be: the mistake this is here to catch is a detune that
      // pulls both oscillators the same way, and that moves the pair by only
      // half the detune. A flat 1% tolerance passed that mutation and was
      // worth nothing.
      for (int midi : {48, 55, 60, 67, 72}) {
        const double want = BotVoice::midiToHz((double)midi);

        for (std::uint32_t seed = 1; seed <= 30; ++seed) {
          auto patch = BotVoice::padPatchFor(seed * 40503u);
          if (patch.secondSemitones != 0)
            continue;
          patch.driftCents = 0.0;

          const int n = (int)(1.5 * 48000.0);
          std::vector<float> buf((size_t)n, 0.0f);
          BotVoice::renderPad(buf.data(), n, n, 48000.0, want, 0.85f, patch,
                              seed);

          // Past the attack, where the filter envelope has settled.
          const int from = (int)(0.7 * 48000.0);
          const double got = AudioMeasure::fundamentalHz(
              buf.data() + from, n - from, 48000.0, want * 0.6, want * 1.6);

          // 0.3%, a little over five cents, and the number is set by what can
          // be measured rather than by what would be nice.
          //
          // Autocorrelation on this signal -- a filtered, saturated,
          // noise-bearing pair of saws -- floors at 0.16%, measured as the
          // worst error over these notes and thirty patches with the drift
          // stilled, and it does not improve with a wider search band. So the
          // threshold is twice the floor.
          //
          // What that does and does not catch is worth being plain about. The
          // both-sharp mutation moves the pair by half the detune: 8 cents on
          // a wide strings patch, which this fails by a comfortable margin,
          // and 2 cents on a narrow one, which it cannot see -- but 2 cents is
          // also not a tuning fault anybody would hear against a band. The
          // test is calibrated to catch the error where it would be audible.
          const double tolerance = 0.003 * want;

          expect(got > 0.0 && std::abs(got - want) < tolerance,
                 "asked for " + juce::String(want, 1) + " Hz and got " +
                     juce::String(got, 1) + ", off by " +
                     juce::String(std::abs(got - want), 3) + " Hz against a " +
                     juce::String(tolerance, 3) + " Hz tolerance (seed " +
                     juce::String((int)seed) + ")");
        }
      }
    }

    beginTest("a pad does not stab, and does not sit still");
    {
      // Two claims that between them are most of what makes a pad a pad.
      //
      // It arrives softly: the amplifier envelope has a real attack, so the
      // first few milliseconds are far below the body of the note. A synth
      // whose envelope was bypassed would fail this immediately.
      //
      // And it does not hold still: two oscillators a few cents apart beat
      // against each other, each drifts on its own slow path, and the filter
      // wanders. Measured as the variation in level from window to window
      // through the middle of a held note -- a single oscillator through a
      // static filter gives essentially zero.
      for (std::uint32_t seed : {3u, 17u, 91u, 404u}) {
        const auto patch = BotVoice::padPatchFor(seed * 2246822519u);
        const int n = (int)(4.0 * 48000.0);
        std::vector<float> buf((size_t)n, 0.0f);
        BotVoice::renderPad(buf.data(), n, n, 48000.0, 220.0, 0.85f, patch,
                            seed);

        const juce::String at =
            juce::String(" (") + BotVoice::padCharacterName(patch.character) +
            ", seed " + juce::String((int)seed) + ")";

        const float onset = rms(buf, 0, (int)(0.010 * 48000.0));
        const float body =
            rms(buf, (int)(1.0 * 48000.0), (int)(2.0 * 48000.0));
        expect(onset < body * 0.25f,
               "the first 10 ms are at " + juce::String(onset, 4) +
                   " against a body of " + juce::String(body, 4) + at);

        // Movement, over the sustained middle where no envelope is acting.
        const int from = (int)(1.0 * 48000.0);
        const int window = (int)(0.05 * 48000.0);
        double lowest = 1.0e9, highest = 0.0;
        for (int w = 0; w < 40; ++w) {
          const float level = rms(buf, from + w * window,
                                  from + (w + 1) * window);
          lowest = std::min(lowest, (double)level);
          highest = std::max(highest, (double)level);
        }
        expect(highest > lowest * 1.05,
               "a held note varied by only " +
                   juce::String(20.0 * std::log10(highest / lowest), 2) +
                   " dB across two seconds, so nothing is moving" + at);
      }
    }

    beginTest("the brass patch swells into the note");
    {
      // What makes a subtractive synth sound blown rather than switched on: the
      // filter starts closed and opens as the note arrives. It had no attack at
      // all -- widest on the first sample, closing from there, which is the
      // shape of something plucked -- and the brass patch consequently sounded
      // like nothing in particular.
      //
      // Measured as brightness in the first 30 ms against brightness at the top
      // of the filter envelope. With the attack removed the second window is
      // DARKER than the first, so this fails in the right direction rather than
      // merely failing.
      int checked = 0;
      for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        const auto patch = BotVoice::padPatchFor(seed * 2654435761u);
        if (patch.character != BotVoice::PadCharacter::Brass)
          continue;
        ++checked;

        const int n = (int)(2.0 * 48000.0);
        std::vector<float> buf((size_t)n, 0.0f);
        BotVoice::renderPad(buf.data(), n, n, 48000.0, 261.63, 0.85f, patch,
                            seed);

        const int window = (int)(0.030 * 48000.0);
        const int top = (int)(patch.envAttack * 48000.0);
        const double closed =
            AudioMeasure::brightnessHz(buf.data(), window, 48000.0);
        const double open =
            AudioMeasure::brightnessHz(buf.data() + top, window, 48000.0);

        expect(open > closed * 1.3,
               "seed " + juce::String((int)seed) + ": the filter went from " +
                   juce::String(closed, 0) + " Hz to " + juce::String(open, 0) +
                   " Hz, which is not a swell");
      }
      expect(checked >= 3, "no brass patches were exercised");
    }

    beginTest("a chord is let go rather than cut off");
    {
      // The release runs on past the note-off, so a chord overlaps the one
      // that replaces it. An envelope whose release has to finish inside its
      // own slot is a player lifting both hands cleanly between every chord,
      // and it reads as chopped however gentle the release is made.
      for (std::uint32_t seed : {3u, 17u, 91u}) {
        const auto patch = BotVoice::padPatchFor(seed * 2246822519u);
        const int n = (int)(4.0 * 48000.0);
        const int hold = (int)(1.0 * 48000.0);
        std::vector<float> buf((size_t)n, 0.0f);
        BotVoice::renderPad(buf.data(), n, hold, 48000.0, 261.63, 0.85f, patch,
                            seed);

        const juce::String at =
            juce::String(" (") + BotVoice::padCharacterName(patch.character) +
            ", release " + juce::String(patch.releaseSeconds, 2) + " s)";

        const float held = rms(buf, (int)(0.6 * 48000.0), hold);
        const float after = rms(buf, hold + (int)(0.15 * 48000.0),
                                hold + (int)(0.35 * 48000.0));

        // Still clearly sounding a fifth of a second after the key came up.
        expect(after > held * 0.25f,
               "the note fell from " + juce::String(held, 4) + " to " +
                   juce::String(after, 4) + " within 350 ms of note-off" + at);

        // And it does end. The release is linear to zero, so past its length
        // the buffer is exactly silent -- which also says the tail cannot run
        // on into whatever the caller renders next.
        const int done = hold + (int)((patch.releaseSeconds + 0.05) * 48000.0);
        if (done < n)
          expectEquals(AudioMeasure::peak(buf.data() + done, n - done), 0.0f,
                       "the note never stopped" + at);
      }
    }

    beginTest("changing patch is not changing volume");
    {
      // The other half of "a safe scope". A seed that picks a different
      // keyboard must not also turn the keyboard up: the patches differ in
      // waveform, drive, filter envelope and release length, and every one of
      // those affects loudness. Measured at 6.4 LU between brass and strings
      // before the per-character output level was fitted.
      //
      // The claim is made about the CHARACTER MEANS rather than about every
      // render, and that split is the whole point of the test. A constant can
      // only correct what the patch does; it cannot correct how many notes the
      // voicing happened to put where, and with releases now ringing over the
      // chord changes that varies by about three decibels from seed to seed.
      // Asserting a tight bound on the total spread would mean either a
      // toothless threshold or a level knob being asked to fix an arrangement.
      //
      // Loudness rather than rms, because that is the unit the complaint would
      // be made in, and measured as the stereo pair the bot transmits.
      std::map<juce::String, std::vector<double>> byCharacter;
      double quietest = 0.0, loudest = -200.0;

      for (std::uint32_t seed : {1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u,
                                 144u, 233u, 777u, 4242u}) {
        const auto s2 = settingsFor("C major", 120, 8, seed);
        const int n = intervalSamplesFor(s2);
        std::vector<float> left((size_t)n, 0.0f), right((size_t)n, 0.0f);
        BotBand::renderInterval(BotBand::Voice::Keys, s2, 0, left.data(),
                                right.data(), n);

        const double lufs = AudioMeasure::integratedLufs(
            left.data(), right.data(), n, s2.sampleRate);

        byCharacter[juce::String(BotVoice::padCharacterName(
                        BotBand::keysPatch(s2).character))]
            .push_back(lufs);

        if (lufs > loudest) loudest = lufs;
        if (quietest == 0.0 || lufs < quietest) quietest = lufs;
      }

      expectEquals((int)byCharacter.size(), 3,
                   "not every character was exercised");

      double lowestMean = 1.0e9, highestMean = -1.0e9;
      juce::String detail;
      for (const auto &entry : byCharacter) {
        double mean = 0.0;
        for (double v : entry.second)
          mean += v;
        mean /= (double)entry.second.size();
        lowestMean = std::min(lowestMean, mean);
        highestMean = std::max(highestMean, mean);
        detail += " " + entry.first + " " + juce::String(mean, 2);
      }

      // This is what the level constants control, so this is where the tight
      // bound belongs. Measured at 0.18 LU.
      expect(highestMean - lowestMean < 0.7,
             "the characters sit " + juce::String(highestMean - lowestMean, 2) +
                 " LU apart:" + detail);

      // And a loose bound on the whole range, so a patch that blew up in some
      // other way still gets caught.
      expect(loudest - quietest < 3.5,
             "the keyboard spans " + juce::String(loudest - quietest, 1) +
                 " LU across seeds");
    }

    beginTest("the keyboard sits above the bass");
    {
      // The ordering the plucked string broke and left on the roadmap.
      //
      // It is a MIX claim rather than a synthesis one: a bass brighter than the
      // chords over it means the two are fighting for the same part of the
      // spectrum, and on a laptop speaker the one that wins is whichever is
      // louder that second. When the plucked bass first landed it measured
      // 835 Hz against a pad's 336 and the ordering was inverted; both are now
      // real instruments and it is the right way round again.
      for (const char *keyName : {"C major", "D minor"}) {
        for (std::uint32_t seed : {1u, 55u, 900u, 4242u}) {
          const auto s = settingsFor(keyName, 120, 8, seed);
          const auto bass = render(BotBand::Voice::Bass, s);
          const auto keys = render(BotBand::Voice::Keys, s);

          const double bassHz = AudioMeasure::brightnessHz(
              bass.data(), (int)bass.size(), s.sampleRate);
          const double keysHz = AudioMeasure::brightnessHz(
              keys.data(), (int)keys.size(), s.sampleRate);

          expect(keysHz > bassHz * 1.2,
                 juce::String(keyName) + " seed " + juce::String((int)seed) +
                     ": keys at " + juce::String(keysHz, 0) +
                     " Hz against a bass at " + juce::String(bassHz, 0) +
                     " Hz");
        }
      }
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
            const int tier = noteTier(line[step], chord);
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
      expectEquals(noteTier(60, cMajor), 0, "C over C is the root");
      expectEquals(noteTier(64, cMajor), 0, "E over C is the third");
      expectEquals(noteTier(65, cMajor), 2, "F sits above the third");
      expectEquals(noteTier(62, cMajor), 1, "D is comfortable");

      const auto aMinor = Harmony::chordOn(9, Harmony::Quality::Minor);
      expectEquals(noteTier(65, aMinor), 2,
                   "the flat sixth sits above the fifth -- the minor problem");
      expectEquals(noteTier(62, aMinor), 1, "the fourth is fine");

      // Lydian's sharp fourth is a whole tone above the third, so it is the
      // characteristic note rather than one to handle carefully.
      const auto fMajor = Harmony::chordOn(5, Harmony::Quality::Major);
      expectEquals(noteTier(71, fMajor), 1, "B over F is Lydian");
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

    beginTest("the seed hands the lead a different instrument");
    {
      // All three must be reachable, and reachable often enough that a player
      // meets them. An instrument no seed produces is dead code wearing a name.
      int epiano = 0, guitar = 0, synth = 0;
      for (std::uint32_t seed = 1; seed <= 300; ++seed) {
        const auto s2 = settingsFor("C major", 120, 8, seed);
        switch (BotBand::leadInstrument(s2)) {
        case BotVoice::LeadInstrument::EPiano: ++epiano; break;
        case BotVoice::LeadInstrument::Guitar: ++guitar; break;
        case BotVoice::LeadInstrument::Synth: ++synth; break;
        }
      }
      expect(epiano > 50 && guitar > 50 && synth > 50,
             "the instruments came up " + juce::String(epiano) + " / " +
                 juce::String(guitar) + " / " + juce::String(synth) +
                 " times in 300 seeds");
    }

    beginTest("asking for an instrument overrides the seed, and only that");
    {
      // The one thing about the band a player can pin. It has to actually
      // stick -- including across a shake, since somebody who asked for a
      // guitar because they came to practise keyboards has not changed their
      // mind about that by asking for a different tune.
      auto s2 = settingsFor("C major", 120, 8, 1u);
      expect(BotBand::leadInstrument(s2) != BotVoice::LeadInstrument::Guitar,
             "seed 1 already gives a guitar, so this proves nothing");

      s2.leadOverride = (int)BotVoice::LeadInstrument::Guitar;
      expect(BotBand::leadInstrument(s2) == BotVoice::LeadInstrument::Guitar,
             "the override was ignored");

      const auto beforeShake = render(BotBand::Voice::Lead, s2);
      s2.seed = 909u;
      expect(BotBand::leadInstrument(s2) == BotVoice::LeadInstrument::Guitar,
             "a new seed took the instrument back");
      expect(render(BotBand::Voice::Lead, s2) != beforeShake,
             "a new seed changed nothing, so the shake is not working either");

      // And nonsense goes back to the seed rather than to a wrong instrument.
      s2.leadOverride = 47;
      expect(BotBand::leadInstrument(s2) ==
                 BotBand::leadInstrument(settingsFor("C major", 120, 8, 909u)),
             "an out-of-range override was taken seriously");
    }

    beginTest("the three instruments are three different instruments");
    {
      // Each has to be recognisably a different thing in the room, or the
      // choice is decoration. Measured on what separates them by ear: the
      // guitar and the piano are struck and decay, the synth is held; the
      // guitar is far brighter than the piano, whose energy sits close to its
      // fundamental because a tine is nearly a sine until it is hit hard.
      double bright[3] = {0.0, 0.0, 0.0};
      double crest[3] = {0.0, 0.0, 0.0};

      for (int i = 0; i < 3; ++i) {
        auto s2 = settingsFor("C major", 120, 8, 4242u);
        s2.leadOverride = i;
        const auto buf = render(BotBand::Voice::Lead, s2);
        bright[i] = AudioMeasure::brightnessHz(buf.data(), (int)buf.size(),
                                               s2.sampleRate);
        crest[i] = AudioMeasure::crest(buf.data(), (int)buf.size());
      }

      const juce::String at =
          " (epiano " + juce::String(bright[0], 0) + " Hz crest " +
          juce::String(crest[0], 2) + ", guitar " + juce::String(bright[1], 0) +
          " Hz crest " + juce::String(crest[1], 2) + ", synth " +
          juce::String(bright[2], 0) + " Hz crest " +
          juce::String(crest[2], 2) + ")";

      expect(bright[1] > bright[0] * 1.5,
             "the guitar should be much brighter than the electric piano" + at);
      expect(crest[1] > crest[2] * 1.5,
             "a plucked line should be peakier than a held one" + at);
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
