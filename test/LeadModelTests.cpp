#include "../src/BotBand.h"
#include "../src/Harmony.h"
#include "../src/MusicalKey.h"
#include <JuceHeader.h>

// The two lead models, and the seam between antiphon's key model and
// chalkwalk-music's.
//
// Antiphon keeps `MusicalKey::Key` for harmony, chord spelling and roman
// numerals -- all of which genuinely need exactly seven degrees, and one of
// which (`spellNote`) refuses to run without them. The shared `KeySig` is a
// pitch-class mask of any size. The conversion runs one way only, at the point
// where a note is ranked. See `../ECOSYSTEM.md`.

class LeadModelTests : public juce::UnitTest {
public:
  LeadModelTests() : juce::UnitTest("LeadModel", "music") {}

  void runTest() override {
    runKeyConversion();
    runChordConversion();
    runRhythmUnchanged();
    runWellFormed();
    runDeterminism();
    runChordAwareness();
  }

private:
  static BotBand::Settings settingsFor(const juce::String &keyName,
                                       std::uint32_t seed,
                                       BotBand::LeadModel model) {
    auto key = MusicalKey::parseName(keyName);
    auto s = BotBand::defaults(key, 120, 8, 48000.0, seed);
    s.leadModel = model;
    return s;
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

  // The models must differ only in WHICH note is chosen, never in whether one
  // sounds. Rhythm comes from the figure and the rest rule, which are shared,
  // so any note-versus-rest difference means the seed stream diverged -- which
  // would make the whole A/B meaningless.
  void runRhythmUnchanged() {
    beginTest("the two models place notes and rests identically");
    for (const char *name : {"C major", "D minor", "Bb Lydian", "A Phrygian"})
      for (std::uint32_t seed = 1; seed <= 25; ++seed) {
        const auto legacy = settingsFor(name, seed, BotBand::LeadModel::Legacy);
        const auto shared = settingsFor(name, seed, BotBand::LeadModel::Shared);
        for (int interval = 0; interval < 4; ++interval) {
          const auto a = BotBand::leadLine(legacy, interval);
          const auto b = BotBand::leadLine(shared, interval);
          expectEquals((int)a.size(), (int)b.size());
          for (size_t i = 0; i < a.size(); ++i)
            expect((a[i] < 0) == (b[i] < 0),
                   juce::String(name) + " seed " + juce::String((int)seed) +
                       " step " + juce::String((int)i) + ": rest mismatch");
        }
      }
  }

  void runWellFormed() {
    beginTest("the shared model stays in range and in register");
    for (const char *name : {"C major", "F# Dorian", "Bb Lydian"})
      for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        const auto s = settingsFor(name, seed, BotBand::LeadModel::Shared);
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
    for (auto model : {BotBand::LeadModel::Legacy, BotBand::LeadModel::Shared})
      for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        const auto s = settingsFor("D minor", seed, model);
        for (int interval = 0; interval < 3; ++interval)
          expect(BotBand::leadLine(s, interval) == BotBand::leadLine(s, interval),
                 "reproducible from the seed");
      }
  }

  // The point of the shared model: the line should follow the chart. Measured
  // as how often a sounding note is a tone of the chord under it.
  void runChordAwareness() {
    beginTest("the shared model lands on chord tones at least as often");
    int legacyHits = 0, legacyNotes = 0;
    int sharedHits = 0, sharedNotes = 0;

    for (const char *name : {"C major", "D minor", "Bb Lydian", "G Mixolydian"})
      for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        const auto legacy = settingsFor(name, seed, BotBand::LeadModel::Legacy);
        const auto shared = settingsFor(name, seed, BotBand::LeadModel::Shared);
        const auto layout = Harmony::layoutChart(legacy.chart, legacy.bpi);

        for (int interval = 0; interval < 4; ++interval) {
          const auto a = BotBand::leadLine(legacy, interval);
          const auto b = BotBand::leadLine(shared, interval);
          for (size_t i = 0; i < a.size(); ++i) {
            const auto &chord = Harmony::chordAtStep(layout, (int)i);
            const auto sounding = BotBand::toSoundingChord(chord);
            if (a[i] >= 0) {
              ++legacyNotes;
              if (chalkwalk::music::maskHas(sounding.tones, a[i] % 12))
                ++legacyHits;
            }
            if (b[i] >= 0) {
              ++sharedNotes;
              if (chalkwalk::music::maskHas(sounding.tones, b[i] % 12))
                ++sharedHits;
            }
          }
        }
      }

    const double legacyRate = legacyNotes ? (double)legacyHits / legacyNotes : 0.0;
    const double sharedRate = sharedNotes ? (double)sharedHits / sharedNotes : 0.0;
    logMessage("  chord-tone rate: legacy " + juce::String(legacyRate, 3) +
               ", shared " + juce::String(sharedRate, 3));
    expect(sharedRate >= legacyRate - 0.02,
           "the shared model does not play the changes LESS than the legacy one");
  }
};

static LeadModelTests leadModelTests;
