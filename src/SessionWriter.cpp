#include "SessionWriter.h"

SessionWriter::~SessionWriter() { stop(); }

bool SessionWriter::start(const juce::File &parent, const juce::String &name) {
  stop();

  juce::ScopedLock sl(lock);
  dir = parent.getChildFile(name + ".ninjam");
  const auto created = dir.createDirectory();
  if (created.failed()) {
    dir = juce::File();
    return false;
  }

  // Appended, matching the server, which reopens the same log when a session
  // directory is reused (usercon.cpp:831 opens it "at").
  manifest = std::make_unique<juce::FileOutputStream>(
      dir.getChildFile("clipsort.log"));
  if (!manifest->openedOk()) {
    manifest.reset();
    dir = juce::File();
    return false;
  }
  manifest->setPosition(manifest->getFile().getSize());

  lastIntervalWritten = -1;
  clips = 0;
  return true;
}

void SessionWriter::stop() {
  juce::ScopedLock sl(lock);
  // Destroying the streams is what flushes them; a clip left open by a
  // disconnect mid-upload is still valid Ogg up to where it stopped.
  openClips.clear();
  manifest.reset();
  dir = juce::File();
  lastIntervalWritten = -1;
}

bool SessionWriter::isActive() const {
  juce::ScopedLock sl(lock);
  return manifest != nullptr;
}

juce::File SessionWriter::directory() const {
  juce::ScopedLock sl(lock);
  return dir;
}

int SessionWriter::clipCount() const {
  juce::ScopedLock sl(lock);
  return clips;
}

void SessionWriter::beginClip(const juce::String &guidHex,
                              const juce::String &username, int channelIndex,
                              const juce::String &channelName, int interval,
                              int bpm, int bpi) {
  juce::ScopedLock sl(lock);
  if (manifest == nullptr || guidHex.length() != 32)
    return;

  // The interval line goes in only when the interval changes, and only when
  // something is actually being written for it. An interval nobody played in
  // therefore has no line at all, which is correct: the reader takes the span
  // between the numbers it sees, so a skipped one becomes silence of the right
  // length rather than a gap that shortens the timeline.
  if (interval != lastIntervalWritten) {
    manifest->writeText(ClipsortLog::intervalLine(interval, bpm, bpi) + "\n",
                        false, false, nullptr);
    lastIntervalWritten = interval;
  }

  ClipsortLog::Clip clip;
  clip.guid = guidHex;
  clip.username = username;
  clip.channelIndex = channelIndex;
  clip.channelName = channelName;
  manifest->writeText(ClipsortLog::userLine(clip) + "\n", false, false,
                      nullptr);
  manifest->flush();

  // Clips are filed under the first character of their guid, which is how the
  // server spreads them and how every reader expects to find them.
  const auto file = dir.getChildFile(ClipsortLog::clipPath(guidHex));
  file.getParentDirectory().createDirectory();
  file.deleteFile();

  auto stream = std::make_unique<juce::FileOutputStream>(file);
  if (stream->openedOk()) {
    openClips[guidHex] = std::move(stream);
    ++clips;
  }
}

void SessionWriter::appendClip(const juce::String &guidHex, const void *data,
                               int numBytes) {
  if (data == nullptr || numBytes <= 0)
    return;

  juce::ScopedLock sl(lock);
  auto it = openClips.find(guidHex);
  if (it == openClips.end() || it->second == nullptr)
    return;
  it->second->write(data, (size_t)numBytes);
}

void SessionWriter::endClip(const juce::String &guidHex) {
  juce::ScopedLock sl(lock);
  openClips.erase(guidHex); // destroying the stream flushes and closes it
}
