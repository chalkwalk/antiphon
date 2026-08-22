#pragma once
#include "NinjamClient.h"
#include <JuceHeader.h>

class AntiphonAudioProcessor;

class RemoteChannelRow : public juce::Component {
public:
  RemoteChannelRow(AntiphonAudioProcessor &p, juce::String username,
                   int channelIndex);

  void update(const NinjamClient::RemoteUserChannel &c);
  void updatePeak(float peak);
  void updateOutputBusCount(int numBuses);
  void toggleMute();
  void toggleSolo();
  void nudgeVolume(float deltaDb);
  void nudgePan(float delta);
  juce::String getChannelName() const { return channelNameLabel.getText(); }

  void paint(juce::Graphics &g) override;
  void resized() override;

private:
  AntiphonAudioProcessor &audioProcessor;
  juce::String username;
  int channelIndex;

  juce::Label channelNameLabel;
  juce::Slider volumeSlider;
  juce::Slider panSlider;
  juce::ToggleButton muteButton{"M"};
  juce::ToggleButton soloButton{"S"};
  juce::ToggleButton recvButton{"R"};
  juce::ComboBox outputBusBox;

  float decayedPeak = 0.0f;
  // Meter release is a rate, so it needs real elapsed time rather than a
  // per-tick constant. 0 means "first update, do not decay".
  double lastPeakUpdateMs = 0.0;
  juce::Rectangle<int> scaleArea;
  juce::Rectangle<int> vuArea;
  // What the meter currently shows; see LocalChannelStrip.
  float shownFraction = -1.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteChannelRow)
};
