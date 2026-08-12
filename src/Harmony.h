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
  HalfDiminished7
};

inline constexpr int kMaxChordTones = 5;

struct Chord {
  int root = 0; // pitch class, 0-11, absolute
  Quality quality = Quality::Major;

  // Semitones above the root. Derived from the quality today, but stored
  // rather than recomputed so an alteration can move or add one tone without
  // needing a quality to name the result.
  std::array<std::int8_t, kMaxChordTones> tones{{0, 4, 7, 0, 0}};
  int toneCount = 3;

  bool operator==(const Chord &o) const {
    if (root != o.root || toneCount != o.toneCount)
      return false;
    for (int i = 0; i < toneCount; ++i)
      if (tones[(size_t)i] != o.tones[(size_t)i])
        return false;
    return true;
  }
};

using Progression = std::vector<Chord>;

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

} // namespace Harmony
