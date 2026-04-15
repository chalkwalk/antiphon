#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h>
#include <memory>

class LocalChannelStrip : public juce::Component {
public:
  LocalChannelStrip(AntiphonAudioProcessor &processor,
                    std::shared_ptr<AntiphonAudioProcessor::LocalChannel> channel);
  ~LocalChannelStrip() override;

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
  juce::ToggleButton xmitButton;
  juce::TextButton removeButton;
  juce::ComboBox inputBusBox;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LocalChannelStrip)
};
