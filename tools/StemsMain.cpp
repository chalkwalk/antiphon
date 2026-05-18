#include <JuceHeader.h>

#include "ClipsortLog.h"
#include "StemRender.h"
#include "VorbisCodec.h"

#include <map>
#include <vector>

// antiphon-stems -- turn a Ninjam session archive into WAV stems.
//
// The archive format is the server's own (see ClipsortLog.h), so this works on
// a session your server wrote, on one downloaded from an archive bot, and on
// one Antiphon saved itself. The point is to remove the step everybody
// complains about: importing a directory of GUID-named Ogg files and a log into
// a DAW just to export them again.
//
// Offline, headless and deliberately outside the plugin: it links
// juce_audio_formats and juce_events and nothing that touches X11 or ALSA.

namespace {

struct StemKey {
  juce::String username;
  int channelIndex = 0;

  bool operator<(const StemKey &o) const {
    if (username != o.username)
      return username < o.username;
    return channelIndex < o.channelIndex;
  }
};

juce::String sanitise(const juce::String &s) {
  juce::String out;
  for (int i = 0; i < s.length(); ++i) {
    const auto c = s[i];
    const bool ok = juce::CharacterFunctions::isLetterOrDigit(c) || c == '-' ||
                    c == '_' || c == '.';
    out += ok ? juce::String::charToString(c) : juce::String("_");
  }
  return out.isEmpty() ? juce::String("unnamed") : out;
}

// Decodes a whole clip to interleaved float.
//
// The whole file is fed before any output is expected: Ogg emits a page only
// every few kilobytes, so a quiet or tonal interval decodes to nothing at all
// until the end-of-stream flush (AGENTS.md, "interval delivery is
// all-or-nothing"). Reading it in pieces and stopping early is how you get
// silent stems from a session that sounded fine.
struct DecodedClip {
  std::vector<float> interleaved;
  int numChannels = 0;
  int sampleRate = 0;
  bool ok = false;
};

DecodedClip decodeClip(const juce::File &file) {
  DecodedClip out;
  juce::MemoryBlock raw;
  if (!file.loadFileAsData(raw) || raw.getSize() == 0)
    return out;

  VorbisDecoder decoder;
  decoder.decode(raw.getData(), (int)raw.getSize());

  out.numChannels = juce::jmax(1, decoder.numChannels());
  out.sampleRate = decoder.sampleRate();

  while (decoder.available() > 0) {
    const int avail = decoder.available();
    const float *pcm = decoder.pcm();
    out.interleaved.insert(out.interleaved.end(), pcm, pcm + avail);
    decoder.skip(avail);
  }

  out.ok = out.sampleRate > 0;
  return out;
}

int usage() {
  std::cout
      << "antiphon-stems -- Ninjam session archive to WAV stems\n\n"
         "  antiphon-stems <session-dir> [-o <out-dir>] [--rate <hz>]\n\n"
         "<session-dir> holds clipsort.log and the GUID-named Ogg clips, as\n"
         "written by a Ninjam server, an archive bot, or Antiphon itself.\n"
         "One WAV per player per channel is written, all the same length and\n"
         "sample-aligned, so they line up when dropped into a DAW.\n\n"
         "  -o <out-dir>   where to write (default: <session-dir>/stems)\n"
         "  --rate <hz>    output rate (default: the highest any clip declares)\n";
  return 1;
}

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI juceInit;

  juce::String sessionArg;
  juce::String outArg;
  double requestedRate = 0.0;

  for (int i = 1; i < argc; ++i) {
    const juce::String arg(argv[i]);
    if (arg == "-o" && i + 1 < argc)
      outArg = argv[++i];
    else if (arg == "--rate" && i + 1 < argc)
      requestedRate = juce::String(argv[++i]).getDoubleValue();
    else if (arg.startsWith("-"))
      return usage();
    else if (sessionArg.isEmpty())
      sessionArg = arg;
    else
      return usage();
  }

  if (sessionArg.isEmpty())
    return usage();

  const juce::File sessionDir(
      juce::File::getCurrentWorkingDirectory().getChildFile(sessionArg));
  const juce::File logFile = sessionDir.getChildFile("clipsort.log");
  if (!logFile.existsAsFile()) {
    std::cerr << "No clipsort.log in " << sessionDir.getFullPathName()
              << "\n";
    return 2;
  }

  const auto session = ClipsortLog::parse(logFile.loadFileAsString());
  if (session.clips.empty()) {
    std::cerr << "No clips in the manifest.\n";
    return 2;
  }
  if (session.malformedLines > 0)
    std::cerr << "Warning: " << session.malformedLines
              << " unreadable line(s) in the manifest, skipped.\n";

  // Index clips by stem and interval, and find the rate to write at.
  std::map<StemKey, std::map<int, ClipsortLog::Clip>> byStem;
  std::map<int, std::pair<int, int>> tempoByInterval; // interval -> (bpm, bpi)
  for (const auto &clip : session.clips) {
    byStem[{clip.username, clip.channelIndex}][clip.interval] = clip;
    tempoByInterval[clip.interval] = {clip.bpm, clip.bpi};
  }

  double outRate = requestedRate;
  if (outRate <= 0.0) {
    // Players can be at different rates, so the highest one is the only choice
    // that never has to throw detail away.
    for (const auto &clip : session.clips) {
      const auto file =
          sessionDir.getChildFile(ClipsortLog::clipPath(clip.guid));
      if (!file.existsAsFile())
        continue;
      const auto decoded = decodeClip(file);
      if (decoded.ok)
        outRate = juce::jmax(outRate, (double)decoded.sampleRate);
    }
  }
  if (outRate <= 0.0)
    outRate = 48000.0;

  const juce::File outDir =
      outArg.isNotEmpty()
          ? juce::File::getCurrentWorkingDirectory().getChildFile(outArg)
          : sessionDir.getChildFile("stems");
  const auto created = outDir.createDirectory();
  if (created.failed()) {
    std::cerr << "Could not create " << outDir.getFullPathName() << ": "
              << created.getErrorMessage() << "\n";
    return 2;
  }

  std::cout << "Session: " << sessionDir.getFullPathName() << "\n"
            << "Intervals: " << session.intervalCount << "   Stems: "
            << byStem.size() << "   Rate: " << (int)outRate << " Hz\n\n";

  juce::WavAudioFormat wav;
  int written = 0;
  juce::int64 totalFrames = 0;

  for (const auto &[key, intervals] : byStem) {
    const juce::String name =
        sanitise(key.username) + "-" +
        sanitise(intervals.begin()->second.channelName.isNotEmpty()
                     ? intervals.begin()->second.channelName
                     : "ch" + juce::String(key.channelIndex)) +
        ".wav";
    const juce::File outFile = outDir.getChildFile(name);
    outFile.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(
        outFile.createOutputStream());
    if (stream == nullptr) {
      std::cerr << "Could not write " << outFile.getFullPathName() << "\n";
      return 2;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.release(), outRate, 2, 24, {}, 0));
    if (writer == nullptr) {
      std::cerr << "Could not create a WAV writer for "
                << outFile.getFullPathName() << "\n";
      return 2;
    }

    juce::int64 framesThisStem = 0;
    int clipsPlaced = 0;

    // Every stem walks the full timeline, so an interval a player sat out
    // becomes silence of exactly the right length rather than a gap that pulls
    // everything after it out of alignment.
    for (int interval = 0; interval < session.intervalCount; ++interval) {
      auto tempo = tempoByInterval.find(interval);
      if (tempo == tempoByInterval.end()) {
        // No clip from anyone in this interval, so its length is unknown.
        // Carry the previous tempo forward if there is one.
        if (interval > 0 && tempoByInterval.count(interval - 1))
          tempo = tempoByInterval.find(interval - 1);
        else
          continue;
      }

      const int frames = ClipsortLog::intervalSamples(
          tempo->second.first, tempo->second.second, outRate);
      if (frames <= 0)
        continue;

      juce::AudioBuffer<float> block(2, frames);
      block.clear();

      const auto clip = intervals.find(interval);
      if (clip != intervals.end()) {
        const auto file =
            sessionDir.getChildFile(ClipsortLog::clipPath(clip->second.guid));
        if (file.existsAsFile()) {
          const auto decoded = decodeClip(file);
          if (decoded.ok) {
            StemRender::placeClip(
                decoded.interleaved.data(),
                decoded.numChannels > 0
                    ? (int)decoded.interleaved.size() / decoded.numChannels
                    : 0,
                decoded.numChannels, (double)decoded.sampleRate,
                block.getWritePointer(0), block.getWritePointer(1), frames,
                outRate);
            ++clipsPlaced;
          }
        }
      }

      writer->writeFromAudioSampleBuffer(block, 0, frames);
      framesThisStem += frames;
    }

    writer.reset(); // destroying the writer is what writes the WAV header

    std::cout << "  " << name << "   " << clipsPlaced << "/"
              << session.intervalCount << " intervals, "
              << juce::String(framesThisStem / outRate, 1) << " s\n";
    ++written;
    if (totalFrames == 0)
      totalFrames = framesThisStem;
    else if (totalFrames != framesThisStem)
      std::cerr << "  Warning: " << name
                << " is a different length from the first stem\n";
  }

  std::cout << "\n" << written << " stem(s) written to "
            << outDir.getFullPathName() << "\n";
  return 0;
}
