#pragma once

#include "jambot/BotBand.h"
#include "jambot/BotVoice.h"

#include <string>
#include <vector>

// Every tunable number in the band, as data you can walk.
//
// The synthesis in BotVoice.h is written for a reader: named fields, comments
// explaining why each one is what it is. That is the right shape for code and
// the wrong shape for a control surface, which needs to ask "what knobs are
// there" without knowing the answer in advance.
//
// So this file is the other view of the same numbers. A small table per patch
// maps a name to a pointer-to-member for the VALUE and a pointer-to-member for
// its RANGE, and `knobsFor` turns those into a flat list bound to a live patch.
// Nothing is duplicated: the ranges come from the same tables the seed draws
// from, so a slider's limits and a seed's sweet spot cannot drift apart. That
// was the whole problem with tuning this by hand -- two sets of numbers, one in
// the code and one in somebody's head.
//
// JUCE-free, so the file format is testable in the headless suite. The band lab
// puts a GUI on top; nothing here knows that.

namespace BandPatch {

// One control on one patch: where its value lives and where its limits live.
template <typename PatchT, typename RangesT> struct Field {
  const char *name;
  double PatchT::*value;
  BotVoice::Range RangesT::*range;
};

// A control bound to a particular patch, which is what a slider needs.
//
// Both are pointers because both are editable. The value is the obvious one;
// the range matters just as much, because "the seed may only pick inside this"
// is a claim about the instrument that is arrived at by listening to both ends
// of it, and the person doing the listening needs to be able to move the ends.
struct Knob {
  std::string name;
  double *value = nullptr;
  BotVoice::Range *range = nullptr;
};

using Fields = std::vector<Knob>;

#define ANTIPHON_FIELD(patch, ranges, member)                                  \
  { #member, &patch::member, &ranges::member }

inline constexpr Field<BotVoice::PadPatch, BotVoice::PadRanges> kPadFields[] = {
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, detuneCents),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, driftCents),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, pulseWidth),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, noiseLevel),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, cutoffPartials),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, resonance),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, envAmount),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, envAttack),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, envDecay),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, envSustain),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, attackSeconds),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, releaseSeconds),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, drive),
    ANTIPHON_FIELD(BotVoice::PadPatch, BotVoice::PadRanges, movementHz),
};

inline constexpr Field<BotVoice::BassPatch, BotVoice::BassRanges>
    kBassFields[] = {
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, pickPosition),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, brightFloor),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, brightSpan),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, decaySeconds),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, contact),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, toneFloor),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, toneSpan),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, bodyHz),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, bodyMix),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, cabinetHz),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, cabinetDrive),
        ANTIPHON_FIELD(BotVoice::BassPatch, BotVoice::BassRanges, gain),
};

inline constexpr Field<BotVoice::EPianoPatch, BotVoice::EPianoRanges>
    kEPianoFields[] = {
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, tineDecay),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, barkGain),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, barkDecay),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, pingGain),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges,
                       hammerLevel),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges,
                       hammerPartials),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, barMix),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, ampCutoff),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges,
                       ampDriveFloor),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges,
                       ampDriveSpan),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, tremoloHz),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges,
                       tremoloDepth),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, release),
        ANTIPHON_FIELD(BotVoice::EPianoPatch, BotVoice::EPianoRanges, gain),
};

inline constexpr Field<BotVoice::GuitarPatch, BotVoice::GuitarRanges>
    kGuitarFields[] = {
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       pickPosition),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       brightFloor),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       brightSpan),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       decaySeconds),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, toneFloor),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, toneSpan),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       toneOpenFloor),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges,
                       toneOpenSpan),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, toneFall),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, pickLevel),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, pickHz),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, airHz),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, airMix),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, topHz),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, topMix),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, boxCutoff),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, boxDrive),
        ANTIPHON_FIELD(BotVoice::GuitarPatch, BotVoice::GuitarRanges, gain),
};

inline constexpr Field<BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges>
    kSynthFields[] = {
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       pulseWidth),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       partialsFloor),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       partialsSpan),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       resonance),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       envAmount),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       envDecay),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       preDrive),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       postDrive),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       postGain),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       vibratoHz),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       vibratoDepth),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       vibratoOnset),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       attack),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       release),
        ANTIPHON_FIELD(BotVoice::SynthLeadPatch, BotVoice::SynthLeadRanges,
                       gain),
};

#undef ANTIPHON_FIELD

// The whole band, as one editable object.
//
// The patches are the ones the render functions take, so what the lab is
// holding is literally what the room would play -- there is no translation
// step to get wrong. The ranges are held alongside them because they are being
// edited too: what comes out of a tuning session is not just "the pad's cutoff
// should be 9" but "the pad's cutoff should be somewhere between 7 and 11", and
// only the second of those is a thing a seed can use.
//
// Each SELECTION gets its own storage -- three keyboards, three bass
// techniques, three lead instruments -- rather than one patch that changes
// meaning when the selector moves. A session spent on the brass patch must
// still be there when you come back from checking the strings one, and a saved
// file has to be able to carry all of it.
struct Band {
  static constexpr int kSelections = 3;

  BotVoice::PadCharacter keysCharacter = BotVoice::PadCharacter::Poly;
  BotVoice::PadPatch keys[kSelections];
  BotVoice::PadRanges keysRanges[kSelections];

  BotVoice::BassTechnique bassTechnique = BotVoice::BassTechnique::Fingered;
  BotVoice::BassPatch bass[kSelections];
  BotVoice::BassRanges bassRanges[kSelections];

  BotVoice::LeadPatch lead;
  BotVoice::EPianoRanges epianoRanges;
  BotVoice::GuitarRanges guitarRanges;
  BotVoice::SynthLeadRanges synthRanges;

  // Drums, Bass, Keys, Lead -- the same order as BotBand::Voice, so an index
  // can be cast between them.
  double trim[BotBand::kNumVoices] = {1.71, 1.67, 0.32, 1.15};

  BotVoice::PadPatch &keysPatch() { return keys[(int)keysCharacter]; }
  BotVoice::BassPatch &bassPatch() { return bass[(int)bassTechnique]; }
};

// The band as the code currently ships it, which is where a tuning session
// starts.
//
// The patches take the MIDDLE of each range rather than a seeded draw, because
// somebody tuning wants the centre of the sweet spot in front of them and not
// one arbitrary point inside it. That also makes the lab's starting position
// reproducible, which a seeded one would not be.
inline Band defaults();

// Rebuild a patch from the middle of its ranges. What "reset" on a panel does,
// and what makes an edited range immediately audible.
inline void centre(BotVoice::PadPatch &p, const BotVoice::PadRanges &r,
                   BotVoice::PadCharacter character) {
  p.character = character;
  p.detuneCents = r.detuneCents.mid();
  p.driftCents = r.driftCents.mid();
  p.pulseWidth = r.pulseWidth.mid();
  p.noiseLevel = r.noiseLevel.mid();
  p.cutoffPartials = r.cutoffPartials.mid();
  p.resonance = r.resonance.mid();
  p.envAmount = r.envAmount.mid();
  p.envAttack = r.envAttack.mid();
  p.envDecay = r.envDecay.mid();
  p.envSustain = r.envSustain.mid();
  p.attackSeconds = r.attackSeconds.mid();
  p.releaseSeconds = r.releaseSeconds.mid();
  p.drive = r.drive.mid();
  p.movementHz = r.movementHz.mid();
  p.secondIsPulse = r.secondIsPulse;
  p.level = r.level;
}

inline Band defaults() {
  Band b;
  for (int c = 0; c < Band::kSelections; ++c) {
    b.keysRanges[c] = BotVoice::padRanges((BotVoice::PadCharacter)c);
    centre(b.keys[c], b.keysRanges[c], (BotVoice::PadCharacter)c);
  }
  for (int t = 0; t < Band::kSelections; ++t)
    b.bass[t] = BotVoice::bassPatchFor((BotVoice::BassTechnique)t);
  return b;
}

// The knobs for one voice, bound to this band.
//
// `voice` is a BotBand::Voice; each reports the knobs of whichever selection is
// currently showing, because the other two are not being heard and a panel of
// controls that do nothing is worse than a smaller panel.
inline Fields knobsFor(Band &band, BotBand::Voice voice) {
  Fields out;

  auto add = [&out](const auto *table, size_t count, auto &patch,
                    auto &ranges) {
    for (size_t i = 0; i < count; ++i)
      out.push_back({table[i].name, &(patch.*(table[i].value)),
                     &(ranges.*(table[i].range))});
  };

  const int keysIndex = (int)band.keysCharacter;
  const int bassIndex = (int)band.bassTechnique;

  switch (voice) {
  case BotBand::Voice::Keys:
    add(kPadFields, sizeof(kPadFields) / sizeof(kPadFields[0]),
        band.keys[keysIndex], band.keysRanges[keysIndex]);
    break;
  case BotBand::Voice::Bass:
    add(kBassFields, sizeof(kBassFields) / sizeof(kBassFields[0]),
        band.bass[bassIndex], band.bassRanges[bassIndex]);
    break;
  case BotBand::Voice::Lead:
    switch (band.lead.instrument) {
    case BotVoice::LeadInstrument::EPiano:
      add(kEPianoFields, sizeof(kEPianoFields) / sizeof(kEPianoFields[0]),
          band.lead.epiano, band.epianoRanges);
      break;
    case BotVoice::LeadInstrument::Guitar:
      add(kGuitarFields, sizeof(kGuitarFields) / sizeof(kGuitarFields[0]),
          band.lead.guitar, band.guitarRanges);
      break;
    case BotVoice::LeadInstrument::Synth:
      add(kSynthFields, sizeof(kSynthFields) / sizeof(kSynthFields[0]),
          band.lead.synth, band.synthRanges);
      break;
    }
    break;
  case BotBand::Voice::Drums:
    // Not yet parameterised. The kit is three voices, a room and a bus stage,
    // and it is the part nobody has complained about.
    break;
  }

  return out;
}

// The name a voice's current selection goes by -- "strings", "fingered",
// "guitar" -- so a saved file says which instrument the numbers describe.
inline std::string selectionName(const Band &band, BotBand::Voice voice) {
  switch (voice) {
  case BotBand::Voice::Keys:
    return BotVoice::padCharacterName(band.keysCharacter);
  case BotBand::Voice::Bass:
    return BotVoice::bassTechniqueName(band.bassTechnique);
  case BotBand::Voice::Lead:
    switch (band.lead.instrument) {
    case BotVoice::LeadInstrument::EPiano:
      return "epiano";
    case BotVoice::LeadInstrument::Guitar:
      return "guitar";
    case BotVoice::LeadInstrument::Synth:
      return "synth";
    }
    return "synth";
  case BotBand::Voice::Drums:
    return "kit";
  }
  return "";
}

// ---------------------------------------------------------------------------
// The file a tuning session produces.
//
// Plain text, one knob per line, VALUE THEN RANGE:
//
//   keys.strings.cutoffPartials  12.400  10.000  16.000
//
// Both halves matter and they answer different questions. The value is what
// sounded right; the range is what the seed is allowed to do around it, which
// is the thing a listening session is uniquely able to establish and which no
// amount of staring at the code will give you.
//
// Deliberately not JSON. It is meant to be read in a diff and pasted into a
// message, and a format with no punctuation survives both.
// ---------------------------------------------------------------------------

std::string write(Band &band);
bool read(const std::string &text, Band &band, std::string &error);

} // namespace BandPatch
