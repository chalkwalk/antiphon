#include "RemoteUserStrip.h"

RemoteUserStrip::RemoteUserStrip(NinjamAudioProcessor &p, const juce::String &u)
    : audioProcessor(p), username(u) {

  usernameLabel.setText(username, juce::dontSendNotification);
  usernameLabel.setFont(juce::Font(16.0f, juce::Font::bold));
  addAndMakeVisible(usernameLabel);

  channelLabel.setText("...", juce::dontSendNotification);
  channelLabel.setFont(juce::Font(12.0f, juce::Font::plain));
  addAndMakeVisible(channelLabel);

  volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(0.0, 2.0, 0.01);
  volumeSlider.setValue(0.5); // Default -6dB
  volumeSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserVolume(
        username, currentChannelIndex, (float)volumeSlider.getValue());
  };
  addAndMakeVisible(volumeSlider);

  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue(0.0);
  panSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserPan(username, currentChannelIndex,
                                                 (float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setButtonText("M");
  muteButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserMute(username, currentChannelIndex,
                                                  muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.setButtonText("S");
  soloButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserSolo(username, currentChannelIndex,
                                                  soloButton.getToggleState());
  };
  addAndMakeVisible(soloButton);
}

RemoteUserStrip::~RemoteUserStrip() {}

void RemoteUserStrip::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff222222));
  g.setColour(juce::Colours::grey);
  g.drawRect(getLocalBounds(), 1);
}

void RemoteUserStrip::resized() {
  auto area = getLocalBounds().reduced(5);
  int rowHeight = area.getHeight() / 2;

  auto topRow = area.removeFromTop(rowHeight);
  usernameLabel.setBounds(topRow.removeFromLeft(120));
  channelLabel.setBounds(topRow);

  auto bottomRow = area;
  volumeSlider.setBounds(bottomRow.removeFromLeft(100));
  panSlider.setBounds(bottomRow.removeFromLeft(60));
  muteButton.setBounds(bottomRow.removeFromLeft(30));
  soloButton.setBounds(bottomRow.removeFromLeft(30));
}

void RemoteUserStrip::updateChannels(
    const std::map<int, NinjamClient::RemoteUserChannel> &channels) {
  if (channels.empty()) {
    channelLabel.setText("No channels", juce::dontSendNotification);
    return;
  }
  auto &firstChan = channels.begin()->second;
  currentChannelIndex = firstChan.channelIndex;
  channelLabel.setText(firstChan.channelName, juce::dontSendNotification);
}
