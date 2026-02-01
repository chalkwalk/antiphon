#pragma once

#include <JuceHeader.h>

// The header's information, in words.
//
// Connection state, server tempo, sync state and interval position are painted
// straight onto the header as Graphics calls, which means they do not exist at
// all for a screen reader. This component draws nothing and occupies the same
// area; it is there purely so that information has somewhere to live in the
// accessibility tree, and so it is the first thing keyboard focus lands on.
class StatusReadout : public juce::Component {
public:
  StatusReadout() {
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(false, false);
    setTitle("Session status");
    setDescription("Not connected");
  }

  // Cheap enough to call every UI tick; only touches JUCE when the words
  // actually change, so a reader is not told the same thing thirty times a
  // second.
  void setStatus(const juce::String &text) {
    if (text == current) return;
    current = text;
    setDescription(text);
    setHelpText(text);
    if (auto *handler = getAccessibilityHandler())
      handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
  }

  const juce::String &getStatus() const { return current; }

  std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::staticText);
  }

private:
  juce::String current;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusReadout)
};
