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

  static constexpr juce::uint32 kMinGapMs = 400;
  static constexpr juce::uint32 kAfterImportantGapMs = 2500;
  static constexpr juce::uint32 kRepeatSuppressMs = 5000;

  // Pure decision step so rate-limiting and priority rules can be unit-tested
  // with explicit timestamps.
  //
  // Important announcements (sync/tempo warnings, votes, direct responses to
  // keystrokes) are never dropped because a low-priority chat or join line
  // just spoke, and once an important announcement starts speaking, low-priority
  // room traffic is held off for kAfterImportantGapMs so it cannot cut off the
  // screen reader mid-sentence.
  bool shouldSay(const juce::String &message, bool important,
                 juce::uint32 now) {
    if (verbosity == Verbosity::Off)
      return false;
    if (!important && verbosity != Verbosity::All)
      return false;
    if (message.isEmpty())
      return false;

    if (hasSpoken && message == lastMessage &&
        now - lastTimeMs < kRepeatSuppressMs)
      return false;

    if (!important) {
      if (hasSpoken && now - lastTimeMs < kMinGapMs)
        return false;
      if (hasSpokenImportant &&
          now - lastImportantTimeMs < kAfterImportantGapMs)
        return false;
    }

    lastMessage = message;
    lastTimeMs = now;
    hasSpoken = true;
    if (important) {
      lastImportantTimeMs = now;
      hasSpokenImportant = true;
    }
    return true;
  }

  // `important` marks the events worth interrupting for at the default level:
  // connection, sync, tempo, votes and chat.
  void say(const juce::String &message, bool important) {
    if (!shouldSay(message, important, juce::Time::getMillisecondCounter()))
      return;
    juce::AccessibilityHandler::postAnnouncement(
        message,
        important ? juce::AccessibilityHandler::AnnouncementPriority::high
                  : juce::AccessibilityHandler::AnnouncementPriority::medium);
  }

  // Format a friendly toggle state announcement:
  // e.g. "Instrument transmit on", "Instrument transmit off", "Mute all on"
  static juce::String formatToggle(const juce::String &target,
                                   const juce::String &action, bool on) {
    const auto *const stateStr = on ? " on" : " off";
    if (target.isEmpty())
      return action.substring(0, 1).toUpperCase() + action.substring(1) +
             stateStr;
    return target + " " + action + stateStr;
  }

  void sayToggle(const juce::String &target, const juce::String &action,
                 bool on) {
    say(formatToggle(target, action, on), true);
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
  Verbosity verbosity = Verbosity::Important;
  juce::String lastMessage;
  juce::uint32 lastTimeMs = 0;
  juce::uint32 lastImportantTimeMs = 0;
  bool hasSpoken = false;
  bool hasSpokenImportant = false;
};
