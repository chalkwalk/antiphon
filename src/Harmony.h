#pragma once

#include "MusicalKey.h"
#include <array>
#include <cstdint>
#include <vector>

// The chords the band plays over.
//
// A chord here is an ABSOLUTE root pitch class plus an explicit list of chord
// tones, not a scale degree. That is deliberate and it is the whole reason this
// file exists rather than the bots reading degrees straight out of MusicalKey.
//
// A degree can only ever name a chord that is diatonic to the current mode. The
// interesting harmony is not: a tritone substitution has a root a tritone away
// from the degree it replaces, an altered dominant has tones that are in no
// mode of the key, and a borrowed chord is by definition from somewhere else.
// Representing chords as root-plus-tones means all of those are already
// expressible, and adding them later is new code in one function rather than a
// new model everywhere.
//
// See `realise` for where a substitution pass would go.
//
// JUCE-light -- only MusicalKey's types -- so the whole thing is testable in the
// headless target.

namespace Harmony {

enum class Quality {
  Major,
  Minor,
  Diminished,
  Augmented,
  Dominant7,
  Major7,
  Minor7,
  HalfDiminished7,
  Diminished7,
  Sus2,
  Sus4,
  Major6,
  Minor6
};

inline constexpr int kMaxChordTones = 5;

struct Chord {
  int root = 0; // pitch class, 0-11, absolute

  // A label, never the truth. Shapes with no name in this enum -- a ninth, a
  // thirteenth, an altered dominant -- carry the closest one and are still
  // exact in their tones, which is what everything actually reads.
  Quality quality = Quality::Major;

  // Semitones above the root, and NOT reduced into an octave: a ninth is 14
  // rather than 2, because a chord that names a ninth wants it voiced above the
  // seventh. Callers that only care about pitch class take it modulo 12.
  //
  // Derived from the quality today, but stored rather than recomputed so an
  // alteration can move or add one tone without needing a quality to name the
  // result.
  std::array<std::int8_t, kMaxChordTones> tones{{0, 4, 7, 0, 0}};
  int toneCount = 3;

  // The pitch class under the chord when it is not the root -- the G of Am7/G.
  // -1 means the root is the bass, which is the ordinary case.
  int bass = -1;

  bool operator==(const Chord &o) const {
    if (root != o.root || toneCount != o.toneCount || bass != o.bass)
      return false;
    for (int i = 0; i < toneCount; ++i)
      if (tones[(size_t)i] != o.tones[(size_t)i])
        return false;
    return true;
  }
};

using Progression = std::vector<Chord>;

// A bar of the chart, holding one chord or several.
//
// Bars exist because the notation carries timing that a flat list throws away:
// "| Dm7 | C# Csus |" says the second bar holds two chords, so Dm7 lasts twice
// as long as either of them. Read as a flat list of three it becomes 3+3+2
// beats of an eight-beat interval, which is not what anybody wrote.
struct Bar {
  std::vector<Chord> chords;
};

using Chart = std::vector<Bar>;

// One chord per bar, which is what a flat progression means.
Chart chartOf(const Progression &progression);

// A chart as it relates to a KEY, rather than as absolute pitches. The form a
// chart is kept in so a key change can move it (`DESIGN.md` section 6.4).
//
// Richer than a scale degree and poorer than a Chord, deliberately. A degree
// cannot express a tritone substitution or a borrowed chord -- which is why
// Chord carries an absolute root -- and a Chord cannot express the fact that
// somebody wrote "I" and meant "whatever the key makes it".
struct RelativeChord {
  enum class Binding {
    // Diatonic to the key it was written in, with the quality that mode gives.
    // The writer delegated the decision to the key, so a key change re-derives
    // it: I becomes i, IV becomes iv, vi becomes VI.
    Delegated,
    // An accidental, or a quality the mode does not give. The writer overrode
    // the key, so a key change transposes it and never re-derives it.
    Overridden,
  };

  Binding binding = Binding::Delegated;

  // Delegated: the scale degree, 0 for the tonic. This is the functional
  // invariant -- the degree survives a mode change and the pitch does not.
  int degree = 0;
  bool seventh = false;

  // Overridden: semitones above the tonic, measured against the PARALLEL
  // MAJOR so the number means the same thing in every mode, and the tones
  // exactly as written. Spelling is derived for display, so this is `bIII` in
  // a major key and `III` in a minor one.
  int semitones = 0;
  std::array<std::int8_t, kMaxChordTones> tones{{0, 4, 7, 0, 0}};
  int toneCount = 3;
  int bassSemitones = -1; // above the tonic; -1 when the root is the bass

  // Carried rather than recomputed, for the reason Chord gives: the label is
  // never the truth, and re-deriving one on the way out could rename a chord
  // the writer had already named.
  Quality quality = Quality::Major;
};

struct RelativeBar {
  std::vector<RelativeChord> chords;
};

using RelativeChart = std::vector<RelativeBar>;

// Read a chart against the key it was written in, deciding each chord's
// binding. Lossless: resolving the result in the same key returns the chart it
// came from, which is the property the tests lead with.
RelativeChart toRelative(const Chart &chart, const MusicalKey::Key &key);

// The chart in a key. Delegated chords are re-derived from the degree;
// overridden ones are transposed and left alone.
Chart resolve(const RelativeChart &chart, const MusicalKey::Key &key);

// The chord a mode gives on a degree, which is what "diatonic" means here.
//
// Not a raw scale readout: minor-ish modes give a MAJOR triad on the fifth,
// because harmonic minor exists for exactly that reason and a minor v is not
// what anybody means by a dominant. `defaultDegreeLoop` already makes the same
// judgement, and this is the same judgement in the same layer.
Chord modeChordOn(const MusicalKey::Key &key, int degree, bool seventh);

// Every chord in the chart, in the order they sound. For display and for tests;
// the band reads a Layout instead, because a flat list has lost the timing.
Progression flatten(const Chart &chart);

// The tones of a quality, as semitones above the root.
Chord chordOn(int rootPitchClass, Quality quality);

// The diatonic triad built on a scale degree of a key: 0 is the tonic triad, 1
// the supertonic, and so on. Degrees run past 6 into the octave above.
Chord diatonicTriad(const MusicalKey::Key &key, int degree);

// The seventh chord on a degree, for when three notes are not enough.
Chord diatonicSeventh(const MusicalKey::Key &key, int degree);

// A loop of scale degrees, before it becomes chords. This is the layer a
// substitution pass would rewrite.
using DegreeLoop = std::vector<int>;

// What the band plays when nobody has said otherwise.
//
// Mode-aware, because I-V-vi-IV over a minor tonic gives a minor v, which is
// weak and not what anybody means by "the four chords". Major-ish modes get
// I-V-vi-IV; minor-ish modes get i-VI-III-VII.
DegreeLoop defaultDegreeLoop(const MusicalKey::Key &key);

// Whether a mode's third is minor -- the question that decides which default
// loop applies, and the one worth asking rather than listing modes at each
// call site.
bool isMinorish(MusicalKey::Mode mode);

// Degrees to chords.
//
// This is the seam. Today it is a straight diatonic realisation; the roadmap
// has secondary and altered dominants, tritone substitution, and borrowing
// from adjacent modes (Dorian from Aeolian or Mixolydian, and so on). Those
// belong here, between choosing degrees and producing tones, and they are why
// Chord carries an absolute root: a substituted chord is not a degree of
// anything.
Progression realise(const MusicalKey::Key &key, const DegreeLoop &degrees);

// The whole default: degrees, then chords.
Progression defaultProgression(const MusicalKey::Key &key);

// The same, as a chart of one chord per bar.
Chart defaultChart(const MusicalKey::Key &key);

// "Am", "F", "C7", "Bbmaj7", "F#m7b5", "Csus4", "Am7/G", "F#m7(b5)", "Cmaj9".
// Returns false for anything it does not recognise, rather than guessing.
//
// Accepts what players actually write, which is a wider vocabulary than the
// band can voice: five tones is the limit, so a thirteenth keeps its name and
// its seventh but not every rung of the stack. Parsing more than we voice is
// deliberate -- the chart is a document as well as an instruction, and a chord
// we refuse to read is a chord the room cannot talk about.
bool parseChordName(const juce::String &text, Chord &out);

// The name back again: "Dm7", "C#sus4", "Am7/G". Spelled sharp or flat as
// asked, since the key signature decides that and a chord does not know it.
//
// Derived from the tones rather than from the quality label, so an altered or
// borrowed chord names itself correctly without an enum entry existing for it.
// Canonical: "CM7" and "Cmaj7" both come back as "Cmaj7".
juce::String chordName(const Chord &chord, bool flat);

// The same, spelled against a key rather than by one flag for everything.
//
// A key signature does not settle the question on its own: D major takes
// sharps and its flattened second is still Eb, so a chart spelled from one
// boolean is wrong for exactly the chords section 6.4 exists to move. Each
// chord is spelled by where its root sits in the scale -- a lowered degree
// keeps its flat, the tritone takes the sharp everybody writes -- and an
// invalid key falls back to `key.flat`, since inventing a spelling from
// nothing would be worse than the flag.
juce::String chordName(const Chord &chord, const MusicalKey::Key &key);

// A pitch class spelled as this key would write it: "Eb" rather than "D#" in
// D major, "B" rather than "Cb" in F minor.
//
// Exported because a bass note, a chord root and a chip all ask the same
// question, and answering it three ways is how a chart ends up disagreeing
// with itself.
juce::String spellNote(int pitchClass, const MusicalKey::Key &key);

// A chart from a chat line, bars and all: "| Dm7 | C# Csus |".
bool parseChart(const juce::String &text, Chart &out);

// A chart written in scale degrees, against the key it is relative to:
// "| I | vi IV |", "| i | VI | III VII |", "| 1 | 4 | b6 |".
//
// Roman case carries the quality -- IV is major, iv is minor -- and an arabic
// degree takes whatever the key gives it, so "1 4 5" is major in a major key
// and minor in a minor one. An altered degree is major unless it says
// otherwise, since "b6" almost always means the borrowed major chord.
//
// Degrees never travel on the wire. The client resolves them against the
// session key and sends the absolute chart, so a bot, a Jamtaba user and
// anything else in the room all see chords they already understand -- and
// there is exactly one place the resolution can be wrong (`PRINCIPLES §10`).
bool parseDegreeChart(const juce::String &text, const MusicalKey::Key &key,
                      Chart &out);

// "| Dm | Bb F |": a chart as a player would write it.
juce::String chartText(const Chart &chart, bool flat);

// The same, spelled per chord against the key. This is what a room should
// see; the boolean form remains for callers that have no key at all.
juce::String chartText(const Chart &chart, const MusicalKey::Key &key);

// The same chart in roman numerals against a key: "| i | VI IV |".
//
// Chromatic and mechanical. A chord whose root is not in the scale is named by
// where it sits against it -- III7, bVI, #ivo -- rather than by guessing at
// what it is doing. V7/vi is a claim about intent and two readings are often
// defensible; where a root sits is not a matter of opinion.
juce::String romanChartText(const Chart &chart, const MusicalKey::Key &key);

// One chord as a roman numeral: "ii7", "V7", "bVI", "#ivo".
juce::String romanName(const Chord &chord, const MusicalKey::Key &key);

// A Jamtaba-style progression from a chat line: "| Am | F | C | G |".
//
// Strict on purpose. Jamtaba's own parser treats "I" and "l" as measure
// separators and so reads "I AM TIRED ..." as a chord progression -- that is a
// real case in their test suite, and MusicalKey.h refuses to guess at prose for
// the same reason. Every measure must parse as a chord or the whole line is
// not a progression.
bool parseProgression(const juce::String &text, Progression &out);

// Whether a line is a chord chart at all, for anything that has to decide how
// to show it before deciding what it means.
//
// The same tokeniser as parseProgression, so a line coloured as a chart in the
// chat pane and a line the band will play are the same set. They were two
// parsers once and they disagreed in both directions: a line could be coloured
// green and silently never reach the band (`PRINCIPLES §8`).
bool looksLikeChart(const juce::String &text);

// Where in the progression a given beat of the interval falls.
//
// The progression fills exactly one interval, so every interval is a complete
// loop and the band cannot drift against a listener whose phase is its own --
// each client plays a received interval from its own downbeat, so an interval
// that is a whole number of progressions always lands right. A dropped
// interval then costs a bar rather than shifting the harmony from then on.
//
// The chord changes are placed by the same Euclidean generator the drums use:
// N chords over BPI beats is E(N, BPI). At rotation 0 that is exactly an even
// division -- the Bresenham form and integer division agree -- so the default
// is the obvious one, and the rotation is there to displace the changes off the
// beat when a seed asks for it.
int chordIndexForBeat(int beat, int bpi, int numChords, int rotation = 0);

// A chart resolved onto one interval's grid: which chord sounds at every step,
// worked out once instead of four times.
//
// Every voice needs the same three answers -- what is sounding now, has it just
// changed, and how long does it last -- and each of them used to re-derive the
// timing from the chord count. That was tolerable while chords were evenly
// spaced and stops being so the moment a bar can hold two of them.
//
// The grid is eighths rather than beats, because a bar one beat long can still
// hold two chords, and because the lead already thinks in eighths.
inline constexpr int kStepsPerBeat = 2;

struct Layout {
  Progression chords;           // in the order they sound
  std::vector<int> stepToChord; // one entry per eighth of the interval
  int bpi = 0;

  int steps() const { return (int)stepToChord.size(); }
  bool empty() const { return chords.empty() || stepToChord.empty(); }
};

// Placement, in two applications of the generator the drums use:
//
//   - bars over the interval, which is exactly `chordIndexForBeat` -- so a
//     chart of one chord per bar lays out precisely as it did before bars
//     existed, and that is asserted rather than assumed;
//   - then each bar's chords over that bar's own steps.
//
// A bar holding more chords than it has steps drops the ones that will not fit,
// the same way a progression longer than the interval always has.
Layout layoutChart(const Chart &chart, int bpi);

// What is sounding at a step, and whether the chord changed on it. Step 0 is
// always a change: an interval opens on its first chord.
const Chord &chordAtStep(const Layout &layout, int step);
bool changesAtStep(const Layout &layout, int step);

// A chord as absolute MIDI notes, ascending: an actual voicing rather than a
// set of intervals.
using Voicing = std::vector<int>;

// Where a chordal instrument sits: G3 to G5, above the bass and below where a
// soloist usually is.
inline constexpr int kVoiceLow = 55;
inline constexpr int kVoiceHigh = 79;

// Voice a sequence of chords so each one moves as little as possible from the
// last -- and so the loop closes.
//
// Root position for everything is what a machine does: C to Am moves all three
// notes when two of them are the same note, and the ear hears three separate
// chords rather than a progression. Choosing an inversion instead lets common
// tones stay exactly where they are, which is the whole of what voice leading
// buys.
//
// The chart repeats every interval, so what matters is the cost around the
// CYCLE, not along the line. The last chord's move back to the first is costed
// like any other, because that seam is the one a listener hears every single
// time round; leaving it out of the objective visibly changes the answer, and
// there is a test that says so.
//
// Deterministic, integer, and free of anything that could allocate on an audio
// thread's behalf: it runs once per interval on the conductor thread.
std::vector<Voicing> voiceLead(const Progression &chords);

// What a chart says about what key it is in.
//
// A progression is evidence, not a declaration -- so this offers rather than
// decides. `confident` is the only field a caller should act on without asking
// a human: below it the answer is "these chords do not say", which for a loop
// like Am F C G is the truthful answer and not a failure.
struct KeyGuess {
  MusicalKey::Key key;
  double score = 0.0;  // the winner's score
  double margin = 0.0; // how far ahead of the runner-up, in points
  bool confident = false;
};

// Weights by how much a tone DISCRIMINATES, which is not the same as how
// important it sounds. A perfect fifth is in the scale for six of the seven
// degrees, so it almost never rules a key out; the third is what separates
// major from minor and Dorian from Aeolian.
inline constexpr double kRootWeight = 3.0;
inline constexpr double kThirdWeight = 2.0;
inline constexpr double kSeventhWeight = 2.0;
inline constexpr double kFifthWeight = 1.0;
inline constexpr double kExtensionWeight = 1.0;

// How far ahead the winner must be before the guess is worth showing.
// Calibrated against the table in HarmonyTests, which is the specification:
// every entry in it is a progression whose key is or is not in doubt.
inline constexpr double kConfidentMargin = 2.0;

KeyGuess inferKey(const Progression &chords);

// What voiceLead is minimising: total semitone movement between two voicings,
// pairing them from the bottom up and charging an added or dropped voice the
// distance to its nearest neighbour. Exposed because a test that cannot measure
// the cost cannot show that the loop was closed.
int voicingDistance(const Voicing &a, const Voicing &b);

} // namespace Harmony
