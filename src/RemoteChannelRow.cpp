#include "RemoteChannelRow.h"
#include "PluginProcessor.h"

RemoteChannelRow::RemoteChannelRow(NinjamAudioProcessor &p,
                                   juce::String uname, int chIdx)
    : audioProcessor(p), username(uname), channelIndex(chIdx) {

  channelNameLabel.setFont(juce::Font(11.0f));
  channelNameLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  addAndMakeVisible(channelNameLabel);

  volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(0.0, 2.0, 0.01);
  volumeSlider.setValue(0.5, juce::dontSendNotification);
  volumeSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserVolume(
        username, channelIndex, (float)volumeSlider.getValue());
  };
  addAndMakeVisible(volumeSlider);

  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue(0.0, juce::dontSendNotification);
  panSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserPan(username, channelIndex,
                                                 (float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserMute(username, channelIndex,
                                                  muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserSolo(username, channelIndex,
                                                  soloButton.getToggleState());
  };
  addAndMakeVisible(soloButton);

  recvButton.setToggleState(true, juce::dontSendNotification);
  recvButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserRecv(username, channelIndex,
                                                  recvButton.getToggleState());
  };
  addAndMakeVisible(recvButton);
}

void RemoteChannelRow::update(const NinjamClient::RemoteUserChannel &c) {
  channelNameLabel.setText(c.channelName, juce::dontSendNotification);
  volumeSlider.setValue(c.volume, juce::dontSendNotification);
  panSlider.setValue(c.pan, juce::dontSendNotification);
  muteButton.setToggleState(c.isMuted, juce::dontSendNotification);
  soloButton.setToggleState(c.isSoloed, juce::dontSendNotification);
  recvButton.setToggleState(c.recvEnabled, juce::dontSendNotification);
}

void RemoteChannelRow::updatePeak(float peak) {
  decayedPeak = juce::jmax(decayedPeak * 0.92f, peak);
  repaint();
}

void RemoteChannelRow::paint(juce::Graphics &g) {
  auto vu = getLocalBounds().reduced(0, 2).removeFromLeft(7);
  g.setColour(juce::Colour(0xff111122));
  g.fillRect(vu);
  if (decayedPeak > 0.0f) {
    float frac = juce::jlimit(0.0f, 1.0f, decayedPeak);
    auto fill = vu.removeFromBottom((int)(vu.getHeight() * frac));
    g.setColour(frac > 0.7f ? juce::Colour(0xffe08000) : juce::Colour(0xff00c040));
    g.fillRect(fill);
  }
}

void RemoteChannelRow::resized() {
  auto area = getLocalBounds().reduced(0, 2);
  area.removeFromLeft(11); // VU bar drawn in paint()

  channelNameLabel.setBounds(area.removeFromLeft(80));
  area.removeFromLeft(4);
  volumeSlider.setBounds(area.removeFromLeft(80));
  area.removeFromLeft(4);
  panSlider.setBounds(area.removeFromLeft(60));
  area.removeFromLeft(4);
  muteButton.setBounds(area.removeFromLeft(22));
  area.removeFromLeft(4);
  soloButton.setBounds(area.removeFromLeft(22));
  area.removeFromLeft(4);
  recvButton.setBounds(area.removeFromLeft(22));
}
