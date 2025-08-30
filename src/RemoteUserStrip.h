#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>

class RemoteUserStrip : public juce::Component {
public:
  RemoteUserStrip(NinjamAudioProcessor &processor,
                  const juce::String &username);
  ~RemoteUserStrip() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void updateChannels(
      const std::map<int, NinjamClient::RemoteUserChannel> &channels);

private:
  NinjamAudioProcessor &audioProcessor;
  juce::String username;

  // We'll just control the first channel (index 0) of the user for simplicity
  // since most users have just 1 channel. A future enhancement could add
  // sub-mixers per channel.
  int currentChannelIndex = 0;

  float decayedPeak = 0.0f;

  juce::Label usernameLabel;
  juce::Label channelLabel;

  juce::Slider volumeSlider;
  juce::Slider panSlider;
  juce::ToggleButton muteButton;
  juce::ToggleButton soloButton;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteUserStrip)
};
