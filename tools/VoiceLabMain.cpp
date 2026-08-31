// antiphon-voicelab: render one bot voice to a WAV and measure it.
//
// A development instrument, not a shipped one. Physical models are tuned by ear
// over dozens of small changes, and the loop that existed before this -- edit a
// constant, rebuild, run the suite, write an audition -- was far too slow to
// converge on a sound.
//
// Every parameter is a flag, so trying a value costs no rebuild. It prints the
// same quantities the unit tests assert, from the same header
// (src/AudioMeasure.h), so tuning by ear and setting a test threshold use one
// instrument rather than two that can disagree (`PRINCIPLES §5`, `§8`).
//
// Follows tools/StemsMain.cpp: a console app with juce_audio_formats for the
// WAV writer and nothing that needs a display.

#include <JuceHeader.h>

#include "AudioMeasure.h"

#include <chrono>
#include <BotBand.h>
#include <BotVoice.h>
#include "MusicalKey.h"

#include <chalkwalk/music/Duration.h>

#include <array>

namespace {

struct Options {
  juce::String voice;
  juce::File out;
  double sampleRate = 48000.0;
  double seconds = 2.0;
  float velocity = 0.8f;
  int midiNote = 40; // E2, a bass note
  std::uint32_t seed = 1;
  int articulation = chalkwalk::music::kArticulationNatural;
  bool open = false;
  int repeats = 1;
  double spacing = 0.5;

  // Band mode.
  juce::String keyName = "C major";
  int bpm = 120, bpi = 8, bars = 4;

  // Play the tune out rather than stopping mid-groove: `bars` intervals of
  // groove, then the wrap-up and the resolve, in one file.
  //
  // Two intervals rendered on their own would answer the wrong question. What
  // is being judged is whether the ending sounds INTENDED, and that is a claim
  // about what came before it -- how far the keys thin from where they were,
  // whether the fill reads as a fill, whether the resolve lands as an arrival
  // or as a dropout with a note on the front. None of it is audible without
  // the groove in front of it.
  bool ending = false;

  // Render one voice's PART rather than the whole band, at the band's settings
  // and with the band's per-voice seed. Not the same as rendering that voice
  // on its own: what is being measured is what it contributes to the mix.
  // Empty renders all four.
  juce::String onlyVoice;

  // Bass articulation.
  BotVoice::BassTechnique technique = BotVoice::BassTechnique::Fingered;

  // Which polysynth patch. Named on the command line, or left alone to take
  // whatever --seed would have given the keyboard player.
  bool patchNamed = false;
  BotVoice::PadCharacter patchCharacter = BotVoice::PadCharacter::Poly;

  // And which instrument the soloist is holding.
  BotVoice::LeadInstrument instrument = BotVoice::LeadInstrument::Synth;
  bool instrumentNamed = false;

  // Sweep.
  juce::String sweepParam;
  double sweepLo = 0.0, sweepHi = 1.0;
  int sweepCount = 5;

  // Normalise the output to this integrated loudness, so two renders can be
  // compared for timbre without one of them simply being louder.
  bool matchLufs = false;
  double targetLufs = -18.0;
};

void usage() {
  std::printf(
      "AntiphonVoiceLab -- render and measure one bot voice\n"
      "\n"
      "  AntiphonVoiceLab <voice> [options]\n"
      "\n"
      "voices: kick snare hat bass lead pad kit keys solo band\n"
      "  file <paths...>  measure WAVs that already exist, and with --lufs\n"
      "                   write matched copies -- for comparing renders from\n"
      "                   builds you can no longer reproduce\n"
      "  kit, keys and band go through the real path -- with the kit's room "
      "and\n"
      "  the keyboard's chorus -- in stereo\n"
      "\n"
      "  -o <path>          output file, or directory when sweeping\n"
      "  --sr <rate>        sample rate (default 48000)\n"
      "  --seconds <s>      length of one hit or note (default 2)\n"
      "  --velocity <0..1>  how hard (default 0.8)\n"
      "  --note <name|midi> pitch for pitched voices: E1, A#2, Bb3, or 40\n"
      "  --seed <n>         noise seed, and the band's seed\n"
      "  --open             open hat\n"
      "  --technique <name> bass articulation: fingered, picked or muted\n"
      "  --patch <name>     polysynth patch: strings, brass or poly\n"
      "  --instrument <n>   what the soloist is holding: epiano, guitar, "
      "synth\n"
      "  --repeats <n>      render n hits (default 1)\n"
      "  --spacing <s>      seconds between repeats (default 0.5)\n"
      "  --sweep p=lo:hi:n  one file per value of p; p is velocity or note\n"
      "  --lufs <target>    normalise the output to this integrated loudness,\n"
      "                     so an A/B is about timbre and not about level\n"
      "\n"
      "bench: what one interval of each voice costs to synthesise. --bars is\n"
      "  the number of intervals timed per voice (default 2).\n\n"
      "band mode only:\n"
      "  --key <name>       C major, D minor, F# Dorian (default C major)\n"
      "  --only <voice>     render one part only: kit, bass, keys or lead\n"
      "  --ending           play the tune out: --bars of groove, then the\n"
      "                     wrap-up and the resolve, as the room hears them\n"
      "  --bpm <n> --bpi <n> --bars <n>\n"
      "  --articulation <n> 0 staccato, 50 as written, 100 legato\n"
      "\n"
      "lead analysis:\n"
      "  leadstats          the lead's melodic interval histogram --\n"
      "                     what the line actually DOES, seed after seed\n"
      "                     (--repeats seeds, --bars intervals each)\n"
      "\n"
      "Prints peak, rms, crest, fundamental and brightness for what it wrote.\n"
      "Those are the quantities the unit tests assert, measured the same "
      "way.\n");
}

// "E1", "A#2", "Bb3", or a plain MIDI number.
bool parseNote(const juce::String &text, int &midiOut) {
  const auto s = text.trim();
  if (s.isEmpty())
    return false;
  if (s.containsOnly("0123456789-")) {
    midiOut = s.getIntValue();
    return true;
  }

  static const char *letters = "CDEFGAB";
  static const int semis[7] = {0, 2, 4, 5, 7, 9, 11};
  const juce::juce_wchar raw = s[0];
  const juce::juce_wchar upper =
      (raw >= 'a' && raw <= 'z') ? (juce::juce_wchar)(raw - 32) : raw;
  const int idx = juce::String(letters).indexOfChar(upper);
  if (idx < 0)
    return false;

  int pc = semis[idx];
  int pos = 1;
  while (pos < s.length() && (s[pos] == '#' || s[pos] == 'b')) {
    pc += (s[pos] == '#') ? 1 : -1;
    ++pos;
  }
  if (pos >= s.length())
    return false;

  const int octave = s.substring(pos).getIntValue();
  midiOut = 12 * (octave + 1) + pc;
  return true;
}

// The patch to audition.
//
// Naming one on the command line does NOT override the fields of whatever the
// seed gave -- the ranges are per-character, so a strings patch with a brass
// label would be a sound the band can never produce. It walks the seed forward
// until it lands on the character asked for, so what gets rendered is always a
// patch the seed could really have chosen.
BotVoice::PadPatch patchFor(const Options &o, std::uint32_t seed) {
  auto patch = BotVoice::padPatchFor(seed);
  if (!o.patchNamed)
    return patch;

  for (int tries = 0; tries < 64 && patch.character != o.patchCharacter;
       ++tries)
    patch =
        BotVoice::padPatchFor(seed + 2654435761u * (std::uint32_t)(tries + 1));
  return patch;
}

// One hit or note of a single voice, rendered into a fresh buffer.
std::vector<float> renderOne(const Options &o) {
  const int hit = juce::jmax(1, (int)(o.seconds * o.sampleRate));
  const int gap = juce::jmax(0, (int)(o.spacing * o.sampleRate));
  const int total = hit + (o.repeats - 1) * juce::jmax(gap, 1);
  std::vector<float> buf((size_t)total, 0.0f);

  const double hz = BotVoice::midiToHz((double)o.midiNote);

  for (int r = 0; r < o.repeats; ++r) {
    const int at = r * gap;
    if (at >= total)
      break;
    float *out = buf.data() + at;
    const int room = total - at;
    const std::uint32_t seed = o.seed + 977u * (std::uint32_t)r;

    if (o.voice == "kick")
      BotVoice::renderKick(out, room, o.sampleRate, o.velocity);
    else if (o.voice == "snare")
      BotVoice::renderSnare(out, room, o.sampleRate, o.velocity, seed);
    else if (o.voice == "hat")
      BotVoice::renderHat(out, room, o.sampleRate, o.velocity, seed, o.open);
    else if (o.voice == "bass")
      BotVoice::renderBassString(out, juce::jmin(room, hit), o.sampleRate, hz,
                                 o.velocity,
                                 BotVoice::bassPatchFor(o.technique), seed);
    else if (o.voice == "lead") {
      const int span = juce::jmin(room, hit);
      BotVoice::LeadPatch patch;
      patch.instrument = o.instrument;
      BotVoice::renderLead(out, span, (int)(0.6 * span), o.sampleRate, hz,
                           o.velocity, patch, seed);
    } else if (o.voice == "pad") {
      const auto patch = patchFor(o, seed);
      if (r == 0)
        std::printf("  patch %s: detune %.1f cents, cutoff %.1f partials, "
                    "res %.2f, env x%.1f, attack %.0f ms, drive %.2f\n",
                    BotVoice::padCharacterName(patch.character),
                    patch.detuneCents, patch.cutoffPartials, patch.resonance,
                    patch.envAmount, 1000.0 * patch.attackSeconds, patch.drive);
      // Held for most of the render, so the release is heard as part of the
      // note rather than falling off the end of the file.
      const int span = juce::jmin(room, hit);
      BotVoice::renderPad(out, span, (int)(0.6 * span), o.sampleRate, hz,
                          o.velocity, patch, seed);
    }
  }
  return buf;
}

// The whole band through the real BotBand path, seeded the way PracticeRoom
// seeds it, so what comes out is what the room would hear.
// How many intervals the render covers, and what each one is.
//
// The order is `BandPlayState`'s, not an arrangement invented here: Playing
// until told otherwise, then Wrapping for exactly one interval and Resolving
// for exactly one. Rendering it any other way would tune the ending against
// something the room never plays.
int intervalCount(const Options &o) { return o.bars + (o.ending ? 2 : 0); }

BotBand::Phase phaseAt(const Options &o, int interval) {
  if (!o.ending)
    return BotBand::Phase::Groove;
  if (interval == o.bars)
    return BotBand::Phase::Wrapping;
  if (interval == o.bars + 1)
    return BotBand::Phase::Resolving;
  return BotBand::Phase::Groove;
}

const char *phaseName(BotBand::Phase p) {
  switch (p) {
  case BotBand::Phase::Groove:
    return "groove";
  case BotBand::Phase::Wrapping:
    return "wrap-up";
  case BotBand::Phase::Resolving:
    return "resolve";
  }
  return "groove";
}

// One voice through the real BotBand path -- with its room, and in stereo if it
// has one. Distinct from `renderOne`, which drives a bare BotVoice function and
// so hears the drum without the kit around it.
void renderVoice(const Options &o, BotBand::Voice voice,
                 std::vector<float> &left, std::vector<float> &right) {
  auto key = MusicalKey::parseName(o.keyName.toStdString());
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, o.seed);

  settings.articulation = o.articulation;
  const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;

  left.clear();
  right.clear();
  for (int interval = 0; interval < intervalCount(o); ++interval) {
    std::vector<float> l((size_t)n, 0.0f), r((size_t)n, 0.0f);
    BotBand::renderInterval(voice, settings, interval, phaseAt(o, interval),
                            l.data(), r.data(), n);
    if (!BotBand::isStereo(voice))
      r = l;
    left.insert(left.end(), l.begin(), l.end());
    right.insert(right.end(), r.begin(), r.end());
  }
}

// What one interval of each voice costs to synthesise, and the band together.
//
// The question "make a voice cheaper" cannot be started without this: the band
// is four voices and they are not equally expensive, so the first thing worth
// knowing is which one to look at. Whole intervals rather than a synthetic
// loop, because that is the unit the pump actually renders and the one
// the interval budget is expressed in.
int benchmarkBand(const Options &o) {
  auto key = MusicalKey::parseName(o.keyName.toStdString());
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
  const double intervalSeconds = (double)n / o.sampleRate;
  const int reps = juce::jmax(1, o.bars);

  std::printf("bench  %d bpm  %d bpi  %.2f s per interval  %d intervals each\n",
              o.bpm, o.bpi, intervalSeconds, reps);

  std::vector<float> l((size_t)n, 0.0f), r((size_t)n, 0.0f);
  double total = 0.0;

  for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                     BotBand::Voice::Keys, BotBand::Voice::Lead}) {
    std::uint32_t seed = o.seed;
    for (int step = 0; step < (int)voice; ++step)
      seed = seed * 1664525u + 1013904223u;
    auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, seed);
    settings.articulation = o.articulation;

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i)
      BotBand::renderInterval(voice, settings, i, BotBand::Phase::Groove,
                              l.data(), r.data(), n);
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count() / reps;
    total += ms;

    // Share of ONE interval of wall clock: what fraction of the time available
    // this voice spends. Four voices summing under 100% is the band keeping up
    // on one core, which is the property that matters.
    std::printf("  %-6s %8.1f ms  %5.1f%% of an interval\n",
                BotBand::voiceName(voice), ms,
                100.0 * ms / (intervalSeconds * 1000.0));
  }

  std::printf("  %-6s %8.1f ms  %5.1f%% of an interval\n", "BAND", total,
              100.0 * total / (intervalSeconds * 1000.0));
  return 0;
}

bool voiceMatches(BotBand::Voice v, const juce::String &name) {
  switch (v) {
  case BotBand::Voice::Drums:
    return name == "drums" || name == "kit";
  case BotBand::Voice::Bass:
    return name == "bass";
  case BotBand::Voice::Keys:
    return name == "keys";
  case BotBand::Voice::Lead:
    return name == "lead";
  }
  return false;
}

void renderBandStereo(const Options &o, std::vector<float> &mixL,
                      std::vector<float> &mixR) {
  auto key = MusicalKey::parseName(o.keyName.toStdString());
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  mixL.clear();
  mixR.clear();
  for (int interval = 0; interval < intervalCount(o); ++interval) {
    const auto phase = phaseAt(o, interval);
    std::vector<float> accL, accR;
    for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                       BotBand::Voice::Keys, BotBand::Voice::Lead}) {
      // The seed still advances for skipped voices, so --only renders exactly
      // the part that voice plays in the full band rather than a different one.
      if (o.onlyVoice.isNotEmpty() && !voiceMatches(voice, o.onlyVoice)) {
        continue;
      }
      std::uint32_t seed = o.seed;
      for (int step = 0; step < (int)voice; ++step)
        seed = seed * 1664525u + 1013904223u;

      auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, seed);

      settings.articulation = o.articulation;
      const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
      if (accL.empty()) {
        accL.assign((size_t)n, 0.0f);
        accR.assign((size_t)n, 0.0f);
      }

      std::vector<float> l((size_t)n, 0.0f), r((size_t)n, 0.0f);
      BotBand::renderInterval(voice, settings, interval, phase, l.data(),
                              r.data(), n);
      if (!BotBand::isStereo(voice))
        r = l;

      // The first groove interval, and then each ending interval: what is
      // being compared is the wrap-up and the resolve against the tune they
      // came out of, so all three want the same numbers.
      if (interval == 0 || (o.ending && interval >= o.bars)) {
        // As the pair that goes out: a mono voice is duplicated by the bot, so
        // measuring one channel would report it 3 LU under the kit for no
        // reason but arithmetic.
        const double lufs =
            AudioMeasure::integratedLufs(l.data(), r.data(), n, o.sampleRate);
        std::printf("  %-7s %-6s peak %.3f  rms %6.1f dBFS  %6.1f LUFS  "
                    "brightness %7.1f Hz%s\n",
                    phaseName(phase), BotBand::voiceName(voice),
                    AudioMeasure::peak(l.data(), n),
                    AudioMeasure::toDb(AudioMeasure::rms(l.data(), n)), lufs,
                    AudioMeasure::brightnessHz(l.data(), n, o.sampleRate),
                    BotBand::isStereo(voice) ? "  stereo" : "");
      }

      // The far end applies kDefaultRemoteChannelVolume to every remote
      // channel, so mix at that level or this is 12 dB hotter than the room.
      for (int j = 0; j < n; ++j) {
        accL[(size_t)j] += 0.25f * l[(size_t)j];
        accR[(size_t)j] += 0.25f * r[(size_t)j];
      }
    }
    mixL.insert(mixL.end(), accL.begin(), accL.end());
    mixR.insert(mixR.end(), accR.begin(), accR.end());
  }
}

std::vector<float> renderBand(const Options &o) {
  auto key = MusicalKey::parseName(o.keyName.toStdString());
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  std::vector<float> mix;
  for (int interval = 0; interval < intervalCount(o); ++interval) {
    const auto phase = phaseAt(o, interval);
    std::vector<float> acc;
    for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                       BotBand::Voice::Keys, BotBand::Voice::Lead}) {
      std::uint32_t s = o.seed;
      for (int step = 0; step < (int)voice; ++step)
        s = s * 1664525u + 1013904223u;

      auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, s);

      settings.articulation = o.articulation;
      const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
      if (acc.empty())
        acc.assign((size_t)n, 0.0f);

      std::vector<float> one((size_t)n, 0.0f);
      BotBand::renderInterval(voice, settings, interval, phase, one.data(),
                              nullptr, n);

      if (interval == 0 || (o.ending && interval >= o.bars)) {
        // Each voice on its own, before it is summed, so a problem can be
        // pinned on a player rather than on the band.
        std::printf("  %-7s %-6s peak %.3f  rms %.4f (%6.1f dBFS)  f0 %7.1f "
                    "Hz  brightness %7.1f Hz\n",
                    phaseName(phase), BotBand::voiceName(voice),
                    AudioMeasure::peak(one.data(), n),
                    AudioMeasure::rms(one.data(), n),
                    AudioMeasure::toDb(AudioMeasure::rms(one.data(), n)),
                    AudioMeasure::fundamentalHz(one.data(), n, o.sampleRate),
                    AudioMeasure::brightnessHz(one.data(), n, o.sampleRate));
      }

      // The far end applies kDefaultRemoteChannelVolume to every remote
      // channel, so mix at that level or this is 12 dB hotter than the room.
      for (int j = 0; j < n; ++j)
        acc[(size_t)j] += 0.25f * one[(size_t)j];
    }
    mix.insert(mix.end(), acc.begin(), acc.end());
  }
  return mix;
}

void report(const juce::String &label, const std::vector<float> &buf,
            double sampleRate, const std::vector<float> *right = nullptr) {
  const int n = (int)buf.size();

  // Measured as the pair that actually goes out, because a bot always
  // transmits two channels -- so a mono voice is measured duplicated, which is
  // what the listener hears, rather than 3 LU quieter than the kit for no
  // reason but arithmetic.
  const double lufs =
      right != nullptr
          ? AudioMeasure::integratedLufs(buf.data(), right->data(), n,
                                         sampleRate)
          : AudioMeasure::integratedLufs(buf.data(), buf.data(), n, sampleRate);

  juce::String loudness = lufs <= AudioMeasure::kSilenceLufs
                              ? juce::String("  --  ")
                              : juce::String(lufs, 1);

  std::printf("%-22s peak %.3f  rms %.4f (%6.1f dBFS)  %6s LUFS  crest %.2f  "
              "f0 %7.1f Hz  brightness %7.1f Hz\n",
              label.toRawUTF8(), AudioMeasure::peak(buf.data(), n),
              AudioMeasure::rms(buf.data(), n),
              AudioMeasure::toDb(AudioMeasure::rms(buf.data(), n)),
              loudness.toRawUTF8(), AudioMeasure::crest(buf.data(), n),
              AudioMeasure::fundamentalHz(buf.data(), n, sampleRate),
              AudioMeasure::brightnessHz(buf.data(), n, sampleRate));

  // A bare voice has no ceiling on it -- that lives in BotBand, so what the
  // band renders can never clip and what the lab renders can. Overlapping
  // repeats are the usual way to get there, and a clipped file listened to as
  // a comparison is a comparison of the clipping.
  if (AudioMeasure::peak(buf.data(), n) > 0.99f)
    std::printf("  WARNING: peaks at %.2f and will clip in the file. Lower "
                "--velocity, or space the repeats so they do not overlap.\n",
                AudioMeasure::peak(buf.data(), n));
}

// Bring a render onto a target loudness, so an A/B is about timbre rather than
// about which one is louder. Reports what it did, because a comparison that
// silently changed the level is a comparison you cannot trust.
void matchLoudness(const Options &o, std::vector<float> &left,
                   std::vector<float> *right) {
  if (!o.matchLufs || left.empty())
    return;

  const int n = (int)left.size();
  const double measured =
      right != nullptr ? AudioMeasure::integratedLufs(
                             left.data(), right->data(), n, o.sampleRate)
                       : AudioMeasure::integratedLufs(left.data(), left.data(),
                                                      n, o.sampleRate);
  if (measured <= AudioMeasure::kSilenceLufs) {
    std::printf("  (too short or too quiet to match loudness)\n");
    return;
  }

  const double gain = AudioMeasure::gainForLufs(measured, o.targetLufs);
  for (auto &x : left)
    x = (float)(x * gain);
  if (right != nullptr)
    for (auto &x : *right)
      x = (float)(x * gain);

  std::printf("  matched %.1f -> %.1f LUFS (%+.1f dB)\n", measured,
              o.targetLufs, 20.0 * std::log10(gain));

  // A loudness target and a peak ceiling are different things, and a sparse
  // percussive voice hits the second long before the first: matching a hi-hat
  // to -18 LUFS wants +11 dB and sends its peaks to 1.5. The file would be
  // clipped on the way out and the comparison would be of distortion, so say
  // so and name the target that would have fitted.
  const float peak = AudioMeasure::peak(left.data(), n);
  if (peak > 0.99f) {
    const double headroom = 20.0 * std::log10((double)peak);
    std::printf("  WARNING: peaks at %.2f, so this file WILL clip. This voice "
                "is too sparse for %.1f LUFS -- try --lufs %.1f\n",
                peak, o.targetLufs, o.targetLufs - headroom - 0.5);
  }
}

bool writeWav(const juce::File &file, const std::vector<float> &buf,
              double sampleRate,
              const std::vector<float> *rightChannel = nullptr) {
  file.deleteFile();
  file.getParentDirectory().createDirectory();

  juce::WavAudioFormat wav;
  std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
  if (stream == nullptr)
    return false;

  const int channels = rightChannel != nullptr ? 2 : 1;
  std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
      stream.release(), sampleRate, (unsigned)channels, 24, {}, 0));
  if (writer == nullptr)
    return false;

  juce::AudioBuffer<float> out(channels, (int)buf.size());
  for (int i = 0; i < (int)buf.size(); ++i) {
    out.setSample(0, i, buf[(size_t)i]);
    if (channels > 1)
      out.setSample(1, i,
                    i < (int)rightChannel->size() ? (*rightChannel)[(size_t)i]
                                                  : 0.0f);
  }
  writer->writeFromAudioSampleBuffer(out, 0, out.getNumSamples());
  return true;
}

// Measure a WAV that already exists, and optionally write a loudness-matched
// copy of it.
//
// The point of this is comparing renders that CANNOT be regenerated: a band
// from three commits ago is a file and nothing else, and the only fair way to
// A/B it against today's is to bring both to the same integrated loudness.
int measureFile(const Options &o, const juce::File &input) {
  juce::WavAudioFormat wav;
  std::unique_ptr<juce::AudioFormatReader> reader(
      wav.createReaderFor(new juce::FileInputStream(input), true));
  if (reader == nullptr) {
    std::fprintf(stderr, "voicelab: could not read %s\n",
                 input.getFullPathName().toRawUTF8());
    return 1;
  }

  const int n = (int)reader->lengthInSamples;
  const int channels = (int)reader->numChannels;
  const double rate = reader->sampleRate;

  juce::AudioBuffer<float> buf(juce::jmax(1, channels), juce::jmax(1, n));
  buf.clear();
  reader->read(&buf, 0, n, 0, true, channels > 1);

  std::vector<float> left((size_t)n), right((size_t)n);
  for (int i = 0; i < n; ++i) {
    left[(size_t)i] = buf.getSample(0, i);
    right[(size_t)i] = channels > 1 ? buf.getSample(1, i) : buf.getSample(0, i);
  }

  Options local = o;
  local.sampleRate = rate;

  const double before =
      AudioMeasure::integratedLufs(left.data(), right.data(), n, rate);
  std::printf("%-34s %2d ch  %5.0f Hz  %6.2f s  peak %.3f  %6.1f LUFS\n",
              input.getFileName().toRawUTF8(), channels, rate, (double)n / rate,
              AudioMeasure::peak(left.data(), n), before);

  if (!o.matchLufs)
    return 0;

  matchLoudness(local, left, &right);

  const juce::File out =
      o.out != juce::File()
          ? o.out
          : input.getSiblingFile(input.getFileNameWithoutExtension() +
                                 "-matched.wav");
  if (!writeWav(out, left, rate, &right)) {
    std::fprintf(stderr, "voicelab: could not write %s\n",
                 out.getFullPathName().toRawUTF8());
    return 1;
  }
  std::printf("  wrote %s\n", out.getFullPathName().toRawUTF8());
  return 0;
}

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI juceInit;

  if (argc < 2) {
    usage();
    return 1;
  }

  Options o;
  juce::StringArray files;
  o.voice = juce::String(argv[1]).toLowerCase();
  if (o.voice == "-h" || o.voice == "--help") {
    usage();
    return 0;
  }

  for (int i = 2; i < argc; ++i) {
    const juce::String arg(argv[i]);
    auto next = [&]() -> juce::String {
      return (i + 1 < argc) ? juce::String(argv[++i]) : juce::String();
    };

    if (arg == "-o")
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile(next());
    else if (arg == "--sr")
      o.sampleRate = next().getDoubleValue();
    else if (arg == "--seconds")
      o.seconds = next().getDoubleValue();
    else if (arg == "--velocity")
      o.velocity = (float)next().getDoubleValue();
    else if (arg == "--articulation")
      o.articulation = next().getIntValue();
    else if (arg == "--seed")
      o.seed = (std::uint32_t)next().getLargeIntValue();
    else if (arg == "--open")
      o.open = true;
    else if (arg == "--repeats")
      o.repeats = next().getIntValue();
    else if (arg == "--spacing")
      o.spacing = next().getDoubleValue();
    else if (arg == "--key")
      o.keyName = next();
    else if (arg == "--bpm")
      o.bpm = next().getIntValue();
    else if (arg == "--bpi")
      o.bpi = next().getIntValue();
    else if (arg == "--bars")
      o.bars = next().getIntValue();
    else if (arg == "--only")
      o.onlyVoice = next().toLowerCase();
    else if (arg == "--ending")
      o.ending = true;
    else if (arg == "--technique") {
      const auto name = next().toLowerCase();
      if (name == "picked")
        o.technique = BotVoice::BassTechnique::Picked;
      else if (name == "muted")
        o.technique = BotVoice::BassTechnique::Muted;
      else if (name == "fingered")
        o.technique = BotVoice::BassTechnique::Fingered;
      else {
        std::fprintf(stderr,
                     "voicelab: technique is fingered, picked or muted\n");
        return 1;
      }
    } else if (arg == "--instrument") {
      const auto name = next().toLowerCase();
      o.instrumentNamed = true;
      if (name == "epiano" || name == "piano")
        o.instrument = BotVoice::LeadInstrument::EPiano;
      else if (name == "guitar")
        o.instrument = BotVoice::LeadInstrument::Guitar;
      else if (name == "synth")
        o.instrument = BotVoice::LeadInstrument::Synth;
      else {
        std::fprintf(stderr,
                     "voicelab: instrument is epiano, guitar or synth\n");
        return 1;
      }
    } else if (arg == "--patch") {
      const auto name = next().toLowerCase();
      o.patchNamed = true;
      if (name == "strings")
        o.patchCharacter = BotVoice::PadCharacter::Strings;
      else if (name == "brass")
        o.patchCharacter = BotVoice::PadCharacter::Brass;
      else if (name == "poly")
        o.patchCharacter = BotVoice::PadCharacter::Poly;
      else {
        std::fprintf(stderr, "voicelab: patch is strings, brass or poly\n");
        return 1;
      }
    } else if (arg == "--lufs") {
      o.matchLufs = true;
      o.targetLufs = next().getDoubleValue();
    } else if (arg == "--note") {
      if (!parseNote(next(), o.midiNote)) {
        std::fprintf(stderr, "voicelab: not a note\n");
        return 1;
      }
    } else if (arg == "--sweep") {
      const auto spec = next();
      const int eq = spec.indexOfChar('=');
      if (eq <= 0) {
        std::fprintf(stderr, "voicelab: --sweep wants name=lo:hi:count\n");
        return 1;
      }
      o.sweepParam = spec.substring(0, eq);
      const auto parts =
          juce::StringArray::fromTokens(spec.substring(eq + 1), ":", "");
      if (parts.size() != 3) {
        std::fprintf(stderr, "voicelab: --sweep wants name=lo:hi:count\n");
        return 1;
      }
      o.sweepLo = parts[0].getDoubleValue();
      o.sweepHi = parts[1].getDoubleValue();
      o.sweepCount = juce::jmax(1, parts[2].getIntValue());
    } else if (arg.startsWithChar('-')) {
      std::fprintf(stderr, "voicelab: unknown option %s\n", arg.toRawUTF8());
      return 1;
    } else {
      files.add(arg);
    }
  }

  // `file` takes a path rather than being a voice, so it is handled before the
  // list of things that can be rendered.
  if (o.voice == "file") {
    if (files.isEmpty()) {
      std::fprintf(stderr, "voicelab: file needs a path\n");
      return 1;
    }
    int failures = 0;
    for (const auto &path : files)
      failures += measureFile(
          o, juce::File::getCurrentWorkingDirectory().getChildFile(path));
    return failures;
  }

  const juce::StringArray known{"kick", "snare", "hat",      "bass",
                                "lead", "pad",   "kit",      "keys",
                                "solo", "band",  "leadstats", "bench"};
  if (!known.contains(o.voice)) {
    std::fprintf(stderr, "voicelab: unknown voice %s\n", o.voice.toRawUTF8());
    usage();
    return 1;
  }
  if (o.sampleRate <= 0.0) {
    std::fprintf(stderr, "voicelab: sample rate must be positive\n");
    return 1;
  }

  // leadstats: what shape is the line, measured rather than described.
  //
  // "It leaps oddly" is a real complaint and not a testable one. This counts
  // every melodic interval across a sweep of seeds and prints the histogram,
  // which turns a judgement about one bar into a number that moves when the
  // objective changes.
  if (o.voice == "leadstats") {
    auto key = MusicalKey::parseName(o.keyName.toStdString());
    if (!key.valid)
      key = MusicalKey::parseName("C major");

    int notes = 0, rests = 0, moves = 0;
    long long totalMotion = 0;
    int biggest = 0;
    std::array<int, 64> hist{};
    int reversals = 0, continuations = 0;
    // A repeated note over a chord that CHANGED is a common tone -- the same
    // pitch re-heard as a new colour, which is a melodic device. A repeated
    // note over the same chord is just standing still. The cost table cannot
    // tell them apart, so count them apart.
    int repeatSameChord = 0, repeatNewChord = 0;
    int stepSameChord = 0, stepNewChord = 0;
    // How much of the space between two onsets the note actually fills. Under
    // the old rule this was always 1.0 except for a colour note; the shared
    // duration model gives a downbeat more room than an off-beat, which is
    // articulation rather than note choice and is worth seeing separately.
    long long fillGap = 0, fillHeld = 0;
    int shortened = 0, sounded = 0;

    const int seeds = juce::jmax(1, o.repeats);
    for (int sd = 0; sd < seeds; ++sd) {
      auto s = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate,
                                 o.seed + (std::uint32_t)sd);
      s.articulation = o.articulation;

      // The line is continuous across intervals, so the interval between the
      // last note of one and the first of the next is a real melodic move and
      // is counted as one.
      const auto layout = Harmony::layoutChart(s.chart, s.bpi);
      int last = -1, lastMove = 0, lastChordRoot = -999, lastChordTones = -1;
      for (int interval = 0; interval < o.bars; ++interval) {
        int step = -1;
        for (int n : BotBand::leadLine(s, interval)) {
          ++step;
          if (n < 0) {
            ++rests;
            continue;
          }
          const auto &ch = Harmony::chordAtStep(layout, step);
          int tonesKey = ch.toneCount;
          for (int t = 0; t < ch.toneCount; ++t)
            tonesKey = tonesKey * 31 + ch.tones[(size_t)t];
          const bool chordChanged =
              (ch.root != lastChordRoot || tonesKey != lastChordTones);
          {
            const auto lineNow = BotBand::leadLine(s, interval);
            size_t nx = (size_t)step + 1;
            while (nx < lineNow.size() && lineNow[nx] < 0)
              ++nx;
            const int beatSamples = (int)(o.sampleRate * 60.0 / o.bpm);
            const int eighth = beatSamples / 2;
            const int gap = (int)(nx - (size_t)step) * eighth;
            const auto sd = BotBand::toSoundingChord(ch);
            const auto tr = chalkwalk::music::tierOf(BotBand::toKeySig(s.key),
                                                     ((n % 12) + 12) % 12, sd);
            const int want = chalkwalk::music::holdIn(
                chalkwalk::music::holdTicks(
                    BotBand::metricStrength(step, s.bpi), tr),
                beatSamples);
            const int held =
                chalkwalk::music::articulate(want, gap, s.articulation);
            fillGap += gap;
            fillHeld += held;
            ++sounded;
            if (held < gap)
              ++shortened;
          }
          lastChordRoot = ch.root;
          lastChordTones = tonesKey;

          ++notes;
          if (last >= 0) {
            const int d = n - last;
            ++moves;
            totalMotion += std::abs(d);
            biggest = juce::jmax(biggest, std::abs(d));
            hist[(size_t)juce::jmin(63, std::abs(d))]++;
            if (d != 0 && lastMove != 0) {
              if ((d > 0) == (lastMove > 0))
                ++continuations;
              else
                ++reversals;
            }
            if (d != 0)
              lastMove = d;
            if (d == 0) {
              if (chordChanged)
                ++repeatNewChord;
              else
                ++repeatSameChord;
            } else {
              if (chordChanged)
                ++stepNewChord;
              else
                ++stepSameChord;
            }
          }
          last = n;
        }
      }
    }

    std::printf(
        "leadstats  %s  %d bpm  %d bpi  seeds %u..%u  %d intervals each\n",
        o.keyName.toRawUTF8(), o.bpm, o.bpi, (unsigned)o.seed,
        (unsigned)(o.seed + (std::uint32_t)seeds - 1), o.bars);
    std::printf("  notes %d   rests %d   moves %d\n", notes, rests, moves);
    if (moves > 0) {
      std::printf("  mean |interval|   %.2f semitones\n",
                  (double)totalMotion / moves);
      std::printf("  largest           %d\n", biggest);
      int stepwise = 0, leaps = 0, wide = 0;
      for (size_t d = 0; d < hist.size(); ++d) {
        if (d <= 2)
          stepwise += hist[d];
        else if (d <= 7)
          leaps += hist[d];
        else
          wide += hist[d];
      }
      std::printf("  stepwise (<=2)    %5d  %5.1f%%\n", stepwise,
                  100.0 * stepwise / moves);
      std::printf("  small leap (3-7)  %5d  %5.1f%%\n", leaps,
                  100.0 * leaps / moves);
      std::printf("  wide (>=8)        %5d  %5.1f%%\n", wide,
                  100.0 * wide / moves);
      const int repeats = repeatSameChord + repeatNewChord;
      std::printf("  repeated notes    %5d  %5.1f%%   of which %d over a NEW "
                  "chord (%.1f%%) and %d over the same (%.1f%%)\n",
                  repeats, 100.0 * repeats / moves, repeatNewChord,
                  repeats ? 100.0 * repeatNewChord / repeats : 0.0,
                  repeatSameChord,
                  repeats ? 100.0 * repeatSameChord / repeats : 0.0);
      if (sounded > 0)
        std::printf(
            "  note fills        %5.1f%% of the space to the next onset;"
            " %d of %d shortened\n",
            100.0 * (double)fillHeld / (double)fillGap, shortened, sounded);
      const int chordChanges = repeatNewChord + stepNewChord;
      std::printf("  chord changed under %5.1f%% of moves\n",
                  100.0 * chordChanges / moves);
      if (continuations + reversals > 0)
        std::printf("  direction kept    %5.1f%%  (of %d turns)\n",
                    100.0 * continuations / (continuations + reversals),
                    continuations + reversals);
      std::printf("  histogram:\n");
      for (size_t d = 0; d < hist.size(); ++d)
        if (hist[d] > 0)
          std::printf("   %3d  %5d  %5.1f%%  %s\n", (int)d, hist[d],
                      100.0 * hist[d] / moves,
                      juce::String::repeatedString(
                          "#", juce::jmax(0, (int)(200.0 * hist[d] / moves)))
                          .toRawUTF8());
    }
    return 0;
  }

  if (o.voice == "bench")
    return benchmarkBand(o);

  if (o.voice == "band") {
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile("band.wav");
    std::printf("band  %s  %d bpm  %d bpi  seed %u\n", o.keyName.toRawUTF8(),
                o.bpm, o.bpi, (unsigned)o.seed);
    std::vector<float> mix, mixR;
    renderBandStereo(o, mix, mixR);
    matchLoudness(o, mix, &mixR);
    report("band (mixed)", mix, o.sampleRate, &mixR);
    if (!writeWav(o.out, mix, o.sampleRate, &mixR)) {
      std::fprintf(stderr, "voicelab: could not write %s\n",
                   o.out.getFullPathName().toRawUTF8());
      return 1;
    }
    std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
    return 0;
  }

  if (o.voice == "solo") {
    // The lead through the real path, so the instrument, the note choices and
    // the ring-on are all the ones the room would hear.
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile("solo.wav");

    auto key = MusicalKey::parseName(o.keyName.toStdString());
    if (!key.valid)
      key = MusicalKey::parseName("C major");
    auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, o.seed);
    settings.articulation = o.articulation;
    if (o.instrumentNamed)
      settings.leadOverride = (int)o.instrument;

    std::printf(
        "solo  seed %u  %s\n", (unsigned)o.seed,
        BotVoice::leadInstrumentName(BotBand::leadInstrument(settings)));

    const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
    std::vector<float> mix;
    for (int interval = 0; interval < o.bars; ++interval) {
      std::vector<float> one((size_t)n, 0.0f);
      BotBand::renderInterval(BotBand::Voice::Lead, settings, interval,
                              one.data(), n);
      mix.insert(mix.end(), one.begin(), one.end());
    }
    matchLoudness(o, mix, nullptr);
    report("solo", mix, o.sampleRate);
    if (!writeWav(o.out, mix, o.sampleRate)) {
      std::fprintf(stderr, "voicelab: could not write %s\n",
                   o.out.getFullPathName().toRawUTF8());
      return 1;
    }
    std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
    return 0;
  }

  if (o.voice == "kit" || o.voice == "keys") {
    const bool isKeys = o.voice == "keys";
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile(o.voice +
                                                                    ".wav");

    if (isKeys) {
      auto key = MusicalKey::parseName(o.keyName.toStdString());
      if (!key.valid)
        key = MusicalKey::parseName("C major");

      // --patch here means "find me a seed whose keyboard player brought
      // that", rather than overriding what the seed chose. The band's patch
      // has to stay a pure function of its seed or the audition would be of a
      // sound the room can never produce.
      if (o.patchNamed)
        for (int tries = 0; tries < 64; ++tries) {
          const auto probe =
              BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, o.seed);
          if (BotBand::keysPatch(probe).character == o.patchCharacter)
            break;
          o.seed += 1u;
        }

      auto settings =
          BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, o.seed);

      settings.articulation = o.articulation;
      const auto patch = BotBand::keysPatch(settings);
      std::printf("keys  seed %u  patch %s: detune %.1f cents, cutoff %.1f "
                  "partials, res %.2f, env x%.1f, attack %.0f ms, drive %.2f\n",
                  (unsigned)o.seed, BotVoice::padCharacterName(patch.character),
                  patch.detuneCents, patch.cutoffPartials, patch.resonance,
                  patch.envAmount, 1000.0 * patch.attackSeconds, patch.drive);
    }

    std::vector<float> l, r;
    renderVoice(o, isKeys ? BotBand::Voice::Keys : BotBand::Voice::Drums, l, r);
    matchLoudness(o, l, &r);
    report(isKeys ? "keys (with chorus)" : "kit (with room)", l, o.sampleRate,
           &r);
    if (!writeWav(o.out, l, o.sampleRate, &r)) {
      std::fprintf(stderr, "voicelab: could not write %s\n",
                   o.out.getFullPathName().toRawUTF8());
      return 1;
    }
    std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
    return 0;
  }

  if (o.sweepParam.isNotEmpty()) {
    // A directory of renders and a manifest, so a sweep can be listened to in
    // order and read as numbers afterwards.
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile("sweep");
    o.out.createDirectory();

    juce::StringArray manifest;
    for (int i = 0; i < o.sweepCount; ++i) {
      const double t =
          o.sweepCount == 1 ? 0.0 : (double)i / (double)(o.sweepCount - 1);
      const double value = o.sweepLo + t * (o.sweepHi - o.sweepLo);

      Options step = o;
      if (o.sweepParam == "velocity")
        step.velocity = (float)value;
      else if (o.sweepParam == "note")
        step.midiNote = (int)std::lround(value);
      else if (o.sweepParam == "seed")
        step.seed = (std::uint32_t)std::lround(value);
      else {
        std::fprintf(stderr, "voicelab: cannot sweep %s\n",
                     o.sweepParam.toRawUTF8());
        return 1;
      }

      const auto buf = renderOne(step);
      const auto name =
          o.voice + "-" + o.sweepParam + "-" + juce::String(value, 3) + ".wav";
      const auto file = o.out.getChildFile(name);
      if (!writeWav(file, buf, o.sampleRate)) {
        std::fprintf(stderr, "voicelab: could not write %s\n",
                     file.getFullPathName().toRawUTF8());
        return 1;
      }
      report(name, buf, o.sampleRate);
      manifest.add(
          name + "  " + o.sweepParam + "=" + juce::String(value, 3) +
          "  peak " +
          juce::String(AudioMeasure::peak(buf.data(), (int)buf.size()), 3) +
          "  rms " +
          juce::String(AudioMeasure::rms(buf.data(), (int)buf.size()), 4));
    }

    const auto index = o.out.getChildFile("index.txt");
    index.replaceWithText(manifest.joinIntoString("\n") + "\n");
    std::printf("wrote %d files, manifest at %s\n", o.sweepCount,
                index.getFullPathName().toRawUTF8());
    return 0;
  }

  if (o.out == juce::File())
    o.out =
        juce::File::getCurrentWorkingDirectory().getChildFile(o.voice + ".wav");

  auto buf = renderOne(o);
  matchLoudness(o, buf, nullptr);
  report(o.voice, buf, o.sampleRate);
  if (!writeWav(o.out, buf, o.sampleRate)) {
    std::fprintf(stderr, "voicelab: could not write %s\n",
                 o.out.getFullPathName().toRawUTF8());
    return 1;
  }
  std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
  return 0;
}
