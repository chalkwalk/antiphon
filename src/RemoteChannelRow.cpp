#include "GainUtils.h"
#include "AntiphonLookAndFeel.h"
#include "RemoteChannelRow.h"
#include "PluginProcessor.h"

RemoteChannelRow::RemoteChannelRow(AntiphonAudioProcessor &p,
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
  volumeSlider.textFromValueFunction = [](double v) {
    return v <= GainUtils::kMinDb ? juce::String("minus infinity decibels")
                                  : juce::String(v, 1) + " dB";
  };
  volumeSlider.setTitle("Volume");
  volumeSlider.setDescription(
      "Volume of this remote channel in your mix, in decibels");
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
  panSlider.textFromValueFunction = [](double v) {
    const int pct = (int)std::round(std::abs(v) * 100.0);
    if (pct == 0) return juce::String("centre");
    return juce::String(v < 0 ? "left " : "right ") + juce::String(pct);
  };
  panSlider.setTitle("Pan");
  panSlider.setDescription(
      "Stereo position of this remote channel in your mix");
  panSlider.setTooltip("Pan: centre = 0, left = -1, right = +1");
  panSlider.onValueChange = [this]() {
    audioProcessor.ninjamClient.setRemoteUserPan(username, channelIndex,
                                                 (float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setTitle("Mute");
  muteButton.setDescription(
      "Silence this remote channel in your mix. Audio is still received.");
  muteButton.setTooltip("Mute: silence this player in your mix (audio still downloads)");
  muteButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserMute(username, channelIndex,
                                                  muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.setTitle("Solo");
  soloButton.setDescription(
      "Hear only soloed remote channels");
  soloButton.setTooltip("Solo: hear only soloed remote channels");
  soloButton.onClick = [this]() {
    audioProcessor.ninjamClient.setRemoteUserSolo(username, channelIndex,
                                                  soloButton.getToggleState());
  };
  addAndMakeVisible(soloButton);

  recvButton.setTitle("Receive");
  recvButton.setDescription(
      "Ask the server to send this channel at all. Turning it off saves bandwidth, unlike Mute.");
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

  outputBusBox.setTitle("Output bus");
  outputBusBox.setDescription(
      "Which plugin output bus this channel is routed to, for recording stems");
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
  setTitle(c.channelName.isNotEmpty() ? c.channelName
                                      : juce::String("Remote channel"));
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
  const double now = juce::Time::getMillisecondCounterHiRes();
  const double elapsed =
      lastPeakUpdateMs > 0.0
          ? juce::jlimit(0.0, 0.25, (now - lastPeakUpdateMs) / 1000.0)
          : 0.0;
  lastPeakUpdateMs = now;

  decayedPeak = GainUtils::decayMeterPeak(decayedPeak, peak, elapsed);
  // As in LocalChannelStrip: the scale beside the meter is static.
  if (!vuArea.isEmpty())
    repaint(vuArea);
  else
    repaint();
}

void RemoteChannelRow::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff111122));
  g.fillRect(vuArea);
  const float frac = GainUtils::meterFraction(decayedPeak);
  if (frac > 0.0f) {
    auto fill = vuArea;
    fill.removeFromTop((int)(vuArea.getHeight() * (1.0f - frac)));
    g.setColour(meterColour(GainUtils::meterZone(decayedPeak)));
    g.fillRect(fill);
  }

  if (!scaleArea.isEmpty())
    drawDbScale(g, volumeSlider, scaleArea);
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

  // Remaining: VU bar | dB scale | fader, sharing one scale.
  auto meter = area.removeFromLeft(6);
  area.removeFromLeft(1);
  scaleArea = area.removeFromLeft(22);
  area.removeFromLeft(2);
  volumeSlider.setBounds(area);

  // Meter extent comes from the fader, so it stops at the 0 dB tick.
  vuArea = meterAreaFor(volumeSlider, meter.getX(), meter.getWidth());
}
