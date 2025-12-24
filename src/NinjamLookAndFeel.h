#pragma once
#include "GainUtils.h"
#include <JuceHeader.h>

// Meter colour by band. Lives here rather than in GainUtils because that
// header is compiled into the test target, which does not link juce_graphics.
inline juce::Colour meterColour(GainUtils::MeterZone zone) {
  switch (zone) {
  case GainUtils::MeterZone::Over:
    return juce::Colour(0xffe03030); // at or above 0 dBFS
  case GainUtils::MeterZone::Hot:
    return juce::Colour(0xffe08000); // -6 dBFS and up
  case GainUtils::MeterZone::Normal:
  default:
    return juce::Colour(0xff00c040);
  }
}

class NinjamLookAndFeel : public juce::LookAndFeel_V4 {
public:
  NinjamLookAndFeel();

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, const float rotaryStartAngle,
                        const float rotaryEndAngle,
                        juce::Slider &slider) override;

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawToggleButton(juce::Graphics &g, juce::ToggleButton &button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  void drawTextEditorOutline(juce::Graphics &g, int width, int height,
                             juce::TextEditor &textEditor) override;
};
