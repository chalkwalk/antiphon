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
  };
  addAndMakeVisible(nameEditor);

  monoButton.setButtonText("M/S");
  monoButton.setToggleState(channel->isMono.load(), juce::dontSendNotification);
  monoButton.onClick = [this]() {
    channel->isMono.store(monoButton.getToggleState());
  };
  addAndMakeVisible(monoButton);

  volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(0.0, 2.0, 0.01);
  volumeSlider.setValue((double)channel->volume.load(), juce::dontSendNotification);
  volumeSlider.onValueChange = [this]() {
    channel->volume.store((float)volumeSlider.getValue());
  };
  addAndMakeVisible(volumeSlider);

  panSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  panSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  panSlider.setRange(-1.0, 1.0, 0.01);
  panSlider.setValue((double)channel->pan.load(), juce::dontSendNotification);
  panSlider.onValueChange = [this]() {
    channel->pan.store((float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setButtonText("M");
  muteButton.setToggleState(channel->muted.load(), juce::dontSendNotification);
  muteButton.onClick = [this]() {
    channel->muted.store(muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  xmitButton.setButtonText("TX");
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

  // VU bars: two 5px bars on the left
  auto vuArea = getLocalBounds().reduced(2, 4).removeFromLeft(14);
  auto vuL = vuArea.removeFromLeft(5);
  vuArea.removeFromLeft(2);
  auto vuR = vuArea.removeFromLeft(5);

  auto drawBar = [&](juce::Rectangle<int> r, float peak) {
    g.setColour(juce::Colour(0xff111122));
    g.fillRect(r);
    float frac = juce::jlimit(0.0f, 1.0f, peak);
    if (frac > 0.0f) {
      auto fill = r.removeFromBottom((int)(r.getHeight() * frac));
      g.setColour(frac > 0.7f ? juce::Colour(0xffe08000) : juce::Colour(0xff00c040));
      g.fillRect(fill);
    }
  };
  drawBar(vuL, decayedPeakL);
  drawBar(vuR, decayedPeakR);
}

void LocalChannelStrip::resized() {
  auto area = getLocalBounds().reduced(4);
  area.removeFromLeft(18); // VU bars

  nameEditor.setBounds(area.removeFromLeft(100));
  area.removeFromLeft(4);
  monoButton.setBounds(area.removeFromLeft(36));
  area.removeFromLeft(4);
  volumeSlider.setBounds(area.removeFromLeft(100));
  area.removeFromLeft(4);
  panSlider.setBounds(area.removeFromLeft(70));
  area.removeFromLeft(4);
  muteButton.setBounds(area.removeFromLeft(28));
  area.removeFromLeft(4);
  xmitButton.setBounds(area.removeFromLeft(28));
  area.removeFromLeft(4);
  removeButton.setBounds(area.removeFromLeft(22));
}
