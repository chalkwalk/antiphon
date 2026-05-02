#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <memory>

class LocalChannelStrip : public juce::Component {
public:
  LocalChannelStrip(AntiphonAudioProcessor &processor,
                    std::shared_ptr<AntiphonAudioProcessor::LocalChannel> channel,
                    int channelIndex);
  ~LocalChannelStrip() override;

  // Toggles transmit and applies it to the whole interval so far, as though it
  // had been that way from the start. Bound to holding the TX button and to a
  // keyboard chord -- a gesture with no keyboard equivalent would be
  // unreachable for a screen-reader user (PRINCIPLES 11).
  void toggleTransmitRetroactively(bool alreadyToggled = false);

  void paint(juce::Graphics &) override;
  void resized() override;

  void updatePeaks();
  void setRemovable(bool removable);
  void updateInputBusCount(int numBuses);
  // Keeps the strip's spoken identity in step with its editable name.
  void refreshAccessibleName();

private:
  AntiphonAudioProcessor &audioProcessor;
  std::shared_ptr<AntiphonAudioProcessor::LocalChannel> channel;

  float decayedPeakL = 0.0f;
  float decayedPeakR = 0.0f;
  // Meter release is a rate, so it needs real elapsed time rather than a
  // per-tick constant. 0 means "first update, do not decay".
  double lastPeakUpdateMs = 0.0;
  juce::Rectangle<int> scaleArea;
  juce::Rectangle<int> vuLArea, vuRArea;
  // What the meter currently shows, so a change too small to see costs nothing.
  float shownFractionL = -1.0f, shownFractionR = -1.0f;
  // Mono draws one wide bar instead of two, so a change to it has to force a
  // repaint even when neither level moved past the redraw threshold. -1 means
  // "nothing drawn yet", so the first pass always paints.
  int shownMono = -1;

  juce::TextEditor nameEditor;
  juce::ToggleButton monoButton;
  juce::Slider volumeSlider;
  juce::Slider panSlider;
  juce::ToggleButton muteButton;
  juce::ToggleButton soloButton;
  // TX, which is also the retroactive gesture. The press moment is captured on
  // mouse-down rather than when the hold expires: the plain toggle has already
  // taken effect from the press, so the rewrite only has to cover what came
  // before it.
  class TransmitButton : public juce::ToggleButton {
  public:
    std::function<void()> onHold;
    void mouseDown(const juce::MouseEvent &e) override;
    void mouseUp(const juce::MouseEvent &e) override;

  private:
    // Long enough not to fire on an ordinary click, short enough to use while
    // an interval is still running -- at 137 BPM and 11 BPI you have under five
    // seconds to decide.
    static constexpr int kHoldMs = 400;
    juce::uint32 pressedAtMs = 0;
  };
  TransmitButton xmitButton;
  int channelIndex = 0;
  // Set when the retroactive gesture fires, so the button can flash and say so.
  double retroFlashUntilMs = 0.0;
  juce::TextButton removeButton;
  juce::ComboBox inputBusBox;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LocalChannelStrip)
};
