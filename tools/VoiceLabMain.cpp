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
#include "BotBand.h"
#include "BotVoice.h"
#include "MusicalKey.h"

namespace {

struct Options {
  juce::String voice;
  juce::File out;
  double sampleRate = 48000.0;
  double seconds = 2.0;
  float velocity = 0.8f;
  int midiNote = 40; // E2, a bass note
  std::uint32_t seed = 1;
  bool open = false;
  int repeats = 1;
  double spacing = 0.5;

  // Band mode.
  juce::String keyName = "C major";
  int bpm = 120, bpi = 8, bars = 4;

  // Sweep.
  juce::String sweepParam;
  double sweepLo = 0.0, sweepHi = 1.0;
  int sweepCount = 5;
};

void usage() {
  std::printf(
      "AntiphonVoiceLab -- render and measure one bot voice\n"
      "\n"
      "  AntiphonVoiceLab <voice> [options]\n"
      "\n"
      "voices: kick snare hat bass lead pad kit band\n"
      "  kit and band go through the real path, with the room, in stereo\n"
      "\n"
      "  -o <path>          output file, or directory when sweeping\n"
      "  --sr <rate>        sample rate (default 48000)\n"
      "  --seconds <s>      length of one hit or note (default 2)\n"
      "  --velocity <0..1>  how hard (default 0.8)\n"
      "  --note <name|midi> pitch for pitched voices: E1, A#2, Bb3, or 40\n"
      "  --seed <n>         noise seed, and the band's seed\n"
      "  --open             open hat\n"
      "  --repeats <n>      render n hits (default 1)\n"
      "  --spacing <s>      seconds between repeats (default 0.5)\n"
      "  --sweep p=lo:hi:n  one file per value of p; p is velocity or note\n"
      "\n"
      "band mode only:\n"
      "  --key <name>       C major, D minor, F# Dorian (default C major)\n"
      "  --bpm <n> --bpi <n> --bars <n>\n"
      "\n"
      "Prints peak, rms, crest, fundamental and brightness for what it wrote.\n"
      "Those are the quantities the unit tests assert, measured the same way.\n");
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
      BotVoice::renderBass(out, juce::jmin(room, hit), o.sampleRate, hz,
                           o.velocity);
    else if (o.voice == "lead")
      BotVoice::renderLead(out, juce::jmin(room, hit), o.sampleRate, hz,
                           o.velocity);
    else if (o.voice == "pad")
      BotVoice::renderPad(out, juce::jmin(room, hit), o.sampleRate, hz,
                          o.velocity);
  }
  return buf;
}

// The whole band through the real BotBand path, seeded the way PracticeRoom
// seeds it, so what comes out is what the room would hear.
// One voice through the real BotBand path -- with its room, and in stereo if it
// has one. Distinct from `renderOne`, which drives a bare BotVoice function and
// so hears the drum without the kit around it.
void renderVoice(const Options &o, BotBand::Voice voice,
                 std::vector<float> &left, std::vector<float> &right) {
  auto key = MusicalKey::parseName(o.keyName);
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  const auto settings = BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, o.seed);
  const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;

  left.clear();
  right.clear();
  for (int interval = 0; interval < o.bars; ++interval) {
    std::vector<float> l((size_t)n, 0.0f), r((size_t)n, 0.0f);
    BotBand::renderInterval(voice, settings, interval, l.data(), r.data(), n);
    if (!BotBand::isStereo(voice))
      r = l;
    left.insert(left.end(), l.begin(), l.end());
    right.insert(right.end(), r.begin(), r.end());
  }
}

void renderBandStereo(const Options &o, std::vector<float> &mixL,
                      std::vector<float> &mixR) {
  auto key = MusicalKey::parseName(o.keyName);
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  mixL.clear();
  mixR.clear();
  for (int interval = 0; interval < o.bars; ++interval) {
    std::vector<float> accL, accR;
    for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                       BotBand::Voice::Keys, BotBand::Voice::Lead}) {
      std::uint32_t seed = o.seed;
      for (int step = 0; step < (int)voice; ++step)
        seed = seed * 1664525u + 1013904223u;

      const auto settings =
          BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, seed);
      const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
      if (accL.empty()) {
        accL.assign((size_t)n, 0.0f);
        accR.assign((size_t)n, 0.0f);
      }

      std::vector<float> l((size_t)n, 0.0f), r((size_t)n, 0.0f);
      BotBand::renderInterval(voice, settings, interval, l.data(), r.data(), n);
      if (!BotBand::isStereo(voice))
        r = l;

      if (interval == 0) {
        std::printf("  %-6s peak %.3f  rms %.4f (%6.1f dBFS)  brightness %7.1f Hz%s\n",
                    BotBand::voiceName(voice), AudioMeasure::peak(l.data(), n),
                    AudioMeasure::rms(l.data(), n),
                    AudioMeasure::toDb(AudioMeasure::rms(l.data(), n)),
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
  auto key = MusicalKey::parseName(o.keyName);
  if (!key.valid)
    key = MusicalKey::parseName("C major");

  std::vector<float> mix;
  for (int interval = 0; interval < o.bars; ++interval) {
    std::vector<float> acc;
    for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                       BotBand::Voice::Keys, BotBand::Voice::Lead}) {
      std::uint32_t s = o.seed;
      for (int step = 0; step < (int)voice; ++step)
        s = s * 1664525u + 1013904223u;

      const auto settings =
          BotBand::defaults(key, o.bpm, o.bpi, o.sampleRate, s);
      const int n = (int)(o.sampleRate * 60.0 / o.bpm) * o.bpi;
      if (acc.empty())
        acc.assign((size_t)n, 0.0f);

      std::vector<float> one((size_t)n, 0.0f);
      BotBand::renderInterval(voice, settings, interval, one.data(), n);

      if (interval == 0) {
        // Each voice on its own, before it is summed, so a problem can be
        // pinned on a player rather than on the band.
        std::printf("  %-6s peak %.3f  rms %.4f (%6.1f dBFS)  f0 %7.1f Hz  "
                    "brightness %7.1f Hz\n",
                    BotBand::voiceName(voice),
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
            double sampleRate) {
  const int n = (int)buf.size();
  std::printf("%-22s peak %.3f  rms %.4f (%6.1f dBFS)  crest %.2f  "
              "f0 %7.1f Hz  brightness %7.1f Hz\n",
              label.toRawUTF8(), AudioMeasure::peak(buf.data(), n),
              AudioMeasure::rms(buf.data(), n),
              AudioMeasure::toDb(AudioMeasure::rms(buf.data(), n)),
              AudioMeasure::crest(buf.data(), n),
              AudioMeasure::fundamentalHz(buf.data(), n, sampleRate),
              AudioMeasure::brightnessHz(buf.data(), n, sampleRate));
}

bool writeWav(const juce::File &file, const std::vector<float> &buf,
              double sampleRate, const std::vector<float> *rightChannel = nullptr) {
  file.deleteFile();
  file.getParentDirectory().createDirectory();

  juce::WavAudioFormat wav;
  std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
  if (stream == nullptr)
    return false;

  const int channels = rightChannel != nullptr ? 2 : 1;
  std::unique_ptr<juce::AudioFormatWriter> writer(
      wav.createWriterFor(stream.release(), sampleRate, (unsigned)channels, 24,
                          {}, 0));
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

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI juceInit;

  if (argc < 2) {
    usage();
    return 1;
  }

  Options o;
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
    else if (arg == "--note") {
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
    } else {
      std::fprintf(stderr, "voicelab: unknown option %s\n", arg.toRawUTF8());
      return 1;
    }
  }

  const juce::StringArray known{"kick", "snare", "hat",  "bass", "lead",
                                "pad",  "kit",   "band"};
  if (!known.contains(o.voice)) {
    std::fprintf(stderr, "voicelab: unknown voice %s\n", o.voice.toRawUTF8());
    usage();
    return 1;
  }
  if (o.sampleRate <= 0.0) {
    std::fprintf(stderr, "voicelab: sample rate must be positive\n");
    return 1;
  }

  if (o.voice == "band") {
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile("band.wav");
    std::printf("band  %s  %d bpm  %d bpi  seed %u\n", o.keyName.toRawUTF8(),
                o.bpm, o.bpi, (unsigned)o.seed);
    std::vector<float> mix, mixR;
    renderBandStereo(o, mix, mixR);
    report("band (mixed)", mix, o.sampleRate);
    if (!writeWav(o.out, mix, o.sampleRate, &mixR)) {
      std::fprintf(stderr, "voicelab: could not write %s\n",
                   o.out.getFullPathName().toRawUTF8());
      return 1;
    }
    std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
    return 0;
  }

  if (o.voice == "kit") {
    if (o.out == juce::File())
      o.out = juce::File::getCurrentWorkingDirectory().getChildFile("kit.wav");
    std::vector<float> l, r;
    renderVoice(o, BotBand::Voice::Drums, l, r);
    report("kit (with room)", l, o.sampleRate);
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
      const auto name = o.voice + "-" + o.sweepParam + "-" +
                        juce::String(value, 3) + ".wav";
      const auto file = o.out.getChildFile(name);
      if (!writeWav(file, buf, o.sampleRate)) {
        std::fprintf(stderr, "voicelab: could not write %s\n",
                     file.getFullPathName().toRawUTF8());
        return 1;
      }
      report(name, buf, o.sampleRate);
      manifest.add(name + "  " + o.sweepParam + "=" + juce::String(value, 3) +
                   "  peak " +
                   juce::String(AudioMeasure::peak(buf.data(), (int)buf.size()),
                                3) +
                   "  rms " +
                   juce::String(AudioMeasure::rms(buf.data(), (int)buf.size()),
                                4));
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

  const auto buf = renderOne(o);
  report(o.voice, buf, o.sampleRate);
  if (!writeWav(o.out, buf, o.sampleRate)) {
    std::fprintf(stderr, "voicelab: could not write %s\n",
                 o.out.getFullPathName().toRawUTF8());
    return 1;
  }
  std::printf("wrote %s\n", o.out.getFullPathName().toRawUTF8());
  return 0;
}
