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

ShortcutsDialog::~ShortcutsDialog() {}

void ShortcutsDialog::populateShortcuts() {
#if JUCE_MAC
  const juce::String mod = "Cmd+Option+";
#else
  const juce::String mod = "Ctrl+Alt+";
#endif

  shortcuts.clear();
  shortcuts.add({mod + "S", "Arm Sync (then start DAW transport)"});
  shortcuts.add({mod + "C", "Focus Chat message box"});
  shortcuts.add({mod + "O", "Open Connect / Server Browser dialog"});
  shortcuts.add({mod + "P", "Start a practice room and join it"});
  shortcuts.add({mod + "M", "Toggle Mute All local channels"});
  shortcuts.add({mod + "H or ?", "Open Keyboard Shortcuts Help dialog"});
  shortcuts.add({mod + "A", "Write Accessibility Audit report to desktop"});
  shortcuts.add(
      {mod + "Shift+T", "Toggle Transmit retroactively for current interval"});
  shortcuts.add({mod + "L", "Focus Local Channels section"});
  shortcuts.add({mod + "R", "Focus Remote Users section"});
  shortcuts.add({"Alt+Right / " + mod + "Right", "Select Next Local Channel"});
  shortcuts.add(
      {"Alt+Left / " + mod + "Left", "Select Previous Local Channel"});
  shortcuts.add({"Alt+Down / " + mod + "Down", "Select Next Remote Player"});
  shortcuts.add({"Alt+Up / " + mod + "Up", "Select Previous Remote Player"});
  shortcuts.add({mod + "1..9", "Jump directly to Local Channel 1..9"});
  shortcuts.add({mod + "Shift+1..9", "Jump directly to Remote Player 1..9"});
  shortcuts.add({"M (channel active)", "Toggle Mute for selected channel"});
  shortcuts.add({"S (channel active)", "Toggle Solo for selected channel"});
  shortcuts.add(
      {"X (local active)", "Toggle Transmit for selected local channel"});
  shortcuts.add(
      {"Up / Down / + / -", "Nudge volume for selected channel (+/- 1 dB)"});
  shortcuts.add({"Left / Right", "Adjust panning for selected channel"});
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
