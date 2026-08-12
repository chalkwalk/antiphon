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
