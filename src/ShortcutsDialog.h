#pragma once
#include <JuceHeader.h>

class ShortcutsDialog : public juce::Component, public juce::TableListBoxModel {
public:
  struct ShortcutItem {
    juce::String shortcut;
    juce::String description;
  };

  std::function<void()> onClose;

  ShortcutsDialog();
  ~ShortcutsDialog() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  int getNumRows() override;
  void paintRowBackground(juce::Graphics &, int, int, int, bool) override;
  void paintCell(juce::Graphics &, int, int, int, int, bool) override;
  juce::Component *
  refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                          juce::Component *existingComponentToUpdate) override;

private:
  static constexpr int kTitleBarH = 30;

  juce::TextButton closeButton{"X"};
  juce::TableListBox table;
  juce::TextButton okButton{"OK"};
  juce::Array<ShortcutItem> shortcuts;

  void populateShortcuts();
  void dismiss();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutsDialog)
};
