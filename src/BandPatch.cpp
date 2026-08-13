#include "BandPatch.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace BandPatch {

namespace {

// Enough digits that a round trip is exact for anything a slider produces, and
// not so many that the file stops being readable.
std::string number(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

void writeVoice(std::ostringstream &out, Band &band, BotBand::Voice voice) {
  const std::string prefix =
      std::string(BotBand::voiceName(voice)) + "." + selectionName(band, voice);

  for (const auto &knob : knobsFor(band, voice))
    out << prefix << "." << knob.name << "  " << number(*knob.value) << "  "
        << number(knob.range->lo) << "  " << number(knob.range->hi) << "\n";
}

// Find a knob by its full dotted name, across every voice and every selection.
//
// Deliberately searches rather than requiring the file to arrive in order: a
// file is something a person edits, and one that breaks because two lines were
// swapped is a file nobody trusts.
//
// The selection is moved to reach a knob and put back afterwards, which is safe
// only because every selection has its own storage -- the pointers `knobsFor`
// hands back while the selector is parked on "brass" go to the brass patch and
// nowhere else.
bool applyLine(Band &band, const std::string &name, double value, double lo,
               double hi, bool hasRange) {
  const auto keysWas = band.keysCharacter;
  const auto bassWas = band.bassTechnique;
  const auto leadWas = band.lead.instrument;

  bool found = false;

  for (int v = 0; v < BotBand::kNumVoices && !found; ++v) {
    const auto voice = (BotBand::Voice)v;
    const int selections = voice == BotBand::Voice::Drums ? 1 : Band::kSelections;

    for (int selection = 0; selection < selections && !found; ++selection) {
      switch (voice) {
      case BotBand::Voice::Keys:
        band.keysCharacter = (BotVoice::PadCharacter)selection;
        break;
      case BotBand::Voice::Bass:
        band.bassTechnique = (BotVoice::BassTechnique)selection;
        break;
      case BotBand::Voice::Lead:
        band.lead.instrument = (BotVoice::LeadInstrument)selection;
        break;
      case BotBand::Voice::Drums:
        break;
      }

      const std::string prefix = std::string(BotBand::voiceName(voice)) + "." +
                                 selectionName(band, voice) + ".";
      if (name.size() <= prefix.size() ||
          name.compare(0, prefix.size(), prefix) != 0)
        continue;

      const std::string leaf = name.substr(prefix.size());
      for (auto &knob : knobsFor(band, voice))
        if (knob.name == leaf) {
          *knob.value = value;
          if (hasRange) {
            knob.range->lo = lo;
            knob.range->hi = hi;
          }
          found = true;
          break;
        }
    }
  }

  band.keysCharacter = keysWas;
  band.bassTechnique = bassWas;
  band.lead.instrument = leadWas;
  return found;
}

} // namespace

std::string write(Band &band) {
  std::ostringstream out;
  out << "# antiphon band patch\n"
      << "# name  value  range-low  range-high\n"
      << "#\n"
      << "# The value is what sounded right. The range is what a seed may pick\n"
      << "# inside, which is the part only a listening session can settle.\n\n";

  // Every selection of every voice, not just the one on screen. A session that
  // saved only what happened to be selected would silently drop the work done
  // on the other two instruments.
  const auto keysWas = band.keysCharacter;
  const auto bassWas = band.bassTechnique;
  const auto leadWas = band.lead.instrument;

  for (int c = 0; c < Band::kSelections; ++c) {
    band.keysCharacter = (BotVoice::PadCharacter)c;
    writeVoice(out, band, BotBand::Voice::Keys);
    out << "\n";
  }
  band.keysCharacter = keysWas;

  for (int t = 0; t < Band::kSelections; ++t) {
    band.bassTechnique = (BotVoice::BassTechnique)t;
    writeVoice(out, band, BotBand::Voice::Bass);
    out << "\n";
  }
  band.bassTechnique = bassWas;

  for (int i = 0; i < Band::kSelections; ++i) {
    band.lead.instrument = (BotVoice::LeadInstrument)i;
    writeVoice(out, band, BotBand::Voice::Lead);
    out << "\n";
  }
  band.lead.instrument = leadWas;

  for (int v = 0; v < BotBand::kNumVoices; ++v)
    out << "trim." << BotBand::voiceName((BotBand::Voice)v) << "  "
        << number(band.trim[v]) << "\n";

  return out.str();
}

bool read(const std::string &text, Band &band, std::string &error) {
  std::istringstream in(text);
  std::string line;
  int lineNumber = 0;
  int applied = 0;

  while (std::getline(in, line)) {
    ++lineNumber;

    const auto hash = line.find('#');
    if (hash != std::string::npos)
      line = line.substr(0, hash);

    std::istringstream fields(line);
    std::string name;
    if (!(fields >> name))
      continue;

    double value = 0.0, lo = 0.0, hi = 0.0;
    if (!(fields >> value)) {
      error = "line " + std::to_string(lineNumber) + ": " + name +
              " has no value";
      return false;
    }
    const bool hasRange = (fields >> lo) && (fields >> hi);

    if (name.compare(0, 5, "trim.") == 0) {
      const std::string leaf = name.substr(5);
      bool found = false;
      for (int v = 0; v < BotBand::kNumVoices; ++v)
        if (leaf == BotBand::voiceName((BotBand::Voice)v)) {
          band.trim[v] = value;
          found = true;
          ++applied;
        }
      if (!found) {
        error = "line " + std::to_string(lineNumber) + ": no voice called " +
                leaf;
        return false;
      }
      continue;
    }

    if (!applyLine(band, name, value, lo, hi, hasRange)) {
      error = "line " + std::to_string(lineNumber) + ": nothing called " + name;
      return false;
    }
    ++applied;
  }

  if (applied == 0) {
    error = "nothing in this file was a setting";
    return false;
  }

  error.clear();
  return true;
}

} // namespace BandPatch
