#pragma once

#include "ClipsortLog.h"
#include <JuceHeader.h>
#include <map>
#include <memory>

// Writes a Ninjam session archive from the client, in the server's own format.
//
// Public servers do not all archive, and those that do do not all let you have
// the files, so saving locally is the only way to be sure you can get stems out
// of a jam you played. The format is the server's (see ClipsortLog.h) so
// antiphon-stems, REAPER and anything else that reads an archive all work on it
// without knowing where it came from.
//
// What gets written is what went over the wire: the Ogg bytes exactly as sent
// and received, never a re-encode. That is the whole value -- it is the
// original material at original quality, per player, before any of our mixing.
//
// Threading: uploads are logged from the message thread and downloads from the
// network thread, so everything here takes a lock. Neither of those is the
// audio thread, which is why a lock is allowed at all (PRINCIPLES 7).

class SessionWriter {
public:
  ~SessionWriter();

  // Creates `<parent>/<name>.ninjam/` and opens its manifest. Returns false if
  // the directory could not be made, in which case nothing else does anything.
  bool start(const juce::File &parent, const juce::String &name);
  void stop();
  bool isActive() const;

  juce::File directory() const;
  int clipCount() const;

  // An upload or download is beginning. Writes the interval line if this is a
  // new interval, then the user line, and opens the clip file.
  //
  // `interval` is the local interval the transfer happens in. Uploads and
  // downloads for the same musical interval occur in the same one -- you send
  // your interval N while receiving everyone else's -- so a single counter
  // keeps every stem on the same timeline.
  void beginClip(const juce::String &guidHex, const juce::String &username,
                 int channelIndex, const juce::String &channelName,
                 int interval, int bpm, int bpi);

  // Raw Ogg bytes, appended in arrival order. Ignored for a guid that was never
  // begun, so a download that started before saving was switched on is dropped
  // rather than written half-formed.
  void appendClip(const juce::String &guidHex, const void *data, int numBytes);

  void endClip(const juce::String &guidHex);

private:
  mutable juce::CriticalSection lock;
  juce::File dir;
  std::unique_ptr<juce::FileOutputStream> manifest;
  std::map<juce::String, std::unique_ptr<juce::FileOutputStream>> openClips;
  int lastIntervalWritten = -1;
  int clips = 0;
};
