#include "AntiphonLookAndFeel.h"
#include "RemoteUserStrip.h"

RemoteUserStrip::RemoteUserStrip(AntiphonAudioProcessor &p,
                                 const juce::String &u)
    : audioProcessor(p), username(u) {

  // A reader navigates player by player; the nickname is the useful identity,
  // so the user@host form goes in the description rather than the name.
  setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
  setTitle(username.upToFirstOccurrenceOf("@", false, false));
  setDescription("Remote player " + username);

  usernameLabel.setText(username, juce::dontSendNotification);
  usernameLabel.setFont(
      juce::FontOptions{}.withHeight(13.0f).withStyle("Bold"));
  addAndMakeVisible(usernameLabel);
}

RemoteUserStrip::~RemoteUserStrip() {}

int RemoteUserStrip::getPreferredWidth() const {
  int n = channelRows.size();
  if (n == 0)
    return 98;
  return 8 + n * 90 + (n - 1) * 4;
}

void RemoteUserStrip::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff2a2a3e));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

  if (selected) {
    g.setColour(juce::Colour(AntiphonTheme::kAccent));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f,
                           2.0f);
  }
}

void RemoteUserStrip::setSelected(bool sel) {
  if (selected != sel) {
    selected = sel;
    repaint();
  }
}

void RemoteUserStrip::resized() {
  auto area = getLocalBounds().reduced(4);
  usernameLabel.setBounds(area.removeFromTop(22));
  area.removeFromTop(4);
  int x = area.getX();
  int y = area.getY();
  int h = area.getHeight();
  for (auto *row : channelRows) {
    row->setBounds(x, y, 90, h);
    x += 94;
  }
}

void RemoteUserStrip::updateEchoTaps(
    const std::vector<NinjamClient::EchoTap> &taps, int maxDelay) {
  // Rows are created once and then only updated: rebuilding them every tick
  // would take focus away from whatever the user was adjusting.
  if (channelRows.size() != (int)taps.size()) {
    channelRows.clear();
    channelIndices.clear();
    for (int i = 0; i < (int)taps.size(); ++i) {
      auto *row = new RemoteChannelRow(audioProcessor, "Echo", i, i);
      channelRows.add(row);
      channelIndices.push_back(i);
      addAndMakeVisible(row);
    }
    resized();
  }

  for (int i = 0; i < (int)taps.size() && i < channelRows.size(); ++i) {
    channelRows[i]->update(taps[(std::size_t)i].channel);
    channelRows[i]->updatePeak(taps[(std::size_t)i].channel.peakLevel);
    channelRows[i]->setEchoDelayOptions(maxDelay,
                                        taps[(std::size_t)i].delayIntervals);
  }
}

void RemoteUserStrip::updateOutputBusCount(int numBuses) {
  for (auto *row : channelRows)
    row->updateOutputBusCount(numBuses);
}

void RemoteUserStrip::updateChannels(
    const std::map<int, NinjamClient::RemoteUserChannel> &channels) {
  // Add rows for new channels
  for (const auto &[chIdx, ch] : channels) {
    bool found = false;
    for (int i = 0; i < (int)channelIndices.size(); ++i) {
      if (channelIndices[i] == chIdx) {
        found = true;
        break;
      }
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
