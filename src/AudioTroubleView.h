#pragma once

#include "AccessibleNaming.h"
#include <JuceHeader.h>
#include <functional>
#include <memory>

// What the standalone's window shows instead of the mixer when there is no
// audio device: what went wrong, a device picker, and a way to commit.
//
// A file of its own rather than a detail of StandaloneApp.cpp, because a class
// in an anonymous namespace cannot be reached by the accessibility audit
// (test/AuditMain.cpp) -- and a screen a user only meets when something has
// already gone wrong is the last screen that should be unchecked.
class AudioTroubleView : public juce::Component {
public:
  AudioTroubleView(juce::AudioDeviceManager &dm, int ins, int outs,
                   const juce::String &reason, std::function<void()> onAccept)
      : accept(std::move(onAccept)) {
    message.setText(reason, juce::dontSendNotification);
    message.setJustificationType(juce::Justification::centredLeft);
    message.setTitle("Audio device problem");
    message.setDescription(reason);
    addAndMakeVisible(message);

    selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        dm, ins, ins, outs, outs, false, false, true, false);
    selector->setTitle("Audio device settings");
    selector->setDescription("Choose an audio device for Antiphon to use");
    addAndMakeVisible(*selector);

    useButton.setButtonText("Use this device");
    useButton.setTitle("Use this device");
    useButton.setDescription(
        "Start Antiphon with the device selected above, and remember it");
    useButton.onClick = [this] { if (accept) accept(); };
    addAndMakeVisible(useButton);
  }

  void paint(juce::Graphics &g) override {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
  }

  void resized() override {
    auto r = getLocalBounds().reduced(16);
    message.setBounds(r.removeFromTop(48));
    r.removeFromTop(8);
    useButton.setBounds(r.removeFromBottom(30).removeFromLeft(160));
    r.removeFromBottom(8);
    selector->setBounds(r);

    // The selector builds its device dropdowns as it lays itself out, so this
    // cannot run in the constructor -- there is nothing to name yet.
    AccessibleNaming::adoptLabelNames(*selector);
  }

private:
  juce::Label message;
  std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
  juce::TextButton useButton;
  std::function<void()> accept;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTroubleView)
};
