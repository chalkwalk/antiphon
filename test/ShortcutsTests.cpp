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
      expect(Shortcuts::match('O', true, true) == A::OpenConnect);
      expect(Shortcuts::match('M', true, true) == A::ToggleMuteAll);
      expect(Shortcuts::match('H', true, true) == A::AnnounceShortcutsHelp);
      expect(Shortcuts::match('L', true, true) == A::FocusLocalSection);
      expect(Shortcuts::match('R', true, true) == A::FocusRemoteSection);
    }

    beginTest("X11 reports letter key codes in lower case");
    {
      expect(Shortcuts::match('a', true, true) == A::WriteAudit,
             "lower case must resolve identically");
      expect(Shortcuts::match('c', true, true) == A::FocusChat);
      expect(Shortcuts::match('s', true, true) == A::ArmSync);
      expect(Shortcuts::match('o', true, true) == A::OpenConnect);
      expect(Shortcuts::match('m', true, true) == A::ToggleMuteAll);
    }

    beginTest("a control character is never the key code");
    {
      for (int ctrlChar = 1; ctrlChar <= 26; ++ctrlChar)
        expect(Shortcuts::match(ctrlChar, true, true) == A::None,
               "a control character must not resolve to an action");
    }

    beginTest("both modifiers are required for global chords");
    {
      expect(Shortcuts::match('A', false, false) == A::None,
             "bare A types text");
      expect(Shortcuts::match('A', true, false) == A::None,
             "Ctrl+A alone is select-all in a text field");
      expect(Shortcuts::match('A', false, true) == A::None,
             "Alt+A alone may be a menu mnemonic");
    }

    beginTest("unclaimed keys are left for everyone else");
    {
      expect(Shortcuts::match('Z', true, true) == A::None);
      expect(Shortcuts::match(0x20, true, true) == A::None,
             "space is transport in every DAW; never claim it");
      expect(Shortcuts::match(0x1b, true, true) == A::None, "escape");
    }

    beginTest("the retroactive transmit chord needs shift, and only shift");
    {
      expect(Shortcuts::match('T', true, true, true) ==
             Shortcuts::Action::RetroactiveTransmit);
      expect(Shortcuts::match('t', true, true, true) ==
             Shortcuts::Action::RetroactiveTransmit);
      expect(Shortcuts::match('T', true, true, false) ==
             Shortcuts::Action::None);
    }

    beginTest("direct channel jump shortcuts with Ctrl+Alt(+Shift)");
    {
      expect(Shortcuts::match('1', true, true, false) ==
             A::SelectLocalChannel1);
      expect(Shortcuts::match('5', true, true, false) ==
             A::SelectLocalChannel5);
      expect(Shortcuts::match('9', true, true, false) ==
             A::SelectLocalChannel9);

      expect(Shortcuts::match('1', true, true, true) == A::SelectRemoteUser1);
      expect(Shortcuts::match('5', true, true, true) == A::SelectRemoteUser5);
      expect(Shortcuts::match('9', true, true, true) == A::SelectRemoteUser9);
    }

    beginTest("navigation shortcuts resolve with Alt or Ctrl+Alt");
    {
      expect(Shortcuts::match(0, false, true, false, false, false, true) ==
                 A::SelectNextLocalChannel,
             "Alt+Right");
      expect(Shortcuts::match(0, false, true, false, false, true, false) ==
                 A::SelectPrevLocalChannel,
             "Alt+Left");
      expect(Shortcuts::match(0, false, true, false, false, false, false, false,
                              true) == A::SelectNextRemoteUser,
             "Alt+Down");
      expect(Shortcuts::match(0, false, true, false, false, false, false, true,
                              false) == A::SelectPrevRemoteUser,
             "Alt+Up");

      expect(Shortcuts::match(0, true, true, false, false, false, true) ==
                 A::SelectNextLocalChannel,
             "Ctrl+Alt+Right");
    }

    beginTest("macOS Cmd+Option shortcuts resolve identically to Ctrl+Alt");
    {
      expect(Shortcuts::match('C', false, true, false, false, false, false,
                              false, false, true) == A::FocusChat);
      expect(Shortcuts::match('S', false, true, false, false, false, false,
                              false, false, true) == A::ArmSync);
      expect(Shortcuts::match('O', false, true, false, false, false, false,
                              false, false, true) == A::OpenConnect);
      expect(Shortcuts::match('M', false, true, false, false, false, false,
                              false, false, true) == A::ToggleMuteAll);
      expect(Shortcuts::match('1', false, true, false, false, false, false,
                              false, false, true) == A::SelectLocalChannel1);
      expect(Shortcuts::match('1', false, true, true, false, false, false,
                              false, false, true) == A::SelectRemoteUser1);
    }

    beginTest(
        "contextual single-key shortcuts when text editor is NOT focused");
    {
      expect(Shortcuts::match('M', false, false, false, false) ==
             A::ToggleMute);
      expect(Shortcuts::match('m', false, false, false, false) ==
             A::ToggleMute);
      expect(Shortcuts::match('S', false, false, false, false) ==
             A::ToggleSolo);
      expect(Shortcuts::match('X', false, false, false, false) ==
             A::ToggleTransmit);
      expect(Shortcuts::match('+', false, false, false, false) ==
             A::NudgeVolumeUp);
      expect(Shortcuts::match('-', false, false, false, false) ==
             A::NudgeVolumeDown);
      expect(Shortcuts::match('?', false, false, false, false) ==
             A::AnnounceShortcutsHelp);
    }

    beginTest("contextual single-key shortcuts are strictly suppressed when "
              "text editor IS focused");
    {
      expect(Shortcuts::match('M', false, false, false, true) == A::None,
             "M in chat box types M");
      expect(Shortcuts::match('S', false, false, false, true) == A::None,
             "S in chat box types S");
      expect(Shortcuts::match('X', false, false, false, true) == A::None,
             "X in chat box types X");
      expect(Shortcuts::match('+', false, false, false, true) == A::None,
             "+ in chat box types +");
      expect(Shortcuts::match('-', false, false, false, true) == A::None,
             "- in chat box types -");
      expect(Shortcuts::match('?', false, false, false, true) == A::None,
             "? in chat box types ?");
      expect(Shortcuts::match(0, false, false, false, true, false, true) ==
                 A::None,
             "Right arrow in text box moves caret");
    }
  }
};

static ShortcutsTests shortcutsTests;

} // namespace
