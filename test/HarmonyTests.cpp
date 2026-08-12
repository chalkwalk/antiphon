#include "../src/Harmony.h"
#include <JuceHeader.h>

// The chords are exact, so these are ordinary equality tests. Only the audio
// that eventually comes out of them has to be measured statistically.

namespace {

MusicalKey::Key keyOf(const juce::String &name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

juce::String toneList(const Harmony::Chord &c) {
  juce::StringArray s;
  for (int i = 0; i < c.toneCount; ++i)
    s.add(juce::String((int)c.tones[(size_t)i]));
  return s.joinIntoString(",");
}

} // namespace

class HarmonyTests : public juce::UnitTest {
public:
  HarmonyTests() : juce::UnitTest("Harmony", "music") {}

  void runTest() override {
    runChordTests();
    runDiatonicTests();
    runDefaultProgressionTests();
    runBeatMappingTests();
  }

  void runChordTests() {
    beginTest("a quality names its tones");
    {
      auto maj = Harmony::chordOn(0, Harmony::Quality::Major);
      expectEquals(maj.root, 0);
      expectEquals(toneList(maj), juce::String("0,4,7"));

      auto min = Harmony::chordOn(2, Harmony::Quality::Minor);
      expectEquals(min.root, 2);
      expectEquals(toneList(min), juce::String("0,3,7"));

      auto dom = Harmony::chordOn(7, Harmony::Quality::Dominant7);
      expectEquals(toneList(dom), juce::String("0,4,7,10"));

      auto halfDim = Harmony::chordOn(11, Harmony::Quality::HalfDiminished7);
      expectEquals(toneList(halfDim), juce::String("0,3,6,10"));
    }

    beginTest("roots wrap into a pitch class");
    {
      expectEquals(Harmony::chordOn(14, Harmony::Quality::Major).root, 2);
      expectEquals(Harmony::chordOn(-1, Harmony::Quality::Major).root, 11);
    }
  }

  void runDiatonicTests() {
    beginTest("C major gives the triads everyone expects");
    {
      const auto c = keyOf("C major");
      // I ii iii IV V vi vii(dim)
      const int roots[] = {0, 2, 4, 5, 7, 9, 11};
      const Harmony::Quality quals[] = {
          Harmony::Quality::Major,      Harmony::Quality::Minor,
          Harmony::Quality::Minor,      Harmony::Quality::Major,
          Harmony::Quality::Major,      Harmony::Quality::Minor,
          Harmony::Quality::Diminished};

      for (int d = 0; d < 7; ++d) {
        const auto chord = Harmony::diatonicTriad(c, d);
        expectEquals(chord.root, roots[d], "degree " + juce::String(d));
        expect(chord.quality == quals[d],
               "degree " + juce::String(d) + " quality");
      }
    }

    beginTest("the mode decides the quality, not a table per key");
    {
      // Lydian's II is major where Ionian's ii is minor -- the case that makes
      // stacking thirds out of the scale worth doing.
      const auto lydian = keyOf("C Lydian");
      const auto two = Harmony::diatonicTriad(lydian, 1);
      expect(two.quality == Harmony::Quality::Major, "Lydian II should be major");

      // Dorian's IV is major where Aeolian's iv is minor.
      const auto dorian = keyOf("D Dorian");
      const auto four = Harmony::diatonicTriad(dorian, 3);
      expect(four.quality == Harmony::Quality::Major, "Dorian IV should be major");

      const auto aeolian = keyOf("A minor");
      const auto minorFour = Harmony::diatonicTriad(aeolian, 3);
      expect(minorFour.quality == Harmony::Quality::Minor,
             "Aeolian iv should be minor");
    }

    beginTest("degrees run past the seventh and below the tonic");
    {
      const auto c = keyOf("C major");
      expectEquals(Harmony::diatonicTriad(c, 7).root,
                   Harmony::diatonicTriad(c, 0).root);
      expectEquals(Harmony::diatonicTriad(c, -1).root, 11);
    }

    beginTest("sevenths stack a fourth note");
    {
      const auto c = keyOf("C major");
      const auto five = Harmony::diatonicSeventh(c, 4);
      expectEquals(five.root, 7);
      expect(five.quality == Harmony::Quality::Dominant7, "V7 should be dominant");

      const auto one = Harmony::diatonicSeventh(c, 0);
      expect(one.quality == Harmony::Quality::Major7);
    }
  }

  void runDefaultProgressionTests() {
    beginTest("major keys get I V vi IV");
    {
      const auto c = keyOf("C major");
      const auto loop = Harmony::defaultDegreeLoop(c);
      expectEquals((int)loop.size(), 4);
      expectEquals(loop[0], 0);
      expectEquals(loop[1], 4);
      expectEquals(loop[2], 5);
      expectEquals(loop[3], 3);

      const auto prog = Harmony::defaultProgression(c);
      expectEquals((int)prog.size(), 4);
      expectEquals(prog[0].root, 0); // C
      expectEquals(prog[1].root, 7); // G
      expectEquals(prog[2].root, 9); // Am
      expectEquals(prog[3].root, 5); // F
      expect(prog[2].quality == Harmony::Quality::Minor);
    }

    beginTest("minor keys get i VI III VII, and never a minor v");
    {
      // I V vi IV over a minor tonic gives a minor v, which is weak and is not
      // what anybody means by "the four chords".
      const auto d = keyOf("D minor");
      const auto prog = Harmony::defaultProgression(d);
      expectEquals((int)prog.size(), 4);
      expectEquals(prog[0].root, 2);  // Dm
      expectEquals(prog[1].root, 10); // Bb
      expectEquals(prog[2].root, 5);  // F
      expectEquals(prog[3].root, 0);  // C

      expect(prog[0].quality == Harmony::Quality::Minor);
      expect(prog[1].quality == Harmony::Quality::Major);
      expect(prog[2].quality == Harmony::Quality::Major);
      expect(prog[3].quality == Harmony::Quality::Major);

      for (const auto &chord : prog)
        expect(!(chord.root == 9 && chord.quality == Harmony::Quality::Minor),
               "a minor v turned up after all");
    }

    beginTest("minorish is decided by the third, not by a list of modes");
    {
      expect(Harmony::isMinorish(MusicalKey::Mode::Minor));
      expect(Harmony::isMinorish(MusicalKey::Mode::Aeolian));
      expect(Harmony::isMinorish(MusicalKey::Mode::Dorian));
      expect(Harmony::isMinorish(MusicalKey::Mode::Phrygian));
      expect(Harmony::isMinorish(MusicalKey::Mode::Locrian));

      expect(!Harmony::isMinorish(MusicalKey::Mode::Major));
      expect(!Harmony::isMinorish(MusicalKey::Mode::Ionian));
      expect(!Harmony::isMinorish(MusicalKey::Mode::Lydian));
      expect(!Harmony::isMinorish(MusicalKey::Mode::Mixolydian));
    }

    beginTest("an invalid key still yields something playable");
    {
      MusicalKey::Key none;
      const auto prog = Harmony::defaultProgression(none);
      expectEquals((int)prog.size(), 4);
    }
  }

  void runBeatMappingTests() {
    beginTest("four chords over sixteen beats is four beats each");
    {
      for (int beat = 0; beat < 16; ++beat)
        expectEquals(Harmony::chordIndexForBeat(beat, 16, 4), beat / 4,
                     "beat " + juce::String(beat));
    }

    beginTest("four chords over eight beats is two beats each");
    {
      for (int beat = 0; beat < 8; ++beat)
        expectEquals(Harmony::chordIndexForBeat(beat, 8, 4), beat / 2,
                     "beat " + juce::String(beat));
    }

    beginTest("a progression that does not divide the interval still fills it");
    {
      // Three chords over eight beats: 3, 3, 2 rather than a clipped last one.
      const int expected[8] = {0, 0, 0, 1, 1, 1, 2, 2};
      for (int beat = 0; beat < 8; ++beat)
        expectEquals(Harmony::chordIndexForBeat(beat, 8, 3), expected[beat],
                     "beat " + juce::String(beat));
    }

    beginTest("every interval starts on the first chord");
    {
      // The property that keeps the band from drifting against a listener whose
      // interval phase is its own.
      for (int bpi = 1; bpi <= 32; ++bpi)
        for (int chords = 1; chords <= 8; ++chords)
          expectEquals(Harmony::chordIndexForBeat(0, bpi, chords), 0,
                       "bpi " + juce::String(bpi) + " chords " +
                           juce::String(chords));
    }

    beginTest("the index never leaves the progression");
    {
      for (int bpi = 1; bpi <= 24; ++bpi)
        for (int chords = 1; chords <= 8; ++chords)
          for (int beat = -bpi; beat < 2 * bpi; ++beat) {
            const int idx = Harmony::chordIndexForBeat(beat, bpi, chords);
            if (idx < 0 || idx >= chords) {
              expect(false, "out of range: bpi " + juce::String(bpi) +
                                " chords " + juce::String(chords) + " beat " +
                                juce::String(beat));
              return;
            }
          }
      expect(true);
    }

    beginTest("it repeats every interval, and survives nonsense");
    {
      for (int beat = 0; beat < 16; ++beat)
        expectEquals(Harmony::chordIndexForBeat(beat, 16, 4),
                     Harmony::chordIndexForBeat(beat + 16, 16, 4));

      expectEquals(Harmony::chordIndexForBeat(3, 0, 4), 0);
      expectEquals(Harmony::chordIndexForBeat(3, 16, 0), 0);
    }

    beginTest("rotation displaces the changes without losing a chord");
    {
      // At rotation 0 the changes fall evenly; rotating moves them off the beat
      // while every chord still gets its turn.
      const int bpi = 16, chords = 4;
      for (int rot = 0; rot < bpi; ++rot) {
        std::set<int> seen;
        for (int beat = 0; beat < bpi; ++beat)
          seen.insert(Harmony::chordIndexForBeat(beat, bpi, chords, rot));
        expectEquals((int)seen.size(), chords,
                     "rotation " + juce::String(rot) + " lost a chord");
      }
    }
  }
};

static HarmonyTests harmonyTests;
