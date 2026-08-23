#pragma once
#include "GainUtils.h"
#include <JuceHeader.h>

// The window's shared colour vocabulary.
//
// Two states in here are load-bearing and were previously indistinguishable:
//
//  - ON vs OFF. A toggle used to shift between 0xff16213e and 0xff0f3460, two
//    dark navies a couple of percent apart. You could not tell whether the
//    metronome was running without listening for it. On is now the product
//    accent, lit, with dark text on it -- the audio-software convention for an
//    engaged control.
//  - ENABLED vs DISABLED. Disabled used to be the same fill at half alpha,
//    which on a dark ground is a barely perceptible change. It is now one flat,
//    low-contrast treatment used by every control AND by the chat panel, so
//    "you cannot use this yet" always looks the same wherever it appears.
namespace AntiphonTheme {

// TWO PALETTES, one shape.
//
// The practice room is a real room and everything in it behaves like one, so
// nothing about the layout changes -- but you should never be in any doubt
// which room you are in. A whole-surface recolour is what makes that obvious
// at a glance, where a tinted header alone was not.
//
// Violet rather than the coral this nearly became. Every warm hue in this app
// is already load-bearing -- amber for a tempo mismatch, orange, red for an
// error -- so a warm theme would read as a room in trouble. Violet carries no
// existing meaning here, sits 75 degrees off the teal, and clears WCAG AAA
// against the window at 9:1.
//
// It does NOT survive red-green colour blindness: violet and teal converge,
// separated only by lightness. That is why the header says "Practice room" in
// words and StatusReadout says it too. The theme is the fast signal, never the
// only one (PRINCIPLES 11).
struct Palette {
  juce::uint32 accent;      // the product accent
  juce::uint32 onText;      // near-black, for use ON the accent
  juce::uint32 control;     // a control at rest
  juce::uint32 window;      // the window behind everything
  juce::uint32 panel;       // a raised panel
  juce::uint32 headerBg;    // the header strip when connected
  juce::uint32 editorBg;    // a text field
  juce::uint32 sliderBg;    // a slider's groove
  juce::uint32 sliderTrack; // and its filled part
};

inline constexpr Palette kJamPalette{0xff00b4d8, 0xff05202b, 0xff16213e,
                                     0xff1a1a2e, 0xff0f3460, 0xff0d0d1a,
                                     0xff0f1524, 0xff11162a, 0xff2a3550};

inline constexpr Palette kPracticePalette{0xffc9a2ff, 0xff1a0f2b, 0xff2a2145,
                                          0xff211a33, 0xff3a2a63, 0xff141026,
                                          0xff17102a, 0xff1a1230, 0xff3d3159};

// Which one is live. Message thread only: it is read while painting and
// written when a room is joined or left.
const Palette &current();
void setPractice(bool on);
bool isPractice();

constexpr juce::uint32 kAccent = 0xff00b4d8;  // teal: the product accent
constexpr juce::uint32 kOnText = 0xff05202b;  // near-black, for use on kAccent
constexpr juce::uint32 kControl = 0xff16213e; // a control at rest

// One disabled treatment, shared by buttons, text editors and the chat.
//
// Deliberately neutral grey rather than a darker shade of the window's navy.
// Desaturation is what reads as "inactive"; a navy-tinted fill still looked
// like a live part of the interface, just dimmer.
constexpr juce::uint32 kDisabledFill = 0xff26282e;
constexpr juce::uint32 kDisabledEdge = 0xff3a3d45;
// Deliberately legible rather than merely dark. A disabled control still has to
// be readable -- you need to know what it is you cannot use, and the previous
// 0xff1e1e1e on 0xff121212 was invisible rather than dimmed.
constexpr juce::uint32 kDisabledText = 0xff8b8f99;

} // namespace AntiphonTheme

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

class AntiphonLookAndFeel : public juce::LookAndFeel_V4 {
public:
  // Re-read the live palette. Called when a practice room is joined or left;
  // JUCE caches colour ids, so switching theme means setting them again.
  void applyPalette();

public:
  AntiphonLookAndFeel();

  void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, const float rotaryStartAngle,
                        const float rotaryEndAngle,
                        juce::Slider &slider) override;

  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawButtonText(juce::Graphics &g, juce::TextButton &button,
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
  // One place that knows what a control surface looks like in each state, so
  // buttons and toggles cannot drift apart again.
  static void paintControlSurface(juce::Graphics &g, juce::Button &button,
                                  juce::Colour fill, bool isOn,
                                  bool highlighted, bool down);

  // class on the editor alone would leave every font as the platform default.
  juce::Typeface::Ptr getTypefaceForFont(const juce::Font &) override;
};

// Installs the product look (and with it the embedded typeface) as JUCE's
// default. Idempotent; call from the editor constructor in addition to
// setLookAndFeel(). See the comment above for why both are needed.
void installProductLookAndFeel();
