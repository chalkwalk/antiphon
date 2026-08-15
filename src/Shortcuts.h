#pragma once

// Which accessibility shortcut a key press means, if any.
//
// Split out of the editor because getting this wrong is silent. The first
// version matched juce::KeyPress::getTextCharacter(), which looks right and
// never fires: with Ctrl held, X11's XLookupString returns a *control
// character*, so Ctrl+Alt+A arrives with a text character of 0x01, not 'A'
// (JUCE/modules/juce_gui_basics/native/juce_XWindowSystem_linux.cpp, in
// handleKeyPressEvent). Nothing happened and nothing said why.
//
// The key *code* is the field to match. JUCE derives it from the keysym with
// the shift index unset, so on Linux a letter arrives lower case; other
// platforms report it upper case. Both must resolve.
//
// Pure and free of JUCE so the mapping can be tested directly, which is the
// only way a shortcut that does nothing gets caught.

namespace Shortcuts {

inline juce::String globalModifierName() {
#if JUCE_MAC
  return "Cmd+Option+";
#else
  return "Ctrl+Alt+";
#endif
}

enum class Action {
  None,
  FocusChat,              // Ctrl+Alt+C
  ArmSync,                // Ctrl+Alt+S
  WriteAudit,             // Ctrl+Alt+A
  RetroactiveTransmit,    // Ctrl+Alt+Shift+T
  OpenConnect,            // Ctrl+Alt+O
  ToggleMuteAll,          // Ctrl+Alt+M
  AnnounceShortcutsHelp,  // Ctrl+Alt+H or '?'
  FocusLocalSection,      // Ctrl+Alt+L
  FocusRemoteSection,     // Ctrl+Alt+R
  SelectNextLocalChannel, // Alt+Right / Ctrl+Alt+Right
  SelectPrevLocalChannel, // Alt+Left / Ctrl+Alt+Left
  SelectNextRemoteUser,   // Alt+Down / Ctrl+Alt+Down
  SelectPrevRemoteUser,   // Alt+Up / Ctrl+Alt+Up
  ToggleMute,             // Contextual 'M'
  ToggleSolo,             // Contextual 'S'
  ToggleTransmit,         // Contextual 'X'
  NudgeVolumeUp,          // Contextual Up Arrow or '+'
  NudgeVolumeDown,        // Contextual Down Arrow or '-'
  NudgePanLeft,           // Contextual Left Arrow
  NudgePanRight,          // Contextual Right Arrow
  SelectLocalChannel1,    // Ctrl+Alt+1
  SelectLocalChannel2,    // Ctrl+Alt+2
  SelectLocalChannel3,    // Ctrl+Alt+3
  SelectLocalChannel4,    // Ctrl+Alt+4
  SelectLocalChannel5,    // Ctrl+Alt+5
  SelectLocalChannel6,    // Ctrl+Alt+6
  SelectLocalChannel7,    // Ctrl+Alt+7
  SelectLocalChannel8,    // Ctrl+Alt+8
  SelectLocalChannel9,    // Ctrl+Alt+9
  SelectRemoteUser1,      // Ctrl+Alt+Shift+1
  SelectRemoteUser2,      // Ctrl+Alt+Shift+2
  SelectRemoteUser3,      // Ctrl+Alt+Shift+3
  SelectRemoteUser4,      // Ctrl+Alt+Shift+4
  SelectRemoteUser5,      // Ctrl+Alt+Shift+5
  SelectRemoteUser6,      // Ctrl+Alt+Shift+6
  SelectRemoteUser7,      // Ctrl+Alt+Shift+7
  SelectRemoteUser8,      // Ctrl+Alt+Shift+8
  SelectRemoteUser9       // Ctrl+Alt+Shift+9
};

inline Action match(int keyCode, bool ctrlDown, bool altDown,
                    bool shiftDown = false, bool isTextEditorFocused = false,
                    bool isLeftKey = false, bool isRightKey = false,
                    bool isUpKey = false, bool isDownKey = false,
                    bool cmdDown = false) {
  const bool primaryMod = (ctrlDown || cmdDown);

  // Direct channel jump with (Cmd/Ctrl)+Alt(+Shift)
  if (primaryMod && altDown && keyCode >= '1' && keyCode <= '9') {
    const int num = keyCode - '0';
    if (shiftDown) {
      switch (num) {
      case 1:
        return Action::SelectRemoteUser1;
      case 2:
        return Action::SelectRemoteUser2;
      case 3:
        return Action::SelectRemoteUser3;
      case 4:
        return Action::SelectRemoteUser4;
      case 5:
        return Action::SelectRemoteUser5;
      case 6:
        return Action::SelectRemoteUser6;
      case 7:
        return Action::SelectRemoteUser7;
      case 8:
        return Action::SelectRemoteUser8;
      case 9:
        return Action::SelectRemoteUser9;
      }
    } else {
      switch (num) {
      case 1:
        return Action::SelectLocalChannel1;
      case 2:
        return Action::SelectLocalChannel2;
      case 3:
        return Action::SelectLocalChannel3;
      case 4:
        return Action::SelectLocalChannel4;
      case 5:
        return Action::SelectLocalChannel5;
      case 6:
        return Action::SelectLocalChannel6;
      case 7:
        return Action::SelectLocalChannel7;
      case 8:
        return Action::SelectLocalChannel8;
      case 9:
        return Action::SelectLocalChannel9;
      }
    }
  }

  // Global Chords: (Cmd/Ctrl)+Alt
  if (primaryMod && altDown) {
    if (isRightKey)
      return Action::SelectNextLocalChannel;
    if (isLeftKey)
      return Action::SelectPrevLocalChannel;
    if (isDownKey)
      return Action::SelectNextRemoteUser;
    if (isUpKey)
      return Action::SelectPrevRemoteUser;

    const int c =
        (keyCode >= 'a' && keyCode <= 'z') ? keyCode - ('a' - 'A') : keyCode;

    switch (c) {
    case 'T':
      return shiftDown ? Action::RetroactiveTransmit : Action::None;
    case 'A':
      return Action::WriteAudit;
    case 'C':
      return Action::FocusChat;
    case 'S':
      return Action::ArmSync;
    case 'O':
      return Action::OpenConnect;
    case 'M':
      return Action::ToggleMuteAll;
    case 'H':
      return Action::AnnounceShortcutsHelp;
    case 'L':
      return Action::FocusLocalSection;
    case 'R':
      return Action::FocusRemoteSection;
    default:
      return Action::None;
    }
  }

  // Alt without Ctrl/Cmd: Navigation
  if (altDown && !primaryMod) {
    if (isRightKey)
      return Action::SelectNextLocalChannel;
    if (isLeftKey)
      return Action::SelectPrevLocalChannel;
    if (isDownKey)
      return Action::SelectNextRemoteUser;
    if (isUpKey)
      return Action::SelectPrevRemoteUser;
  }

  // Single-key / Contextual shortcuts: strictly suppressed when typing in a text field
  if (!isTextEditorFocused && !primaryMod && !altDown) {
    if (isUpKey)
      return Action::NudgeVolumeUp;
    if (isDownKey)
      return Action::NudgeVolumeDown;
    if (isLeftKey)
      return Action::NudgePanLeft;
    if (isRightKey)
      return Action::NudgePanRight;

    const int c =
        (keyCode >= 'a' && keyCode <= 'z') ? keyCode - ('a' - 'A') : keyCode;

    switch (c) {
    case 'M':
      return Action::ToggleMute;
    case 'S':
      return Action::ToggleSolo;
    case 'X':
      return Action::ToggleTransmit;
    case '+':
    case '=':
      return Action::NudgeVolumeUp;
    case '-':
    case '_':
      return Action::NudgeVolumeDown;
    case '?':
      return Action::AnnounceShortcutsHelp;
    default:
      break;
    }
  }

  return Action::None;
}

} // namespace Shortcuts
