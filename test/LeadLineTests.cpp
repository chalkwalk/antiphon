#include "../src/BotBand.h"
#include "../src/Harmony.h"
#include "../src/MusicalKey.h"
#include <JuceHeader.h>

#include <chalkwalk/music/Euclidean.h>
#include <chalkwalk/music/Melody.h>

#include <cmath>

// The lead's note choice, and the seam between antiphon's key model and
// chalkwalk-music's.
//
// Antiphon keeps `MusicalKey::Key` for harmony, chord spelling and roman
// numerals -- all of which genuinely need exactly seven degrees, and one of
// which (`spellNote`) refuses to run without them. The shared `KeySig` is a
// pitch-class mask of any size. The conversion runs one way only, at the point
// where a note is ranked. See `../ECOSYSTEM.md`.

class LeadLineTests : public juce::UnitTest {
public:
  LeadLineTests() : juce::UnitTest("LeadLine", "music") {}

  void runTest() override {
    runKeyConversion();
    runChordConversion();
    runRhythmFromTheFigure();
    runWellFormed();
    runDeterminism();
    runChordAwareness();
    runMelodicShape();
    runIntervalSeam();
  }

private:
  static BotBand::Settings settingsFor(const juce::String &keyName,
                                       std::uint32_t seed) {
    auto key = MusicalKey::parseName(keyName);
    return BotBand::defaults(key, 120, 8, 48000.0, seed);
  }

  // Every mode antiphon can express must land on the right fifths window.
  // Major and Minor are Ionian and Aeolian: they differ in what a player is
  // shown, never in pitch, and a conversion that treated them as distinct
  // would put the band in the wrong key for two of the nine spellings.
  void runKeyConversion() {
    beginTest("every mode converts to the right brightness");
    namespace m = chalkwalk::music;
    const struct { const char *name; int brightness; } cases[] = {
        {"C major", m::kIonian},      {"C Ionian", m::kIonian},
        {"C minor", m::kAeolian},     {"C Aeolian", m::kAeolian},
        {"C Dorian", m::kDorian},     {"C Phrygian", m::kPhrygian},
        {"C Lydian", m::kLydian},     {"C Mixolydian", m::kMixolydian},
        {"C Locrian", m::kLocrian},
    };
    for (const auto &c : cases) {
      const auto key = MusicalKey::parseName(c.name);
      expect(key.valid, juce::String("parses ") + c.name);
      const auto sig = BotBand::toKeySig(key);
      expectEquals(static_cast<int>(sig.brightness), c.brightness,
                   juce::String(c.name));
      expectEquals(static_cast<int>(sig.root), 0);
    }

    beginTest("the converted scale has the notes the mode has");
    for (const char *name : {"D minor", "F# Dorian", "Bb Lydian", "E Phrygian"}) {
      const auto key = MusicalKey::parseName(name);
      const auto sig = BotBand::toKeySig(key);
      const auto mask = m::pcMask(sig);

      const int *steps = MusicalKey::scaleSteps(key.mode);
      for (int d = 0; d < MusicalKey::kScaleDegrees; ++d) {
        const int pc = ((key.tonic + steps[d]) % 12 + 12) % 12;
        expect(m::maskHas(mask, pc),
               juce::String(name) + " contains degree " + juce::String(d));
      }
      int count = 0;
      for (int pc = 0; pc < 12; ++pc)
        if (m::maskHas(mask, pc))
          ++count;
      expectEquals(count, 7, juce::String(name) + " has seven notes");
    }
  }

  void runChordConversion() {
    beginTest("chord tones fold into pitch classes");
    Harmony::Chord c;
    c.root = 2;                     // D
    c.tones = {{0, 3, 7, 14, 0}};   // minor triad plus a ninth, unreduced
    c.toneCount = 4;

    const auto sounding = BotBand::toSoundingChord(c);
    expect(sounding.present());
    expectEquals(sounding.root, 2);
    namespace m = chalkwalk::music;
    expect(m::maskHas(sounding.tones, 2), "D");
    expect(m::maskHas(sounding.tones, 5), "F");
    expect(m::maskHas(sounding.tones, 9), "A");
    // The ninth is 14 semitones up and must fold to E, not be dropped.
    expect(m::maskHas(sounding.tones, 4), "E, from the unreduced ninth");
  }

  // THE ONE-WAY COUPLING, asserted.
  //
  // Note choice may inform the rhythm; the rhythm must never depend on it. The
  // onset grid is the Euclidean figure, which the rest of the band shares, so
  // a note that sounded somewhere the figure has no onset would mean the lead
  // had quietly stopped playing the same groove as everyone else.
  void runRhythmFromTheFigure() {
    beginTest("every sounding step is an onset of the lead's figure");
    for (const char *name : {"C major", "D minor", "Bb Lydian", "A Phrygian"})
      for (std::uint32_t seed = 1; seed <= 25; ++seed) {
        const auto s = settingsFor(name, seed);
        const auto f = BotBand::figureFor(BotBand::Voice::Lead, s);
        for (int interval = 0; interval < 4; ++interval) {
          const auto line = BotBand::leadLine(s, interval);
          for (size_t i = 0; i < line.size(); ++i)
            if (line[i] >= 0)
              expect(chalkwalk::music::hit((int)i, f.steps, f.pulses, f.rotation),
                     juce::String(name) + " seed " + juce::String((int)seed) +
                         ": note at step " + juce::String((int)i) +
                         ", which the figure does not strike");
        }
      }
  }

  void runWellFormed() {
    beginTest("the line stays in range and in register");
    for (const char *name : {"C major", "F# Dorian", "Bb Lydian"})
      for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        const auto s = settingsFor(name, seed);
        for (int interval = 0; interval < 4; ++interval)
          for (int note : BotBand::leadLine(s, interval)) {
            if (note < 0)
              continue;
            expect(note >= 0 && note <= 127, "a valid MIDI note");
            // The lead sits above the keys by design, and wandering out of
            // that register is what makes it stop reading as a melody.
            expect(note >= 48 && note <= 100,
                   juce::String(name) + ": note " + juce::String(note) +
                       " is outside the lead's register");
          }
      }
  }

  void runDeterminism() {
    beginTest("the same seed gives the same line, every time");
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
      const auto s = settingsFor("D minor", seed);
      for (int interval = 0; interval < 3; ++interval)
        expect(BotBand::leadLine(s, interval) == BotBand::leadLine(s, interval),
               "reproducible from the seed");
    }
  }

  // The line should follow the chart, measured as how often a sounding note is
  // a tone of the chord under it. A floor rather than a target: pinning it too
  // tightly would forbid the passing notes that make it a melody.
  void runChordAwareness() {
    beginTest("the line plays the changes");
    int hits = 0, notes = 0;

    for (const char *name : {"C major", "D minor", "Bb Lydian", "G Mixolydian"})
      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto s = settingsFor(name, seed);
        const auto layout = Harmony::layoutChart(s.chart, s.bpi);
        for (int interval = 0; interval < 4; ++interval) {
          const auto line = BotBand::leadLine(s, interval);
          for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] < 0)
              continue;
            ++notes;
            const auto sounding =
                BotBand::toSoundingChord(Harmony::chordAtStep(layout, (int)i));
            if (chalkwalk::music::maskHas(sounding.tones, line[i] % 12))
              ++hits;
          }
        }
      }

    const double rate = notes ? (double)hits / notes : 0.0;
    logMessage("  chord-tone rate: " + juce::String(rate, 3));
    expect(rate > 0.5, "over half the notes are chord tones");
    expect(rate < 0.95, "not EVERY note is a chord tone -- that is an arpeggio");
  }

  // What the interval objective bought, asserted rather than described.
  //
  // The claim is not "the line moves by exactly this much", it is "wide
  // awkward leaps do not happen, and the line is mostly stepwise". Every
  // threshold below is set from what the previous behaviour actually measured,
  // so each one is a real regression detector rather than a guess:
  //
  //                        before      after     threshold
  //   mean interval     2.17-2.84  2.31-2.47         < 2.6
  //   stepwise          53%-67%      74%-76%         > 70%
  //   repeated notes    20%        1.8%-5.2%          < 10%
  //   awkward wide      20-31 per 1939     0    < 0.5% of moves
  //
  // The mean is HIGHER than the 1.74-2.08 an earlier revision measured, and
  // that is arithmetic rather than a regression: a repeated note is a move of
  // zero, so 20% repeats drag a mean down. Pricing the unison removed most of
  // them, and what is left is the real melodic motion. Stepwise and awkward
  // wide are the qualities to read; the mean is a cross-check.
  //
  // The awkward-wide count is the sharpest: it was 31 in C major even AFTER
  // the interval objective landed, because the melody's memory still reset at
  // every interval boundary. Carrying the previous line's last note across
  // that seam took it to zero.
  void runMelodicShape() {
    beginTest("the line moves mostly by step, and leaps idiomatically");
    for (const char *name : {"C major", "D minor", "Bb Lydian", "G Mixolydian"}) {
      int moves = 0, stepwise = 0, wideAwkward = 0, repeats = 0;
      long long motion = 0;

      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto s = settingsFor(name, seed);
        int last = -1;
        for (int interval = 0; interval < 6; ++interval)
          for (int n : BotBand::leadLine(s, interval)) {
            if (n < 0)
              continue;
            if (last >= 0) {
              const int d = std::abs(n - last);
              ++moves;
              motion += d;
              if (d == 0)
                ++repeats;
              if (d <= 2)
                ++stepwise;
              // Wide AND not one of the leaps a melody actually makes.
              if (d >= 8 && d != 12)
                ++wideAwkward;
            }
            last = n;
          }
      }

      const double mean = moves ? (double)motion / moves : 0.0;
      const double stepRate = moves ? (double)stepwise / moves : 0.0;
      const double repeatRate = moves ? (double)repeats / moves : 0.0;
      logMessage(juce::String(name) + ": mean " + juce::String(mean, 2) +
                 ", stepwise " + juce::String(100.0 * stepRate, 1) +
                 "%, repeats " + juce::String(100.0 * repeatRate, 1) +
                 "%, wide awkward " + juce::String(wideAwkward));

      expect(mean < 2.6, juce::String(name) + ": mean interval " +
                             juce::String(mean, 2) + " is too wide");
      expect(stepRate > 0.7, juce::String(name) + ": only " +
                                 juce::String(100.0 * stepRate, 1) +
                                 "% of moves are stepwise");
      expect(wideAwkward * 200 < moves,
             juce::String(name) + ": " + juce::String(wideAwkward) +
                 " awkward wide leaps in " + juce::String(moves) + " moves");

      // A line that mostly repeats itself is a drone, and nothing else here
      // notices. This was NOT hypothetical: with the unison priced at 0 like
      // a step, raising the direction weight took repeats to 54% of all moves
      // -- the objective had made standing still the cheapest thing to do.
      expect(repeatRate < 0.10, juce::String(name) + ": " +
                                    juce::String(100.0 * repeatRate, 1) +
                                    "% of moves repeat the note");
    }
  }
  // The seam between two intervals is a real melodic move and must be priced
  // like one. It was not: the line's memory reset every four seconds, which
  // made the boundary the single most leap-prone moment in the whole melody.
  // Asserted separately from the shape test because it is invisible there --
  // 200 bad moves in 1939 barely shift a mean.
  void runIntervalSeam() {
    beginTest("the line joins across the interval boundary");
    int seams = 0, wide = 0;
    long long motion = 0;

    for (const char *name : {"C major", "D minor", "Bb Lydian", "G Mixolydian"})
      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto s = settingsFor(name, seed);
        for (int interval = 1; interval < 6; ++interval) {
          const auto before = BotBand::leadLine(s, interval - 1);
          const auto after = BotBand::leadLine(s, interval);

          int last = -1;
          for (int n : before)
            if (n >= 0)
              last = n;
          int first = -1;
          for (int n : after)
            if (n >= 0) {
              first = n;
              break;
            }
          if (last < 0 || first < 0)
            continue;

          ++seams;
          const int d = std::abs(first - last);
          motion += d;
          if (d >= 8 && d != 12)
            ++wide;
        }
      }

    const double mean = seams ? (double)motion / seams : 0.0;
    logMessage("  " + juce::String(seams) + " seams, mean " +
               juce::String(mean, 2) + ", awkward wide " + juce::String(wide));
    expect(seams > 100, "the sweep actually produced seams to measure");

    // Measured with the carry disabled: mean 4.00 and 90 awkward leaps in 800
    // seams. With it: 3.29 and 4. The awkward count is the assertion that
    // matters, and the mean has the same caveat as the shape test above -- it
    // rose from 2.97 when the unison was repriced, because a seam that used to
    // repeat the note is now a real move rather than a zero dragging the mean
    // down.
    expect(wide * 100 < seams, juce::String(wide) + " awkward leaps across " +
                                   juce::String(seams) + " seams");

    // The seam stays WIDER than the line inside an interval, which measures
    // about 2.0, and that is not a defect being tolerated. The contour is
    // rerolled per interval, so a new phrase legitimately begins somewhere new
    // -- a Fall handing over to a Rise aims twelve semitones away, and the
    // interval cost should lose that argument. What must not survive is the
    // seam being the most leap-prone moment in the melody, which is what 4.00
    // and 90 awkward leaps meant.
    expect(mean < 3.7, "the boundary leaps more than a new phrase justifies: " +
                           juce::String(mean, 2));
  }
};

static LeadLineTests leadLineTests;
