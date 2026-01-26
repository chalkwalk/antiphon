#include "NinjamLookAndFeel.h"

#include <AntiphonFonts.h>

namespace {

// Parsed once and kept. createSystemTypefaceFor reads the whole font file, and
// getTypefaceForFont is called for every Font the surface builds -- which, on a
// paint path that constructs fonts inline, is many per frame.
juce::Typeface::Ptr interRegular() {
  static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
      AntiphonFonts::InterRegular_ttf, AntiphonFonts::InterRegular_ttfSize);
  return t;
}

// The real Bold cut, not a synthesised one: resolving bold to the regular face
// would silently render every bold label as regular.
juce::Typeface::Ptr interBold() {
  static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
      AntiphonFonts::InterBold_ttf, AntiphonFonts::InterBold_ttfSize);
  return t;
}

} // namespace

juce::Typeface::Ptr
NinjamLookAndFeel::getTypefaceForFont(const juce::Font &f) {
  // Substitute only for the unnamed default sans, so that a future request for
  // a monospace face gets one instead of Inter.
  if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    if (auto t = f.isBold() ? interBold() : interRegular())
      return t;

  return LookAndFeel_V4::getTypefaceForFont(f);
}

void installProductLookAndFeel() {
  // Process lifetime, destroyed at exit rather than leaked: it must outlive
  // every editor, because JUCE reads the default-look pointer long after a
  // plugin window closes. Destruction order is safe either way -- Desktop holds
  // a WeakReference, so the pointer nulls rather than dangles.
  static NinjamLookAndFeel productLnf;

  if (&juce::LookAndFeel::getDefaultLookAndFeel() != &productLnf)
    juce::LookAndFeel::setDefaultLookAndFeel(&productLnf);
}

NinjamLookAndFeel::NinjamLookAndFeel() {
  setColour(juce::ResizableWindow::backgroundColourId,
            juce::Colour(0xff1a1a2e)); // Dark background
  setColour(juce::TextButton::buttonColourId,
            juce::Colour(0xff16213e)); // Button surface
  setColour(juce::TextButton::buttonOnColourId,
            juce::Colour(0xff0f3460)); // Active button
  setColour(juce::TextButton::textColourOffId, juce::Colours::lightgrey);
  setColour(juce::TextButton::textColourOnId, juce::Colours::white);
  setColour(juce::Slider::thumbColourId,
            juce::Colour(0xff00b4d8)); // Teal Accent
  setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00b4d8));
  setColour(juce::Slider::rotarySliderOutlineColourId,
            juce::Colour(0xff111111));
  setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0f1524));
  setColour(juce::TextEditor::textColourId, juce::Colour(0xffe0e0e0));
  setColour(juce::TextEditor::highlightColourId,
            juce::Colour(0xff00b4d8).withAlpha(0.3f));
  setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff333344));
  setColour(juce::Label::textColourId, juce::Colour(0xffe0e0e0));
}

void NinjamLookAndFeel::drawRotarySlider(juce::Graphics &g, int x, int y,
                                         int width, int height, float sliderPos,
                                         const float rotaryStartAngle,
                                         const float rotaryEndAngle,
                                         juce::Slider &slider) {
  auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
  auto centreX = (float)x + (float)width * 0.5f;
  auto centreY = (float)y + (float)height * 0.5f;
  auto rx = centreX - radius;
  auto ry = centreY - radius;
  auto rw = radius * 2.0f;
  auto angle =
      rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

  // Outline
  g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
  g.drawEllipse(rx, ry, rw, rw, 2.0f);

  // Fill
  juce::Path p;
  auto pointerLength = radius * 0.33f;
  auto pointerThickness = 2.0f;
  p.addRectangle(-pointerThickness * 0.5f, -radius, pointerThickness,
                 pointerLength);
  p.applyTransform(
      juce::AffineTransform::rotation(angle).translated(centreX, centreY));
  g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
  g.fillPath(p);
}

void NinjamLookAndFeel::drawButtonBackground(
    juce::Graphics &g, juce::Button &button,
    const juce::Colour &backgroundColour, bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown) {
  auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
  auto baseColour =
      backgroundColour
          .withMultipliedSaturation(button.hasKeyboardFocus(true) ? 1.3f : 0.9f)
          .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f);

  if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
    baseColour = baseColour.contrasting(shouldDrawButtonAsDown ? 0.2f : 0.1f);

  g.setColour(baseColour);
  g.fillRoundedRectangle(bounds, 4.0f);

  g.setColour(button.findColour(juce::ComboBox::outlineColourId));
  g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
}

void NinjamLookAndFeel::drawToggleButton(juce::Graphics &g,
                                         juce::ToggleButton &button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown) {
  auto fontSize = juce::jmin(15.0f, (float)button.getHeight() * 0.75f);
  auto tickWidth = fontSize * 1.1f;

  auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);

  if (button.getButtonText().isNotEmpty()) {
    // Treat it somewhat like a text button if it has text (e.g., Mute/Solo)
    juce::Colour baseColour =
        button.findColour(juce::TextButton::buttonColourId);
    if (button.getToggleState()) {
      baseColour = button.findColour(juce::TextButton::buttonOnColourId);
    }

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
      baseColour = baseColour.contrasting(shouldDrawButtonAsDown ? 0.2f : 0.1f);

    g.setColour(baseColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    auto textColour = button.getToggleState()
        ? button.findColour(juce::TextButton::textColourOnId)
        : button.findColour(juce::TextButton::textColourOffId);
    g.setColour(textColour);
    g.setFont(fontSize);
    g.drawFittedText(button.getButtonText(), button.getLocalBounds(),
                     juce::Justification::centred, 1);
  } else {
    // Normal checkbox style
    auto tickBounds = juce::Rectangle<float>(
        4.0f, ((float)button.getHeight() - tickWidth) * 0.5f, tickWidth,
        tickWidth);
    g.setColour(button.findColour(juce::ToggleButton::tickColourId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle(tickBounds, 3.0f, 1.0f);

    if (button.getToggleState()) {
      g.setColour(juce::Colour(0xff00b4d8)); // Teal tick
      juce::Path tickPath;
      tickPath.startNewSubPath(tickBounds.getX() + 3.0f,
                               tickBounds.getCentreY());
      tickPath.lineTo(tickBounds.getCentreX(), tickBounds.getBottom() - 3.0f);
      tickPath.lineTo(tickBounds.getRight() - 2.0f, tickBounds.getY() + 3.0f);
      g.strokePath(tickPath, juce::PathStrokeType(2.5f));
    }
  }
}

void NinjamLookAndFeel::drawTextEditorOutline(juce::Graphics &g, int width,
                                              int height,
                                              juce::TextEditor &textEditor) {
  g.setColour(textEditor.findColour(juce::TextEditor::outlineColourId));
  g.drawRoundedRectangle(0.5f, 0.5f, width - 1.0f, height - 1.0f, 3.0f, 1.0f);
}
