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

// Vertical position of a dB value, taken from the fader itself rather than
// computed from the bounds: JUCE insets a linear slider's track by half the
// thumb, so an independently calculated scale would sit a few pixels off.
// Everything on the strip -- ticks, labels and the meter extent -- is derived
// from this one mapping, which is what lets the meter stop exactly at the
// 0 dB tick while the fader carries on up to +6.
inline int yForDb(const juce::Slider &fader, double db) {
  return fader.getY() + (int)std::round(fader.getPositionOfValue(db));
}

// The meter occupies only the part of the strip from the bottom of the scale
// up to 0 dBFS, so it is physically shorter than the fader and cannot imply a
// level it could not reach.
inline juce::Rectangle<int> meterAreaFor(const juce::Slider &fader, int x,
                                         int width) {
  const int top = yForDb(fader, GainUtils::kMeterMaxDb);
  const int bottom = yForDb(fader, GainUtils::kMinDb);
  return juce::Rectangle<int>(x, top, width, juce::jmax(1, bottom - top));
}

inline void drawDbScale(juce::Graphics &g, const juce::Slider &fader,
                        juce::Rectangle<int> gutter) {
  g.setFont(juce::FontOptions{}.withHeight(9.0f));
  for (double db : GainUtils::scaleTicksDb()) {
    const int y = yForDb(fader, db);
    const bool isUnity = (db == 0.0);

    g.setColour(isUnity ? juce::Colour(0xff90a0b0) : juce::Colour(0xff4a4a5a));
    g.fillRect(gutter.getRight() - (isUnity ? 6 : 4), y, isUnity ? 6 : 4, 1);

    g.setColour(isUnity ? juce::Colour(0xffb0c0d0) : juce::Colour(0xff70707e));
    g.drawText(db <= GainUtils::kMinDb ? juce::String("-inf")
                                       : juce::String((int)db),
               gutter.getX(), y - 5, gutter.getWidth() - 7, 10,
               juce::Justification::centredRight, false);
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

  // The product typeface. Nothing in src/ names a font family, so this single
  // hook decides what the whole surface renders with.
  //
  // It has to live on a LookAndFeel because that is the only seam JUCE offers:
  // a Font that names no typeface resolves through
  // LookAndFeel::getDefaultLookAndFeel(), NOT through the component's own
  // look-and-feel. Hence installProductLookAndFeel() below -- setting this
  // class on the editor alone would leave every font as the platform default.
  juce::Typeface::Ptr getTypefaceForFont(const juce::Font &) override;
};

// Installs the product look (and with it the embedded typeface) as JUCE's
// default. Idempotent; call from the editor constructor in addition to
// setLookAndFeel(). See the comment above for why both are needed.
void installProductLookAndFeel();
