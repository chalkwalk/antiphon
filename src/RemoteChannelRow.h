#pragma once
#include "NinjamClient.h"
#include <JuceHeader.h>

class NinjamAudioProcessor;

class RemoteChannelRow : public juce::Component {
public:
  RemoteChannelRow(NinjamAudioProcessor &p, juce::String username,
                   int channelIndex);

  void update(const NinjamClient::RemoteUserChannel &c);
  void updatePeak(float peak);
  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  NinjamAudioProcessor &audioProcessor;
  juce::String username;
  int channelIndex;

  juce::Label channelNameLabel;
  juce::Slider volumeSlider;
  juce::Slider panSlider;
  juce::ToggleButton muteButton{"M"};
  juce::ToggleButton soloButton{"S"};
  juce::ToggleButton recvButton{"R"};

  float decayedPeak = 0.0f;
  juce::Rectangle<int> vuArea;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteChannelRow)
};
