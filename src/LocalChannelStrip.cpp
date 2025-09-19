#include "LocalChannelStrip.h"

LocalChannelStrip::LocalChannelStrip(
    NinjamAudioProcessor &p,
    std::shared_ptr<NinjamAudioProcessor::LocalChannel> ch)
    : audioProcessor(p), channel(std::move(ch)) {

  nameEditor.setText(channel->name, false);
  nameEditor.setFont(juce::FontOptions{}.withHeight(13.0f));
  nameEditor.setMultiLine(false);
  nameEditor.setReturnKeyStartsNewLine(false);
  nameEditor.onTextChange = [this]() {
    channel->name = nameEditor.getText();
    audioProcessor.sendChannelInfoToServer();
  };
  addAndMakeVisible(nameEditor);

  monoButton.setButtonText("Mono");
  monoButton.setTooltip("Sum stereo input to mono before encoding and monitoring");
  monoButton.setToggleState(channel->isMono.load(), juce::dontSendNotification);
  monoButton.onClick = [this]() {
    channel->isMono.store(monoButton.getToggleState());
  };
  addAndMakeVisible(monoButton);

  volumeSlider.setSliderStyle(juce::Slider::LinearVertical);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(0.0, 2.0, 0.01);
  volumeSlider.setValue((double)channel->volume.load(), juce::dontSendNotification);
  volumeSlider.setTooltip("Volume: 0 = silent, 1.0 = unity (centre), 2.0 = double");
  volumeSlider.onValueChange = [this]() {
    channel->volume.store((float)volumeSlider.getValue());
  };
  addAndMakeVisible(volumeSlider);

  panSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue((double)channel->pan.load(), juce::dontSendNotification);
  panSlider.setTooltip("Pan: centre = 0, left = -1, right = +1");
  panSlider.onValueChange = [this]() {
    channel->pan.store((float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setButtonText("M");
  muteButton.setTooltip("Mute: silence this channel in your monitor mix (others still hear you)");
  muteButton.setToggleState(channel->muted.load(), juce::dontSendNotification);
  muteButton.onClick = [this]() {
    channel->muted.store(muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.setButtonText("S");
  soloButton.setTooltip("Solo: hear only soloed channels in your monitor mix");
  soloButton.setToggleState(channel->monitorSolo.load(), juce::dontSendNotification);
  soloButton.onClick = [this]() {
    channel->monitorSolo.store(soloButton.getToggleState());
  };
  addAndMakeVisible(soloButton);

  xmitButton.setButtonText("TX");
  xmitButton.setTooltip("Transmit: send this channel's audio to the server");
  xmitButton.setColour(juce::TextButton::buttonOnColourId,  juce::Colour(0xff0d5c2a)); // green = live
  xmitButton.setColour(juce::TextButton::buttonColourId,    juce::Colour(0xff5a1515)); // red = silent
  xmitButton.setColour(juce::TextButton::textColourOnId,    juce::Colours::white);
  xmitButton.setColour(juce::TextButton::textColourOffId,   juce::Colour(0xffcc6666));
  xmitButton.setToggleState(channel->xmitEnabled.load(), juce::dontSendNotification);
  xmitButton.onClick = [this]() {
    channel->xmitEnabled.store(xmitButton.getToggleState());
  };
  addAndMakeVisible(xmitButton);

  removeButton.setButtonText("X");
  removeButton.onClick = [this]() {
    audioProcessor.removeLastLocalChannel();
  };
  removeButton.setVisible(false);
  addAndMakeVisible(removeButton);
}

LocalChannelStrip::~LocalChannelStrip() {}

void LocalChannelStrip::setRemovable(bool removable) {
  removeButton.setVisible(removable);
}

void LocalChannelStrip::updatePeaks() {
  decayedPeakL = std::max(decayedPeakL * 0.92f, channel->peakL.load());
  decayedPeakR = std::max(decayedPeakR * 0.92f, channel->peakR.load());
  repaint();
}

void LocalChannelStrip::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff1e1e32));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

  auto drawVBar = [&](juce::Rectangle<int> r, float peak) {
    g.setColour(juce::Colour(0xff111122));
    g.fillRect(r);
    float frac = juce::jlimit(0.0f, 1.0f, peak);
    if (frac > 0.0f) {
      auto fill = r.removeFromBottom((int)(r.getHeight() * frac));
      g.setColour(frac > 0.7f ? juce::Colour(0xffe08000) : juce::Colour(0xff00c040));
      g.fillRect(fill);
    }
  };
  drawVBar(vuLArea, decayedPeakL);
  drawVBar(vuRArea, decayedPeakR);
}

void LocalChannelStrip::resized() {
  auto area = getLocalBounds().reduced(4);

  // Fixed top elements
  nameEditor.setBounds(area.removeFromTop(22));
  area.removeFromTop(4);
  panSlider.setBounds(area.removeFromTop(44));
  area.removeFromTop(4);

  // Fixed bottom elements
  area.removeFromBottom(4);
  auto monoRemoveRow = area.removeFromBottom(22);
  removeButton.setBounds(monoRemoveRow.removeFromRight(38));
  monoRemoveRow.removeFromRight(4);
  monoButton.setBounds(monoRemoveRow);

  area.removeFromBottom(4);
  xmitButton.setBounds(area.removeFromBottom(22));

  area.removeFromBottom(4);
  auto muteSoloRow = area.removeFromBottom(22);
  soloButton.setBounds(muteSoloRow.removeFromRight(38));
  muteSoloRow.removeFromRight(4);
  muteButton.setBounds(muteSoloRow);
  area.removeFromBottom(4);

  // Remaining flex area: L VU bar | fader | R VU bar
  vuLArea = area.removeFromLeft(4);
  vuRArea = area.removeFromRight(4);
  volumeSlider.setBounds(area);
}
