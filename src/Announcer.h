#pragma once

#include <JuceHeader.h>

// Speaks the things a blind player cannot otherwise notice: that a vote is
// running, that the tempo changed, that someone joined, that sync engaged.
//
// Two rules shape this. Announcements interrupt whatever the reader is saying,
// so anything emitted on a timer -- meter levels, interval position -- would
// make the app unusable rather than more usable; only discrete events qualify.
// And a burst of events (six players joining at once) must not become six
// interruptions, so identical messages are suppressed and a minimum gap is
// enforced.
class Announcer {
public:
  enum class Verbosity { Off = 0, Important = 1, All = 2 };

  void setVerbosity(Verbosity v) { verbosity = v; }
  Verbosity getVerbosity() const { return verbosity; }

  // `important` marks the events worth interrupting for at the default level:
  // connection, sync, tempo, votes and chat.
  void say(const juce::String &message, bool important) {
    if (verbosity == Verbosity::Off)
      return;
    if (!important && verbosity != Verbosity::All)
      return;
    if (message.isEmpty())
      return;

    const auto now = juce::Time::getMillisecondCounter();
    if (message == lastMessage && now - lastTimeMs < kRepeatSuppressMs)
      return;
    if (now - lastTimeMs < kMinGapMs)
      return;

    lastMessage = message;
    lastTimeMs = now;
    juce::AccessibilityHandler::postAnnouncement(
        message, juce::AccessibilityHandler::AnnouncementPriority::medium);
  }

  static juce::String verbosityName(Verbosity v) {
    switch (v) {
    case Verbosity::Off:
      return "Off";
    case Verbosity::Important:
      return "Important only";
    case Verbosity::All:
      return "All";
    }
    return "Off";
  }

private:
  static constexpr juce::uint32 kMinGapMs = 400;
  static constexpr juce::uint32 kRepeatSuppressMs = 5000;

  Verbosity verbosity = Verbosity::Important;
  juce::String lastMessage;
  juce::uint32 lastTimeMs = 0;
};
