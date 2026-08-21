#include "MusicalKey.h"

namespace MusicalKey {

namespace {

struct ModeName {
  const char *name;
  Mode mode;
};

// Longest-first within each family does not matter here because the whole
// remainder of the string is matched, not a prefix.
const ModeName kModeNames[] = {
    {"major", Mode::Major},     {"maj", Mode::Major},
    {"minor", Mode::Minor},     {"min", Mode::Minor},
    {"m", Mode::Minor},         {"ionian", Mode::Ionian},
    {"dorian", Mode::Dorian},   {"phrygian", Mode::Phrygian},
    {"lydian", Mode::Lydian},   {"mixolydian", Mode::Mixolydian},
    {"mixo", Mode::Mixolydian}, {"aeolian", Mode::Aeolian},
    {"locrian", Mode::Locrian},
};

// Semitones above C for the natural notes.
int naturalSemitone(char letter) {
  switch (letter) {
  case 'C':
    return 0;
  case 'D':
    return 2;
  case 'E':
    return 4;
  case 'F':
    return 5;
  case 'G':
    return 7;
  case 'A':
    return 9;
  case 'B':
    return 11;
  default:
    return -1;
  }
}

// Steps of each mode from its tonic. Major and Ionian coincide, as do Minor and
// Aeolian -- they are kept separate only so the name you typed comes back.
const int *modeSteps(Mode mode) {
  static const int major[] = {0, 2, 4, 5, 7, 9, 11};
  static const int dorian[] = {0, 2, 3, 5, 7, 9, 10};
  static const int phrygian[] = {0, 1, 3, 5, 7, 8, 10};
  static const int lydian[] = {0, 2, 4, 6, 7, 9, 11};
  static const int mixolydian[] = {0, 2, 4, 5, 7, 9, 10};
  static const int aeolian[] = {0, 2, 3, 5, 7, 8, 10};
  static const int locrian[] = {0, 1, 3, 5, 6, 8, 10};

  switch (mode) {
  case Mode::Major:
  case Mode::Ionian:
    return major;
  case Mode::Minor:
  case Mode::Aeolian:
    return aeolian;
  case Mode::Dorian:
    return dorian;
  case Mode::Phrygian:
    return phrygian;
  case Mode::Lydian:
    return lydian;
  case Mode::Mixolydian:
    return mixolydian;
  case Mode::Locrian:
    return locrian;
  }
  return major;
}

// How many semitones above its relative major each mode's tonic sits.
// Indexed by the Mode enum.
const int kModeOffsetFromRelativeMajor[] = {
    0,  // Major
    9,  // Minor
    0,  // Ionian
    2,  // Dorian
    4,  // Phrygian
    5,  // Lydian
    7,  // Mixolydian
    9,  // Aeolian
    11, // Locrian
};

} // namespace

bool usesFlats(int tonic, Mode mode) {
  const int offset = kModeOffsetFromRelativeMajor[(int)mode];
  const int relativeMajor = (((tonic - offset) % 12) + 12) % 12;
  // The major keys written with flats: F, Bb, Eb, Ab, Db. Everything else takes
  // sharps, including the enharmonic toss-ups, where F# is the usual choice.
  return relativeMajor == 5 || relativeMajor == 10 || relativeMajor == 3 ||
         relativeMajor == 8 || relativeMajor == 1;
}

std::string noteName(int semitone, bool flat) {
  static const char *sharp[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  static const char *flatNames[] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                    "Gb", "G",  "Ab", "A",  "Bb", "B"};
  const int s = ((semitone % 12) + 12) % 12;
  return flat ? flatNames[s] : sharp[s];
}

std::string modeName(Mode mode) {
  switch (mode) {
  case Mode::Major:
    return "major";
  case Mode::Minor:
    return "minor";
  case Mode::Ionian:
    return "Ionian";
  case Mode::Dorian:
    return "Dorian";
  case Mode::Phrygian:
    return "Phrygian";
  case Mode::Lydian:
    return "Lydian";
  case Mode::Mixolydian:
    return "Mixolydian";
  case Mode::Aeolian:
    return "Aeolian";
  case Mode::Locrian:
    return "Locrian";
  }
  return "major";
}

Key parseName(const std::string &text) {
  Key key;
  const auto trimmed = TextUtil::trim(text);
  if (trimmed.empty())
    return key;

  // Tonic letter, upper or lower case.
  const auto letter = TextUtil::upperChar(trimmed[0]);
  const int natural = naturalSemitone(letter);
  if (natural < 0)
    return key;

  int pos = 1;
  int semitone = natural;
  bool explicitFlat = false;
  bool explicitSharp = false;
  if (pos < trimmed.length()) {
    const auto accidental = trimmed[pos];
    if (accidental == '#') {
      semitone += 1;
      explicitSharp = true;
      ++pos;
    } else if (accidental == 'b' || accidental == 'B') {
      // Safe to take unconditionally: no mode name begins with "b", so a "b"
      // in second position can only be a flat. "Bb" is B flat major, "Bbm" is
      // B flat minor, and "Bm" never reaches here because "m" is not "b".
      semitone -= 1;
      explicitFlat = true;
      ++pos;
    }
  }

  // An accidental the user typed wins for the tonic, so "Bb" comes back as
  // "Bb"; otherwise the signature decides.
  auto resolveSpelling = [&](Mode mode) {
    key.flat = explicitFlat ||
               (!explicitSharp && usesFlats(((semitone % 12) + 12) % 12, mode));
  };

  auto rest = TextUtil::lower(TextUtil::trim(trimmed.substr((size_t)pos)));
  // An empty mode means major, so "D" is D major and "Bb" is B flat major.
  if (rest.empty()) {
    key.valid = true;
    key.tonic = ((semitone % 12) + 12) % 12;
    key.mode = Mode::Major;
    resolveSpelling(key.mode);
    return key;
  }

  for (const auto &entry : kModeNames) {
    if (rest == entry.name) {
      key.valid = true;
      key.tonic = ((semitone % 12) + 12) % 12;
      key.mode = entry.mode;
      resolveSpelling(key.mode);
      return key;
    }
  }

  return key; // a mode we do not recognise is not a key
}

Key parseTagged(const std::string &text) {
  const int open = TextUtil::indexOfIgnoreCase(text, tagPrefix());
  if (open < 0)
    return {};

  const size_t contentStart = (size_t)open + tagPrefix().size();
  const auto close = text.find(']', contentStart);
  if (close == std::string::npos)
    return {};

  return parseName(text.substr(contentStart, close - contentStart));
}

Key parseAnnouncement(const std::string &line) {
  if (const auto tagged = parseTagged(line); tagged.valid)
    return tagged;

  // Line-leading only. Accepting `/key` anywhere would undo the whole point of
  // having a second form: a bot explaining it would trigger it again.
  const auto trimmed = TextUtil::trim(line);
  if (!TextUtil::startsWithIgnoreCase(trimmed, "/key "))
    return {};
  return parseName(trimmed.substr(5));
}

std::string buildTagged(const Key &key) {
  if (!key.valid)
    return {};
  return "[key: " + displayName(key) + "]";
}

std::string displayName(const Key &key) {
  if (!key.valid)
    return {};
  return noteName(key.tonic, key.flat) + " " + modeName(key.mode);
}

std::string scaleNotes(const Key &key) {
  if (!key.valid)
    return {};

  const int *steps = modeSteps(key.mode);
  std::vector<std::string> notes;
  for (int i = 0; i < 7; ++i)
    notes.push_back(noteName(key.tonic + steps[i], key.flat));
  return TextUtil::join(notes, " ");
}

const int *scaleSteps(Mode mode) { return modeSteps(mode); }

int degreeToMidi(const Key &key, int degree, int octave) {
  if (!key.valid)
    return -1;

  // Degrees run on past the seventh into the octaves above, and below zero into
  // the ones beneath, so a bass line may walk down out of its starting octave
  // without the caller doing the arithmetic.
  const int *steps = scaleSteps(key.mode);
  int octaveShift = degree / kScaleDegrees;
  int within = degree % kScaleDegrees;
  if (within < 0) {
    within += kScaleDegrees;
    --octaveShift;
  }

  // MIDI 60 is middle C, and octave 4 is the octave containing it.
  return 12 * (octave + 1 + octaveShift) + key.tonic + steps[within];
}

} // namespace MusicalKey
