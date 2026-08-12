#include "BotBand.h"

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

const Harmony::Chord &chordAtBeat(const Settings &s, int beat) {
  static const Harmony::Chord fallback{};
  if (s.progression.empty())
    return fallback;
  const int idx =
      Harmony::chordIndexForBeat(beat, s.bpi, (int)s.progression.size());
  return s.progression[(size_t)idx];
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
  s.progression = Harmony::defaultProgression(key);
  s.seed = seed;
  return s;
}

Figure figureFor(Voice voice, const Settings &s) {
  switch (voice) {
  case Voice::Drums:
    return kickFigure(s);

  case Voice::Bass: {
    // Locked to the kick, then displaced by the bass's own seed so it is
    // related rather than identical -- the difference between a band and a
    // sequencer playing one pattern through two sounds.
    Rng rng(saltedSeed(Voice::Bass, s.seed));
    Figure f = kickFigure(s);
    f.rotation = rng.range(0, 1); // usually with the kick, sometimes pushed
    f.accents = std::max(1, f.pulses / 3);
    return f;
  }

  case Voice::Keys: {
    // Not a rhythmic figure: the chord changes are the rhythm. Reported as one
    // pulse per chord so the shape of the answer is the same for every voice.
    Figure f;
    f.steps = std::max(1, s.bpi);
    f.pulses = std::max(1, (int)s.progression.size());
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

std::vector<int> leadLine(const Settings &s, int intervalIndex) {
  const int eighths = std::max(1, s.bpi * 2);
  std::vector<int> line((size_t)eighths, -1);
  if (!s.key.valid || s.progression.empty())
    return line;

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
    const auto &chord = chordAtBeat(s, step / 2);

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

    // Metric strength decides WHICH notes are allowed here: a strong beat
    // takes a chord tone, a weak one may pass through the scale. That coupling
    // is what makes the result sound intended rather than sprinkled.
    std::vector<int> allowed;
    if (strength >= 2) {
      for (int t = 0; t < chord.toneCount; ++t)
        for (int octave = -1; octave <= 1; ++octave)
          allowed.push_back(chord.root + chord.tones[(size_t)t] + 60 +
                            12 * octave);
    } else {
      for (int degree = 0; degree < MusicalKey::kScaleDegrees; ++degree)
        for (int octave = 4; octave <= 6; ++octave)
          allowed.push_back(MusicalKey::degreeToMidi(s.key, degree, octave));
    }
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
// Three drums overlap -- the kick alone rings for 0.32 s, which at 16 BPI is
// several hits deep -- and unlike the mixer at the far end, nothing between
// here and the encoder is going to catch a peak over 1.0. Vorbis encodes a
// clipped signal as real distortion, so the trim happens before the encoder or
// not at all. Measured worst case across the seeds and BPIs in the test sweep
// was 1.41, so this leaves a little over.
inline constexpr float kDrumHeadroom = 0.55f;

void renderDrums(const Settings &s, int intervalIndex, float *out,
                 int numSamples) {
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
}

// C2. Must be a C: chord roots are pitch classes where 0 means C.
inline constexpr double kBassAnchorMidi = 36.0;

// C4, and the same rule applies.
inline constexpr double kKeysAnchorMidi = 60.0;

void renderBass(const Settings &s, float *out, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0 || !s.key.valid)
    return;

  const Figure f = figureFor(Voice::Bass, s);
  Rng rng(saltedSeed(Voice::Bass, s.seed));

  const int numChords = (int)s.progression.size();

  // Collect the onsets first, so each note can be held until the next one
  // rather than for an arbitrary fixed length. A sustained voice needs to know
  // where it stops.
  std::vector<int> onsets;
  std::vector<bool> isChange;
  for (int step = 0; step < f.steps; ++step) {
    // A chord change always gets a note, whether or not the figure has an
    // onset there. A bass player lands on the change; leaving it to the
    // rotation means the harmony is sometimes announced by nobody, and the
    // first thing heard over a new chord is its fifth.
    const bool onChange =
        step == 0 ||
        (numChords > 0 &&
         Harmony::chordIndexForBeat(step, s.bpi, numChords) !=
             Harmony::chordIndexForBeat(step - 1, s.bpi, numChords));

    if (!onChange && !Euclidean::hit(step, f.steps, f.pulses, f.rotation))
      continue;

    onsets.push_back(step);
    isChange.push_back(onChange);
  }

  for (size_t n = 0; n < onsets.size(); ++n) {
    const int step = onsets[n];
    const bool onChange = isChange[n];

    const int at = step * beatSamples;
    if (at >= numSamples)
      break;

    // Up to the next note, or the end of the interval.
    const int nextStep =
        (n + 1 < onsets.size()) ? onsets[n + 1] : f.steps;
    const int length =
        std::min(numSamples - at, (nextStep - step) * beatSamples);
    if (length <= 0)
      continue;

    const auto &chord = chordAtBeat(s, step);

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
    const double midi =
        kBassAnchorMidi + (double)chord.root + (double)semitoneAboveRoot;
    BotVoice::renderBass(out + at, length, s.sampleRate,
                         BotVoice::midiToHz(midi), 0.7f);
  }
}

void renderKeys(const Settings &s, float *out, int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0 || s.progression.empty())
    return;

  // One sustained chord per slot: held, not stabbed.
  int step = 0;
  while (step < s.bpi) {
    const int idx =
        Harmony::chordIndexForBeat(step, s.bpi, (int)s.progression.size());

    int end = step + 1;
    while (end < s.bpi &&
           Harmony::chordIndexForBeat(end, s.bpi,
                                      (int)s.progression.size()) == idx)
      ++end;

    const int at = step * beatSamples;
    if (at >= numSamples)
      break;
    const int length = std::min(numSamples - at, (end - step) * beatSamples);

    const auto &chord = s.progression[(size_t)idx];
    for (int t = 0; t < chord.toneCount; ++t) {
      // Around C4, above the bass and below where a soloist usually sits.
      const double midi =
          kKeysAnchorMidi + (double)chord.root + (double)chord.tones[(size_t)t];
      BotVoice::renderPad(out + at, length, s.sampleRate,
                          BotVoice::midiToHz(midi), 0.85f);
    }

    step = end;
  }
}

void renderLead(const Settings &s, int intervalIndex, float *out,
                int numSamples) {
  const int beatSamples = samplesPerBeat(s);
  if (beatSamples <= 0)
    return;

  const auto line = leadLine(s, intervalIndex);
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

    BotVoice::renderLead(out + at, length, s.sampleRate,
                         BotVoice::midiToHz((double)line[step]), velocity);
  }
}

} // namespace

void renderInterval(Voice voice, const Settings &s, int intervalIndex,
                    float *out, int numSamples) {
  if (out == nullptr || numSamples <= 0 || s.sampleRate <= 0.0 || s.bpi <= 0)
    return;

  // Negative indices would reflect the modulo arithmetic below onto the wrong
  // variation; the conductor counts up from zero, but nothing here should
  // depend on that.
  if (intervalIndex < 0)
    intervalIndex = 0;

  switch (voice) {
  case Voice::Drums:
    renderDrums(s, intervalIndex, out, numSamples);
    return;
  case Voice::Bass:
    renderBass(s, out, numSamples);
    return;
  case Voice::Keys:
    renderKeys(s, out, numSamples);
    return;
  case Voice::Lead:
    renderLead(s, intervalIndex, out, numSamples);
    return;
  }
}

} // namespace BotBand
