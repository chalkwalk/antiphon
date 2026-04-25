#include "GainUtils.h"
#include "AntiphonLookAndFeel.h"
#include "LocalChannelStrip.h"

LocalChannelStrip::LocalChannelStrip(
    AntiphonAudioProcessor &p,
    std::shared_ptr<AntiphonAudioProcessor::LocalChannel> ch)
    : audioProcessor(p), channel(std::move(ch)) {

  // The strip is a focus container, so a reader navigates strip by strip and
  // announces "Instrument, Mute" rather than presenting forty flat controls
  // whose names repeat once per channel.
  setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
  refreshAccessibleName();

  nameEditor.setTitle("Channel name");
  nameEditor.setDescription(
      "The name other players see for this channel. Sent to the server.");
  nameEditor.setText(channel->name, false);
  nameEditor.setFont(juce::FontOptions{}.withHeight(13.0f));
  nameEditor.setMultiLine(false);
  nameEditor.setReturnKeyStartsNewLine(false);
  nameEditor.onTextChange = [this]() {
    channel->name = nameEditor.getText();
    refreshAccessibleName();
    audioProcessor.sendChannelInfoToServer();
  };
  addAndMakeVisible(nameEditor);

  monoButton.setButtonText("Mono");
  monoButton.setTitle("Mono");
  monoButton.setDescription(
      "Sum the stereo input to mono before monitoring and transmitting");
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
  volumeSlider.setTitle("Volume");
  volumeSlider.setDescription(
      "Channel volume in decibels. Applies to both your monitor mix and what "
      "is transmitted.");
  // Spoken as "-12.0 dB" rather than a bare number: a slider that announces
  // "-12" leaves the unit, and therefore the meaning, to guesswork.
  volumeSlider.textFromValueFunction = [](double v) {
    return v <= GainUtils::kMinDb ? juce::String("minus infinity decibels")
                                  : juce::String(v, 1) + " dB";
  };
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
  panSlider.setTitle("Pan");
  panSlider.setDescription("Stereo position of this channel");
  panSlider.textFromValueFunction = [](double v) {
    const int pct = (int)std::round(std::abs(v) * 100.0);
    if (pct == 0) return juce::String("centre");
    return juce::String(v < 0 ? "left " : "right ") + juce::String(pct);
  };
  panSlider.setTooltip("Pan: centre = 0, left = -1, right = +1");
  panSlider.onValueChange = [this]() {
    channel->pan.store((float)panSlider.getValue());
  };
  addAndMakeVisible(panSlider);

  muteButton.setButtonText("M");
  muteButton.setTitle("Mute");
  muteButton.setDescription(
      "Silence this channel in your own monitor mix. Other players still hear "
      "you.");
  muteButton.setTooltip("Mute: silence this channel in your monitor mix (others still hear you)");
  muteButton.setToggleState(channel->muted.load(), juce::dontSendNotification);
  muteButton.onClick = [this]() {
    channel->muted.store(muteButton.getToggleState());
  };
  addAndMakeVisible(muteButton);

  soloButton.setButtonText("S");
  soloButton.setTitle("Solo");
  soloButton.setDescription(
      "Hear only soloed channels in your own monitor mix. Does not affect what "
      "other players hear.");
  soloButton.setTooltip("Solo: hear only soloed channels in your monitor mix");
  soloButton.setToggleState(channel->monitorSolo.load(), juce::dontSendNotification);
  soloButton.onClick = [this]() {
    channel->monitorSolo.store(soloButton.getToggleState());
    // Solo is one bus across local and remote, so the client has to be told.
    audioProcessor.refreshLocalSoloState();
  };
  addAndMakeVisible(soloButton);

  xmitButton.setButtonText("TX");
  xmitButton.setTitle("Transmit");
  xmitButton.setDescription(
      "Send this channel's audio to the other players");
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
  removeButton.setTitle("Remove channel");
  removeButton.setDescription("Delete this channel strip");
  removeButton.setTooltip("Remove this channel");
  removeButton.onClick = [this]() {
    audioProcessor.removeLastLocalChannel();
  };
  removeButton.setVisible(false);
  addAndMakeVisible(removeButton);

  inputBusBox.setTitle("Input bus");
  inputBusBox.setDescription("Which plugin input bus feeds this channel");
  inputBusBox.setTooltip("Input bus: which DAW input bus feeds this channel");
  inputBusBox.onChange = [this]() {
    int sel = inputBusBox.getSelectedId() - 1;
    if (sel >= 0) channel->inputBusIndex.store(sel);
  };
  addAndMakeVisible(inputBusBox);
  updateInputBusCount(1);
}

LocalChannelStrip::~LocalChannelStrip() {}

void LocalChannelStrip::refreshAccessibleName() {
  const juce::String n =
      channel->name.isNotEmpty() ? channel->name : juce::String("Local channel");
  setTitle(n);
  setDescription("Local input channel " + n);
}

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
  // Only the meter gutter moves, and only when it has moved far enough to
  // redraw to different pixels. Repainting the whole strip redrew the dB scale
  // -- seven pieces of text per strip -- 30 times a second, for a bar a few
  // pixels wide.
  const float fL = GainUtils::meterFraction(decayedPeakL);
  const float fR = GainUtils::meterFraction(decayedPeakR);
  // Mono changes the meter's shape, not its level, so the level threshold
  // cannot see it: toggling Mono left the meter drawn as a stereo pair until
  // something happened to move a bar far enough to redraw for its own reasons.
  const int mono = (channel != nullptr && channel->isMono.load()) ? 1 : 0;
  const bool shapeChanged = mono != shownMono;
  if (!shapeChanged && !GainUtils::meterNeedsRepaint(shownFractionL, fL) &&
      !GainUtils::meterNeedsRepaint(shownFractionR, fR))
    return;
  shownFractionL = fL;
  shownFractionR = fR;
  shownMono = mono;

  const auto meters = vuLArea.getUnion(vuRArea);
  if (!meters.isEmpty())
    repaint(meters);
  else
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
