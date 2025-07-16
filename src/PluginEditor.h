#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

class NinjamAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  NinjamAudioProcessorEditor(NinjamAudioProcessor &);
  ~NinjamAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  NinjamAudioProcessor &audioProcessor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessorEditor)
};
