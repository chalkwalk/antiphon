#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <memory>

class LocalChannelStrip : public juce::Component {
public:
  LocalChannelStrip(NinjamAudioProcessor &processor,
                    std::shared_ptr<NinjamAudioProcessor::LocalChannel> channel);
  ~LocalChannelStrip() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void updatePeaks();
  void setRemovable(bool removable);
  void updateInputBusCount(int numBuses);

private:
  NinjamAudioProcessor &audioProcessor;
  std::shared_ptr<NinjamAudioProcessor::LocalChannel> channel;

  float decayedPeakL = 0.0f;
  float decayedPeakR = 0.0f;
  // Meter release is a rate, so it needs real elapsed time rather than a
  // per-tick constant. 0 means "first update, do not decay".
  double lastPeakUpdateMs = 0.0;
  juce::Rectangle<int> vuLArea, vuRArea;

  juce::TextEditor nameEditor;
  juce::ToggleButton monoButton;
  juce::Slider volumeSlider;
  juce::Slider panSlider;
  juce::ToggleButton muteButton;
  juce::ToggleButton soloButton;
  juce::ToggleButton xmitButton;
  juce::TextButton removeButton;
  juce::ComboBox inputBusBox;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LocalChannelStrip)
};
