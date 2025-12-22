#include "GainUtils.h"
#include "RemoteChannelRow.h"
#include "PluginProcessor.h"

RemoteChannelRow::RemoteChannelRow(NinjamAudioProcessor &p,
                                   juce::String uname, int chIdx)
    : audioProcessor(p), username(uname), channelIndex(chIdx) {

  channelNameLabel.setFont(juce::FontOptions{}.withHeight(11.0f));
  channelNameLabel.setColour(juce::Label::textColourId,
                             juce::Colours::lightgrey);
  addAndMakeVisible(channelNameLabel);

  volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(GainUtils::kMinDb, GainUtils::kMaxDb,
                        GainUtils::kStepDb);
  volumeSlider.setValue(
      GainUtils::gainToDb(NinjamClient::kDefaultRemoteChannelVolume),
      juce::dontSendNotification);
  volumeSlider.setTooltip(
      "Volume in dB: -inf to +6, unity at 0. Remote channels default to "
      "-12 dB, matching the reference client.");
  volumeSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserVolume(
        username, channelIndex, GainUtils::dbToGain(volumeSlider.getValue()));
  };
  addAndMakeVisible(volumeSlider);

  panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue(0.0, juce::dontSendNotification);
  panSlider.setTooltip("Pan: centre = 0, left = -1, right = +1");
  panSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserPan(username, channelIndex,
                                                 (float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setTooltip("Mute: silence this player in your mix (audio still downloads)");
  muteButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserMute(username, channelIndex,
                                                  muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.setTooltip("Solo: hear only soloed remote channels");
  soloButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserSolo(username, channelIndex,
                                                  soloButton.getToggleState());
  };
  addAndMakeVisible(soloButton);

  recvButton.setTooltip("Receive: download this player's audio from the server");
  recvButton.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff0d5c2a)); // green = receiving
  recvButton.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff5a1515)); // red = not receiving
  recvButton.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
  recvButton.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffcc6666));
  recvButton.setToggleState(true, juce::dontSendNotification);
  recvButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserRecv(username, channelIndex,
                                                  recvButton.getToggleState());
  };
  addAndMakeVisible(recvButton);

  outputBusBox.setTooltip("Output bus: which plugin output bus receives this channel");
  outputBusBox.onChange = [this]() {
    int sel = outputBusBox.getSelectedId() - 1;
    if (sel >= 0)
      audioProcessor.setRemoteUserOutputBus(username, channelIndex, sel);
  };
  addAndMakeVisible(outputBusBox);
  updateOutputBusCount(1);
}

void RemoteChannelRow::update(const NinjamClient::RemoteUserChannel &c) {
  channelNameLabel.setText(c.channelName, juce::dontSendNotification);
  volumeSlider.setValue(GainUtils::gainToDb(c.volume),
                        juce::dontSendNotification);
  panSlider.setValue(c.pan, juce::dontSendNotification);
  muteButton.setToggleState(c.isMuted, juce::dontSendNotification);
  soloButton.setToggleState(c.isSoloed, juce::dontSendNotification);
  recvButton.setToggleState(c.recvEnabled, juce::dontSendNotification);
  int busId = c.outputBusIndex + 1;
  if (busId != outputBusBox.getSelectedId())
    outputBusBox.setSelectedId(busId, juce::dontSendNotification);
}

void RemoteChannelRow::updateOutputBusCount(int numBuses) {
  int current = outputBusBox.getSelectedId();
  outputBusBox.clear(juce::dontSendNotification);
  for (int i = 0; i < numBuses; ++i)
    outputBusBox.addItem("Out " + juce::String(i + 1), i + 1);
  int sel = (current >= 1 && current <= numBuses) ? current : 1;
  outputBusBox.setSelectedId(sel, juce::dontSendNotification);
}

void RemoteChannelRow::updatePeak(float peak) {
  decayedPeak = juce::jmax(decayedPeak * 0.92f, peak);
  repaint();
}

void RemoteChannelRow::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff111122));
  g.fillRect(vuArea);
  float frac = juce::jlimit(0.0f, 1.0f, decayedPeak);
  if (frac > 0.0f) {
    auto fill = vuArea;
    fill.removeFromTop((int)(vuArea.getHeight() * (1.0f - frac)));
    g.setColour(frac > 0.7f ? juce::Colour(0xffe08000) : juce::Colour(0xff00c040));
    g.fillRect(fill);
  }
}

void RemoteChannelRow::resized() {
  auto area = getLocalBounds().reduced(4);

  channelNameLabel.setBounds(area.removeFromTop(22));
  area.removeFromTop(4);
  panSlider.setBounds(area.removeFromTop(44));
  area.removeFromTop(4);

  area.removeFromBottom(4);
  recvButton.setBounds(area.removeFromBottom(22));
  area.removeFromBottom(4);
  outputBusBox.setBounds(area.removeFromBottom(22));
  area.removeFromBottom(4);
  auto muteSoloRow = area.removeFromBottom(22);
  soloButton.setBounds(muteSoloRow.removeFromRight(38));
  muteSoloRow.removeFromRight(4);
  muteButton.setBounds(muteSoloRow);
  area.removeFromBottom(4);

  // Remaining: single VU bar on left, fader fills the rest
  vuArea = area.removeFromLeft(6);
  area.removeFromLeft(2);
  volumeSlider.setBounds(area);
}
