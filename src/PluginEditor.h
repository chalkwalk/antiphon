#pragma once

#include "LocalChannelStrip.h"
#include "NinjamClient.h"
#include "AntiphonLookAndFeel.h"
#include "Announcer.h"
#include "StatusReadout.h"
#include "PluginProcessor.h"
#include "RemoteUserStrip.h"
#include <JuceHeader.h>

class ServerBrowserDialog;

class AntiphonEditor : public juce::AudioProcessorEditor,
                                   public juce::Timer,
                                   public NinjamClientListener {
public:
  AntiphonEditor(AntiphonAudioProcessor &);
  ~AntiphonEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;
  void mouseExit(const juce::MouseEvent &) override;
  bool keyPressed(const juce::KeyPress &) override;

  // NinjamClientListener
  void onConnected() override;
  void onDisconnected(const juce::String &error) override;
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override;

private:
  AntiphonAudioProcessor &audioProcessor;
  AntiphonLookAndFeel customLookAndFeel;
  juce::TooltipWindow tooltipWindow{this, 700};
  int diagTickCounter = 0;

  // Elastic channel-panel layout state
  juce::Rectangle<int> cachedChannelPanelBounds;
  int channelAreaLocalW = 320; // stored for paint() label alignment
  void relayoutChannelArea();
  void updateToolbarStates();
  void setChatConnectedState(bool connected);

  // Speaks the header, which is otherwise only pixels. First in tab order.
  StatusReadout statusReadout;
  Announcer announcer;
  void updateStatusReadout();
  juce::String lastAnnouncedSyncState;
  int lastAnnouncedBpm = 0, lastAnnouncedBpi = 0;
  bool lastAnnouncedConnected = false;

  juce::TextButton browseButton;
  juce::TextButton disconnectButton;

  // A real top-level window, not a child overlay. An embedded plugin view can
  // never hold the X input focus -- JUCE decides focus with
  // isParentWindowOf(ourWindow, focusedWindow), and the host's window is our
  // ancestor, not our descendant -- so juce::Component::takeKeyboardFocus
  // bails out and no text field inside the editor can ever be typed into.
  // A desktop window has its own peer and takes focus normally.
  std::unique_ptr<ServerBrowserDialog> serverBrowser;
  juce::Component::SafePointer<juce::DialogWindow> serverBrowserWindow;
  // JUCE does not return keyboard focus when a modal window closes, which
  // leaves a screen reader user somewhere arbitrary after connecting. We put
  // them back where they were.
  juce::Component::SafePointer<juce::Component> focusBeforeDialog;
  void openServerBrowser();
  void closeServerBrowser();

  juce::ToggleButton metronomeToggle;
  juce::Slider metronomeVolumeSlider;
  juce::ToggleButton saveTxToggle;
  juce::ToggleButton saveRxToggle;
  juce::ToggleButton testToneToggle;
  juce::TextButton syncButton;
  juce::ToggleButton chatToggle;

  // Compact toolbar groups: "Channel: [+]"  "Input bus: [+][-]"  "Output bus: [+][-]"
  juce::Label channelGroupLabel;
  juce::TextButton addChannelButton;
  juce::Label inputBusGroupLabel;
  juce::TextButton addInBusButton;
  juce::TextButton removeInBusButton;
  juce::Label outputBusGroupLabel;
  juce::TextButton addOutBusButton;
  juce::TextButton removeOutBusButton;

  // Local Channel Strips
  juce::OwnedArray<LocalChannelStrip> localChannelStrips;
  juce::Viewport localChannelsViewport;
  juce::Component localChannelsContainer;

  // Chat UI Controls
  juce::TextEditor chatDisplay;
  juce::TextEditor chatInput;

  // Remote Mixer Controls
  juce::Viewport remoteUsersViewport;
  juce::Component remoteUsersContainer;
  juce::OwnedArray<RemoteUserStrip> remoteUserStrips;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiphonEditor)
};
