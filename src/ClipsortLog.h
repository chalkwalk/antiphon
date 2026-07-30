#pragma once

#include <JuceHeader.h>
#include <vector>

// The Ninjam session archive manifest: which Ogg clip belongs to whom, on which
// channel, in which interval.
//
// This is not a format we invented. The server writes it for every session it
// archives -- one Ogg per user per channel per interval, plus a `clipsort.log`
// naming them -- and it is what REAPER imports and what the archive bots on
// ninbot and ninjammer publish. `scripts/analyze_archive.py` parses the same
// thing and is the de facto spec; this is its grammar in C++.
//
//   interval <n> <bpm> <bpi>
//   user <32-hex-guid> "<username>" <channel> "<channel name>"
//
// A `user` line belongs to the most recent `interval` line above it, so the
// interval number is carried forward while scanning. The server writes `user`
// when an upload *begins* (usercon.cpp:652) and `interval` when the loop
// counter advances (:900), which is what makes carry-forward the right reading.
//
// A real log opens with a `user` line before any `interval` line -- the session
// directory is created on a periodic check that can land mid-interval, so the
// upload already in progress is logged first. Those clips belong to interval 0.
// Rejecting them, which seems like the safe thing to do, silently drops real
// audio from the front of every session.
//
// BPM and BPI can change mid-session, which is why each clip records the values
// in force when it was sent -- interval lengths are not constant across a
// session. A real four-segment session seen here ran 100, 120, 80 and 60 bpm.
//
// The server rotates to a new directory every half hour and **restarts interval
// numbering at 1 in each**, so a session is usually several directories that
// have to be concatenated. See `Segment` in the stem tool.
//
// Both halves live here on purpose: `parse` for the stem converter, and the
// line builders for the client's own session saving, so the two cannot drift.
//
// JUCE-light and free of juce_gui_basics, so it is testable headlessly.

namespace ClipsortLog {

struct Clip {
  juce::String guid;
  juce::String username;
  int channelIndex = 0;
  juce::String channelName;
  int interval = 0;
  int bpm = 0;
  int bpi = 0;
};

struct Session {
  std::vector<Clip> clips;
  // The span of intervals this log covers. Not assumed to start at 0 or 1: a
  // server log starts at 1, while a clip logged before the first boundary sits
  // at 0. The timeline the stems are laid out against -- a player who sent
  // nothing in an interval still needs its length of silence, or every later
  // stem drifts against the others.
  int firstInterval = 0;
  int lastInterval = -1;
  int intervalCount = 0;  // lastInterval - firstInterval + 1, or 0
  int malformedLines = 0; // reported rather than swallowed
};

Session parse(const juce::String &logText);

// Where a clip sits inside a session directory: a subdirectory named by the
// first character of its GUID, then the GUID with an .OGG extension.
juce::String clipPath(const juce::String &guid);

// The two line forms, for writing a manifest.
juce::String intervalLine(int interval, int bpm, int bpi);
juce::String userLine(const Clip &clip);

// Samples in one interval at a given rate.
//
// Truncated, not rounded, reproducing `njclient.cpp:806` exactly as
// IntervalClock does. Stems have to be laid out on the same grid the clients
// used or they will not line up with each other.
int intervalSamples(int bpm, int bpi, double sampleRate);

} // namespace ClipsortLog
