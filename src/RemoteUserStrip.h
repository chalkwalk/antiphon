#pragma once

#include "RemoteChannelRow.h"
#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <vector>

class RemoteUserStrip : public juce::Component {
public:
  RemoteUserStrip(AntiphonAudioProcessor &processor,
                  const juce::String &username);
  ~RemoteUserStrip() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void updateChannels(
      const std::map<int, NinjamClient::RemoteUserChannel> &channels);

  int getPreferredWidth() const;
  void updateOutputBusCount(int numBuses);

  // Turns this into the practice-echo strip: one row per tap, each driving an
  // echo rather than a remote channel. Everything else about the strip is
  // unchanged, because an echo goes through the same mix path as a player --
  // fader, pan, mute, solo and output bus all work, so an echo can be routed
  // to its own DAW bus like anyone else.
  void updateEchoTaps(const std::vector<NinjamClient::EchoTap> &taps,
                      int maxDelay);

private:
  AntiphonAudioProcessor &audioProcessor;
  juce::String username;

  juce::Label usernameLabel;
  juce::OwnedArray<RemoteChannelRow> channelRows;
  std::vector<int> channelIndices; // parallel to channelRows

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteUserStrip)
};
