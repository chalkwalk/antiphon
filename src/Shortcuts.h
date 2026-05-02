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

enum class Action {
  None,
  FocusChat,          // Ctrl+Alt+C
  ArmSync,            // Ctrl+Alt+S
  WriteAudit,         // Ctrl+Alt+A
  // Ctrl+Alt+Shift+T. The keyboard equivalent of holding the TX button:
  // toggles transmit and applies it to the whole interval so far. A gesture
  // available only to the mouse would be unreachable for a screen-reader user
  // and for anyone with a motor impairment (PRINCIPLES 11).
  RetroactiveTransmit
};

inline Action match(int keyCode, bool ctrlDown, bool altDown,
                    bool shiftDown = false) {
  if (!ctrlDown || !altDown) return Action::None;

  // Fold case rather than trusting the platform. Guarded so a control
  // character or a non-letter key code can never land on a letter.
  const int c = (keyCode >= 'a' && keyCode <= 'z') ? keyCode - ('a' - 'A')
                                                   : keyCode;

  switch (c) {
  case 'T':
    // Shift is what separates it from a plain toggle, and there is no
    // unshifted Ctrl+Alt+T to collide with.
    return shiftDown ? Action::RetroactiveTransmit : Action::None;
  case 'A':
    return Action::WriteAudit;
  case 'C':
    return Action::FocusChat;
  case 'S':
    return Action::ArmSync;
  default:
    return Action::None;
  }
}

} // namespace Shortcuts
