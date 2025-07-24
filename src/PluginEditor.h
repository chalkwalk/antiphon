#pragma once

#include "PluginProcessor.h"
#include "RemoteUserStrip.h"
#include <JuceHeader.h>

class NinjamAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::Timer {
public:
  NinjamAudioProcessorEditor(NinjamAudioProcessor &);
  ~NinjamAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;

private:
  NinjamAudioProcessor &audioProcessor;

  juce::TextEditor serverInput;
  juce::TextEditor usernameInput;
  juce::TextButton connectButton;
  juce::TextButton disconnectButton;

  juce::ToggleButton metronomeToggle;
  juce::ToggleButton saveTxToggle;
  juce::ToggleButton saveRxToggle;

  // Local Mixer Controls
  juce::Slider localVolumeSlider;
  juce::Slider localPanSlider;
  juce::ToggleButton localMuteButton;

  // Remote Mixer Controls
  juce::Viewport remoteUsersViewport;
  juce::Component remoteUsersContainer;
  juce::OwnedArray<RemoteUserStrip> remoteUserStrips;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessorEditor)
};
