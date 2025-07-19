#pragma once

#include "PluginProcessor.h"
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

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessorEditor)
};
