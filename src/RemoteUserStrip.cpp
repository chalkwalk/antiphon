#include "RemoteUserStrip.h"

RemoteUserStrip::RemoteUserStrip(NinjamAudioProcessor &p, const juce::String &u)
    : audioProcessor(p), username(u) {

  usernameLabel.setText(username, juce::dontSendNotification);
  usernameLabel.setFont(juce::Font(13.0f, juce::Font::bold));
  addAndMakeVisible(usernameLabel);
}

RemoteUserStrip::~RemoteUserStrip() {}

int RemoteUserStrip::getPreferredHeight() const {
  return 4 + 22 + (int)channelRows.size() * 28 + 4;
}

void RemoteUserStrip::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff2a2a3e));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
}

void RemoteUserStrip::resized() {
  auto area = getLocalBounds().reduced(4);
  usernameLabel.setBounds(area.removeFromTop(22));
  for (auto *row : channelRows) {
    row->setBounds(area.removeFromTop(28));
  }
}

void RemoteUserStrip::updateChannels(
    const std::map<int, NinjamClient::RemoteUserChannel> &channels) {
  // Add rows for new channels
  for (const auto &[chIdx, ch] : channels) {
    bool found = false;
    for (int i = 0; i < (int)channelIndices.size(); ++i) {
      if (channelIndices[i] == chIdx) { found = true; break; }
    }
    if (!found) {
      auto *row = new RemoteChannelRow(audioProcessor, username, chIdx);
      channelRows.add(row);
      channelIndices.push_back(chIdx);
      addAndMakeVisible(row);
    }
  }

  // Remove rows for departed channels
  for (int i = (int)channelIndices.size() - 1; i >= 0; --i) {
    if (channels.find(channelIndices[i]) == channels.end()) {
      channelRows.remove(i);
      channelIndices.erase(channelIndices.begin() + i);
    }
  }

  // Update each row
  for (int i = 0; i < (int)channelIndices.size(); ++i) {
    const auto &ch = channels.at(channelIndices[i]);
    channelRows[i]->update(ch);
    channelRows[i]->updatePeak(ch.peakLevel);
  }

  resized();
}
