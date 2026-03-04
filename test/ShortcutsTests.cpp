#include <JuceHeader.h>

#include "Shortcuts.h"

namespace {

using A = Shortcuts::Action;

class ShortcutsTests : public juce::UnitTest {
public:
  ShortcutsTests() : juce::UnitTest("Shortcuts", "Shortcuts") {}

  void runTest() override {
    beginTest("the accessibility shortcuts resolve");
    {
      expect(Shortcuts::match('A', true, true) == A::WriteAudit);
      expect(Shortcuts::match('C', true, true) == A::FocusChat);
      expect(Shortcuts::match('S', true, true) == A::ArmSync);
    }

    beginTest("X11 reports letter key codes in lower case");
    {
      // The bug this module exists for. On Linux JUCE derives the key code from
      // xkbKeycodeToKeysym with the shift index unset, so Ctrl+Alt+A arrives as
      // 0x61 'a', not 0x41 'A'. Matching only upper case silently did nothing.
      expect(Shortcuts::match('a', true, true) == A::WriteAudit,
             "lower case must resolve identically");
      expect(Shortcuts::match('c', true, true) == A::FocusChat);
      expect(Shortcuts::match('s', true, true) == A::ArmSync);
    }

    beginTest("a control character is never the key code");
    {
      // With Ctrl held, XLookupString yields a control character and JUCE
      // passes it as the *text* character. Matching on that is what broke the
      // shortcuts: 'A' with Ctrl produces 0x01, which is not a letter at all.
      for (int ctrlChar = 1; ctrlChar <= 26; ++ctrlChar)
        expect(Shortcuts::match(ctrlChar, true, true) == A::None,
               "a control character must not resolve to an action");
    }

    beginTest("both modifiers are required");
    {
      expect(Shortcuts::match('A', false, false) == A::None, "bare A types text");
      expect(Shortcuts::match('A', true, false) == A::None,
             "Ctrl+A alone is select-all in a text field");
      expect(Shortcuts::match('A', false, true) == A::None,
             "Alt+A alone may be a menu mnemonic");
    }

    beginTest("unclaimed keys are left for everyone else");
    {
      // The editor must return false for these so the host still sees them.
      expect(Shortcuts::match('Z', true, true) == A::None);
      expect(Shortcuts::match('1', true, true) == A::None);
      // Raw codes: juce::KeyPress is in juce_gui_basics, which this target
      // deliberately does not link so the suite stays headless.
      expect(Shortcuts::match(0x20, true, true) == A::None,
             "space is transport in every DAW; never claim it");
      expect(Shortcuts::match(0x1b, true, true) == A::None, "escape");
    }
  }
};

static ShortcutsTests shortcutsTests;

} // namespace
