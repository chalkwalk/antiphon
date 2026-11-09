#include "ShortcutsDialog.h"
#include "AntiphonLookAndFeel.h"

ShortcutsDialog::ShortcutsDialog() : table("Shortcuts", this) {
  setWantsKeyboardFocus(true);
  setTitle("Keyboard Shortcuts");
  setDescription("Reference list of keyboard shortcuts in Antiphon");
  setFocusContainerType(juce::Component::FocusContainerType::focusContainer);
  setInterceptsMouseClicks(true, true);

  closeButton.onClick = [this]() { dismiss(); };
  closeButton.setTitle("Close");
  closeButton.setDescription("Close this dialog");
  addAndMakeVisible(closeButton);

  okButton.setButtonText("OK");
  okButton.setTitle("Dismiss Keyboard Shortcuts dialog");
  okButton.setDescription("Dismiss the keyboard shortcuts help dialog");
  okButton.onClick = [this]() { dismiss(); };
  addAndMakeVisible(okButton);

  table.getHeader().addColumn("Shortcut", 1, 180);
  table.getHeader().addColumn("Description", 2, 380);
  table.setHeaderHeight(28);
  table.setRowHeight(24);
  table.setColour(juce::TableListBox::backgroundColourId,
                  juce::Colour(0xff111122));
  table.setOutlineThickness(1);
  table.setTitle("Shortcut list");
  table.setDescription("Keyboard shortcut key bindings and their actions");
  addAndMakeVisible(table);

  populateShortcuts();
  setSize(600, 460);
}

ShortcutsDialog::~ShortcutsDialog() = default;

void ShortcutsDialog::populateShortcuts() {
#if JUCE_MAC
  const juce::String mod = "Cmd+Option+";
#else
  const juce::String mod = "Ctrl+Alt+";
#endif

  shortcuts.clear();
  const auto add = [this](const juce::String &keys, const juce::String &what) {
    shortcuts.add({.shortcut = keys, .description = what});
  };
  add(mod + "S", "Sync now: does whatever syncing needs next");
  add(mod + "I", "Say status: connection, tempo and sync");
  add(mod + "F", "Clock: synced to DAW, or Antiphon's clock");
  add(mod + "G", "Play or stop Antiphon's clock");
  add(mod + "C", "Focus Chat message box");
  add(mod + "O", "Open Connect / Server Browser dialog");
  add(mod + "P", "Start a practice room and join it");
  add(mod + "M", "Toggle Mute All local channels");
  add(mod + "H or ?", "Open Keyboard Shortcuts Help dialog");
  add(mod + "V", "Cycle spoken announcements: all, important, off");
  add(mod + "A", "Write Accessibility Audit report to desktop");
  add(mod + "Shift+T", "Toggle Transmit retroactively for current interval");
  add(mod + "L", "Focus Local Channels section");
  add(mod + "R", "Focus Remote Users section");
  add("Alt+Right / " + mod + "Right", "Select Next Local Channel");
  add("Alt+Left / " + mod + "Left", "Select Previous Local Channel");
  add("Alt+Down / " + mod + "Down", "Select Next Remote Player");
  add("Alt+Up / " + mod + "Up", "Select Previous Remote Player");
  add(mod + "1..9", "Jump directly to Local Channel 1..9");
  add(mod + "Shift+1..9", "Jump directly to Remote Player 1..9");
  add("M (channel active)", "Toggle Mute for selected channel");
  add("S (channel active)", "Toggle Solo for selected channel");
  add("X (local active)", "Toggle Transmit for selected local channel");
  add("Up / Down / + / -", "Nudge volume for selected channel (+/- 1 dB)");
  add("Left / Right", "Adjust panning for selected channel");
  table.updateContent();
}

void ShortcutsDialog::dismiss() {
  if (onClose)
    onClose();
}

void ShortcutsDialog::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff12121e));

  // Header / Title bar
  auto titleArea = getLocalBounds().removeFromTop(kTitleBarH);
  g.setColour(juce::Colour(0xff1e1e2e));
  g.fillRect(titleArea);

  g.setFont(juce::FontOptions{}.withHeight(14.0f).withStyle("Bold"));
  g.setColour(juce::Colours::white);
  g.drawText("Keyboard Shortcuts", titleArea.reduced(10, 0),
             juce::Justification::centredLeft);

  // Outer border
  g.setColour(juce::Colour(0xff2a2a3e));
  g.drawRect(getLocalBounds(), 1);
}

void ShortcutsDialog::resized() {
  auto area = getLocalBounds();
  area.removeFromTop(kTitleBarH);
  area.reduce(12, 12);

  auto bottomRow = area.removeFromBottom(28);
  okButton.setBounds(bottomRow.removeFromRight(80));

  table.setBounds(area);

  // Position close button in top title bar
  closeButton.setBounds(getWidth() - 26, 4, 22, 22);
}

int ShortcutsDialog::getNumRows() { return shortcuts.size(); }

void ShortcutsDialog::paintRowBackground(juce::Graphics &g, int rowNumber,
                                         int /*width*/, int /*height*/,
                                         bool rowIsSelected) {
  if (rowIsSelected)
    g.fillAll(juce::Colour(0xff2a2a44));
  else if (rowNumber % 2 == 1)
    g.fillAll(juce::Colour(0xff161628));
  else
    g.fillAll(juce::Colour(0xff111122));
}

void ShortcutsDialog::paintCell(juce::Graphics & /*g*/, int /*row*/,
                                int /*col*/, int /*w*/, int /*h*/,
                                bool /*selected*/) {
  // Cell text and accessibility attributes are rendered by cell label components
  // in refreshComponentForCell.
}

juce::Component *ShortcutsDialog::refreshComponentForCell(
    int rowNumber, int columnId, bool /*isRowSelected*/,
    juce::Component *existingComponentToUpdate) {
  auto *label = dynamic_cast<juce::Label *>(existingComponentToUpdate);
  if (label == nullptr) {
    delete existingComponentToUpdate;
    label = new juce::Label();
    label->setFont(juce::FontOptions{}.withHeight(12.0f));
    label->setInterceptsMouseClicks(false, false);
  }

  if (rowNumber < 0 || rowNumber >= shortcuts.size()) {
    label->setText("", juce::dontSendNotification);
    label->setTitle("");
    label->setDescription("");
    return label;
  }

  const auto &item = shortcuts.getReference(rowNumber);
  if (columnId == 1) {
    label->setColour(juce::Label::textColourId,
                     juce::Colour(AntiphonTheme::kAccent));
    label->setText(item.shortcut, juce::dontSendNotification);
    label->setTitle("");
    label->setDescription("");
  } else if (columnId == 2) {
    label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    label->setText(item.description, juce::dontSendNotification);
    label->setTitle("");
    label->setDescription("");
  }

  return label;
}
