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

// One archive directory: the server rotates every half hour and restarts
// interval numbering at 1 in each, so a session that ran from 18:00 to 20:00 is
// four directories that have to be laid end to end. Clips live inside the
// segment that logged them, so each keeps its own base directory.
struct Segment {
  juce::File dir;
  ClipsortLog::Session session;
  int base = 0; // where this segment starts on the merged timeline
};

// Sorted by name, which for the server's YYYYMMDD_HHMM.ninjam form is
// chronological. Accepts either a single session directory or a parent holding
// several.
std::vector<juce::File> findSegmentDirs(const juce::File &root) {
  std::vector<juce::File> dirs;
  if (root.getChildFile("clipsort.log").existsAsFile()) {
    dirs.push_back(root);
    return dirs;
  }

  for (const auto &entry : juce::RangedDirectoryIterator(
           root, false, "*", juce::File::findDirectories)) {
    if (entry.getFile().getChildFile("clipsort.log").existsAsFile())
      dirs.push_back(entry.getFile());
  }

  std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b) {
    return a.getFileName() < b.getFileName();
  });
  return dirs;
}

// Parses the leading YYYYMMDD_HHMM of a server session directory name.
// Returns a comparable minute count, or -1 if the name is not that shape.
juce::int64 timestampMinutes(const juce::File &dir) {
  const auto name = dir.getFileNameWithoutExtension();
  if (name.length() < 13 || name[8] != '_')
    return -1;
  const auto date = name.substring(0, 8);
  const auto time = name.substring(9, 13);
  for (const auto &part : {date, time})
    for (int i = 0; i < part.length(); ++i)
      if (part[i] < '0' || part[i] > '9')
        return -1;

  const int y = date.substring(0, 4).getIntValue();
  const int mo = date.substring(4, 6).getIntValue();
  const int d = date.substring(6, 8).getIntValue();
  const int h = time.substring(0, 2).getIntValue();
  const int mi = time.substring(2, 4).getIntValue();
  // Not calendar-exact, and does not need to be: it only has to order and
  // difference names from the same few days.
  return (((juce::int64)y * 12 + mo) * 31 + d) * 1440 + h * 60 + mi;
}

void warnIfNotConsecutive(const std::vector<juce::File> &dirs) {
  juce::int64 previous = -1;
  for (const auto &dir : dirs) {
    const auto t = timestampMinutes(dir);
    if (t < 0) {
      std::cerr << "Warning: " << dir.getFileName()
                << " is not named like a session directory, so its place in "
                   "the running order is a guess.\n";
      previous = -1;
      continue;
    }
    if (previous >= 0) {
      const auto gap = t - previous;
      // The server rotates every 30 minutes. Anything much larger means these
      // are probably separate jams that happen to share a folder.
      if (gap > 60)
        std::cerr << "Warning: " << juce::String(gap)
                  << " minutes between segments before " << dir.getFileName()
                  << ". If these are separate sessions, convert them "
                     "separately -- joining them makes one long wrong file.\n";
    }
    previous = t;
  }
}

int usage() {
  std::cout
      << "antiphon-stems -- Ninjam session archive to WAV stems\n\n"
         "  antiphon-stems <session-dir> [-o <out-dir>] [--rate <hz>]\n\n"
         "<session-dir> holds clipsort.log and the GUID-named Ogg clips, as\n"
         "written by a Ninjam server, an archive bot, or Antiphon itself.\n"
         "One WAV per player per channel is written, all the same length and\n"
         "sample-aligned, so they line up when dropped into a DAW.\n\n"
         "A session is usually several directories, because the server rotates\n"
         "every half hour and restarts interval numbering in each. Point this at\n"
         "the parent and they are laid end to end automatically.\n\n"
         "  -o <out-dir>   where to write (default: <session-dir>/stems)\n"
         "  --rate <hz>    output rate (default: the highest any clip declares)\n"
         "  --bits <n>     16 (default), 24 or 32. Vorbis decodes to float and\n"
         "                 carries no bit depth; 16 is all the decode justifies,\n"
         "                 and 24 costs 50% more disk for no more detail.\n";
  return 1;
}

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI juceInit;

  juce::StringArray sessionArgs;
  juce::String outArg;
  double requestedRate = 0.0;
  // 16-bit by default. Vorbis has no bit depth to match -- it decodes to float,
  // and an Ogg declares bits_per_sample=0 -- so the only question is what
  // precision the decode actually justifies. At Ninjam bitrates the codec's own
  // noise floor sits far above 16-bit quantisation at -96 dBFS, so the extra
  // eight bits store decode noise and 50% more disk. Measured across the
  // loudest clips of a real session: no sample exceeded 0.573, so there is no
  // overshoot for the wider integer range to protect either.
  int bitDepth = 16;

  for (int i = 1; i < argc; ++i) {
    const juce::String arg(argv[i]);
    if (arg == "-o" && i + 1 < argc)
      outArg = argv[++i];
    else if (arg == "--rate" && i + 1 < argc)
      requestedRate = juce::String(argv[++i]).getDoubleValue();
    else if (arg == "--bits" && i + 1 < argc)
      bitDepth = juce::String(argv[++i]).getIntValue();
    else if (arg.startsWith("-"))
      return usage();
    else
      sessionArgs.add(arg);
  }

  if (sessionArgs.isEmpty())
    return usage();

  // Several paths may be named, in which case they are used exactly as given,
  // in that order -- the escape hatch for an archive we cannot make sense of
  // automatically. One path is searched: itself if it holds a manifest, else
  // one level below.
  std::vector<juce::File> segmentDirs;
  const juce::File sessionDir(
      juce::File::getCurrentWorkingDirectory().getChildFile(sessionArgs[0]));

  if (sessionArgs.size() > 1) {
    for (const auto &arg : sessionArgs) {
      const auto f =
          juce::File::getCurrentWorkingDirectory().getChildFile(arg);
      if (!f.getChildFile("clipsort.log").existsAsFile()) {
        std::cerr << "No clipsort.log in " << f.getFullPathName() << "\n";
        return 2;
      }
      segmentDirs.push_back(f);
    }
  } else {
    segmentDirs = findSegmentDirs(sessionDir);
  }

  if (segmentDirs.empty()) {
    std::cerr << "No clipsort.log in " << sessionDir.getFullPathName()
              << " or any directory below it.\n";
    return 2;
  }

  // Combining is only right if these really are consecutive parts of one jam.
  // The server names its directories by the half hour they opened, so the
  // timestamps say whether they are contiguous -- and silently welding two
  // unrelated sessions together produces a long, plausible, wrong file.
  if (sessionArgs.size() == 1 && segmentDirs.size() > 1)
    warnIfNotConsecutive(segmentDirs);

  // Lay the segments end to end. Each restarts its own numbering, so the offset
  // is the running total rather than the interval numbers themselves.
  std::vector<Segment> segments;
  int totalIntervals = 0;
  int totalMalformed = 0;
  for (const auto &dir : segmentDirs) {
    Segment seg;
    seg.dir = dir;
    seg.session =
        ClipsortLog::parse(dir.getChildFile("clipsort.log").loadFileAsString());
    if (seg.session.intervalCount <= 0)
      continue;
    seg.base = totalIntervals;
    totalIntervals += seg.session.intervalCount;
    totalMalformed += seg.session.malformedLines;
    segments.push_back(seg);
  }

  if (segments.empty()) {
    std::cerr << "No clips in any manifest.\n";
    return 2;
  }
  if (totalMalformed > 0)
    std::cerr << "Warning: " << totalMalformed
              << " unreadable line(s) across the manifests, skipped.\n";

  // Index clips by stem and by their position on the merged timeline. A clip
  // also has to remember which segment it lives in, because that is the
  // directory its Ogg file sits under.
  struct Located {
    ClipsortLog::Clip clip;
    juce::File dir;
  };
  std::map<StemKey, std::map<int, Located>> byStem;
  std::map<int, std::pair<int, int>> tempoByInterval;
  int totalClips = 0;
  for (const auto &seg : segments) {
    for (const auto &clip : seg.session.clips) {
      const int at = seg.base + (clip.interval - seg.session.firstInterval);
      byStem[{clip.username, clip.channelIndex}][at] = Located{clip, seg.dir};
      tempoByInterval[at] = {clip.bpm, clip.bpi};
      ++totalClips;
    }
  }

  double outRate = requestedRate;
  if (outRate <= 0.0) {
    // Players can be at different rates, so the highest one is the only choice
    // that never has to throw detail away.
    // One clip per stem is enough to learn the rates in play; decoding all of
    // them twice would double the run time of a long session for nothing.
    for (const auto &[key, intervals] : byStem) {
      juce::ignoreUnused(key);
      if (intervals.empty())
        continue;
      const auto &located = intervals.begin()->second;
      const auto file =
          located.dir.getChildFile(ClipsortLog::clipPath(located.clip.guid));
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
            << "Segments: " << segments.size() << "   Intervals: "
            << totalIntervals << "   Clips: " << totalClips
            << "   Stems: " << byStem.size() << "   Rate: " << (int)outRate
            << " Hz\n";
  for (const auto &seg : segments)
    std::cout << "  " << seg.dir.getFileName() << "  intervals "
              << seg.base << ".." << (seg.base + seg.session.intervalCount - 1)
              << "\n";
  std::cout << "\n";

  if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
    std::cerr << "--bits must be 16, 24 or 32\n";
    return 2;
  }

  // WAV cannot address more than 4 GB, and a long jam gets close: two hours of
  // 24-bit stereo at 48 kHz is about 2 GB per stem. Say so before spending
  // several minutes producing a file that will not open.
  {
    juce::int64 projectedFrames = 0;
    for (const auto &[at, tempo] : tempoByInterval) {
      juce::ignoreUnused(at);
      projectedFrames +=
          ClipsortLog::intervalSamples(tempo.first, tempo.second, outRate);
    }
    const juce::int64 bytes = projectedFrames * 2 * (bitDepth / 8);
    const double gb = (double)bytes / 1.0e9;
    std::cout << "Each stem will be about " << juce::String(gb, 2) << " GB.\n";
    if (bytes > 3900000000LL)
      std::cerr << "Warning: that is at or past what a WAV file can address. "
                   "Use --bits 16, or split the session.\n";
    std::cout << "\n";
  }

  // Annotate the stems, because a WAV that knows its own tempo is worth a lot
  // more when it lands in a DAW than one that does not.
  //
  //  - the ACID chunk carries a tempo, which several hosts read to warp or to
  //    set the project tempo on import;
  //  - cue markers, labelled, land on every tempo change, which is the thing a
  //    Ninjam jam does that a normal recording does not -- this session went
  //    100, 120, 80, 60 bpm and without markers you would have to find those
  //    boundaries by ear;
  //  - a RIFF INFO comment records where the audio came from, since a bare
  //    stem otherwise says nothing about which jam it was.
  juce::StringPairArray metadata;
  {
    juce::int64 frame = 0;
    int lastBpm = 0, lastBpi = 0;
    int cueCount = 0;
    int firstBpm = 0;
    juce::int64 totalBeats = 0;

    for (int interval = 0; interval < totalIntervals; ++interval) {
      auto tempo = tempoByInterval.find(interval);
      if (tempo == tempoByInterval.end()) {
        if (interval > 0 && tempoByInterval.count(interval - 1))
          tempo = tempoByInterval.find(interval - 1);
        else
          continue;
      }
      const int bpm = tempo->second.first;
      const int bpi = tempo->second.second;
      const int frames = ClipsortLog::intervalSamples(bpm, bpi, outRate);
      if (frames <= 0)
        continue;

      if (bpm != lastBpm || bpi != lastBpi) {
        if (firstBpm == 0)
          firstBpm = bpm;
        const juce::String prefix = "Cue" + juce::String(cueCount);
        metadata.set(prefix + "Identifier", juce::String(cueCount + 1));
        metadata.set(prefix + "Order", juce::String(cueCount));
        metadata.set(prefix + "ChunkID", juce::String(0x61746164)); // 'data'
        metadata.set(prefix + "ChunkStart", "0");
        metadata.set(prefix + "BlockStart", "0");
        metadata.set(prefix + "Offset", juce::String(frame));
        // A label needs its own count and a Text suffix; without NumCueLabels
        // the adtl list is never emitted and the markers arrive unnamed.
        const juce::String label = "Cue" + juce::String(cueCount);
        juce::ignoreUnused(label);
        metadata.set("CueLabel" + juce::String(cueCount) + "Identifier",
                     juce::String(cueCount + 1));
        metadata.set("CueLabel" + juce::String(cueCount) + "Text",
                     juce::String(bpm) + " BPM, " + juce::String(bpi) + " BPI");
        ++cueCount;
        lastBpm = bpm;
        lastBpi = bpi;
      }

      frame += frames;
      totalBeats += bpi;
    }

    if (cueCount > 0) {
      metadata.set("NumCuePoints", juce::String(cueCount));
      metadata.set("NumCueLabels", juce::String(cueCount));
    }

    if (firstBpm > 0) {
      metadata.set(juce::WavAudioFormat::acidizerFlag, "1");
      metadata.set(juce::WavAudioFormat::acidOneShot, "0");
      metadata.set(juce::WavAudioFormat::acidStretch, "1");
      metadata.set(juce::WavAudioFormat::acidDiskBased, "1");
      metadata.set(juce::WavAudioFormat::acidTempo, juce::String(firstBpm));
      metadata.set(juce::WavAudioFormat::acidBeats, juce::String(totalBeats));
      // Ninjam has no time signature, so 4/4 is stated as the assumption it is
      // rather than left for a host to invent.
      metadata.set(juce::WavAudioFormat::acidNumerator, "4");
      metadata.set(juce::WavAudioFormat::acidDenominator, "4");
    }

    metadata.set(juce::WavAudioFormat::riffInfoSoftware, "Antiphon antiphon-stems");
    metadata.set(juce::WavAudioFormat::riffInfoComment,
                 "Ninjam session " + sessionDir.getFileName() + ", " +
                     juce::String((int)segments.size()) + " segment(s), " +
                     juce::String(totalIntervals) + " intervals");
    if (cueCount > 0)
      std::cout << cueCount << " tempo marker(s) written into each stem.\n\n";
  }

  // 5 ms, the same length the live mix uses for a mute (GainRamp).
  const int edgeFade = (int)(outRate * 0.005);

  juce::WavAudioFormat wav;
  int written = 0;
  juce::int64 totalFrames = 0;

  for (const auto &[key, intervals] : byStem) {
    const juce::String name =
        sanitise(key.username) + "-" +
        sanitise(intervals.begin()->second.clip.channelName.isNotEmpty()
                     ? intervals.begin()->second.clip.channelName
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
        wav.createWriterFor(stream.release(), outRate, 2,
                            (unsigned int)bitDepth, metadata, 0));
    if (writer == nullptr) {
      std::cerr << "Could not create a WAV writer for "
                << outFile.getFullPathName() << "\n";
      return 2;
    }

    juce::int64 framesThisStem = 0;
    int clipsPlaced = 0;
    int faded = 0;

    // Every stem walks the full timeline, so an interval a player sat out
    // becomes silence of exactly the right length rather than a gap that pulls
    // everything after it out of alignment.
    for (int interval = 0; interval < totalIntervals; ++interval) {
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
      int produced = 0;
      if (clip != intervals.end()) {
        const auto file = clip->second.dir.getChildFile(
            ClipsortLog::clipPath(clip->second.clip.guid));
        if (file.existsAsFile()) {
          const auto decoded = decodeClip(file);
          if (decoded.ok) {
            produced = StemRender::placeClip(
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

      // Only the joins where audio meets silence need a fade. Between two
      // intervals that both carry audio the waveform continues by itself, and
      // fading there would put an amplitude wobble at the interval rate through
      // the whole stem.
      if (produced > 0) {
        const bool prevHadAudio = intervals.count(interval - 1) > 0;
        const bool nextHasAudio = intervals.count(interval + 1) > 0;
        const bool shortClip = produced < frames;

        // A clip that came up short decays into its own padding rather than
        // stepping to zero part-way through the interval.
        if (shortClip)
          StemRender::fadeEdges(block.getWritePointer(0),
                                block.getWritePointer(1), produced, edgeFade,
                                !prevHadAudio, true);
        else
          StemRender::fadeEdges(block.getWritePointer(0),
                                block.getWritePointer(1), frames, edgeFade,
                                !prevHadAudio, !nextHasAudio);
        if (!prevHadAudio || !nextHasAudio || shortClip)
          ++faded;
      }

      writer->writeFromAudioSampleBuffer(block, 0, frames);
      framesThisStem += frames;
    }

    writer.reset(); // destroying the writer is what writes the WAV header

    std::cout << "  " << name << "   " << clipsPlaced << "/"
              << totalIntervals << " intervals, "
              << juce::String(framesThisStem / outRate, 1) << " s, "
              << faded << " faded edge(s)\n";
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
