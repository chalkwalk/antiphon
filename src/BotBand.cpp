#include "BotBand.h"

#include "BotDsp.h"
#include "BotVoice.h"
#include <chalkwalk/music/Euclidean.h>
#include <algorithm>

namespace BotBand {

namespace {

// A cheap integer hash, so a salted seed is unrecognisably different from its
// neighbour rather than one greater than it.
std::uint32_t mix(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

// A small deterministic generator, so choices are reproducible from the seed.
struct Rng {
  std::uint32_t state;
  explicit Rng(std::uint32_t s) : state(s | 1u) {}

  std::uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }

  // Inclusive.
  int range(int lo, int hi) {
    if (hi <= lo)
      return lo;
    return lo + (int)(next() % (std::uint32_t)(hi - lo + 1));
  }
};

int samplesPerBeat(const Settings &s) {
  if (s.bpm <= 0)
    return 0;
  return (int)(s.sampleRate * 60.0 / (double)s.bpm);
}

// The chart resolved onto this interval's grid. Every voice works from one of
// these rather than re-deriving the timing, which is what lets a bar hold two
// chords without four places having to agree about what that means.
Harmony::Layout layoutOf(const Settings &s) {
  return Harmony::layoutChart(s.chart, s.bpi);
}

// The kick's figure, needed by the bass as well as the drums: a bass line that
// rolls its own rhythm fights the kick instead of locking to it, which is what
// real bass playing mostly does not do.
Figure kickFigure(const Settings &s) {
  Rng rng(saltedSeed(Voice::Drums, s.seed));
  Figure f;
  f.steps = std::max(1, s.bpi);
  // Sparse enough to leave room, dense enough to be a groove.
  f.pulses = std::min(f.steps, rng.range(3, std::max(3, s.bpi / 2)));
  f.rotation = 0; // the kick lands on the downbeat; everything else moves
  f.accents = std::max(1, f.pulses / 2);
  return f;
}

} // namespace

const char *voiceName(Voice v) {
  switch (v) {
  case Voice::Drums:
    return "Kit";
  case Voice::Bass:
    return "Bass";
  case Voice::Keys:
    return "Keys";
  case Voice::Lead:
    return "Lead";
  }
  return "Bot";
}

int metricStrength(int step, int bpi) {
  if (bpi <= 0)
    return 0;

  const int eighths = bpi * 2;
  const int s = ((step % eighths) + eighths) % eighths;

  if (s == 0)
    return 4;      // the downbeat of the interval
  if (s % 2 != 0)
    return 0;      // an off-beat eighth

  const int beat = s / 2;
  if (beat % 4 == 0)
    return 3;      // the head of a four-beat bar
  if (beat % 2 == 0)
    return 2;      // a half bar
  return 1;        // an ordinary beat
}

std::uint32_t saltedSeed(Voice voice, std::uint32_t seed) {
  // Without this, one seed gives every instrument the same figure -- the bass
  // playing the kick pattern note for note. seq_play's MelodyGen documents
  // hitting exactly this and fixing it the same way.
  return mix(seed ^ (0x9E3779B9U * (std::uint32_t)((int)voice + 1)));
}

Settings defaults(const MusicalKey::Key &key, int bpm, int bpi,
                  double sampleRate, std::uint32_t seed) {
  Settings s;
  s.bpm = bpm;
  s.bpi = bpi;
  s.sampleRate = sampleRate;
  s.key = key;
  s.chart = Harmony::defaultChart(key);
  s.seed = seed;
  return s;
}

Figure figureFor(Voice voice, const Settings &s) {
  switch (voice) {
  case Voice::Drums:
    return kickFigure(s);

  case Voice::Bass: {
    // Twice the kick's density, at twice its resolution -- a bass part has far
    // more notes than there are kicks, and matching the kick one for one made
    // it sound like a second kick drum rather than a part.
    //
    // This figure is only half the answer. Doubling does NOT contain the kick:
    // E(2p, 2s) at step 2j reduces to (2jp) mod s < p, which is not the kick's
    // (jp) mod s < p. An earlier comment here claimed otherwise and the test
    // that checked it disagreed. renderBass therefore takes the UNION of the
    // kick's onsets and this figure's, which is what locking to the kick while
    // playing more notes than it actually means.
    //
    // The count is then nudged to the nearest one COPRIME with the steps,
    // because exactly 2k shares a factor with 2s and so repeats inside the
    // interval -- and a bass figure that repeats is doubling the kick again by
    // another route. Twice four pulses over thirty-two steps has period four:
    // `x...` eight times, a metronome. Nine has period thirty-two.
    //
    // The kick deliberately does NOT get this treatment. A short period is
    // what makes a kick a pulse you can rely on; movement is what a bass wants
    // and a kick does not.
    Rng rng(saltedSeed(Voice::Bass, s.seed));
    const Figure kick = kickFigure(s);
    Figure f;
    f.steps = kick.steps * 2;
    f.pulses = chalkwalk::music::nearestCoprimePulses(f.steps, kick.pulses * 2,
                                               rng.range(0, 1) == 1);
    f.rotation = 0;
    f.accents = std::max(1, f.pulses / 4);
    return f;
  }

  case Voice::Keys: {
    // Not a rhythmic figure: the chord changes are the rhythm. Reported as one
    // pulse per chord so the shape of the answer is the same for every voice.
    Figure f;
    f.steps = std::max(1, s.bpi);
    f.pulses = std::max(1, (int)Harmony::flatten(s.chart).size());
    f.rotation = 0;
    f.accents = 1;
    return f;
  }

  case Voice::Lead: {
    // Eighths, and denser than anything else: a line has to move to be a line.
    Rng rng(saltedSeed(Voice::Lead, s.seed));
    Figure f;
    f.steps = std::max(1, s.bpi * 2);
    f.pulses = std::min(f.steps, rng.range(s.bpi, s.bpi + s.bpi / 2));
    f.rotation = rng.range(0, 3);
    f.accents = std::max(1, f.pulses / 4);
    return f;
  }
  }
  return {};
}

int noteTier(int midiNote, const Harmony::Chord &chord) {
  const int pc = ((midiNote % 12) + 12) % 12;

  for (int t = 0; t < chord.toneCount; ++t) {
    const int tone = (((chord.root + chord.tones[(size_t)t]) % 12) + 12) % 12;
    if (pc == tone)
      return 0;
  }

  for (int t = 0; t < chord.toneCount; ++t) {
    const int tone = (((chord.root + chord.tones[(size_t)t]) % 12) + 12) % 12;
    if (pc == (tone + 1) % 12)
      return 2;
  }

  return 1;
}

// How the lead trades its phrase shape against its own smoothness.
//
// `contour` is the unit: the cost of sitting one semitone away from where the
// shape wants the line. At interval 0 this is exactly the old behaviour --
// take the admissible note nearest the contour, wherever the last one was.
//
// 2 was chosen by measurement rather than taste. At 1 the wide leaps in Bb
// Lydian roughly halve; at 2 they nearly vanish while the contour is still
// clearly audible as rising, falling or arching; at 4 the line starts refusing
// to follow the shape at all and wanders in a narrow band, which is a
// different fault and a more boring one.
//
// DIRECTION IS OFF HERE, and that is a decision rather than an oversight. All
// four of antiphon's contours state a direction of their own -- even Walk,
// which is a fixed sine wiggle rather than the true random walk seq_play has
// -- so the term had almost nothing left to say: it moved the proportion of
// continued runs from 57.6% to 61.5% and did not sound more musical for it,
// while pushing repeats up and occasionally buying a leap. seq_play keeps it,
// because its Walk genuinely has no shape and it has a smoothing dial that
// goes high enough for a line to zigzag without it.
//
// THE REPEAT COST IS NOT CONSTANT. A repeated note over a chord that changed
// is a common tone, and one under a static chord is standing still; the same
// interval, two different musical events. Taxing both equally measured as one
// key keeping 5.2% repeats and sounding right while another kept 1.8% and
// sounded like it was dodging the unison -- and 93% of what survived in the
// second was common tones, so the tax was landing hardest where the repeat was
// most justified. Waived on a chord change, charged otherwise.
inline constexpr int kRepeatCost = 4;

inline constexpr chalkwalk::music::MelodyWeights kLeadWeights{
    /*contour=*/1, /*interval=*/2, /*direction=*/0, /*repeat=*/kRepeatCost};

inline constexpr chalkwalk::music::MelodyWeights kLeadWeightsOverChange{
    /*contour=*/1, /*interval=*/2, /*direction=*/0, /*repeat=*/0};

chalkwalk::music::KeySig toKeySig(const MusicalKey::Key &key) {
  namespace m = chalkwalk::music;

  // Brightness is the fifths window's position, which IS the mode: Lydian
  // brightest through Locrian darkest, one accidental per step. Major and
  // Minor are Ionian and Aeolian -- they differ here only in what a player
  // should be shown, which is `MusicalKey`'s business and not the mask's.
  int brightness = m::kIonian;
  switch (key.mode) {
  case MusicalKey::Mode::Major:
  case MusicalKey::Mode::Ionian:
    brightness = m::kIonian;
    break;
  case MusicalKey::Mode::Minor:
  case MusicalKey::Mode::Aeolian:
    brightness = m::kAeolian;
    break;
  case MusicalKey::Mode::Dorian:
    brightness = m::kDorian;
    break;
  case MusicalKey::Mode::Phrygian:
    brightness = m::kPhrygian;
    break;
  case MusicalKey::Mode::Lydian:
    brightness = m::kLydian;
    break;
  case MusicalKey::Mode::Mixolydian:
    brightness = m::kMixolydian;
    break;
  case MusicalKey::Mode::Locrian:
    brightness = m::kLocrian;
    break;
  }
  return m::KeySig{((key.tonic % 12) + 12) % 12,
                   static_cast<std::int8_t>(brightness),
                   {},
                   m::ScaleType::Diatonic};
}

chalkwalk::music::SoundingChord toSoundingChord(const Harmony::Chord &chord) {
  // `Chord::tones` are semitones above the root and NOT octave-reduced -- a
  // ninth is 14 rather than 2 -- because a chord that names a ninth wants it
  // voiced above the seventh. Ranking is a pitch-class question, so they fold.
  std::vector<int> intervals;
  intervals.reserve(static_cast<std::size_t>(chord.toneCount));
  for (int t = 0; t < chord.toneCount; ++t)
    intervals.push_back(static_cast<int>(chord.tones[static_cast<std::size_t>(t)]));
  return chalkwalk::music::chordOf(chord.root, intervals);
}

// The lead's line.
//
// Three things decide a note, and keeping them apart is the whole design:
//
//   THE POOL       the scale across the lead's register, plus the chord's own
//                  tones, which a borrowed or altered chord can put outside it
//   THE GATE       metric strength -> rankCeiling -> which of those are
//                  ADMISSIBLE here. A hard constraint; a strong beat taking a
//                  clashing chromatic sounds wrong however it was approached
//   THE OBJECTIVE  contour distance plus interval cost -> which admissible
//                  note WINS
//
// The objective is the part that arrived last and the part that makes this
// sound like a melody rather than a sequence of individually defensible
// notes. Before it, each step independently took the admissible note nearest
// the contour, with nothing anywhere that knew where the previous note was --
// so a step whose nearest note was inadmissible could land a long way off and
// nothing objected to the size of the jump. Over forty seeds of Bb Lydian
// that produced 33 moves of an octave or wider; with the interval term it
// produces far fewer, and the ones that remain are fourths, fifths and
// octaves rather than sevenths.
std::vector<int> leadLineFrom(const Settings &s, int intervalIndex,
                              int carryIn) {
  namespace m = chalkwalk::music;

  const int eighths = std::max(1, s.bpi * 2);
  std::vector<int> line((size_t)eighths, -1);
  if (!s.key.valid || s.chart.empty())
    return line;

  const auto layout = layoutOf(s);
  const Figure f = figureFor(Voice::Lead, s);
  Rng rng(saltedSeed(Voice::Lead, s.seed) + 7919u * (std::uint32_t)intervalIndex);
  const auto contour = (Contour)(rng.next() % 4);

  const int centre = 72;
  const int span = 12;
  const auto keySig = toKeySig(s.key);

  // The line's memory, and the only state this loop carries. Negative when
  // nothing came before, which is what makes that note pure contour following
  // -- there is no interval to price.
  m::MelodyState melody;
  melody.lastNote = carryIn;

  // The harmony under the previous SOUNDING note, which is what a common tone
  // is measured against -- not the previous step, which may have been a rest.
  m::SoundingChord lastSounding{};

  for (int step = 0; step < eighths; ++step) {
    if (!m::hit(step, f.steps, f.pulses, f.rotation))
      continue;

    const int strength = metricStrength(step, s.bpi);
    const auto &chord = Harmony::chordAtStep(layout, step);
    const auto sounding = toSoundingChord(chord);

    // Has the harmony moved since the note the line is about to repeat? If so
    // a repeat is a common tone rather than standing still, and is not charged
    // for. `sounding` is already the chord reduced to pitch classes, so this
    // compares what the ear compares.
    const bool harmonyMoved =
        sounding.root != lastSounding.root || sounding.tones != lastSounding.tones;

    const double u = (double)step / (double)eighths;
    double target = 0.0;
    switch (contour) {
    case Contour::Rise:
      target = -0.5 + u;
      break;
    case Contour::Fall:
      target = 0.5 - u;
      break;
    case Contour::Arch:
      target = -0.5 + std::sin(u * 3.14159265358979);
      break;
    case Contour::Walk:
      target = 0.35 * std::sin(u * 6.2831853 * 1.5);
      break;
    }
    const int wanted = centre + (int)std::lround(target * span);

    // The pool: the scale across the lead's register, plus the chord's own
    // tones, which a borrowed or altered chord can put outside the scale.
    std::vector<int> cand;
    for (int degree = 0; degree < MusicalKey::kScaleDegrees; ++degree)
      for (int octave = 4; octave <= 6; ++octave) {
        const int note = MusicalKey::degreeToMidi(s.key, degree, octave);
        if (note >= 0 && note <= 127)
          cand.push_back(note);
      }
    for (int t = 0; t < chord.toneCount; ++t)
      for (int octave = 5; octave <= 6; ++octave) {
        const int note =
            chord.root + chord.tones[(size_t)t] + 12 * octave;
        if (note >= 0 && note <= 127)
          cand.push_back(note);
      }
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    if (cand.empty())
      continue;

    std::vector<int> ranks(cand.size());
    for (size_t i = 0; i < cand.size(); ++i)
      ranks[i] = m::noteStrength(keySig, cand[i] % 12, sounding);

    // The admissible candidate that best serves the contour AND the interval
    // from the last note. A little seeded deviation on the aim so two
    // intervals with the same contour are not the same line.
    const int jitter = rng.range(-2, 2);
    const size_t idx =
        m::chooseNote(cand, ranks, m::rankCeiling(strength, /*hasChart=*/true),
                      wanted + jitter, melody,
                      harmonyMoved ? kLeadWeightsOverChange : kLeadWeights);

    // Both draws happen on every onset step whether or not the note sounds,
    // so the seed stream does not depend on the outcome -- otherwise one
    // dropped note reshuffles everything after it and the same seed stops
    // giving the same line.
    if (strength == 0 && rng.range(0, 2) == 0)
      continue;

    line[(size_t)step] = cand[idx];
    melody.advance(cand[idx]);
    lastSounding = sounding;
  }

  return line;
}

// The line for one interval, joined to the one before it.
//
// An interval is a closed unit everywhere else in this file, and for the lead
// that was quietly wrong: the melody's memory reset every four seconds, so the
// seam between two intervals was the one move in the line with no interval
// cost priced against it. It showed up exactly where you would expect --
// excluding boundary moves dropped the measured mean interval from 2.17
// semitones to 1.93, so the seams were carrying far more than their share of
// the leaps.
//
// So the previous interval is generated first, purely to learn its last note.
// That is one extra evaluation and never more: the interval before THAT is not
// consulted, so this cannot recurse. The cost is arithmetic -- renderLead
// already generates the previous line for its own reasons -- and the price of
// the bound is that the previous line's own opening note was chosen without a
// predecessor, which changes which note is carried in only rarely. The
// alternative is a generator whose cost grows with how long the band has been
// playing, which is not a trade worth making for one note.
std::vector<int> leadLine(const Settings &s, int intervalIndex) {
  int carryIn = -1;
  if (intervalIndex > 0)
    for (int n : leadLineFrom(s, intervalIndex - 1, -1))
      if (n >= 0)
        carryIn = n;
  return leadLineFrom(s, intervalIndex, carryIn);
}

namespace {

// Headroom for the kit.
//
// Three drums overlap -- the kick's fundamental rings for a third of a second,
// which at 16 BPI is several hits deep -- and unlike the mixer at the far end,
// nothing between here and the encoder is going to catch a peak over 1.0.
// Vorbis encodes a clipped signal as real distortion, so the trim happens
// before the encoder or not at all.
//
// Re-derived when the drums became modal, because resonators overshoot in a way
// the old additive voices could not: at the previous 0.55 the sweep peaked at
// 1.0264 and clipped. Measured worst case across 96 combinations -- bpm 60 to
// 180, bpi 4 to 24, six seeds -- is now 0.9599, which leaves 0.35 dB.
inline constexpr float kDrumHeadroom = 0.44f;

// The kit's bus stage, and the one piece of processing here that is not part
// of a voice.
//
// Three drums rendered independently and summed are three drums, not a kit.
// Shaping the sum is what makes them one thing: because the nonlinearity sees
// the total, the loudest element momentarily pushes the others down, so the
// hats duck a little under each kick and come back between them. That
// intermodulation is audible as the parts belonging together, and no amount of
// per-voice shaping produces it -- it only exists in the sum.
//
// Raised from 1.1 with the modal voices, and the two constants were found
// together rather than separately. Drive turns out to buy loudness and almost
// no peak control -- at 1.8 the kit gained 2.4 dB and its worst peak moved by
// 0.15 dB -- while headroom does the opposite. So drive sets the level and the
// trim above sets the ceiling, and the pair lands the kit at rms 0.078, which
// is where the additive kit sat, with more margin than it had.
inline constexpr double kKitDrive = 1.8;

// How much of the room is heard.
//
// Overheads rather than a reverb send: enough that muting it sounds wrong and
// not enough to be audible as an effect. The early reflections do the work --
// the pattern of the first bounces is what says how big a room is -- so this
// does not need to be large to place the kit somewhere.
//
// It was 0.12, which turned out to be too careful to hear. The dry signal is
// common to both channels and only the wet differs, so the mix number IS the
// stereo image, and at 0.12 the kit measured 22 dB of mid against side and a
// left-right correlation of 0.988 -- which is a mono kit with a hint of
// something behind it. Measured across the range: 0.22 gives -16.8 dB, 0.32
// gives -13.6 dB and a correlation of 0.92, and 0.45 gives -10.9 dB and starts
// sounding like a reverb rather than a room. 0.32 costs 0.2 LU of level.
inline constexpr float kRoomMix = 0.32f;

void renderDrums(const Settings &s, int intervalIndex, Phase phase, float *out,
                 float *right, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  const Figure kick = kickFigure(s);
  Rng rng(saltedSeed(Voice::Drums, s.seed) ^ 0xB5297A4DU);

  const auto kickVel = chalkwalk::music::accents(kick.steps, kick.pulses,
                                          kick.rotation, kick.accents);

  // The snare answers the kick rather than rolling its own: two onsets, half an
  // interval apart, which is the backbeat in every time signature this can be.
  const int snarePulses = std::max(1, s.bpi / 4);
  const int snareRotation = std::max(1, s.bpi / 4);

  // Hats run at twice the beat resolution -- eighths -- which is what stops the
  // kit sounding like three things hitting the same grid.
  const int hatSteps = std::max(1, s.bpi * 2);
  const int hatPulses = std::min(hatSteps, rng.range(s.bpi, hatSteps));
  const int halfBeat = beatSamples / 2;

  for (int step = 0; step < kick.steps; ++step) {
    const int at = step * beatSamples;
    if (at >= numSamples)
      break;
    const int v = kickVel[(size_t)step];
    if (v > 0)
      BotVoice::renderKick(out + at, numSamples - at, s.sampleRate,
                           kDrumHeadroom *
                               (v >= chalkwalk::music::kAccentedVelocity ? 0.9f
                                                                  : 0.65f));
  }

  for (int step = 0; step < s.bpi; ++step) {
    if (!chalkwalk::music::hit(step, s.bpi, snarePulses, snareRotation))
      continue;
    const int at = step * beatSamples;
    if (at >= numSamples)
      break;
    // Raised from 0.55 with the snare's retuning. Moving its weight off the
    // wires and onto the body cost 2.4 LU -- a drum body is a narrower thing
    // than a burst of noise -- and without this the kit came back with the
    // backbeat sitting under the kick and the hats.
    BotVoice::renderSnare(out + at, numSamples - at, s.sampleRate,
                          kDrumHeadroom * 0.72f,
                          saltedSeed(Voice::Drums, s.seed) + (std::uint32_t)step);
  }

  // The hats carry the variation. Rotating their pattern by the interval index
  // means the kit is not bit-identical every four seconds, which is the
  // difference between a band and a loop -- and it costs one integer, because
  // the phase relationship to the kick is what changes, not the density.
  const int hatRotation = intervalIndex % std::max(1, hatSteps);

  for (int step = 0; step < hatSteps; ++step) {
    if (!chalkwalk::music::hit(step, hatSteps, hatPulses, hatRotation))
      continue;
    const int at = step * halfBeat;
    if (at >= numSamples)
      break;
    // Every fourth hat opens, which is enough motion to stop it ticking.
    const bool open = (step % 4) == 3;
    BotVoice::renderHat(out + at, numSamples - at, s.sampleRate,
                        kDrumHeadroom * ((step % 2) == 0 ? 0.5f : 0.32f),
                        saltedSeed(Voice::Drums, s.seed) + 977u * (std::uint32_t)step,
                        open);
  }

  // A fill at the end of every fourth interval: extra snares through the last
  // beat. Four intervals is the phrase length a listener hears whether or not
  // anyone intended one, so it is where a fill belongs.
  // A wrap-up always fills, whatever the phrase count says. This is the whole
  // reason the ending has a first interval: the fill is what tells the room the
  // next downbeat is the last one, and a resolve with nothing leading into it
  // is a dropout with a note on the front (docs/BOT-CHAT.md section 15).
  if (intervalIndex % 4 == 3 || phase == Phase::Wrapping) {
    const int lastBeat = (s.bpi - 1) * beatSamples;
    for (int sub = 0; sub < 4; ++sub) {
      const int at = lastBeat + sub * (beatSamples / 4);
      if (at < 0 || at >= numSamples)
        continue;
      BotVoice::renderSnare(out + at, numSamples - at, s.sampleRate,
                            kDrumHeadroom * (0.46f + 0.16f * (float)sub),
                            saltedSeed(Voice::Drums, s.seed) + 31u * (std::uint32_t)sub);
    }
  }

  // The room, and then the bus.
  //
  // In that order, and it matters twice. Physically the console hears the room
  // rather than the other way round; practically, the room ADDS its
  // reflections to the dry signal, so putting the soft clip after it is what
  // keeps the sum inside the headroom the trim above was measured for.
  //
  // The room is what makes the kit stereo, and the only reason any voice is.
  BotDsp::Room room;
  room.prepare(s.sampleRate, 4.0, kRoomMix);

  for (int i = 0; i < numSamples; ++i) {
    float wetL = 0.0f, wetR = 0.0f;
    room.process(out[i], wetL, wetR);
    out[i] = BotVoice::saturate(wetL, kKitDrive);
    if (right != nullptr)
      right[i] = BotVoice::saturate(wetR, kKitDrive);
  }
}

// C2. Must be a C: chord roots are pitch classes where 0 means C.
inline constexpr double kBassAnchorMidi = 36.0;

// The keys have no anchor of their own any more: a voicing is absolute MIDI
// notes chosen by Harmony::voiceLead, inside the register it names.

void renderBass(const Settings &s, float *out, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0 || !s.key.valid)
    return;

  const Figure f = figureFor(Voice::Bass, s);
  Rng rng(saltedSeed(Voice::Bass, s.seed));

  const auto layout = layoutOf(s);
  const auto patch = bassPatch(s);

  // The figure runs finer than the beat, so a step is a fraction of one.
  const int stepsPerBeat = std::max(1, f.steps / std::max(1, s.bpi));
  const int stepSamples = beatSamples / stepsPerBeat;
  if (stepSamples <= 0)
    return;

  // The figure's grid and the harmony's need not be the same resolution, so
  // map one onto the other rather than assuming they match.
  auto layoutStepOf = [stepsPerBeat](int step) {
    return step * Harmony::kStepsPerBeat / stepsPerBeat;
  };

  // Collect the onsets first, so each note can be held until the next one
  // rather than for an arbitrary fixed length. A sustained voice needs to know
  // where it stops.
  std::vector<int> onsets;
  std::vector<bool> isChange;
  const Figure kick = figureFor(Voice::Drums, s);

  for (int step = 0; step < f.steps; ++step) {
    // A chord change always gets a note, whether or not the figure has an
    // onset there. A bass player lands on the change; leaving it to the
    // rotation means the harmony is sometimes announced by nobody, and the
    // first thing heard over a new chord is its fifth.
    const bool onChange =
        step == 0 ||
        (step * Harmony::kStepsPerBeat % stepsPerBeat == 0 &&
         Harmony::changesAtStep(layout, layoutStepOf(step)));

    // Every kick gets a bass note, plus the figure's own. The union rather
    // than the figure alone: locking to the kick has to mean actually landing
    // on it, and the doubled Euclidean does not do that by itself.
    const bool onKick =
        step % stepsPerBeat == 0 &&
        chalkwalk::music::hit(step / stepsPerBeat, kick.steps, kick.pulses,
                       kick.rotation);

    if (!onChange && !onKick &&
        !chalkwalk::music::hit(step, f.steps, f.pulses, f.rotation))
      continue;

    onsets.push_back(step);
    isChange.push_back(onChange);
  }

  for (size_t n = 0; n < onsets.size(); ++n) {
    const int step = onsets[n];
    const bool onChange = isChange[n];

    const int at = step * stepSamples;
    if (at >= numSamples)
      break;

    // Up to the next note, or the end of the interval.
    const int nextStep = (n + 1 < onsets.size()) ? onsets[n + 1] : f.steps;
    const int length =
        std::min(numSamples - at, (nextStep - step) * stepSamples);
    if (length <= 0)
      continue;

    const auto &chord = Harmony::chordAtStep(layout, layoutStepOf(step));

    // Root, octave and fifth: the three notes that state a chord without
    // getting in the way of anyone playing over it.
    int semitoneAboveRoot = 0;
    if (!onChange) {
      const int roll = rng.range(0, 9);
      if (roll < 5)
        semitoneAboveRoot = 0; // root, most of the time
      else if (roll < 8)
        semitoneAboveRoot = 12; // octave
      else
        semitoneAboveRoot = chord.toneCount > 2 ? chord.tones[2] : 7; // fifth
    }

    // MIDI 36 is C2, and a chord root is a pitch class where 0 means C, so the
    // anchor has to BE a C or every root comes out transposed. It was 28 --
    // which is E1, not C1 -- so the bass played a fourth above the chord while
    // the keys, anchored correctly at 60 (C4), played the chord. Two voices
    // disagreeing about the harmony.
    //
    // C2 rather than C1 for the second reason it was wrong: 41-78 Hz is below
    // what most laptop and monitor speakers reproduce at all, so the part was
    // not merely wrong but inaudible. C2-B2 is 65-123 Hz, which carries.
    // A slash chord names the note underneath it, and the bass is what is
    // underneath: the G of Am7/G is the bass player's job, not the pad's.
    const int lowest = (chord.bass >= 0 && onChange) ? chord.bass : chord.root;
    const double midi =
        kBassAnchorMidi + (double)lowest + (double)semitoneAboveRoot;

    // Dynamics, which this voice had none of: every note was velocity 0.7.
    //
    // A bass player does not hit everything equally. The chord change is the
    // note the part exists to state, so it is the hardest; a note that lands
    // with the kick is next; a passing note between them is the softest. That
    // ordering is what makes a line sound phrased rather than typed, and it is
    // also what gives velocity something to articulate -- the string gets
    // brighter as it is played harder, continuously.
    float velocity = 0.45f;
    if (onChange)
      velocity = 1.0f;
    else if (step % stepsPerBeat == 0 &&
             chalkwalk::music::hit(step / stepsPerBeat, kick.steps, kick.pulses,
                            kick.rotation))
      velocity = 0.72f;

    // A few percent either way, so two notes of the same weight are not the
    // same note. Deterministic, like everything else here.
    velocity *= 0.94f + 0.12f * (float)rng.range(0, 100) / 100.0f;

    BotVoice::renderBassString(out + at, length, s.sampleRate,
                               BotVoice::midiToHz(midi), velocity, patch,
                               saltedSeed(Voice::Bass, s.seed) +
                                   131u * (std::uint32_t)step);
  }
}

// The keyboard's output stage, after every voice has been summed.
//
// Two things live here rather than in the voice, and for the same reason the
// kit's room lives in renderDrums rather than in renderKick: on the instrument
// being modelled they are downstream of the whole keyboard, not per note. A
// chorus applied to each note separately would be six chorus units, which is
// not what is inside any of these machines and would smear each voice against
// itself instead of spreading them against each other.
//
// The chorus is the more visible of the two. On a Juno or a Polysix it is a
// front-panel switch that most factory patches leave on, and it is a large
// part of what people are hearing when they call these instruments lush. It is
// also the only thing making this voice stereo.
inline constexpr double kKeysChorusRate = 0.55;   // Hz
inline constexpr double kKeysChorusBase = 12.0;   // ms
inline constexpr double kKeysChorusDepth = 3.2;   // ms
// Raised from 0.55 for the same reason and by the same measurement: at 0.55
// the keyboard's side channel sat 11.6 dB under its mid, which is a real
// chorus but a polite one. 0.75 gives -9.6 dB. These instruments were not
// polite about it.
inline constexpr float kKeysChorusMix = 0.75f;

// And the output amplifier. Gentle -- this is the last of the several places
// the signal is shaped rather than the one doing the work, and the point of
// spreading saturation along a chain is that no single stage has to be pushed
// far enough to be heard as distortion.
inline constexpr double kKeysDrive = 1.2;

void renderKeys(const Settings &s, float *out, float *right, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  const auto layout = layoutOf(s);
  if (beatSamples <= 0 || layout.empty())
    return;

  const auto patch = keysPatch(s);

  // Sample positions are worked out from the beat rather than accumulated per
  // step, so a beat length that is not even does not drift across the interval.
  auto atStep = [beatSamples](int step) {
    return step * beatSamples / Harmony::kStepsPerBeat;
  };

  // The chords in the order they actually sound, which is the loop the voice
  // leading has to close: a chord in the chart that never gets any time must
  // not pull the voicing of the ones that do.
  struct Span {
    int from, to, chord;
  };
  std::vector<Span> spans;
  Harmony::Progression sounding;
  for (int step = 0; step < layout.steps();) {
    const int idx = layout.stepToChord[(size_t)step];
    int end = step + 1;
    while (end < layout.steps() && layout.stepToChord[(size_t)end] == idx)
      ++end;
    spans.push_back({step, end, (int)sounding.size()});
    sounding.push_back(layout.chords[(size_t)idx]);
    step = end;
  }

  const auto voicings = Harmony::voiceLead(sounding);
  if (voicings.size() != sounding.size())
    return;

  // One sustained chord per slot: held, not stabbed -- and let go rather than
  // cut off.
  //
  // The hands come up at the end of the slot and the notes ring on past it, so
  // a chord overlaps the one that replaces it. That overlap is not a detail:
  // an envelope whose release has to finish inside its own slot is a keyboard
  // player lifting both hands cleanly between every chord, which nobody does,
  // and it reads as chopped however gentle the release is made.
  //
  const double longestRelease = 2.0;
  const int tail = (int)(longestRelease * s.sampleRate);

  for (const auto &span : spans) {
    const int at = atStep(span.from);
    if (at >= numSamples)
      break;
    const int hold = std::min(numSamples - at, atStep(span.to) - at);
    if (hold <= 0)
      break;
    const int length = std::min(numSamples - at, hold + tail);

    for (int note : voicings[(size_t)span.chord])
      // Seeded by the NOTE and by where it falls, so the voices of a chord
      // drift apart from each other and the same chord played twice is not the
      // same waveform twice.
      BotVoice::renderPad(out + at, length, hold, s.sampleRate,
                          BotVoice::midiToHz((double)note), 0.85f, patch,
                          saltedSeed(Voice::Keys, s.seed) +
                              2654435761u * (std::uint32_t)note +
                              97u * (std::uint32_t)span.from);
  }

  // The interval wraps onto itself.
  //
  // The last chord's release runs past the end of the buffer, and a Ninjam
  // interval is a closed unit, so that tail has nowhere to go: every four
  // seconds the pad was cut off mid-release and started again. Audible as a
  // seam, and the more audible the longer the release -- which is exactly the
  // direction the envelopes were just moved in.
  //
  // But the chart is the same every interval, so what was sounding at the end
  // of the previous one is not merely knowable, it is IDENTICAL to what is
  // sounding at the end of this one. So the last chord is rendered once more
  // into a scratch buffer, and the part of it that falls past the boundary is
  // added at the head. The result is a genuinely continuous instrument built
  // out of intervals that are still closed units, with no state carried
  // between calls and nothing to break determinism.
  //
  // Costs one chord's worth of rendering per interval, on the conductor
  // thread, which is allowed to allocate.
  if (!spans.empty()) {
    const auto &last = spans.back();
    const int at = atStep(last.from);
    const int hold = std::min(numSamples - at, atStep(last.to) - at);
    if (hold > 0) {
      std::vector<float> scratch((size_t)(hold + tail), 0.0f);
      for (int note : voicings[(size_t)last.chord])
        BotVoice::renderPad(scratch.data(), hold + tail, hold, s.sampleRate,
                            BotVoice::midiToHz((double)note), 0.85f, patch,
                            saltedSeed(Voice::Keys, s.seed) +
                                2654435761u * (std::uint32_t)note +
                                97u * (std::uint32_t)last.from);

      const int carried = std::min(numSamples, tail);
      for (int i = 0; i < carried; ++i)
        out[i] += scratch[(size_t)(hold + i)];
    }
  }

  BotDsp::Chorus chorus;
  chorus.prepare(s.sampleRate, kKeysChorusRate, kKeysChorusBase,
                 kKeysChorusDepth, kKeysChorusMix);

  for (int i = 0; i < numSamples; ++i) {
    float wetL = 0.0f, wetR = 0.0f;
    chorus.process(out[i], wetL, wetR);
    out[i] = BotVoice::saturate(wetL, kKeysDrive);
    if (right != nullptr)
      right[i] = BotVoice::saturate(wetR, kKeysDrive);
  }
}

void renderLead(const Settings &s, int intervalIndex, int noNewNotesAfter,
                float *out, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  const auto line = leadLine(s, intervalIndex);
  const auto layout = layoutOf(s);
  const int eighth = beatSamples / 2;
  if (eighth <= 0)
    return;

  const auto patch = leadPatch(s);

  // Two of the three instruments are struck or plucked and go on ringing after
  // the hand leaves, so a note is given room past the slot it was played in --
  // the same arrangement the keys use, and for the same reason: a line whose
  // every note stops dead at the next one is a sequencer.
  const int tail = (int)(1.5 * s.sampleRate);

  for (size_t step = 0; step < line.size(); ++step) {
    if (line[step] < 0)
      continue;

    const int at = (int)step * eighth;
    if (at >= numSamples)
      break;
    // Laying out. A note already under way rings on and finishes its phrase,
    // which is what a player does -- stopping dead mid-note is a mute, not a
    // musician deciding the tune is ending.
    if (at >= noNewNotesAfter)
      break;

    // Held until the next note or rest, so a line has phrasing rather than a
    // uniform stutter of equal-length blips.
    size_t next = step + 1;
    while (next < line.size() && line[next] < 0)
      ++next;
    const int length =
        std::min(numSamples - at, (int)(next - step) * eighth);
    if (length <= 0)
      continue;

    const int strength = metricStrength((int)step, s.bpi);
    const float velocity = strength >= 3 ? 0.85f : (strength >= 1 ? 0.7f : 0.5f);

    // A colour note passes; it does not sit. Holding a semitone above a chord
    // tone until the next note is the difference between a line that leans
    // into the clash and one that trips over it -- and it is the other half of
    // why minor sounded wrong, because that is where those notes live.
    int held = length;
    const auto &chord = Harmony::chordAtStep(layout, (int)step);
    if (noteTier(line[step], chord) == 2)
      held = std::min(length, eighth);

    BotVoice::renderLead(out + at, std::min(numSamples - at, held + tail), held,
                         s.sampleRate, BotVoice::midiToHz((double)line[step]),
                         velocity, patch,
                         saltedSeed(Voice::Lead, s.seed) +
                             613u * (std::uint32_t)step);
  }

  // And the note that was still ringing when the last interval ended, for the
  // reason renderKeys documents: an interval is a closed unit, so without this
  // a plucked or struck lead is chopped off every four seconds.
  //
  // The lead's line is rerolled per interval, so unlike the chart this is not
  // the same note -- it has to be worked out from the PREVIOUS interval's line,
  // which is a pure function of the seed and the index and so costs nothing but
  // the arithmetic. The first interval a bot ever plays genuinely has no
  // predecessor, and is left alone.
  if (intervalIndex > 0) {
    const auto previous = leadLine(s, intervalIndex - 1);
    int lastStep = -1;
    for (int step = (int)previous.size() - 1; step >= 0; --step)
      if (previous[(size_t)step] >= 0) {
        lastStep = step;
        break;
      }

    if (lastStep >= 0) {
      const int at = lastStep * eighth;
      int held = std::min(numSamples - at,
                          (int)((int)previous.size() - lastStep) * eighth);

      // A colour note is cut short wherever it falls, so if it was one it had
      // already stopped well before the boundary and there is nothing to carry.
      const auto &chord = Harmony::chordAtStep(layout, lastStep);
      if (noteTier(previous[(size_t)lastStep], chord) == 2)
        held = std::min(held, eighth);

      if (held > 0 && at + held >= numSamples) {
        const int strength = metricStrength(lastStep, s.bpi);
        const float velocity =
            strength >= 3 ? 0.85f : (strength >= 1 ? 0.7f : 0.5f);

        std::vector<float> scratch((size_t)(held + tail), 0.0f);
        BotVoice::renderLead(scratch.data(), held + tail, held, s.sampleRate,
                             BotVoice::midiToHz((double)previous[(size_t)lastStep]),
                             velocity, patch,
                             saltedSeed(Voice::Lead, s.seed) +
                                 613u * (std::uint32_t)lastStep);

        const int carried = std::min(numSamples, tail);
        for (int i = 0; i < carried; ++i)
          out[i] += scratch[(size_t)(held + i)];
      }
    }
  }
}

} // namespace

// What each voice is trimmed to, and why a band needs this at all.
//
// The four voices were never levelled against each other, and it showed: the
// pad sat 10 dB ABOVE the drums, so a rebuilt kit could improve as much as it
// liked and still be buried. A backing band you play along to wants the
// opposite shape -- drums and bass carrying it, chords underneath, the melody
// present without owning the room.
//
// Targets, as rms over an interval, and the reasoning for each:
//
//   Kit    -16 dBFS   the anchor
//   Bass   -14 dBFS   2 dB up: the bass carries a jam
//   Keys   -19 dBFS   3 dB down: chords are the floor, not the feature
//   Lead   -16 dBFS   level with the kit
//
// Absolute level is close to a free parameter here, which is worth saying
// because it makes the numbers above less precious than they look: every
// remote channel arrives at the listener multiplied by
// kDefaultRemoteChannelVolume and with a fader of its own. What is NOT free is
// crest factor, so the anchor is set where the kit needs only gentle limiting
// rather than wherever a target number happened to fall.
//
// Set by rms and then checked by LOUDNESS, which is the unit that matters and
// is not the same thing: rms weights a kick and a hi-hat equally and the ear
// does not. Measured with AudioMeasure::integratedLufs over five seeds, as the
// stereo pair each bot actually transmits:
//
//   Bass  -12.2 LUFS      Kit  -13.0 LUFS
//   Lead  -13.4 LUFS      Keys -18.2 LUFS
//
// The keys sit further down than the others, and further down than an equal
// loudness would put them, which is the one place a measurement had to be
// overruled by a judgement. Loudness says how loud a thing is, not how much
// room it takes up: the pad was two sines and is now a pair of filtered saws,
// and at the SAME integrated loudness the second masks far more of the band
// than the first because it occupies far more of the spectrum. Levelled by the
// meter it was audibly in the way. This is what a mixer would have done, and
// the meter has no opinion about it.
//
// Every number here was re-measured after the kit, bass and keys were retuned;
// they move whenever a voice does, which is why they are quoted rather than
// derived. The KIT'S OWN loudness still varies by 3.7 LU from seed to seed
// depending on how busy the figure is, so nothing here is fitted more finely
// than about half a decibel -- tuning a trim against material that moves by
// four would be false precision. Making a seed's density not change the band's
// level is a real piece of work and is on the roadmap.
inline constexpr float kVoiceTrim[kNumVoices] = {
    1.71f, // Drums
    1.67f, // Bass
    0.32f, // Keys
    1.15f, // Lead
};

// How this bass player plays, chosen once and then held for the whole session.
//
// A FRESH Rng with its own constant rather than a draw from the figure's
// sequence: taking a value out of an existing stream shifts every subsequent
// draw and silently rewrites the notes (see renderDrums' hat rotation for the
// same trick and the same reason).
BotVoice::BassTechnique bassTechnique(const Settings &s) {
  Rng rng(saltedSeed(Voice::Bass, s.seed) ^ 0x27D4EB2Fu);
  switch (rng.range(0, 2)) {
  case 0:
    return BotVoice::BassTechnique::Picked;
  case 1:
    return BotVoice::BassTechnique::Muted;
  default:
    return BotVoice::BassTechnique::Fingered;
  }
}

BotVoice::BassPatch bassPatch(const Settings &s) {
  if (s.usePatchOverrides)
    return s.bassPatchOverride;
  return BotVoice::bassPatchFor(bassTechnique(s));
}

BotVoice::LeadPatch leadPatch(const Settings &s) {
  if (s.usePatchOverrides)
    return s.leadPatchOverride;
  BotVoice::LeadPatch p;
  p.instrument = leadInstrument(s);
  return p;
}

BotVoice::LeadInstrument leadInstrument(const Settings &s) {
  if (s.leadOverride >= 0 && s.leadOverride <= 2)
    return (BotVoice::LeadInstrument)s.leadOverride;

  // A fresh generator with its own constant, for the reason bassTechnique
  // documents.
  Rng rng(saltedSeed(Voice::Lead, s.seed) ^ 0x68E31DA4u);
  switch (rng.range(0, 2)) {
  case 0:
    return BotVoice::LeadInstrument::EPiano;
  case 1:
    return BotVoice::LeadInstrument::Guitar;
  default:
    return BotVoice::LeadInstrument::Synth;
  }
}

BotVoice::PadPatch keysPatch(const Settings &s) {
  if (s.usePatchOverrides)
    return s.keysPatchOverride;

  // A fresh generator with its own constant, for the reason bassTechnique
  // documents: drawing from an existing sequence shifts every later draw and
  // silently rewrites the notes.
  return BotVoice::padPatchFor(saltedSeed(Voice::Keys, s.seed) ^ 0x1D2C6FE3u);
}

// Two voices are stereo, and each has earned it by being a real thing rather
// than a width effect: the kit is heard through overheads in a room, and the
// keyboard through the stereo chorus on its own output. Bass and lead are
// close-miked and centred, which is where they belong.
bool isStereo(Voice voice) {
  return voice == Voice::Drums || voice == Voice::Keys;
}

// The final chord: everything arrives together on the downbeat, rings, and the
// rest of the interval is quiet.
//
// Its own function rather than a modified groove, because it IS different
// material -- one event, not a figure. The chord is
// `Harmony::resolutionChord`, so a blues ends on its own seventh rather than a
// derived triad (docs/BOT-CHAT.md section 15).
void renderResolve(Voice voice, const Settings &s, float *out, float *right,
                   int numSamples) {
  const auto chord = Harmony::resolutionChord(s.chart, s.key);
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  // Held for two beats, then released -- the tail does the rest. Deliberately
  // short of the whole interval: the point of the resolve is that the band
  // lands and gets out of the way, and at 32 bpi holding it would be half a
  // minute of one chord.
  const int ring = std::min(numSamples, beatSamples * 2);

  switch (voice) {
  case Voice::Drums: {
    // A crash is what a band lands on, and the kit has no crash -- so an open
    // hat with a long tail plus the kick underneath it, which is the same
    // gesture made from the pieces that exist.
    BotVoice::renderKick(out, numSamples, s.sampleRate, kDrumHeadroom * 0.95f);
    BotVoice::renderHat(out, numSamples, s.sampleRate, kDrumHeadroom * 0.62f,
                        saltedSeed(Voice::Drums, s.seed) + 4111u, true);
    if (right != nullptr)
      std::copy(out, out + numSamples, right);
    break;
  }
  case Voice::Bass: {
    // The root, low and alone. The bass is what makes a landing sound final.
    const auto patch = bassPatch(s);
    const int midi = 36 + chord.root; // C2 upward, the register it lives in
    BotVoice::renderBassString(out, numSamples, s.sampleRate,
                               BotVoice::midiToHz((double)midi), 0.9f, patch,
                               saltedSeed(Voice::Bass, s.seed));
    break;
  }
  case Voice::Keys: {
    const auto patch = keysPatch(s);
    const auto voicing = Harmony::voiceLead({chord});
    if (voicing.empty())
      break;
    for (int note : voicing.front())
      BotVoice::renderPad(out, numSamples, ring, s.sampleRate,
                          BotVoice::midiToHz((double)note), 0.8f, patch,
                          saltedSeed(Voice::Keys, s.seed) +
                              97u * (std::uint32_t)note);
    if (right != nullptr)
      std::copy(out, out + numSamples, right);
    break;
  }
  case Voice::Lead:
    // Silent. A soloist who hears the band ending does not start another
    // phrase over the top of the final chord.
    break;
  }
}

void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                    Phase phase, float *left, float *right, int numSamples) {
  if (left == nullptr || numSamples <= 0 || s.sampleRate <= 0.0 || s.bpi <= 0)
    return;

  float *out = left;

  // Negative indices would reflect the modulo arithmetic below onto the wrong
  // variation; the conductor counts up from zero, but nothing here should
  // depend on that.
  if (intervalIndex < 0)
    intervalIndex = 0;

  // The wrap-up is a TAPER, not a switch: the first half is the tune and the
  // second half winds down. Everyone dropping out at once is not what winding
  // down sounds like, so only the lead actually stops -- it is the clearest
  // signal there is, and it leaves room for the fill to be heard.
  const int halfway = numSamples / 2;
  const int leadStops =
      phase == Phase::Wrapping ? halfway : numSamples;

  if (phase == Phase::Resolving) {
    renderResolve(voice, s, out, right, numSamples);
  } else {
    switch (voice) {
    case Voice::Drums:
      renderDrums(s, intervalIndex, phase, out, right, numSamples);
      break;
    case Voice::Bass:
      renderBass(s, out, numSamples);
      break;
    case Voice::Keys:
      renderKeys(s, out, right, numSamples);
      break;
    case Voice::Lead:
      renderLead(s, intervalIndex, leadStops, out, numSamples);
      break;
    }
  }

  // The thinning, for the voices that keep playing. The bass and the kit carry
  // the time into the downbeat and are left alone; the keys back off, which is
  // what a player does when the tune is ending.
  if (phase == Phase::Wrapping && voice == Voice::Keys) {
    for (int i = halfway; i < numSamples; ++i) {
      const float t = (float)(i - halfway) / (float)std::max(1, numSamples - halfway);
      const float g = 1.0f - 0.45f * t;
      out[i] *= g;
      if (right != nullptr)
        right[i] *= g;
    }
  }

  // Balance, then a ceiling.
  //
  // The ceiling is a backstop rather than a sound: it is exactly transparent
  // below its knee, so the only thing it ever touches is a peak that would
  // have clipped the encoder -- and Vorbis turns a clipped sample into real
  // distortion. With it here, no voice can clip whatever a trim, a seed or a
  // future character does, which is a stronger guarantee than a measured
  // headroom constant can give.
  const float trim = s.trimOverride[(int)voice] >= 0.0
                         ? (float)s.trimOverride[(int)voice]
                         : kVoiceTrim[(int)voice];
  for (int i = 0; i < numSamples; ++i)
    out[i] = BotDsp::softClip(out[i] * trim);
  if (right != nullptr && isStereo(voice))
    for (int i = 0; i < numSamples; ++i)
      right[i] = BotDsp::softClip(right[i] * trim);
}

} // namespace BotBand
