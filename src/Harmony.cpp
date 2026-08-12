#include "Harmony.h"

#include "Euclidean.h"

#include <algorithm>
#include <cstdlib>

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
  case Quality::Diminished7:
    // A diminished seventh is nine semitones up, which is a major sixth by
    // another name -- the triad under it is what makes it a seventh.
    c.tones = {{0, 3, 6, 9, 0}};
    c.toneCount = 4;
    break;
  case Quality::Sus2:
    c.tones = {{0, 2, 7, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Sus4:
    c.tones = {{0, 5, 7, 0, 0}};
    c.toneCount = 3;
    break;
  case Quality::Major6:
    c.tones = {{0, 4, 7, 9, 0}};
    c.toneCount = 4;
    break;
  case Quality::Minor6:
    c.tones = {{0, 3, 7, 9, 0}};
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

Chart chartOf(const Progression &progression) {
  Chart chart;
  chart.reserve(progression.size());
  for (const auto &c : progression)
    chart.push_back(Bar{{c}});
  return chart;
}

Progression flatten(const Chart &chart) {
  Progression out;
  for (const auto &bar : chart)
    for (const auto &c : bar.chords)
      out.push_back(c);
  return out;
}

Chart defaultChart(const MusicalKey::Key &key) {
  return chartOf(defaultProgression(key));
}

namespace {

// A note letter and its accidentals: "C", "F#", "Bb". Advances `pos` past what
// it read and returns the pitch class, or -1.
int parseNote(const juce::String &s, int &pos) {
  static const char *letters = "CDEFGAB";
  static const int letterSemis[7] = {0, 2, 4, 5, 7, 9, 11};

  if (pos >= s.length())
    return -1;

  const juce::juce_wchar raw = s[pos];
  const juce::juce_wchar upper =
      (raw >= 'a' && raw <= 'z') ? (juce::juce_wchar)(raw - 32) : raw;
  const int idx = juce::String(letters).indexOfChar(upper);
  if (idx < 0)
    return -1;

  int pc = letterSemis[idx];
  ++pos;
  while (pos < s.length() && (s[pos] == '#' || s[pos] == 'b')) {
    // A 'b' can be an accidental or the start of "b5", so only take it as a
    // flat while it sits directly against the letter.
    if (s[pos] == 'b' && pos + 1 < s.length() &&
        juce::CharacterFunctions::isDigit(s[pos + 1]))
      break;
    pc += (s[pos] == '#') ? 1 : -1;
    ++pos;
  }
  return wrapPitchClass(pc);
}

// The chord's shape, as the suffix is read. Kept as intervals rather than as a
// quality because most of what players write has no name in the enum.
struct Shape {
  int third = 4;    // 4 major, 3 minor
  int sus = -1;     // 2 or 5 when the third is suspended
  int fifth = 7;    // 6 diminished, 8 augmented
  int seventh = -1; // 10 minor, 11 major, 9 diminished
  bool sixth = false;
  bool dimBase = false;
  std::vector<int> extras; // 13, 14, 15, 17, 18, 20, 21 -- in the order named
};

// A cursor over the suffix. Case-sensitive, because "M7" and "m7" are different
// chords and lowercasing early is how that gets lost.
struct Cursor {
  juce::String text;
  int pos = 0;

  bool take(const char *literal) {
    const juce::String want(literal);
    if (text.substring(pos, pos + want.length()) != want)
      return false;
    pos += want.length();
    return true;
  }
  bool done() const { return pos >= text.length(); }
};

void addSeventh(Shape &shape, bool major) {
  if (shape.seventh >= 0)
    return;
  shape.seventh = shape.dimBase && !major ? 9 : (major ? 11 : 10);
}

// The suffix after the root: "m7b5", "sus4", "maj9", "7b9", "add9".
bool parseSuffix(Cursor &c, Shape &shape) {
  bool majorSeventh = false;

  // The base quality comes first and only once. "M" is major and "m" is minor,
  // which is the one place capitals carry meaning in a chord symbol.
  if (c.take("maj") || c.take("Maj") || c.take("MAJ") || c.take("M")) {
    majorSeventh = true;
  } else if (c.take("min") || c.take("mi") || c.take("m") || c.take("-")) {
    shape.third = 3;
  } else if (c.take("dim")) {
    shape.third = 3;
    shape.fifth = 6;
    shape.dimBase = true;
  } else if (c.take("aug")) {
    shape.fifth = 8;
  } else if (c.take("o") || c.take("0")) {
    // The degree sign is not ASCII and not on a keyboard, so people type "o"
    // or a zero. Both mean diminished, and refusing the zero would be refusing
    // the spelling half of them use.
    shape.third = 3;
    shape.fifth = 6;
    shape.dimBase = true;
  } else if (c.take("+")) {
    shape.fifth = 8;
  }

  while (!c.done()) {
    // Two-digit numbers first, or "13" reads as "1" and then fails.
    if (c.take("13")) {
      addSeventh(shape, majorSeventh);
      shape.extras.push_back(21);
    } else if (c.take("11")) {
      addSeventh(shape, majorSeventh);
      shape.extras.push_back(17);
    } else if (c.take("9")) {
      addSeventh(shape, majorSeventh);
      shape.extras.push_back(14);
    } else if (c.take("7")) {
      addSeventh(shape, majorSeventh);
    } else if (c.take("6")) {
      shape.sixth = true;
    } else if (c.take("sus2")) {
      shape.sus = 2;
    } else if (c.take("sus4") || c.take("sus")) {
      shape.sus = 5;
    } else if (c.take("add9") || c.take("add2")) {
      shape.extras.push_back(14);
    } else if (c.take("add11") || c.take("add4")) {
      shape.extras.push_back(17);
    } else if (c.take("add13") || c.take("add6")) {
      shape.extras.push_back(21);
    } else if (c.take("b5")) {
      shape.fifth = 6;
    } else if (c.take("#5")) {
      shape.fifth = 8;
    } else if (c.take("b9")) {
      shape.extras.push_back(13);
    } else if (c.take("#9")) {
      shape.extras.push_back(15);
    } else if (c.take("#11")) {
      shape.extras.push_back(18);
    } else if (c.take("b13")) {
      shape.extras.push_back(20);
    } else {
      return false;
    }
  }
  return true;
}

// Give the shape the closest name the enum has. Nothing depends on it -- the
// tones are the truth -- but a Chord that can say "minor 7" is easier to read
// in a test failure than one that can only list intervals.
Quality qualityOf(const Shape &shape) {
  if (shape.sus == 2)
    return Quality::Sus2;
  if (shape.sus == 5)
    return Quality::Sus4;

  if (shape.third == 3) {
    if (shape.fifth == 6)
      return shape.seventh == 10
                 ? Quality::HalfDiminished7
                 : (shape.seventh == 9 ? Quality::Diminished7
                                       : Quality::Diminished);
    if (shape.sixth)
      return Quality::Minor6;
    return shape.seventh >= 0 ? Quality::Minor7 : Quality::Minor;
  }

  if (shape.fifth == 8)
    return Quality::Augmented;
  if (shape.sixth)
    return Quality::Major6;
  if (shape.seventh == 11)
    return Quality::Major7;
  if (shape.seventh == 10)
    return Quality::Dominant7;
  return Quality::Major;
}

Chord chordFrom(int root, const Shape &shape) {
  Chord c;
  c.root = wrapPitchClass(root);
  c.quality = qualityOf(shape);

  // Most defining first, because five tones is the ceiling: a thirteenth chord
  // keeps its seventh and its thirteenth and loses the rungs between, which is
  // how a keyboard player would voice it anyway.
  std::vector<int> tones;
  tones.push_back(0);
  tones.push_back(shape.sus >= 0 ? shape.sus : shape.third);
  tones.push_back(shape.fifth);
  if (shape.seventh >= 0)
    tones.push_back(shape.seventh);
  if (shape.sixth)
    tones.push_back(9);
  for (int e : shape.extras)
    tones.push_back(e);

  c.toneCount = juce::jmin((int)tones.size(), kMaxChordTones);
  for (int i = 0; i < c.toneCount; ++i)
    c.tones[(size_t)i] = (std::int8_t)tones[(size_t)i];
  return c;
}

} // namespace

bool parseChordName(const juce::String &text, Chord &out) {
  juce::String s = text.trim();
  if (s.isEmpty())
    return false;

  // Parentheses are decoration around an alteration -- "F#m7(b5)" is
  // "F#m7b5" -- so they come out before the suffix is read rather than being
  // handled at every alteration. Unbalanced ones are a typo, not a chord:
  // dropping them silently would make "C(" a C major triad.
  if (s.indexOfChar('(') >= 0 || s.indexOfChar(')') >= 0) {
    int opens = 0, closes = 0;
    for (int i = 0; i < s.length(); ++i) {
      opens += (s[i] == '(') ? 1 : 0;
      closes += (s[i] == ')') ? 1 : 0;
    }
    if (opens != closes)
      return false;
    s = s.removeCharacters("()");
  }

  int bass = -1;
  const int slash = s.lastIndexOfChar('/');
  if (slash >= 0) {
    juce::String bassText = s.substring(slash + 1).trim();
    int bassPos = 0;
    bass = parseNote(bassText, bassPos);
    if (bass < 0 || bassPos != bassText.length())
      return false;
    s = s.substring(0, slash).trim();
  }

  int pos = 0;
  const int root = parseNote(s, pos);
  if (root < 0)
    return false;

  Cursor cursor{s.substring(pos), 0};
  Shape shape;
  if (!parseSuffix(cursor, shape))
    return false;

  out = chordFrom(root, shape);
  out.bass = (bass == out.root) ? -1 : bass;
  return true;
}

namespace {

// The part of a chord symbol after the root: "m7", "sus4", "maj9", "dim7".
juce::String chordSuffix(const Chord &chord) {
  auto has = [&](int semitone) {
    for (int i = 0; i < chord.toneCount; ++i)
      if (chord.tones[(size_t)i] == semitone)
        return true;
    return false;
  };

  const bool sus2 = has(2) && !has(3) && !has(4);
  const bool sus4 = has(5) && !has(3) && !has(4);
  const bool minor = has(3);
  const bool flatFive = has(6) && !has(7);
  const bool sharpFive = has(8) && !has(7);
  const int seventh = has(11) ? 11 : (has(10) ? 10 : -1);
  // A tone 9 is a sixth on an ordinary chord and a diminished seventh on a
  // diminished one. The triad under it is the only thing that tells them apart.
  const bool dimSeventh = has(9) && minor && flatFive;
  const bool sixth = has(9) && !dimSeventh;

  // The number a chord is called by: the highest rung it names. Only a chord
  // with a seventh under it counts up -- an added ninth with no seventh is
  // "add9" and calling it a ninth would name a chord with one more note in it.
  int top = seventh >= 0 ? 7 : 0;
  if (seventh >= 0) {
    if (has(21))
      top = 13;
    else if (has(17))
      top = 11;
    else if (has(14))
      top = 9;
  }

  juce::String suffix;
  if (minor && flatFive && seventh == 10) {
    suffix = "m7b5";
  } else if (dimSeventh) {
    suffix = "dim7";
  } else if (minor && flatFive) {
    suffix = "dim";
  } else {
    if (minor)
      suffix = "m";
    if (sharpFive)
      suffix += "aug";

    if (sixth)
      suffix += "6";
    else if (top >= 7)
      suffix += (seventh == 11 ? "maj" : "") + juce::String(top);
    else if (has(14))
      suffix += "add9"; // a ninth with no seventh under it is an added note

    if (flatFive && !minor)
      suffix += "b5";
  }

  if (sus2)
    suffix += "sus2";
  else if (sus4)
    suffix += "sus4";

  // Alterations last, in the order a player would read them.
  if (has(13))
    suffix += "b9";
  if (has(15))
    suffix += "#9";
  if (has(18))
    suffix += "#11";
  if (has(20))
    suffix += "b13";

  return suffix;
}

} // namespace

juce::String chordName(const Chord &chord, bool flat) {
  juce::String name =
      MusicalKey::noteName(chord.root, flat) + chordSuffix(chord);
  if (chord.bass >= 0 && chord.bass != chord.root)
    name += "/" + MusicalKey::noteName(chord.bass, flat);
  return name;
}

juce::String romanName(const Chord &chord, const MusicalKey::Key &key) {
  if (!key.valid)
    return {};

  static const char *numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
  const int *steps = MusicalKey::scaleSteps(key.mode);
  const int interval = wrapPitchClass(chord.root - key.tonic);

  auto degreeAt = [&](int semitones) {
    for (int d = 0; d < MusicalKey::kScaleDegrees; ++d)
      if (steps[d] == ((semitones % 12) + 12) % 12)
        return d;
    return -1;
  };

  juce::String accidental;
  int degree = degreeAt(interval);
  if (degree < 0) {
    // Not in the scale. Name it as a lowered degree above -- bIII, bVI, bVII --
    // which is what a player writes, except at the tritone, where "bV" is
    // nobody's spelling and "#IV" is everybody's.
    const int above = degreeAt(interval + 1);
    const int below = degreeAt(interval - 1);
    if (above >= 0 && above != 4) {
      degree = above;
      accidental = "b";
    } else if (below >= 0) {
      degree = below;
      accidental = "#";
    } else if (above >= 0) {
      degree = above;
      accidental = "b";
    } else {
      return {}; // a root a scale reaches from neither side: not nameable
    }
  }

  juce::String numeral(numerals[degree]);
  const bool minorThird =
      chord.toneCount > 1 &&
      (chord.tones[1] == 3 || (chord.tones[1] != 4 && chord.toneCount > 2 &&
                               chord.tones[2] == 6));
  if (minorThird)
    numeral = numeral.toLowerCase();

  // The case already says minor, so the symbol must not say it twice: Dm7 in C
  // is ii7, not iim7. "m7b5" stays whole, because it names a fifth as well as a
  // third, and "dim" reads better as the "o" a chart would use.
  juce::String suffix = chordSuffix(chord);
  if (suffix == "m")
    suffix = "";
  else if (suffix.startsWith("m") && !suffix.startsWith("maj") &&
           !suffix.startsWith("m7b5"))
    suffix = suffix.substring(1);
  else if (suffix.startsWith("dim"))
    suffix = "o" + suffix.substring(3);

  juce::String out = accidental + numeral + suffix;
  if (chord.bass >= 0 && chord.bass != chord.root)
    out += "/" + MusicalKey::noteName(chord.bass, key.flat);
  return out;
}

juce::String chartText(const Chart &chart, bool flat) {
  if (chart.empty())
    return {};

  juce::String out = "|";
  for (const auto &bar : chart) {
    for (const auto &c : bar.chords)
      out += " " + chordName(c, flat);
    out += " |";
  }
  return out;
}

juce::String romanChartText(const Chart &chart, const MusicalKey::Key &key) {
  if (chart.empty() || !key.valid)
    return {};

  juce::String out = "|";
  for (const auto &bar : chart) {
    for (const auto &c : bar.chords) {
      const auto name = romanName(c, key);
      out += " " + (name.isEmpty() ? chordName(c, key.flat) : name);
    }
    out += " |";
  }
  return out;
}

namespace {

// The one tokeniser. `chords` is filled when it is asked for; `looksLikeChart`
// passes nullptr and only wants the verdict.
bool readChart(const juce::String &text, std::vector<std::vector<Chord>> *bars) {
  const auto trimmed = text.trim();

  // A chart opens with a bar line. Requiring it is what keeps prose out:
  // Jamtaba's parser treats "I" and "l" as separators and so reads "I AM TIRED"
  // as a progression, which is exactly the guess this refuses to make.
  if (!trimmed.startsWithChar('|'))
    return false;

  int measures = 0;
  int chords = 0;
  for (const auto &part : juce::StringArray::fromTokens(trimmed, "|", "")) {
    const auto measure = part.trim();
    if (measure.isEmpty())
      continue; // the empty pieces either side of the outer bars

    ++measures;
    std::vector<Chord> bar;
    for (const auto &token : juce::StringArray::fromTokens(measure, " \t", "")) {
      const auto name = token.trim();
      if (name.isEmpty())
        continue;
      Chord c;
      if (!parseChordName(name, c))
        return false; // one unreadable token and the line is not a chart
      ++chords;
      bar.push_back(c);
    }
    if (bars != nullptr)
      bars->push_back(std::move(bar));
  }

  // Two measures minimum, so a stray "|C" cannot become a progression.
  return measures >= 2 && chords >= 2;
}

} // namespace

bool looksLikeChart(const juce::String &text) {
  return readChart(text, nullptr);
}

namespace {

// "I", "iv", "bVI", "#ivo", "1", "b6", "5sus4". The key decides what an
// unqualified degree means.
bool parseDegreeName(const juce::String &text, const MusicalKey::Key &key,
                     Chord &out) {
  juce::String s = text.trim().removeCharacters("()");
  if (s.isEmpty())
    return false;

  int alter = 0;
  if (s.startsWithChar('b')) {
    alter = -1;
    s = s.substring(1);
  } else if (s.startsWithChar('#')) {
    alter = 1;
    s = s.substring(1);
  }
  if (s.isEmpty())
    return false;

  // Roman first, longest first so "vii" is not read as "v".
  static const char *romans[] = {"vii", "vi", "iv", "v", "iii", "ii", "i"};
  static const int romanDegree[] = {6, 5, 3, 4, 2, 1, 0};

  int degree = -1;
  bool minorCase = false;
  bool fromRoman = false;

  for (size_t i = 0; i < 7; ++i) {
    const juce::String lower(romans[i]);
    const juce::String upper = lower.toUpperCase();
    if (s.startsWith(lower)) {
      degree = romanDegree[i];
      minorCase = true;
      fromRoman = true;
      s = s.substring(lower.length());
      break;
    }
    if (s.startsWith(upper)) {
      degree = romanDegree[i];
      fromRoman = true;
      s = s.substring(upper.length());
      break;
    }
  }

  if (degree < 0) {
    if (!juce::CharacterFunctions::isDigit(s[0]))
      return false;
    const int number = s[0] - '0';
    if (number < 1 || number > 7)
      return false;
    degree = number - 1;
    s = s.substring(1);
  }

  if (!key.valid)
    return false;

  const int *steps = MusicalKey::scaleSteps(key.mode);
  const int root = wrapPitchClass(key.tonic + steps[degree] + alter);

  Shape shape;
  if (fromRoman) {
    shape.third = minorCase ? 3 : 4;
  } else if (alter == 0) {
    // An arabic degree takes the chord the key already has there, so "1 4 5" is
    // major in a major key and minor in a minor one.
    const auto diatonic = diatonicTriad(key, degree);
    shape.third = diatonic.tones[1];
    shape.fifth = diatonic.tones[2];
  }
  // An altered arabic degree keeps the major default: "b6" means the borrowed
  // major chord nearly every time it is written.

  Cursor cursor{s, 0};
  if (!parseSuffix(cursor, shape))
    return false;

  out = chordFrom(root, shape);
  return true;
}

} // namespace

bool parseDegreeChart(const juce::String &text, const MusicalKey::Key &key,
                      Chart &out) {
  if (!key.valid)
    return false;

  const auto trimmed = text.trim();
  if (!trimmed.startsWithChar('|'))
    return false;

  Chart chart;
  int chords = 0;
  for (const auto &part : juce::StringArray::fromTokens(trimmed, "|", "")) {
    const auto measure = part.trim();
    if (measure.isEmpty())
      continue;

    Bar bar;
    for (const auto &token : juce::StringArray::fromTokens(measure, " \t", "")) {
      const auto name = token.trim();
      if (name.isEmpty())
        continue;
      Chord c;
      if (!parseDegreeName(name, key, c))
        return false;
      ++chords;
      bar.chords.push_back(c);
    }
    if (!bar.chords.empty())
      chart.push_back(std::move(bar));
  }

  if ((int)chart.size() < 2 || chords < 2)
    return false;

  out = std::move(chart);
  return true;
}

bool parseChart(const juce::String &text, Chart &out) {
  std::vector<std::vector<Chord>> bars;
  if (!readChart(text, &bars))
    return false;

  Chart chart;
  for (auto &bar : bars) {
    // A chart may be written with an empty bar in it -- "|C|F||G|F" is in
    // Jamtaba's own test suite -- and an empty bar holds no time.
    if (bar.empty())
      continue;
    chart.push_back(Bar{std::move(bar)});
  }

  out = std::move(chart);
  return true;
}

bool parseProgression(const juce::String &text, Progression &out) {
  Chart chart;
  if (!parseChart(text, chart))
    return false;
  out = flatten(chart);
  return true;
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

Layout layoutChart(const Chart &chart, int bpi) {
  Layout layout;
  layout.bpi = bpi;
  if (chart.empty() || bpi <= 0)
    return layout;

  const int steps = bpi * kStepsPerBeat;
  layout.stepToChord.assign((size_t)steps, 0);

  // Where each bar starts and ends, in steps. The bars are placed by the same
  // rule the chords used to be, so a chart of one chord per bar is unchanged.
  std::vector<int> barOfBeat((size_t)bpi, 0);
  for (int beat = 0; beat < bpi; ++beat)
    barOfBeat[(size_t)beat] = chordIndexForBeat(beat, bpi, (int)chart.size());

  int firstIndexInBar = 0;
  for (size_t bar = 0; bar < chart.size(); ++bar) {
    // The stretch of the interval this bar owns.
    int firstBeat = -1, lastBeat = -1;
    for (int beat = 0; beat < bpi; ++beat) {
      if (barOfBeat[(size_t)beat] != (int)bar)
        continue;
      if (firstBeat < 0)
        firstBeat = beat;
      lastBeat = beat;
    }

    const auto &chords = chart[bar].chords;
    for (const auto &c : chords)
      layout.chords.push_back(c);

    // More bars than beats: this one never sounds, but its chords still exist
    // in the chart, so they take an index and no time.
    if (firstBeat < 0) {
      firstIndexInBar += (int)chords.size();
      continue;
    }

    const int barSteps = (lastBeat - firstBeat + 1) * kStepsPerBeat;
    const int fit = juce::jmin((int)chords.size(), barSteps);

    for (int s = 0; s < barSteps; ++s) {
      const int within = chordIndexForBeat(s, barSteps, fit);
      layout.stepToChord[(size_t)(firstBeat * kStepsPerBeat + s)] =
          firstIndexInBar + juce::jlimit(0, fit - 1, within);
    }
    firstIndexInBar += (int)chords.size();
  }

  return layout;
}

const Chord &chordAtStep(const Layout &layout, int step) {
  static const Chord fallback{};
  if (layout.empty())
    return fallback;

  const int steps = layout.steps();
  const int s = ((step % steps) + steps) % steps;
  const int idx = layout.stepToChord[(size_t)s];
  if (idx < 0 || idx >= (int)layout.chords.size())
    return fallback;
  return layout.chords[(size_t)idx];
}

namespace {

double weightOfTone(int semitonesAboveRoot) {
  const int i = ((semitonesAboveRoot % 12) + 12) % 12;
  if (i == 0)
    return kRootWeight;
  if (i == 3 || i == 4)
    return kThirdWeight;
  if (i == 6 || i == 7 || i == 8)
    return kFifthWeight;
  if (i == 9 || i == 10 || i == 11)
    return kSeventhWeight;
  return kExtensionWeight; // seconds, fourths, and the ninths above them
}

// How common a mode is, before any evidence. Major and minor are most of the
// music anybody plays, and a mode that only differs from one of them by a
// single note should have to earn the difference.
double priorForMode(MusicalKey::Mode mode) {
  switch (mode) {
  case MusicalKey::Mode::Major:
  case MusicalKey::Mode::Minor:
    return 3.0;
  default:
    return 1.5;
  }
}

bool inScale(const MusicalKey::Key &key, int pitchClass) {
  const int *steps = MusicalKey::scaleSteps(key.mode);
  for (int d = 0; d < MusicalKey::kScaleDegrees; ++d)
    if (wrapPitchClass(key.tonic + steps[d]) == pitchClass)
      return true;
  return false;
}

// Which scale degree a pitch class is, or -1 if it is not in the scale.
int degreeOf(const MusicalKey::Key &key, int pitchClass) {
  const int *steps = MusicalKey::scaleSteps(key.mode);
  for (int d = 0; d < MusicalKey::kScaleDegrees; ++d)
    if (wrapPitchClass(key.tonic + steps[d]) == pitchClass)
      return d;
  return -1;
}

double scoreKey(const MusicalKey::Key &key, const Progression &chords) {
  // Averaged per chord, so a long chart and a short one are on the same scale
  // and the function terms below mean the same thing in both.
  double content = 0.0;
  for (const auto &chord : chords) {
    for (int i = 0; i < chord.toneCount; ++i) {
      const int tone = chord.tones[(size_t)i];
      const double w = weightOfTone(tone);
      content += inScale(key, wrapPitchClass(chord.root + tone)) ? w : -w;
    }
  }
  content /= (double)chords.size();

  // Content alone cannot separate a key from its relative -- they are the same
  // seven notes -- so what a chord DOES has to break the tie.
  double function = 0.0;
  if (chords.front().root == key.tonic)
    function += 3.0;
  if (chords.back().root == key.tonic)
    function += 4.0; // a loop resolves onto its tonic, and that is stronger
  for (const auto &chord : chords) {
    // A major-quality chord on the fifth degree: the dominant, and the single
    // clearest statement a progression makes about where home is.
    if (degreeOf(key, chord.root) != 4)
      continue;
    const bool majorThird = chord.toneCount > 1 && chord.tones[1] == 4;
    if (majorThird)
      function += chord.toneCount > 3 && chord.tones[3] == 10 ? 3.0 : 2.0;
  }

  return content + function + priorForMode(key.mode);
}

} // namespace

KeyGuess inferKey(const Progression &chords) {
  KeyGuess best;
  if (chords.empty())
    return best;

  // Ionian and Aeolian are the same notes as major and minor, so offering them
  // as separate candidates would only split the vote between two names for one
  // answer. Phrygian and Locrian are left out for the same reason the prior
  // exists: they are rare enough that a chart which fits one also fits
  // something likelier.
  const MusicalKey::Mode modes[] = {
      MusicalKey::Mode::Major, MusicalKey::Mode::Minor,
      MusicalKey::Mode::Dorian, MusicalKey::Mode::Mixolydian};

  double runnerUp = 0.0;
  bool haveBest = false, haveRunnerUp = false;

  for (int tonic = 0; tonic < 12; ++tonic) {
    for (auto mode : modes) {
      MusicalKey::Key key;
      key.valid = true;
      key.tonic = tonic;
      key.mode = mode;
      key.flat = MusicalKey::usesFlats(tonic, mode);

      const double score = scoreKey(key, chords);
      if (!haveBest || score > best.score) {
        if (haveBest) {
          runnerUp = best.score;
          haveRunnerUp = true;
        }
        best.key = key;
        best.score = score;
        haveBest = true;
      } else if (!haveRunnerUp || score > runnerUp) {
        runnerUp = score;
        haveRunnerUp = true;
      }
    }
  }

  best.margin = best.score - runnerUp;
  best.confident = best.score > 0.0 && best.margin >= kConfidentMargin;
  return best;
}

int voicingDistance(const Voicing &a, const Voicing &b) {
  if (a.empty() || b.empty())
    return 0;

  const int shared = juce::jmin((int)a.size(), (int)b.size());
  int cost = 0;
  for (int i = 0; i < shared; ++i)
    cost += std::abs(a[(size_t)i] - b[(size_t)i]);

  // A chord with more voices than the last one has to put the extra somewhere,
  // and the cheapest honest answer is how far it is from the nearest note that
  // was already sounding. Without this a triad-to-seventh move would look free.
  const Voicing &longer = a.size() > b.size() ? a : b;
  const Voicing &shorter = a.size() > b.size() ? b : a;
  for (size_t i = (size_t)shared; i < longer.size(); ++i) {
    int nearest = std::abs(longer[i] - shorter[0]);
    for (int n : shorter)
      nearest = juce::jmin(nearest, std::abs(longer[i] - n));
    cost += nearest;
  }
  return cost;
}

namespace {

// Every way of voicing one chord inside the register: each inversion, at each
// octave that fits.
std::vector<Voicing> voicingsOf(const Chord &chord) {
  std::vector<int> pcs;
  for (int i = 0; i < chord.toneCount; ++i) {
    const int pc = wrapPitchClass(chord.root + chord.tones[(size_t)i]);
    if (std::find(pcs.begin(), pcs.end(), pc) == pcs.end())
      pcs.push_back(pc);
  }
  if (pcs.empty())
    return {};
  std::sort(pcs.begin(), pcs.end());

  std::vector<Voicing> out;
  for (size_t rotation = 0; rotation < pcs.size(); ++rotation) {
    // Stack the chord from this inversion's bass note upwards, each note in the
    // first octave above the one below it.
    Voicing shape;
    int previous = -1;
    for (size_t i = 0; i < pcs.size(); ++i) {
      int note = pcs[(rotation + i) % pcs.size()];
      while (note <= previous)
        note += 12;
      shape.push_back(note);
      previous = note;
    }

    for (int octave = 0; octave < 11; ++octave) {
      Voicing v;
      v.reserve(shape.size());
      for (int n : shape)
        v.push_back(n + 12 * octave);
      if (v.front() >= kVoiceLow && v.back() <= kVoiceHigh)
        out.push_back(std::move(v));
    }
  }

  // A voicing too wide to fit the register still has to be playable, so take
  // the lowest placement of the closest inversion rather than returning none.
  if (out.empty()) {
    Voicing v;
    int previous = kVoiceLow - 1;
    for (int pc : pcs) {
      int note = pc;
      while (note <= previous)
        note += 12;
      v.push_back(note);
      previous = note;
    }
    out.push_back(std::move(v));
  }
  return out;
}

} // namespace

std::vector<Voicing> voiceLead(const Progression &chords) {
  std::vector<Voicing> out;
  if (chords.empty())
    return out;

  std::vector<std::vector<Voicing>> candidates;
  candidates.reserve(chords.size());
  for (const auto &c : chords)
    candidates.push_back(voicingsOf(c));

  for (const auto &set : candidates)
    if (set.empty())
      return out; // nothing voiceable; the caller falls back

  const size_t n = chords.size();
  if (n == 1) {
    // One chord is a loop of one: nothing to lead to, so take the inversion
    // nearest the middle of the register.
    const int centre = (kVoiceLow + kVoiceHigh) / 2;
    const Voicing *best = &candidates[0].front();
    int bestCost = -1;
    for (const auto &v : candidates[0]) {
      const int cost = std::abs((v.front() + v.back()) / 2 - centre);
      if (bestCost < 0 || cost < bestCost) {
        bestCost = cost;
        best = &v;
      }
    }
    out.push_back(*best);
    return out;
  }

  // For each way of voicing the first chord, walk the rest keeping the cheapest
  // path to every candidate, then close the loop back onto where we started.
  //
  // Trying every start is what makes this exhaustive rather than conditioned on
  // one arbitrary voicing of the first chord. Measured honestly, it has never
  // yet changed the answer: over 244 progressions -- every diatonic seventh
  // loop of twelve tonics in four modes, plus chromatic ones -- fixing the
  // start to the lowest voicing gave identical results, because the rest of the
  // chart can always accommodate it. It is kept because it costs a few hundred
  // integer operations and it is the difference between "optimal" and "optimal
  // given where we happened to begin". Costing the wrap, on the other hand,
  // does change the answer, and is what the tests pin.
  int bestTotal = -1;
  std::vector<size_t> bestPath;

  for (size_t start = 0; start < candidates[0].size(); ++start) {
    std::vector<int> cost(candidates[0].size(), -1);
    cost[start] = 0;
    std::vector<std::vector<size_t>> from(n);

    std::vector<int> previous = cost;
    for (size_t i = 1; i < n; ++i) {
      std::vector<int> next(candidates[i].size(), -1);
      from[i].assign(candidates[i].size(), 0);
      for (size_t b = 0; b < candidates[i].size(); ++b) {
        for (size_t a = 0; a < candidates[i - 1].size(); ++a) {
          if (previous[a] < 0)
            continue;
          const int total =
              previous[a] +
              voicingDistance(candidates[i - 1][a], candidates[i][b]);
          if (next[b] < 0 || total < next[b]) {
            next[b] = total;
            from[i][b] = a;
          }
        }
      }
      previous = std::move(next);
    }

    for (size_t last = 0; last < candidates[n - 1].size(); ++last) {
      if (previous[last] < 0)
        continue;
      const int total =
          previous[last] +
          voicingDistance(candidates[n - 1][last], candidates[0][start]);
      if (bestTotal >= 0 && total >= bestTotal)
        continue;

      bestTotal = total;
      bestPath.assign(n, 0);
      size_t at = last;
      for (size_t i = n - 1; i > 0; --i) {
        bestPath[i] = at;
        at = from[i][at];
      }
      bestPath[0] = start;
    }
  }

  if (bestPath.empty())
    return out;

  out.reserve(n);
  for (size_t i = 0; i < n; ++i)
    out.push_back(candidates[i][bestPath[i]]);
  return out;
}

bool changesAtStep(const Layout &layout, int step) {
  if (layout.empty())
    return false;

  const int steps = layout.steps();
  const int s = ((step % steps) + steps) % steps;
  if (s == 0)
    return true; // an interval opens on its first chord
  return layout.stepToChord[(size_t)s] != layout.stepToChord[(size_t)(s - 1)];
}

} // namespace Harmony
