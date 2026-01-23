#include "GainUtils.h"
#include "NinjamLookAndFeel.h"
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
  volumeSlider.setRange(GainUtils::kMinDb, GainUtils::kMaxDb,
                        GainUtils::kStepDb);
  volumeSlider.setValue(GainUtils::gainToDb(channel->volume.load()),
                        juce::dontSendNotification);
  volumeSlider.setTooltip(
      "Volume in dB: -inf to +6, unity at 0. Applies to both your monitor mix "
      "and what is transmitted.");
  volumeSlider.onValueChange = [this]() {
    channel->volume.store(GainUtils::dbToGain(volumeSlider.getValue()));
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

  inputBusBox.setTooltip("Input bus: which DAW input bus feeds this channel");
  inputBusBox.onChange = [this]() {
    int sel = inputBusBox.getSelectedId() - 1;
    if (sel >= 0) channel->inputBusIndex.store(sel);
  };
  addAndMakeVisible(inputBusBox);
  updateInputBusCount(1);
}

LocalChannelStrip::~LocalChannelStrip() {}

void LocalChannelStrip::setRemovable(bool removable) {
  removeButton.setVisible(removable);
}

void LocalChannelStrip::updateInputBusCount(int numBuses) {
  int current = inputBusBox.getSelectedId();
  inputBusBox.clear(juce::dontSendNotification);
  for (int i = 0; i < numBuses; ++i)
    inputBusBox.addItem("In " + juce::String(i + 1), i + 1);
  int stored = channel->inputBusIndex.load() + 1;
  inputBusBox.setSelectedId(
      (stored >= 1 && stored <= numBuses) ? stored : 1,
      juce::dontSendNotification);
  (void)current;
}

void LocalChannelStrip::updatePeaks() {
  const double now = juce::Time::getMillisecondCounterHiRes();
  // Clamped so a long gap (editor hidden, host stalled) drops the meter by a
  // sane amount instead of snapping it straight to silence.
  const double elapsed =
      lastPeakUpdateMs > 0.0
          ? juce::jlimit(0.0, 0.25, (now - lastPeakUpdateMs) / 1000.0)
          : 0.0;
  lastPeakUpdateMs = now;

  decayedPeakL =
      GainUtils::decayMeterPeak(decayedPeakL, channel->peakL.load(), elapsed);
  decayedPeakR =
      GainUtils::decayMeterPeak(decayedPeakR, channel->peakR.load(), elapsed);
  repaint();
}

void LocalChannelStrip::paint(juce::Graphics &g) {
  g.setColour(juce::Colour(0xff1e1e32));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);

  auto drawVBar = [&](juce::Rectangle<int> r, float peak) {
    g.setColour(juce::Colour(0xff111122));
    g.fillRect(r);
    const float frac = GainUtils::meterFraction(peak);
    if (frac > 0.0f) {
      auto fill = r.removeFromBottom((int)(r.getHeight() * frac));
      g.setColour(meterColour(GainUtils::meterZone(peak)));
      g.fillRect(fill);
    }
  };
  // A mono channel transmits one summed signal, so it gets one bar spanning
  // both gutters. Two bars would imply a stereo pair that is not being sent.
  if (channel != nullptr && channel->isMono.load()) {
    drawVBar(vuLArea.getUnion(vuRArea), decayedPeakL);
  } else {
    drawVBar(vuLArea, decayedPeakL);
    drawVBar(vuRArea, decayedPeakR);
  }

  if (!scaleArea.isEmpty())
    drawDbScale(g, volumeSlider, scaleArea);
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
  inputBusBox.setBounds(area.removeFromBottom(22));

  area.removeFromBottom(4);
  xmitButton.setBounds(area.removeFromBottom(22));

  area.removeFromBottom(4);
  auto muteSoloRow = area.removeFromBottom(22);
  soloButton.setBounds(muteSoloRow.removeFromRight(38));
  muteSoloRow.removeFromRight(4);
  muteButton.setBounds(muteSoloRow);
  area.removeFromBottom(4);

  // Remaining flex area: L/R VU bars | dB scale | fader.
  // The meters sit together on the left so a single scale gutter serves both
  // them and the fader.
  auto meters = area.removeFromLeft(10);
  area.removeFromLeft(1);
  scaleArea = area.removeFromLeft(22);
  area.removeFromLeft(2);
  volumeSlider.setBounds(area);

  // Meter extent comes from the fader, so a given dB is at the same height on
  // both, and the meters stop at the 0 dB tick.
  vuLArea = meterAreaFor(volumeSlider, meters.getX(), 4);
  vuRArea = meterAreaFor(volumeSlider, meters.getX() + 6, 4);
}
