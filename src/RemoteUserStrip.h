#pragma once

#include "RemoteChannelRow.h"
#include "PluginProcessor.h"
#include <JuceHeader.h>

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

private:
  AntiphonAudioProcessor &audioProcessor;
  juce::String username;

  juce::Label usernameLabel;
  juce::OwnedArray<RemoteChannelRow> channelRows;
  std::vector<int> channelIndices; // parallel to channelRows

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteUserStrip)
};
