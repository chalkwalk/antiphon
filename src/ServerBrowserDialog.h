#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <thread>

class ServerBrowserDialog : public juce::Component,
                            public juce::TableListBoxModel {
public:
  struct ServerEntry {
    juce::String host;
    int port = 2049;
    int bpm = 0;
    int bpi = 0;
    juce::String players;
  };

  // Called when the user clicks Connect (fill and connect) or Cancel/close.
  std::function<void(const juce::String &, int, const juce::String &,
                     const juce::String &)>
      onConnect;

  // Join the practice room instead of a server on the list.
  //
  // A destination rather than a mode, which is why it lives HERE and not on
  // the toolbar: joining the band uses the same path as joining anyone else,
  // and everything past it is the ordinary connected UI.
  std::function<void()> onPractice;
  std::function<void()> onClose;

  ServerBrowserDialog();
  ~ServerBrowserDialog() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  // What the dialog is telling you right now. Public so a caller can report a
  // failure it owns -- the practice room refusing to start is the editor's
  // news, not the browser's.
  void setStatus(const juce::String &text);

  int getNumRows() override;
  void paintRowBackground(juce::Graphics &, int, int, int, bool) override;
  void paintCell(juce::Graphics &, int, int, int, int, bool) override;
  void cellClicked(int, int, const juce::MouseEvent &) override;
  void cellDoubleClicked(int, int, const juce::MouseEvent &) override;
  void selectedRowsChanged(int lastRowSelected) override;
  void returnKeyPressed(int lastRowSelected) override;
  juce::Component *
  refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                          juce::Component *existingComponentToUpdate) override;

  // Public so PluginEditor can pre-populate from saved state
  juce::TextEditor hostInput, portInput, usernameInput, passwordInput;
  juce::ToggleButton anonymousToggle{"Anonymous"};

private:
  static constexpr int kTitleBarH = 30;

  juce::TextButton closeButton{"X"};
  juce::TableListBox table;
  juce::TextButton connectButton{"Connect"}, cancelButton{"Cancel"};
  juce::TextButton practiceButton{"Practice room"};
  juce::Label statusLabel;

  juce::Array<ServerEntry> servers;

  std::atomic<bool> stopFetch{false};
  std::thread fetchThread;
  juce::StreamingSocket fetchSocket;

  void populateStaticList();
  juce::Array<ServerEntry> parseServersJSON(const juce::String &json);
  void doFetch();
  void dismiss();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ServerBrowserDialog)
};
