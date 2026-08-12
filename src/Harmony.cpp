#include "Harmony.h"

#include "Euclidean.h"

namespace Harmony {

namespace {

int wrapPitchClass(int pc) { return ((pc % 12) + 12) % 12; }

} // namespace

Chord chordOn(int rootPitchClass, Quality quality) {
  Chord c;
  c.root = wrapPitchClass(rootPitchClass);
  c.quality = quality;

  switch (quality) {
  case Quality::Major:
    c.tones = {{0, 4, 7, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Minor:
    c.tones = {{0, 3, 7, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Diminished:
    c.tones = {{0, 3, 6, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Augmented:
    c.tones = {{0, 4, 8, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Dominant7:
    c.tones = {{0, 4, 7, 10, 0}};
    c.toneCount = 4;
    break;
  case Quality::Major7:
    c.tones = {{0, 4, 7, 11, 0}};
    c.toneCount = 4;
    break;
  case Quality::Minor7:
    c.tones = {{0, 3, 7, 10, 0}};
    c.toneCount = 4;
    break;
  case Quality::HalfDiminished7:
    c.tones = {{0, 3, 6, 10, 0}};
    c.toneCount = 4;
    break;
  }
  return c;
}

namespace {

// Stacks thirds out of the scale itself rather than looking the quality up in a
// table per mode. Any mode gives the right triads and sevenths for free, which
// matters because Antiphon carries all seven and a Lydian II is major where a
// Ionian ii is minor.
Chord stackThirds(const MusicalKey::Key &key, int degree, int numNotes) {
  const int *steps = MusicalKey::scaleSteps(key.mode);

  auto degreeSemitone = [&](int d) {
    int octave = d / MusicalKey::kScaleDegrees;
    int within = d % MusicalKey::kScaleDegrees;
    if (within < 0) {
      within += MusicalKey::kScaleDegrees;
      --octave;
    }
    return 12 * octave + steps[within];
  };

  const int rootSemi = degreeSemitone(degree);

  Chord c;
  c.root = wrapPitchClass(key.tonic + rootSemi);
  c.toneCount = juce::jlimit(1, kMaxChordTones, numNotes);
  for (int i = 0; i < c.toneCount; ++i)
    c.tones[(size_t)i] =
        (std::int8_t)(degreeSemitone(degree + 2 * i) - rootSemi);

  // Name it if it happens to have a name. Nothing depends on the label -- the
  // tones are the truth -- but a Chord that can say "minor 7" is easier to read
  // in a test failure than one that can only list intervals.
  const int third = c.tones[1];
  const int fifth = c.toneCount > 2 ? c.tones[2] : 7;
  const int seventh = c.toneCount > 3 ? (int)c.tones[3] : -1;

  if (c.toneCount >= 4) {
    if (third == 4 && fifth == 7 && seventh == 10)
      c.quality = Quality::Dominant7;
    else if (third == 4 && fifth == 7 && seventh == 11)
      c.quality = Quality::Major7;
    else if (third == 3 && fifth == 7 && seventh == 10)
      c.quality = Quality::Minor7;
    else if (third == 3 && fifth == 6 && seventh == 10)
      c.quality = Quality::HalfDiminished7;
  } else {
    if (third == 4 && fifth == 7)
      c.quality = Quality::Major;
    else if (third == 3 && fifth == 7)
      c.quality = Quality::Minor;
    else if (third == 3 && fifth == 6)
      c.quality = Quality::Diminished;
    else if (third == 4 && fifth == 8)
      c.quality = Quality::Augmented;
  }
  return c;
}

} // namespace

Chord diatonicTriad(const MusicalKey::Key &key, int degree) {
  return stackThirds(key, degree, 3);
}

Chord diatonicSeventh(const MusicalKey::Key &key, int degree) {
  return stackThirds(key, degree, 4);
}

bool isMinorish(MusicalKey::Mode mode) {
  // The third is what decides it, so ask the scale rather than listing modes.
  return MusicalKey::scaleSteps(mode)[2] == 3;
}

DegreeLoop defaultDegreeLoop(const MusicalKey::Key &key) {
  if (!key.valid)
    return {0, 4, 5, 3};

  if (isMinorish(key.mode))
    return {0, 5, 2, 6}; // i VI III VII
  return {0, 4, 5, 3};   // I V vi IV
}

Progression realise(const MusicalKey::Key &key, const DegreeLoop &degrees) {
  Progression out;
  out.reserve(degrees.size());
  for (int d : degrees)
    out.push_back(diatonicTriad(key, d));
  return out;
}

Progression defaultProgression(const MusicalKey::Key &key) {
  return realise(key, defaultDegreeLoop(key));
}

int chordIndexForBeat(int beat, int bpi, int numChords, int rotation) {
  if (numChords <= 0 || bpi <= 0)
    return 0;
  if (numChords >= bpi)
    return ((beat % bpi) + bpi) % bpi % numChords;

  const int b = ((beat % bpi) + bpi) % bpi;

  // Count the chord changes at or before this beat. E(numChords, bpi) places
  // them; a progression that does not divide the interval evenly still fills
  // it, with the chords taking turns being a beat longer.
  int idx = -1;
  for (int i = 0; i <= b; ++i)
    if (Euclidean::hit(i, bpi, numChords, rotation))
      ++idx;

  // Rotated far enough that the interval opens before the first change: the
  // chord sounding is the one the loop ended on.
  if (idx < 0)
    return numChords - 1;
  return idx >= numChords ? numChords - 1 : idx;
}

} // namespace Harmony
