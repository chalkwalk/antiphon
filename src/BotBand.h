#pragma once

#include "BotVoice.h"
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

  // Bars, not a flat list: a bar holding two chords is half the time each, and
  // that is the difference between playing what was written and playing the
  // same chords evenly spread (see Harmony::layoutChart).
  Harmony::Chart chart;

  // Rerolled by "shake". Salted per voice inside, so one seed does not give
  // every instrument the same shape -- the mistake seq_play's MelodyGen
  // documents having made and fixed.
  std::uint32_t seed = 1;

  // Which instrument the soloist is holding, or negative for whatever the seed
  // chose, which is the default.
  //
  // It SURVIVES a shake, deliberately. Shake rerolls what the band plays, and
  // somebody who asked for a guitar because they came to practise keyboards
  // has not changed their mind about that by asking for a different tune.
  //
  // This is the only thing about the band a player can pin, and it is the one
  // worth pinning: the lead is the part you mute so you can play it, and
  // whether it is in your way depends on what you brought. Everything else is
  // a property of the seed and stays that way, because a band with a dozen
  // settings is a band you configure instead of play with.
  int leadOverride = -1;

  // Explicit patches and trims, for the band lab and nothing else.
  //
  // The room never sets these: a bot's sound is a function of its seed, which
  // is what makes "shake" meaningful and a session reproducible. But a person
  // tuning by ear needs to hear a number they just moved, not the nearest one
  // a seed happened to pick, so the render path takes an override when one is
  // offered and derives everything as usual when it is not.
  //
  // Kept here rather than as extra arguments so that every caller -- the bots,
  // the tests, both tools -- keeps working unchanged, and so that "what the
  // room would play" and "what the lab is playing" differ by exactly one flag.
  bool usePatchOverrides = false;
  BotVoice::PadPatch keysPatchOverride;
  BotVoice::BassPatch bassPatchOverride;
  BotVoice::LeadPatch leadPatchOverride;

  // Negative for "use the measured constant", which is the normal case.
  double trimOverride[4] = {-1.0, -1.0, -1.0, -1.0};
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

// The other half of the coupling: how strong a NOTE is against a chord.
//
// MelodyGen pairs beat strength with note strength -- strong beats take strong
// notes -- and porting only the first axis is why an early version of this
// sounded fine in major and wrong in minor. There, the weak-beat pool included
// the flat sixth, and since notes are held until the next one it sat on the
// clash rather than passing through it.
//
// The tiers are derived from the chord rather than listed per mode, using the
// avoid-note rule: a scale tone a semitone above a chord tone is the one that
// clashes. That gives the flat sixth in Aeolian over i, the fourth in Ionian
// over I, and the flat second in Phrygian -- and correctly leaves Lydian's
// sharp fourth alone, since it is a whole tone above the third and is the
// characteristic note of the mode rather than a note to handle carefully.
//
//   0  a chord tone
//   1  a scale tone that sits comfortably
//   2  a semitone above a chord tone: colour, and only in passing
int noteTier(int midiNote, const Harmony::Chord &chord);

// The lead's line for one interval, as MIDI notes with -1 for a rest, one
// entry per eighth. Exposed so the note choices can be asserted exactly,
// where the audio can only be measured.
std::vector<int> leadLine(const Settings &s, int intervalIndex);

// How the bass player plays, chosen once from the seed and then held for the
// whole session.
BotVoice::BassTechnique bassTechnique(const Settings &s);

// The patch each voice is actually rendered with: the seed's, or the override
// if one has been set.
BotVoice::BassPatch bassPatch(const Settings &s);
BotVoice::LeadPatch leadPatch(const Settings &s);

// Which instrument the lead is playing: the seed's choice, unless a player has
// asked for one.
BotVoice::LeadInstrument leadInstrument(const Settings &s);

// The patch the keyboard player is using this session, chosen from the seed.
//
// Exposed because it is worth asserting exactly -- every field has a range it
// must stay inside, and that constraint is what makes a seed-chosen timbre safe
// rather than a lottery -- and because it is a thing a bot can eventually be
// asked about in words.
BotVoice::PadPatch keysPatch(const Settings &s);

// Whether a voice fills both channels or only the left.
//
// Two voices, and each has a physical reason rather than a width effect. The
// kit is heard through a pair of overheads, so its early reflections differ
// side to side and that difference IS the image. The keyboard is heard through
// the stereo chorus on its own output, which is a front-panel switch on the
// instruments it is modelled on. Bass and lead are close-miked and stay mono,
// so the listener's pan control decides where they sit.
bool isStereo(Voice voice);

// Renders one interval into `left`, and into `right` when the voice is stereo.
// Both must hold `numSamples` frames.
//
// The buffers must be this voice's own and cleared by the caller. Notes within
// a voice add into them so overlapping ones mix, but the kit finishes by
// shaping the whole buffer as a bus, which would shape anything else that was
// already there.
//
// `right` may be null, which renders a stereo voice's left channel only. A
// mono voice never touches `right` at all, so the caller mirrors it.
void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                    float *left, float *right, int numSamples);

// Mono, for callers that do not care: the same thing with no right channel.
inline void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                           float *out, int numSamples) {
  renderInterval(voice, s, intervalIndex, out, nullptr, numSamples);
}

// The seed a voice actually uses. Salting matters enough to be testable on its
// own: without it, one seed makes the bass and the drums the same shape.
std::uint32_t saltedSeed(Voice voice, std::uint32_t seed);

} // namespace BotBand
