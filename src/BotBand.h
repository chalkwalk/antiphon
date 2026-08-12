#pragma once

#include "Harmony.h"
#include "MusicalKey.h"
#include <cstdint>
#include <vector>

// What each bot plays, for one interval.
//
// Deterministic: the same settings and the same seed always give the same
// interval, which is what makes it testable and what makes "roll for a new one"
// a meaningful thing to ask for.
//
// The progression fills exactly one interval (see Harmony), so an interval is a
// complete musical unit. That is also why nothing here depends on the interval
// index by default: a band repeats, and a listener whose phase is its own still
// hears whole bars.

namespace BotBand {

// A full rhythm section plus a lead, so any one part can be muted or sent home
// and played by a person instead. That is the point of the band: it supports a
// drummer or a rhythm guitarist as readily as it supports someone soloing.
enum class Voice { Drums, Bass, Keys, Lead };

inline constexpr int kNumVoices = 4;

// The shape a melodic phrase traces across an interval. Ported from
// chalkwalk/seq_play src/core/MelodyGen.h, whose spine is worth having: pitch
// follows a contour, and metric strength decides which notes may sit where.
enum class Contour { Rise, Fall, Arch, Walk };

const char *voiceName(Voice v);

struct Settings {
  int bpm = 120;
  int bpi = 8;
  double sampleRate = 48000.0;
  MusicalKey::Key key;
  Harmony::Progression progression;

  // Rerolled by "shake". Salted per voice inside, so one seed does not give
  // every instrument the same shape -- the mistake seq_play's MelodyGen
  // documents having made and fixed.
  std::uint32_t seed = 1;
};

// Fills a complete, valid Settings for a key, using the mode-aware default
// progression when none has been announced.
Settings defaults(const MusicalKey::Key &key, int bpm, int bpi,
                  double sampleRate, std::uint32_t seed);

// The rhythmic figure a voice plays, derived from the salted seed. Exposed
// because it is worth asserting exactly, where the audio can only be measured
// statistically.
struct Figure {
  int steps = 8;    // resolution over one interval
  int pulses = 3;   // onsets
  int rotation = 0; // displacement
  int accents = 1;
};

Figure figureFor(Voice voice, const Settings &s);

// How strongly a beat is stressed by the metre: the downbeat of the interval
// is highest, then the halves, then the quarters, then the beat, and an
// off-beat subdivision lowest.
//
// This is the spine of the melodic writing, and the reason it sounds composed
// rather than sprinkled: strong beats take strong notes -- chord tones, longer
// -- and weak beats take passing notes. `step` is in eighths, so twice the
// beat resolution.
int metricStrength(int step, int bpi);

// The lead's line for one interval, as MIDI notes with -1 for a rest, one
// entry per eighth. Exposed so the note choices can be asserted exactly,
// where the audio can only be measured.
std::vector<int> leadLine(const Settings &s, int intervalIndex);

// Renders one interval into `out`, which must hold `numSamples` frames and is
// added to rather than overwritten. Mono: the caller decides how it is placed.
void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                    float *out, int numSamples);

// The seed a voice actually uses. Salting matters enough to be testable on its
// own: without it, one seed makes the bass and the drums the same shape.
std::uint32_t saltedSeed(Voice voice, std::uint32_t seed);

} // namespace BotBand
