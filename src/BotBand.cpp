#include "BotBand.h"

#include "BotDsp.h"
#include "BotVoice.h"
#include "Euclidean.h"
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
    f.pulses = Euclidean::nearestCoprimePulses(f.steps, kick.pulses * 2,
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

std::vector<int> leadLine(const Settings &s, int intervalIndex) {
  const int eighths = std::max(1, s.bpi * 2);
  std::vector<int> line((size_t)eighths, -1);
  if (!s.key.valid || s.chart.empty())
    return line;

  const auto layout = layoutOf(s);
  const Figure f = figureFor(Voice::Lead, s);
  Rng rng(saltedSeed(Voice::Lead, s.seed) + 7919u * (std::uint32_t)intervalIndex);

  // The contour is rerolled per interval, so the line develops across a phrase
  // instead of repeating verbatim, while the seed still makes the whole
  // sequence reproducible.
  const auto contour = (Contour)(rng.next() % 4);

  // Where the line sits: an octave above the keys, so it is heard as a melody
  // over the chords rather than as part of them.
  const int centre = 72;
  const int span = 12;

  for (int step = 0; step < eighths; ++step) {
    if (!Euclidean::hit(step, f.steps, f.pulses, f.rotation))
      continue;

    const int strength = metricStrength(step, s.bpi);
    // The lead already runs in eighths, which is the layout's own grid, so it
    // sees a chord change inside a beat rather than only on one.
    const auto &chord = Harmony::chordAtStep(layout, step);

    // Where the contour wants to be, as a fraction of the way through.
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
      // A random walk that still has to come home, so it wanders without
      // drifting off the end of the register.
      target = 0.35 * std::sin(u * 6.2831853 * 1.5);
      break;
    }
    const int wanted = centre + (int)std::lround(target * span);

    // The coupling: beat strength decides how strong a note may be. A strong
    // beat takes a chord tone; an ordinary one takes any comfortable scale
    // tone; only an off-beat may touch a semitone above a chord tone, and only
    // in passing (see the duration cap below).
    //
    // Porting the beat axis without this one is what made minor keys sound
    // wrong: the flat sixth was as welcome on beat three as the fifth was.
    const int worstTierAllowed = strength >= 3 ? 0 : (strength >= 1 ? 1 : 2);

    std::vector<int> allowed;
    for (int degree = 0; degree < MusicalKey::kScaleDegrees; ++degree)
      for (int octave = 4; octave <= 6; ++octave) {
        const int note = MusicalKey::degreeToMidi(s.key, degree, octave);
        if (note >= 0 && noteTier(note, chord) <= worstTierAllowed)
          allowed.push_back(note);
      }

    // A chord may be borrowed or altered, in which case its tones are not all
    // in the scale. On a strong beat the chord wins.
    if (worstTierAllowed == 0)
      for (int t = 0; t < chord.toneCount; ++t)
        for (int octave = 5; octave <= 6; ++octave)
          allowed.push_back(chord.root + chord.tones[(size_t)t] + 12 * octave);

    if (allowed.empty())
      continue;

    // The allowed note nearest the contour, with a little seeded deviation so
    // two intervals with the same contour are not the same line.
    const int jitter = rng.range(-2, 2);
    int best = allowed[0];
    int bestDistance = std::abs(best - (wanted + jitter));
    for (int note : allowed) {
      const int d = std::abs(note - (wanted + jitter));
      if (d < bestDistance) {
        bestDistance = d;
        best = note;
      }
    }

    // Rests matter as much as notes: a line that never stops is a drone. Weak
    // beats drop out often enough to leave the phrase somewhere to breathe.
    if (strength == 0 && rng.range(0, 2) == 0)
      continue;

    line[(size_t)step] = best;
  }

  return line;
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
// can stay low and still place the kit somewhere.
inline constexpr float kRoomMix = 0.12f;

void renderDrums(const Settings &s, int intervalIndex, float *out,
                 float *right, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  const Figure kick = kickFigure(s);
  Rng rng(saltedSeed(Voice::Drums, s.seed) ^ 0xB5297A4DU);

  const auto kickVel = Euclidean::accents(kick.steps, kick.pulses,
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
                               (v >= Euclidean::kAccentedVelocity ? 0.9f
                                                                  : 0.65f));
  }

  for (int step = 0; step < s.bpi; ++step) {
    if (!Euclidean::hit(step, s.bpi, snarePulses, snareRotation))
      continue;
    const int at = step * beatSamples;
    if (at >= numSamples)
      break;
    BotVoice::renderSnare(out + at, numSamples - at, s.sampleRate,
                          kDrumHeadroom * 0.55f,
                          saltedSeed(Voice::Drums, s.seed) + (std::uint32_t)step);
  }

  // The hats carry the variation. Rotating their pattern by the interval index
  // means the kit is not bit-identical every four seconds, which is the
  // difference between a band and a loop -- and it costs one integer, because
  // the phase relationship to the kick is what changes, not the density.
  const int hatRotation = intervalIndex % std::max(1, hatSteps);

  for (int step = 0; step < hatSteps; ++step) {
    if (!Euclidean::hit(step, hatSteps, hatPulses, hatRotation))
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
  if (intervalIndex % 4 == 3) {
    const int lastBeat = (s.bpi - 1) * beatSamples;
    for (int sub = 0; sub < 4; ++sub) {
      const int at = lastBeat + sub * (beatSamples / 4);
      if (at < 0 || at >= numSamples)
        continue;
      BotVoice::renderSnare(out + at, numSamples - at, s.sampleRate,
                            kDrumHeadroom * (0.35f + 0.12f * (float)sub),
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
  const auto technique = bassTechnique(s);

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
        Euclidean::hit(step / stepsPerBeat, kick.steps, kick.pulses,
                       kick.rotation);

    if (!onChange && !onKick &&
        !Euclidean::hit(step, f.steps, f.pulses, f.rotation))
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
             Euclidean::hit(step / stepsPerBeat, kick.steps, kick.pulses,
                            kick.rotation))
      velocity = 0.72f;

    // A few percent either way, so two notes of the same weight are not the
    // same note. Deterministic, like everything else here.
    velocity *= 0.94f + 0.12f * (float)rng.range(0, 100) / 100.0f;

    BotVoice::renderBassString(out + at, length, s.sampleRate,
                               BotVoice::midiToHz(midi), velocity, technique,
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
inline constexpr float kKeysChorusMix = 0.55f;

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

  // One sustained chord per slot: held, not stabbed.
  for (const auto &span : spans) {
    const int at = atStep(span.from);
    if (at >= numSamples)
      break;
    const int length = std::min(numSamples - at, atStep(span.to) - at);
    if (length <= 0)
      break;

    for (int note : voicings[(size_t)span.chord])
      // Seeded by the NOTE and by where it falls, so the voices of a chord
      // drift apart from each other and the same chord played twice is not the
      // same waveform twice.
      BotVoice::renderPad(out + at, length, s.sampleRate,
                          BotVoice::midiToHz((double)note), 0.85f, patch,
                          saltedSeed(Voice::Keys, s.seed) +
                              2654435761u * (std::uint32_t)note +
                              97u * (std::uint32_t)span.from);
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

void renderLead(const Settings &s, int intervalIndex, float *out,
                int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  const auto line = leadLine(s, intervalIndex);
  const auto layout = layoutOf(s);
  const int eighth = beatSamples / 2;
  if (eighth <= 0)
    return;

  for (size_t step = 0; step < line.size(); ++step) {
    if (line[step] < 0)
      continue;

    const int at = (int)step * eighth;
    if (at >= numSamples)
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

    BotVoice::renderLead(out + at, held, s.sampleRate,
                         BotVoice::midiToHz((double)line[step]), velocity);
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
//   Bass  -11.74 LUFS      Kit  -13.36 LUFS
//   Lead  -12.74 LUFS      Keys -16.70 LUFS
//
// which is the intended shape, and within 0.7 dB of it on every voice. The two
// units agreed to about half a LU here because all four voices carry real
// midrange -- the bass is not a sub -- so K-weighting had little to separate.
//
// Left exactly where rms put it, deliberately: the corrections would have been
// under 0.7 dB, and the KIT'S OWN loudness varies by 3.7 LU from seed to seed
// depending on how busy the figure is. Tuning a trim by half a dB against
// material that moves by four is false precision. Making a seed's density not
// change the band's level is a real piece of work and is on the roadmap.
inline constexpr float kVoiceTrim[kNumVoices] = {
    2.02f, // Drums
    1.50f, // Bass
    0.50f, // Keys
    1.15f, // Lead
};

BotVoice::PadPatch keysPatch(const Settings &s) {
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

void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                    float *left, float *right, int numSamples) {
  if (left == nullptr || numSamples <= 0 || s.sampleRate <= 0.0 || s.bpi <= 0)
    return;

  float *out = left;

  // Negative indices would reflect the modulo arithmetic below onto the wrong
  // variation; the conductor counts up from zero, but nothing here should
  // depend on that.
  if (intervalIndex < 0)
    intervalIndex = 0;

  switch (voice) {
  case Voice::Drums:
    renderDrums(s, intervalIndex, out, right, numSamples);
    break;
  case Voice::Bass:
    renderBass(s, out, numSamples);
    break;
  case Voice::Keys:
    renderKeys(s, out, right, numSamples);
    break;
  case Voice::Lead:
    renderLead(s, intervalIndex, out, numSamples);
    break;
  }

  // Balance, then a ceiling.
  //
  // The ceiling is a backstop rather than a sound: it is exactly transparent
  // below its knee, so the only thing it ever touches is a peak that would
  // have clipped the encoder -- and Vorbis turns a clipped sample into real
  // distortion. With it here, no voice can clip whatever a trim, a seed or a
  // future character does, which is a stronger guarantee than a measured
  // headroom constant can give.
  const float trim = kVoiceTrim[(int)voice];
  for (int i = 0; i < numSamples; ++i)
    out[i] = BotDsp::softClip(out[i] * trim);
  if (right != nullptr && isStereo(voice))
    for (int i = 0; i < numSamples; ++i)
      right[i] = BotDsp::softClip(right[i] * trim);
}

} // namespace BotBand
