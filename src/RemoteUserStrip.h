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


  bool isSelected() const { return selected; }
  void setSelected(bool sel);
  juce::String getUsername() const { return username; }
  int getNumChannels() const { return channelRows.size(); }
  RemoteChannelRow *getChannelRow(int index = 0) const {
    return (index >= 0 && index < channelRows.size()) ? channelRows[index]
                                                      : nullptr;
  }

private:
  bool selected = false;
  AntiphonAudioProcessor &audioProcessor;
  juce::String username;

  juce::Label usernameLabel;
  juce::OwnedArray<RemoteChannelRow> channelRows;
  std::vector<int> channelIndices; // parallel to channelRows

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteUserStrip)
};
