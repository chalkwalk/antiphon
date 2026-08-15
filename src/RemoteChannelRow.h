#pragma once
#include "NinjamClient.h"
#include <JuceHeader.h>

class AntiphonAudioProcessor;

class RemoteChannelRow : public juce::Component {
public:
  // `echoTap` >= 0 makes this an echo row: the controls drive an echo tap
  // rather than a remote channel, and the Recv button gives up its place to a
  // delay picker. Recv is meaningless for an echo -- there is no server to stop
  // sending -- so the space is free.
  RemoteChannelRow(AntiphonAudioProcessor &p, juce::String username,
                   int channelIndex, int echoTap = -1);

  void setEchoDelayOptions(int maxDelay, int current);

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
  juce::ComboBox delayBox; // echo rows only, in the Recv button's place
  juce::ComboBox outputBusBox;
  int echoTap = -1;
  bool isEcho() const { return echoTap >= 0; }

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
