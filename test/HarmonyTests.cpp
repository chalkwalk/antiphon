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
    runChordNameTests();
    runBeatMappingTests();
    runLayoutTests();
    runVoiceLeadingTests();
    runNotationTests();
    runKeyInferenceTests();
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

    beginTest("a flat root takes a suffix");
    {
      // `Bb7` was REJECTED outright while `Bb` and `C#7` parsed. parseNote
      // refuses a `b` followed by a digit so that the `b5` of `C7b5` is not
      // eaten as an accidental -- but it applied that guard to the FIRST
      // character after the letter, which is the one position where a `b` can
      // only ever be a flat. The root came back as B, the leftover `b7` did not
      // parse as a suffix, and the whole chord was refused.
      //
      // Found by a chart that could not be written down: "| C | Db7 | C |".
      const struct { const char *name; int root; } kFlatRoots[] = {
          {"Bb7", 10}, {"Db7", 1},  {"Eb9", 3},
          {"Ab13", 8}, {"Gb6", 6},  {"Bbm7", 10},
      };
      for (const auto &c : kFlatRoots) {
        Harmony::Chord chord;
        expect(Harmony::parseChordName(c.name, chord),
               juce::String(c.name) + " was refused");
        expectEquals(chord.root, c.root, juce::String(c.name) + " root");
      }

      // The alteration this guard exists for still works, because a quality
      // always sits between the letter and the alteration.
      Harmony::Chord alt;
      expect(Harmony::parseChordName("C7b5", alt));
      expectEquals(alt.root, 0);
      expect(Harmony::parseChordName("F#m7b5", alt));
      expectEquals(alt.root, 6);
    }

    beginTest("a chart survives a round trip through the key it was written in");
    {
      // The property everything else rests on. Reading a chart against its own
      // key and resolving it straight back must be the identity -- if that does
      // not hold, no key CHANGE can be trusted either, and the failure would
      // show up as chords quietly altering when nothing was asked for.
      const char *charts[] = {
          "| C | Am | F | G |",       // plain diatonic
          "| Dm7 | G7 | Cmaj7 |",     // diatonic sevenths
          "| C | Bb | F |",           // a borrowed bVII
          "| C | Db7 | C |",          // a tritone substitution
          "| Am7/G | F |",            // a slash bass
          "| Csus4 | C |",            // a quality no mode gives
          "| C Am | F G |",           // two chords to a bar
      };
      const char *keys[] = {"C major", "A minor", "D dorian", "F# major",
                            "Bb minor"};

      for (const auto *keyName : keys) {
        const auto key = MusicalKey::parseName(keyName);
        expect(key.valid);
        for (const auto *text : charts) {
          Harmony::Chart original;
          expect(Harmony::parseChart(text, original),
                 juce::String(text) + " did not parse");

          const auto round =
              Harmony::resolve(Harmony::toRelative(original, key), key);

          expectEquals((int)round.size(), (int)original.size(),
                       juce::String(text) + " lost bars in " + keyName);
          for (size_t b = 0; b < original.size() && b < round.size(); ++b) {
            expectEquals((int)round[b].chords.size(),
                         (int)original[b].chords.size(),
                         juce::String(text) + " lost chords in " + keyName);
            for (size_t c = 0;
                 c < original[b].chords.size() && c < round[b].chords.size();
                 ++c)
              expect(round[b].chords[c] == original[b].chords[c],
                     juce::String(text) + " in " + keyName + " came back as " +
                         Harmony::chartText(round, false));
          }
        }
      }
    }

    beginTest("a key change re-derives what was delegated and moves the rest");
    {
      // The worked examples from DESIGN.md section 6.4, which are the whole
      // argument in table form.
      const struct {
        const char *from;
        const char *chart;
        const char *to;
        const char *expected;
        const char *why;
      } kCases[] = {
          {"C major", "| C | Am | F | G |", "A minor", "| Am | F | Dm | E |",
           "all delegated; V stays major by the minor-mode table"},
          {"C major", "| C | Bb | F |", "C minor", "| Cm | Bb | Fm |",
           "bVII was an override and survives, now spelled VII"},
          {"C major", "| C | G |", "D major", "| D | A |",
           "tonic move only, nothing re-derived"},
          // Spelled D#7 rather than Eb7: chartText spells the whole chart from
          // the key signature, and D major takes sharps. Notationally a bII
          // wants the flat whatever the key does. Same pitches, and the
          // spelling gap is its own roadmap item.
          {"C major", "| C | Db7 | C |", "D major", "| D | D#7 | D |",
           "a tritone substitution transposes with the tonic"},
          {"C major", "| Csus4 | C |", "C minor", "| Csus4 | Cm |",
           "sus is a quality no mode gives, so it is an override"},
      };

      for (const auto &c : kCases) {
        const auto from = MusicalKey::parseName(c.from);
        const auto to = MusicalKey::parseName(c.to);
        expect(from.valid && to.valid);

        Harmony::Chart original;
        expect(Harmony::parseChart(c.chart, original),
               juce::String(c.chart) + " did not parse");

        const auto moved = Harmony::resolve(Harmony::toRelative(original, from), to);
        const auto flat = MusicalKey::usesFlats(to.tonic, to.mode);

        expectEquals(Harmony::chartText(moved, flat), juce::String(c.expected),
                     juce::String(c.chart) + " from " + c.from + " to " + c.to +
                         " -- " + c.why);
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

  void runChordNameTests() {
    beginTest("chord names parse");
    {
      struct Case {
        const char *text;
        int root;
        Harmony::Quality quality;
      };
      const Case cases[] = {
          {"C", 0, Harmony::Quality::Major},
          {"Am", 9, Harmony::Quality::Minor},
          {"F#", 6, Harmony::Quality::Major},
          {"Bb", 10, Harmony::Quality::Major},
          {"G7", 7, Harmony::Quality::Dominant7},
          {"Cmaj7", 0, Harmony::Quality::Major7},
          {"Dm7", 2, Harmony::Quality::Minor7},
          {"Bm7b5", 11, Harmony::Quality::HalfDiminished7},
          {"Edim", 4, Harmony::Quality::Diminished},
          {"Caug", 0, Harmony::Quality::Augmented},
          {"Abmin", 8, Harmony::Quality::Minor},
      };

      for (const auto &c : cases) {
        Harmony::Chord out;
        if (!Harmony::parseChordName(c.text, out)) {
          expect(false, juce::String("failed to parse ") + c.text);
          continue;
        }
        expectEquals(out.root, c.root, juce::String(c.text) + " root");
        expect(out.quality == c.quality, juce::String(c.text) + " quality");
      }
    }

    beginTest("the vocabulary players actually write");
    {
      // Tones, because the quality enum has no name for most of these and the
      // tones are what the band plays. Ninths and above are not folded into the
      // octave: a ninth is 14, so a voicing puts it above the seventh.
      struct Case {
        const char *text;
        const char *tones;
      };
      const Case cases[] = {
          {"Csus4", "0,5,7"},        {"Csus2", "0,2,7"},
          {"Csus", "0,5,7"},         {"C7sus4", "0,5,7,10"},
          {"C6", "0,4,7,9"},         {"Am6", "0,3,7,9"},
          {"C9", "0,4,7,10,14"},     {"Cmaj9", "0,4,7,11,14"},
          {"Cm9", "0,3,7,10,14"},    {"C11", "0,4,7,10,17"},
          {"C13", "0,4,7,10,21"},    {"Cadd9", "0,4,7,14"},
          {"C7b9", "0,4,7,10,13"},   {"C7#9", "0,4,7,10,15"},
          {"C7#11", "0,4,7,10,18"},  {"C7b13", "0,4,7,10,20"},
          {"Cdim7", "0,3,6,9"},      {"Co7", "0,3,6,9"},
          {"C7b5", "0,4,6,10"},      {"C7#5", "0,4,8,10"},
          {"F#m7(b5)", "0,3,6,10"},  {"C-7", "0,3,7,10"},
          {"CM7", "0,4,7,11"},       {"Cmi7", "0,3,7,10"},
      };

      for (const auto &c : cases) {
        Harmony::Chord out;
        if (!Harmony::parseChordName(c.text, out)) {
          expect(false, juce::String("failed to parse ") + c.text);
          continue;
        }
        expectEquals(toneList(out), juce::String(c.tones),
                     juce::String(c.text) + " tones");
      }
    }

    beginTest("a slash chord keeps the note underneath it");
    {
      Harmony::Chord out;
      expect(Harmony::parseChordName("Am7/G", out));
      expectEquals(out.root, 9);
      expectEquals(out.bass, 7);
      expect(out.quality == Harmony::Quality::Minor7);

      // A slash naming the root is not an inversion, so it is not recorded.
      expect(Harmony::parseChordName("C/C", out));
      expectEquals(out.bass, -1);

      // The bass has to be a note, or the whole symbol is refused.
      expect(!Harmony::parseChordName("Am7/H", out));
      expect(!Harmony::parseChordName("Am7/", out));
    }

    beginTest("a chord can be written back out");
    {
      // Round trip, and canonical: the spellings on the right are what comes
      // back, so "CM7" normalises to "Cmaj7" and "F#m7(b5)" loses its brackets.
      struct Case {
        const char *in;
        const char *out;
        bool flat;
      };
      const Case cases[] = {
          {"C", "C", false},           {"Am", "Am", false},
          {"G7", "G7", false},         {"Cmaj7", "Cmaj7", false},
          {"CM7", "Cmaj7", false},     {"Dm7", "Dm7", false},
          {"Bm7b5", "Bm7b5", false},   {"F#m7(b5)", "F#m7b5", false},
          {"Edim", "Edim", false},     {"Eo", "Edim", false},
          {"Caug", "Caug", false},     {"C+", "Caug", false},
          {"Csus4", "Csus4", false},   {"Csus", "Csus4", false},
          {"Csus2", "Csus2", false},   {"C6", "C6", false},
          {"Am6", "Am6", false},       {"C9", "C9", false},
          {"Cmaj9", "Cmaj9", false},   {"C13", "C13", false},
          {"Cadd9", "Cadd9", false},   {"C7b9", "C7b9", false},
          {"Cdim7", "Cdim7", false},   {"Am7/G", "Am7/G", false},
          {"C7sus4", "C7sus4", false}, {"Abmin", "Abm", true},
          {"Bbmaj7", "Bbmaj7", true},  {"Dm7/Bb", "Dm7/Bb", true},
      };

      for (const auto &c : cases) {
        Harmony::Chord chord;
        if (!Harmony::parseChordName(c.in, chord)) {
          expect(false, juce::String("failed to parse ") + c.in);
          continue;
        }
        const auto written = Harmony::chordName(chord, c.flat);
        expectEquals(written, juce::String(c.out),
                     juce::String(c.in) + " written back");

        // And the name it produces must parse to the same chord.
        Harmony::Chord again;
        expect(Harmony::parseChordName(written, again),
               "could not re-read " + written);
        expect(again == chord, written + " did not survive the round trip");
      }
    }

    beginTest("a root is spelled to match the key signature");
    {
      Harmony::Chord bFlat;
      expect(Harmony::parseChordName("Bb", bFlat));
      expectEquals(Harmony::chordName(bFlat, true), juce::String("Bb"));
      expectEquals(Harmony::chordName(bFlat, false), juce::String("A#"));
    }

    beginTest("nonsense is refused rather than guessed at");
    {
      Harmony::Chord out;
      for (const char *bad : {"", "H", "hello", "Cxyz", "7", "#", "Ammm",
                              "Cmaj7x", "Csus3", "C(", "Cb5b", "and"})
        expect(!Harmony::parseChordName(bad, out),
               juce::String("accepted ") + bad);
    }

    beginTest("a Jamtaba-style progression parses");
    {
      Harmony::Progression p;
      expect(Harmony::parseProgression("| Am | F | C | G |", p));
      expectEquals((int)p.size(), 4);
      expectEquals(p[0].root, 9);
      expect(p[0].quality == Harmony::Quality::Minor);
      expectEquals(p[3].root, 7);

      // Two chords in one measure.
      Harmony::Progression q;
      expect(Harmony::parseProgression("| Am F | C G |", q));
      expectEquals((int)q.size(), 4);
    }

    beginTest("what looks like a chart is what parses as one");
    {
      // The property, not the implementation: these were two parsers once, and
      // a line could be coloured green in the chat pane and then rejected by
      // the band. Whatever the rule is, both answers have to agree.
      for (const char *line :
           {"| Am | F | C | G |", "|C |Fmaj7 |G7 |Am7 |Am7/G |F#m7(b5) |Fmaj9",
            "| Dm7 | C# Csus |", "|C|F||G|F", "| C | and then something else",
            "I AM TIRED OF THIS", "no bars here", "| Am | not-a-chord |",
            "| Am |", "|C", "", "|", "|| ||", "Am | F |"}) {
        Harmony::Progression p;
        expect(Harmony::looksLikeChart(line) ==
                   Harmony::parseProgression(line, p),
               juce::String("the two disagree about: ") + line);
      }
    }

    beginTest("prose is not a chord progression");
    {
      // Jamtaba's own parser reads "I AM TIRED ..." as chords, because it
      // treats I and l as separators. Refusing to guess is the whole point.
      Harmony::Progression p;
      expect(!Harmony::parseProgression("I AM TIRED OF THIS", p));
      expect(!Harmony::parseProgression("no bars here", p));
      expect(!Harmony::parseProgression("| Am | not-a-chord |", p),
             "one bad measure should reject the line");
      expect(!Harmony::parseProgression("| Am |", p),
             "one chord is not a progression");
      expect(!Harmony::parseProgression("", p));
    }
  }

  void runLayoutTests() {
    beginTest("one chord per bar lays out exactly as it did before bars");
    {
      // The compatibility claim, and the reason bars could be introduced at
      // all: a flat progression is a chart of one-chord bars, and it must land
      // on precisely the beats it used to. If this ever goes red, every
      // existing recording of the band changed.
      for (int bpi = 1; bpi <= 16; ++bpi) {
        for (int n = 1; n <= 8; ++n) {
          Harmony::Progression p;
          for (int i = 0; i < n; ++i)
            p.push_back(Harmony::chordOn(i, Harmony::Quality::Major));

          const auto layout = Harmony::layoutChart(Harmony::chartOf(p), bpi);
          for (int beat = 0; beat < bpi; ++beat) {
            const int want = Harmony::chordIndexForBeat(beat, bpi, n);
            for (int half = 0; half < Harmony::kStepsPerBeat; ++half) {
              const int step = beat * Harmony::kStepsPerBeat + half;
              expectEquals(layout.stepToChord[(size_t)step], want,
                           "bpi " + juce::String(bpi) + ", " +
                               juce::String(n) + " chords, beat " +
                               juce::String(beat));
            }
          }
        }
      }
    }

    beginTest("a bar holding two chords gives each of them half the bar");
    {
      // The whole point. Read as a flat list of three chords over eight beats
      // this is 3+3+2; read as two bars it is 4+2+2, which is what was written.
      Harmony::Chart chart;
      expect(Harmony::parseChart("| Dm7 | C# Csus |", chart));
      expectEquals((int)chart.size(), 2, "bars");
      expectEquals((int)chart[0].chords.size(), 1);
      expectEquals((int)chart[1].chords.size(), 2);

      const auto layout = Harmony::layoutChart(chart, 8);
      const int wantPerBeat[8] = {0, 0, 0, 0, 1, 1, 2, 2};
      for (int beat = 0; beat < 8; ++beat)
        expectEquals(layout.stepToChord[(size_t)(beat * 2)],
                     wantPerBeat[beat], "beat " + juce::String(beat));

      expectEquals(Harmony::chordAtStep(layout, 0).root, 2, "Dm7");
      expectEquals(Harmony::chordAtStep(layout, 8).root, 1, "C#");
      expectEquals(Harmony::chordAtStep(layout, 12).root, 0, "Csus");
    }

    beginTest("a chord change is where the chord changes");
    {
      Harmony::Chart chart;
      expect(Harmony::parseChart("| Dm7 | C# Csus |", chart));
      const auto layout = Harmony::layoutChart(chart, 8);

      expect(Harmony::changesAtStep(layout, 0), "an interval opens on a chord");

      int changes = 0;
      for (int step = 0; step < layout.steps(); ++step)
        if (Harmony::changesAtStep(layout, step))
          ++changes;
      expectEquals(changes, 3, "one change per chord that sounds");

      expect(Harmony::changesAtStep(layout, 8), "the second bar");
      expect(Harmony::changesAtStep(layout, 12), "inside the second bar");
      expect(!Harmony::changesAtStep(layout, 9), "mid-chord");
    }

    beginTest("a bar shorter than its chords keeps the ones that fit");
    {
      // Two bars over two beats is a beat each, and eighths is as fine as the
      // grid goes, so a bar of three chords sounds two of them.
      Harmony::Chart chart;
      expect(Harmony::parseChart("| C G Am | F |", chart));
      const auto layout = Harmony::layoutChart(chart, 2);

      expectEquals(layout.steps(), 4);
      // The first bar owns one beat, which is two eighths.
      expectEquals(layout.stepToChord[0], 0, "C");
      expectEquals(layout.stepToChord[1], 1, "G, an eighth later");
      expectEquals(layout.stepToChord[2], 3, "F, in the second bar");

      // Am is still in the chart and still has an index; it simply has no time.
      expectEquals((int)layout.chords.size(), 4);
    }

    beginTest("a layout survives being asked for nonsense");
    {
      const auto empty = Harmony::layoutChart({}, 8);
      expect(empty.empty());
      expect(!Harmony::changesAtStep(empty, 0));
      expectEquals(Harmony::chordAtStep(empty, 3).root, 0, "the fallback chord");

      Harmony::Chart chart;
      expect(Harmony::parseChart("| C | F |", chart));
      const auto zero = Harmony::layoutChart(chart, 0);
      expect(zero.empty(), "no interval, no layout");

      // Steps outside the interval wrap rather than reading off the end.
      const auto layout = Harmony::layoutChart(chart, 4);
      expectEquals(Harmony::chordAtStep(layout, 100).root,
                   Harmony::chordAtStep(layout, 100 % layout.steps()).root);
      expectEquals(Harmony::chordAtStep(layout, -1).root,
                   Harmony::chordAtStep(layout, layout.steps() - 1).root);
    }

    beginTest("a chart keeps its bars through a parse");
    {
      Harmony::Chart chart;
      expect(Harmony::parseChart("| Am F | C G |", chart));
      expectEquals((int)chart.size(), 2);
      expectEquals((int)Harmony::flatten(chart).size(), 4);

      // An empty measure holds no time, so it is not a bar. "|C|F||G|F" is in
      // Jamtaba's test suite.
      Harmony::Chart withGap;
      expect(Harmony::parseChart("|C|F||G|F", withGap));
      expectEquals((int)withGap.size(), 4);
    }
  }

  void runVoiceLeadingTests() {
    auto chordsOf = [](const char *text) {
      Harmony::Progression p;
      const bool ok = Harmony::parseProgression(text, p);
      jassert(ok);
      juce::ignoreUnused(ok);
      return p;
    };

    auto totalMovement = [](const std::vector<Harmony::Voicing> &v) {
      // Around the loop, which is the number that matters: the last chord's
      // move back to the first is heard every time the interval comes round.
      int total = 0;
      for (size_t i = 0; i < v.size(); ++i)
        total += Harmony::voicingDistance(v[i], v[(i + 1) % v.size()]);
      return total;
    };

    beginTest("the voicings are these voicings");
    {
      // Exact, because the layer is integer arithmetic and because every
      // looser assertion tried here passed under a deliberately broken
      // implementation. If a change to the candidates or the search is
      // intended, these lines are what to update -- and reading them is how
      // you check the intent.
      struct Case {
        const char *text;
        const char *voicings;
        int movement;
      };
      const Case cases[] = {
          // C-E-G, C-E-A, C-F-A, B-D-G: two voices held into Am, the top
          // moving a tone; then the classic step down onto G.
          {"| C | Am | F | G |",
           "[60 64 67] [60 64 69] [60 65 69] [59 62 67]", 12},
          // Chromatic and unrelated by key, where root position costs 36.
          {"| C | Eb | Ab | G |",
           "[60 64 67] [58 63 67] [60 63 68] [59 62 67]", 12},
          // A ii-V-I, where the sevenths resolve down by a semitone.
          {"| Dm7 | G7 | Cmaj7 |",
           "[60 62 65 69] [59 62 65 67] [59 60 64 67]", 12},
      };

      for (const auto &c : cases) {
        const auto v = Harmony::voiceLead(chordsOf(c.text));
        juce::StringArray notes;
        for (const auto &one : v) {
          juce::StringArray x;
          for (int note : one)
            x.add(juce::String(note));
          notes.add("[" + x.joinIntoString(" ") + "]");
        }
        expectEquals(notes.joinIntoString(" "), juce::String(c.voicings),
                     c.text);
        expectEquals(totalMovement(v), c.movement,
                     juce::String(c.text) + " movement around the loop");
      }
    }

    beginTest("common tones do not move");
    {
      // C to Am shares C and E. Voiced in root position all three voices move,
      // which is the sound of a machine reading a list rather than a player.
      const auto v = Harmony::voiceLead(chordsOf("| C | Am |"));
      expectEquals((int)v.size(), 2);

      std::set<int> first(v[0].begin(), v[0].end());
      int held = 0;
      for (int n : v[1])
        held += first.count(n) > 0 ? 1 : 0;
      expectEquals(held, 2, "C and E should have stayed exactly where they were");

      expectEquals(Harmony::voicingDistance(v[0], v[1]), 2,
                   "one voice moves a tone, and that is the whole move");
    }

    beginTest("a chart that comes back to its first chord comes back to its voicing");
    {
      // What costing the turnaround buys, and the property a listener hears:
      // the loop must not arrive home in a different inversion from the one it
      // left in, or every time round has a seam in it.
      for (const char *text : {"| C | F | G | C |", "| E | A | B | E |",
                               "| Am | Dm | E7 | Am |"}) {
        const auto v = Harmony::voiceLead(chordsOf(text));
        expect(v.size() >= 2, text);
        expect(v.front() == v.back(),
               juce::String(text) +
                   ": came home to a different voicing from the one it left");
        expectEquals(Harmony::voicingDistance(v.front(), v.back()), 0);
      }
    }

    beginTest("it beats what it replaced");
    {
      // Root position anchored at C4 is what renderKeys did before this
      // existed, so it is the number worth beating.
      for (const char *text : {"| C | Am | F | G |", "| C | Eb | Ab | G |",
                               "| Cmaj7 | Am7 | Dm7 | G7 |"}) {
        const auto chords = chordsOf(text);
        std::vector<Harmony::Voicing> rootPosition;
        for (const auto &c : chords) {
          Harmony::Voicing v;
          for (int i = 0; i < c.toneCount; ++i)
            v.push_back(60 + c.root + c.tones[(size_t)i]);
          rootPosition.push_back(v);
        }

        const auto best = Harmony::voiceLead(chords);
        expect(totalMovement(best) < totalMovement(rootPosition),
               juce::String(text) + ": voice leading cost " +
                   juce::String(totalMovement(best)) +
                   " against root position's " +
                   juce::String(totalMovement(rootPosition)));
      }
    }

    beginTest("voicings stay in the register");
    {
      for (const char *text :
           {"| C | Am | F | G |", "| Bmaj7 | Ebm7 | F#13 | Bmaj7 |",
            "| Csus2 | Gsus4 |", "| Cdim7 | F#dim7 |"}) {
        for (const auto &v : Harmony::voiceLead(chordsOf(text))) {
          expect(!v.empty(), text);
          for (int n : v)
            expect(n >= Harmony::kVoiceLow && n <= Harmony::kVoiceHigh,
                   juce::String(text) + ": note " + juce::String(n) +
                       " left the register");
          for (size_t i = 1; i < v.size(); ++i)
            expect(v[i] > v[i - 1], "a voicing must be ascending");
        }
      }
    }

    beginTest("voicing is deterministic and copes with the degenerate cases");
    {
      const auto a = Harmony::voiceLead(chordsOf("| Dm7 | G7 | Cmaj7 |"));
      const auto b = Harmony::voiceLead(chordsOf("| Dm7 | G7 | Cmaj7 |"));
      expect(a == b, "two runs gave different voicings");

      expect(Harmony::voiceLead({}).empty());

      // One chord is a loop of one: it has nothing to lead to, and must still
      // come back voiced.
      const auto one = Harmony::voiceLead({Harmony::chordOn(0, Harmony::Quality::Major)});
      expectEquals((int)one.size(), 1);
      expectEquals((int)one[0].size(), 3);
    }

    beginTest("a distance is what it costs to move");
    {
      expectEquals(Harmony::voicingDistance({60, 64, 67}, {60, 64, 67}), 0);
      expectEquals(Harmony::voicingDistance({60, 64, 67}, {60, 65, 69}), 3);
      // A fourth voice has to come from somewhere, and the nearest note it
      // could have moved from is what it costs.
      expectEquals(Harmony::voicingDistance({60, 64, 67}, {60, 64, 67, 70}), 3);
      expectEquals(Harmony::voicingDistance({}, {60}), 0);
    }
  }

  void runNotationTests() {
    beginTest("a chord names its degree against a key");
    {
      struct Case {
        const char *key;
        const char *chord;
        const char *roman;
      };
      const Case cases[] = {
          {"C major", "C", "I"},        {"C major", "Dm7", "ii7"},
          {"C major", "Em", "iii"},     {"C major", "F", "IV"},
          {"C major", "G7", "V7"},      {"C major", "Am", "vi"},
          {"C major", "Bm7b5", "viim7b5"}, {"C major", "Cmaj7", "Imaj7"},
          // Not in the key: named by where the root sits, never by what it
          // might be doing.
          {"C major", "E7", "III7"},    {"C major", "Ab", "bVI"},
          {"C major", "Eb", "bIII"},    {"C major", "Bb", "bVII"},
          {"C major", "F#dim", "#ivo"}, {"C major", "Db", "bII"},
          // Minor keys read from their own scale, so VI is major and v minor.
          {"D minor", "Dm", "i"},       {"D minor", "Bb", "VI"},
          {"D minor", "Gm", "iv"},      {"D minor", "C", "VII"},
          {"D minor", "A7", "V7"},      {"D minor", "Am", "v"},
          // Modes name their own degrees: Dorian's IV is major.
          {"D Dorian", "G", "IV"},      {"D Dorian", "Dm7", "i7"},
          // A slash keeps the note underneath it.
          {"C major", "Am7/G", "vi7/G"},
      };

      for (const auto &c : cases) {
        Harmony::Chord chord;
        expect(Harmony::parseChordName(c.chord, chord), c.chord);
        expectEquals(Harmony::romanName(chord, keyOf(c.key)),
                     juce::String(c.roman),
                     juce::String(c.chord) + " in " + c.key);
      }
    }

    beginTest("a chart writes itself out in both notations");
    {
      Harmony::Chart chart;
      expect(Harmony::parseChart("| Dm7 | C# Csus |", chart));
      expectEquals(Harmony::chartText(chart, false),
                   juce::String("| Dm7 | C# Csus4 |"));

      Harmony::Chart four;
      expect(Harmony::parseChart("| Am | F | C | G |", four));
      expectEquals(Harmony::chartText(four, false),
                   juce::String("| Am | F | C | G |"));
      expectEquals(Harmony::romanChartText(four, keyOf("C major")),
                   juce::String("| vi | IV | I | V |"));
      expectEquals(Harmony::romanChartText(four, keyOf("A minor")),
                   juce::String("| i | VI | III | VII |"));

      // Bars survive the round trip, which is the whole point of having them.
      Harmony::Chart again;
      expect(Harmony::parseChart(Harmony::chartText(chart, false), again));
      expectEquals((int)again.size(), 2);
      expectEquals((int)again[1].chords.size(), 2);

      expectEquals(Harmony::chartText({}, false), juce::String());
      MusicalKey::Key none;
      expectEquals(Harmony::romanChartText(four, none), juce::String());
    }

    beginTest("a chord is spelled by where it sits in the key");
    {
      // One flag for a whole chart cannot be right: D major takes sharps, and
      // its flattened second is still Eb. Both facts at once are what the
      // per-chord spelling is for.
      const auto d = keyOf("D major");
      struct Case {
        const char *written;
        const char *spelled;
      };
      const Case inD[] = {
          {"Eb7", "Eb7"},   // bII: a lowered degree keeps its flat
          {"C", "C"},       // bVII: the scale's C# lowered is C, not B#
          {"G#dim", "G#dim"}, // #IV: the tritone is everybody's sharp
          {"F#m", "F#m"},   // diatonic: spelled as the key spells it
          {"Bm/A", "Bm/A"}, // a bass note is spelled by the same rule
      };
      for (const auto &c : inD) {
        Harmony::Chord chord;
        expect(Harmony::parseChordName(c.written, chord), juce::String(c.written));
        expectEquals(Harmony::chordName(chord, d), juce::String(c.spelled));
      }

      // A flat key gets the mirror image: its raised fourth is a natural, and
      // its lowered seventh keeps the flat the signature already implies.
      const auto eb = keyOf("Eb major");
      const Case inEb[] = {{"A7", "A7"}, {"Db", "Db"}, {"Bbm7", "Bbm7"}};
      for (const auto &c : inEb) {
        Harmony::Chord chord;
        expect(Harmony::parseChordName(c.written, chord), juce::String(c.written));
        expectEquals(Harmony::chordName(chord, eb), juce::String(c.spelled));
      }

      // The worked example from DESIGN.md section 6.4, which came out as
      // "D#7" while a chart was spelled from one flag.
      Harmony::Chart chart;
      expect(Harmony::parseChart("| C | Db7 | C |", chart));
      const auto moved =
          Harmony::resolve(Harmony::toRelative(chart, keyOf("C major")), d);
      expectEquals(Harmony::chartText(moved, d), juce::String("| D | Eb7 | D |"));

      // No key, no better answer than the flag. It does NOT come back as it
      // was written: a Chord holds pitch classes and has never remembered how
      // somebody typed it, so a spelling with nothing to spell against is the
      // one thing this cannot recover.
      MusicalKey::Key unknown;
      expectEquals(Harmony::chartText(chart, unknown),
                   juce::String("| C | C#7 | C |"));
    }

    beginTest("degrees resolve against the key");
    {
      struct Case {
        const char *key;
        const char *degrees;
        const char *absolute;
      };
      const Case cases[] = {
          // Roman case carries the quality.
          {"C major", "| I | vi | IV | V |", "| C | Am | F | G |"},
          {"D minor", "| i | VI | III VII |", "| Dm | Bb | F C |"},
          {"C major", "| ii7 | V7 | Imaj7 |", "| Dm7 | G7 | Cmaj7 |"},
          {"C major", "| I | vii0 |", "| C | Bdim |"},
          {"C major", "| I | viio |", "| C | Bdim |"},
          // Arabic degrees take what the key gives them.
          {"C major", "| 1 | 4 | 5 |", "| C | F | G |"},
          {"A minor", "| 1 | 4 | 5 |", "| Am | Dm | Em |"},
          // An altered degree is the borrowed major chord.
          {"C major", "| 1 | b6 | b7 |", "| C | Ab | Bb |"},
          {"C major", "| I | bVI | bVII |", "| C | Ab | Bb |"},
          // A suffix still applies on top.
          {"C major", "| 1 | 5sus4 |", "| C | Gsus4 |"},
      };

      for (const auto &c : cases) {
        Harmony::Chart chart;
        if (!Harmony::parseDegreeChart(c.degrees, keyOf(c.key), chart)) {
          expect(false, juce::String("failed to read ") + c.degrees);
          continue;
        }
        const bool flat = juce::String(c.absolute).contains("b ") ||
                          juce::String(c.absolute).contains("b |");
        expectEquals(Harmony::chartText(chart, flat),
                     juce::String(c.absolute),
                     juce::String(c.degrees) + " in " + c.key);
      }
    }

    beginTest("degrees are refused rather than guessed at");
    {
      Harmony::Chart chart;
      const auto c = keyOf("C major");

      // Prose can never become a chart, which is why the bar lines are
      // required here exactly as they are for chord names.
      expect(!Harmony::parseDegreeChart("2 5 1", c, chart));
      expect(!Harmony::parseDegreeChart("I IV V", c, chart));
      expect(!Harmony::parseDegreeChart("| VIII | II |", c, chart));
      expect(!Harmony::parseDegreeChart("| 8 | 2 |", c, chart));
      expect(!Harmony::parseDegreeChart("| I | hello |", c, chart));
      expect(!Harmony::parseDegreeChart("| I |", c, chart), "one chord is not a chart");

      // Without a key there is nothing to resolve against.
      MusicalKey::Key none;
      expect(!Harmony::parseDegreeChart("| I | IV |", none, chart));
    }
  }

  void runKeyInferenceTests() {
    auto chordsOf = [](const char *text) {
      Harmony::Progression p;
      const bool ok = Harmony::parseProgression(text, p);
      jassert(ok);
      juce::ignoreUnused(ok);
      return p;
    };

    beginTest("a chart that names its key is read correctly");
    {
      // This table is the specification, and the confidence threshold is
      // calibrated against it rather than picked. Every entry is a progression
      // whose key either is or is not in doubt, and saying which is the whole
      // job -- a wrong suggestion is worse than none.
      struct Case {
        const char *text;
        const char *key;
        bool confident;
      };
      const Case cases[] = {
          // A ii-V-I says it outright.
          {"| Dm7 | G7 | Cmaj7 |", "C major", true},
          // The dominant's major third is what makes this minor and not its
          // relative major: E7's G# is in neither scale, but the E chord is
          // the fifth degree of A and nothing in C.
          {"| Am | Dm | E7 | Am |", "A minor", true},
          {"| Am | G | F | E7 |", "A minor", true},
          {"| C | F | G | C |", "C major", true},
          {"| C | Am | F | G |", "C major", true},
          {"| D | G | A | D |", "D major", true},
          {"| Bb | Eb | F | Bb |", "Bb major", true},

          // The same four chords, starting somewhere else. Nothing here says
          // whether home is C or its relative A minor, and the honest answer
          // is to keep quiet rather than guess and be wrong half the time.
          {"| Am | F | C | G |", "A minor", false},
          // F natural against a G tonic is Mixolydian, and the evidence for it
          // exactly cancels how much likelier plain major is.
          {"| G | F | C | G |", "G major", false},
          // Chromatic: three major triads a third apart belong to no one key.
          {"| C | E | Ab | C |", "C major", false},
      };

      for (const auto &c : cases) {
        const auto guess = Harmony::inferKey(chordsOf(c.text));
        expectEquals(MusicalKey::displayName(guess.key), juce::String(c.key),
                     c.text);
        expect(guess.confident == c.confident,
               juce::String(c.text) + ": margin " +
                   juce::String(guess.margin, 2) + ", expected " +
                   (c.confident ? "confidence" : "no confidence"));
      }
    }

    beginTest("a guessed key is spelled the way the key signature spells it");
    {
      const auto guess = Harmony::inferKey(chordsOf("| Bb | Eb | F | Bb |"));
      expect(guess.key.flat, "Bb major should not be spelled A#");
      expectEquals(MusicalKey::scaleNotes(guess.key),
                   juce::String("Bb C D Eb F G A"));
    }

    beginTest("inferring a key from nothing says nothing");
    {
      const auto none = Harmony::inferKey({});
      expect(!none.confident);
      expect(!none.key.valid, "an empty chart has no key");

      // One chord is not a progression, but it must not crash or claim
      // certainty either.
      const auto one =
          Harmony::inferKey({Harmony::chordOn(0, Harmony::Quality::Major)});
      expect(one.key.valid);
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
