#include "ClipsortLog.h"

namespace ClipsortLog {

namespace {

// Reads a `"quoted field"` starting at `pos`, which must be on the opening
// quote. Advances `pos` past the closing quote. Returns false if unterminated.
bool readQuoted(const juce::String &line, int &pos, juce::String &out) {
  while (pos < line.length() && line[pos] == ' ')
    ++pos;
  if (pos >= line.length() || line[pos] != '"')
    return false;

  const int start = pos + 1;
  const int close = line.indexOfChar(start, '"');
  if (close < 0)
    return false;

  out = line.substring(start, close);
  pos = close + 1;
  return true;
}

// Reads a run of non-space characters.
juce::String readToken(const juce::String &line, int &pos) {
  while (pos < line.length() && line[pos] == ' ')
    ++pos;
  const int start = pos;
  while (pos < line.length() && line[pos] != ' ')
    ++pos;
  return line.substring(start, pos);
}

bool isHexGuid(const juce::String &s) {
  if (s.length() != 32)
    return false;
  for (int i = 0; i < 32; ++i) {
    const auto c = juce::CharacterFunctions::toLowerCase(s[i]);
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    if (!hex)
      return false;
  }
  return true;
}

// juce::String::getIntValue returns 0 for anything unparseable, which would
// silently turn a malformed line into interval 0. Checked explicitly.
bool readInt(const juce::String &token, int &out) {
  if (token.isEmpty())
    return false;
  for (int i = 0; i < token.length(); ++i) {
    const auto c = token[i];
    if (i == 0 && (c == '-' || c == '+'))
      continue;
    if (c < '0' || c > '9')
      return false;
  }
  out = token.getIntValue();
  return true;
}

} // namespace

Session parse(const juce::String &logText) {
  Session session;
  int currentInterval = 0;
  int currentBpm = 0;
  int currentBpi = 0;
  int minInterval = 0;
  int maxInterval = -1;
  int firstBpm = 0;
  int firstBpi = 0;
  bool sawAny = false;

  auto note = [&](int n) {
    if (!sawAny) {
      minInterval = maxInterval = n;
      sawAny = true;
    } else {
      minInterval = juce::jmin(minInterval, n);
      maxInterval = juce::jmax(maxInterval, n);
    }
  };

  juce::StringArray lines;
  lines.addLines(logText);

  for (const auto &raw : lines) {
    const auto line = raw.trim();
    if (line.isEmpty())
      continue;

    int pos = 0;
    const auto verb = readToken(line, pos);

    if (verb == "interval") {
      int n = 0, bpm = 0, bpi = 0;
      if (!readInt(readToken(line, pos), n) ||
          !readInt(readToken(line, pos), bpm) ||
          !readInt(readToken(line, pos), bpi)) {
        ++session.malformedLines;
        continue;
      }
      currentInterval = n;
      currentBpm = bpm;
      currentBpi = bpi;
      if (firstBpm <= 0) {
        firstBpm = bpm;
        firstBpi = bpi;
      }
      note(n);
      continue;
    }

    if (verb == "user") {
      Clip clip;
      clip.guid = readToken(line, pos);
      if (!isHexGuid(clip.guid)) {
        ++session.malformedLines;
        continue;
      }
      if (!readQuoted(line, pos, clip.username)) {
        ++session.malformedLines;
        continue;
      }
      if (!readInt(readToken(line, pos), clip.channelIndex)) {
        ++session.malformedLines;
        continue;
      }
      // The channel name is the one field a server has been seen to omit, so a
      // missing one is not malformed -- just unnamed.
      readQuoted(line, pos, clip.channelName);

      // A clip before the first interval line belongs to interval 0. Real logs
      // open with one, because the session directory can be created part-way
      // through an interval; dropping it loses real audio.
      clip.interval = currentInterval;
      note(currentInterval);
      clip.bpm = currentBpm;
      clip.bpi = currentBpi;
      session.clips.push_back(clip);
      continue;
    }

    ++session.malformedLines; // a verb we do not know
  }

  // Clips logged before the first interval line have no tempo yet, and an
  // interval with no tempo has no length, so they would be written as nothing
  // at all -- the rejection removed earlier, arrived at by a different route.
  // The first tempo the log states is the best evidence for what was in force a
  // moment earlier.
  for (auto &clip : session.clips) {
    if (clip.bpm > 0 && clip.bpi > 0)
      break; // they are in file order, so the first tempo-bearing one ends it
    clip.bpm = firstBpm;
    clip.bpi = firstBpi;
  }

  if (sawAny) {
    session.firstInterval = minInterval;
    session.lastInterval = maxInterval;
    session.intervalCount = maxInterval - minInterval + 1;
  }
  return session;
}

juce::String clipPath(const juce::String &guid) {
  if (guid.isEmpty())
    return {};
  return guid.substring(0, 1) + "/" + guid + ".OGG";
}

juce::String intervalLine(int interval, int bpm, int bpi) {
  return "interval " + juce::String(interval) + " " + juce::String(bpm) + " " +
         juce::String(bpi);
}

juce::String userLine(const Clip &clip) {
  return "user " + clip.guid + " \"" + clip.username + "\" " +
         juce::String(clip.channelIndex) + " \"" + clip.channelName + "\"";
}

int intervalSamples(int bpm, int bpi, double sampleRate) {
  if (bpm <= 0 || bpi <= 0 || sampleRate <= 0.0)
    return 0;
  // Deliberately identical to IntervalClock::recomputeGrid, which reproduces
  // njclient.cpp:806: truncated, not rounded.
  const double v = (double)bpi / ((double)bpm * (1.0 / 60.0)) * sampleRate;
  return (int)v;
}

} // namespace ClipsortLog
