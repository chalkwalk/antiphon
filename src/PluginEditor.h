#pragma once

#include "LocalChannelStrip.h"
#include "NinjamClient.h"
#include "AntiphonLookAndFeel.h"
#include "ChatFormat.h"
#include "MusicalKey.h"
#include "Announcer.h"
#include "Shortcuts.h"
#include "SelectionModel.h"
#include "ShortcutsDialog.h"
#include "StatusReadout.h"
#include "PluginProcessor.h"
#include "RemoteUserStrip.h"
#include <JuceHeader.h>

class ServerBrowserDialog;

class AntiphonEditor : public juce::AudioProcessorEditor,
                       public juce::Timer,
                       public juce::KeyListener,
                       public NinjamClientListener {
public:
  AntiphonEditor(AntiphonAudioProcessor &);
  ~AntiphonEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;
  void mouseExit(const juce::MouseEvent &) override;
  bool keyPressed(const juce::KeyPress &) override;
  // KeyListener: the same handler, reached when focus sits on an ancestor
  // rather than inside the editor. See handleShortcut().
  bool keyPressed(const juce::KeyPress &, juce::Component *) override;
  void parentHierarchyChanged() override;

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
  // The band the 30 Hz tick actually needs to redraw.
  juce::Rectangle<int> headerRepaintArea;
  int lastLayoutKey = -1;

  // How finely the phase bar steps, in divisions of a beat.
  //
  // Named in note values to avoid the ambiguity that "1/16th beat" invites: at
  // 137 BPM a beat is a quarter note, so a sixteenth note is four per beat --
  // 9.1 per second -- not one sixteenth of a beat, which would be 36.5 and
  // worse than the 30 Hz timer it replaces.
  //
  // The bar moves 3.5-4.8 px per tick at 30 Hz, so "repaint only when it moves"
  // saves nothing; it always did move. Saving means stepping it deliberately,
  // as other clients do. Per second, at 100-137 BPM:
  //
  //   1  quarter notes  (once a beat)   1.7-2.3   62.5 px/step
  //   4  sixteenths     (ours)          6.7-9.1   15.6 px/step
  //   8  thirty-seconds               13.3-18.3    7.8 px/step
  //
  // Sixteenths are four times cheaper than the timer and still step finely
  // enough to read as motion rather than as a jump.
  static constexpr int kPhaseStepsPerBeat = 4;
  int lastPhaseStep = -1;
  int channelAreaLocalW = 320; // stored for paint() label alignment
  void relayoutChannelArea();
  void updateToolbarStates();
  void setChatConnectedState(bool connected);

  // Speaks the header, which is otherwise only pixels. First in tab order.
  StatusReadout statusReadout;
  Announcer announcer;
  Selection::Model selectionModel;
  void updateSelectionHighlights();
  void announceCurrentSelection();
  // Returns true when the spoken/painted status actually changed.
  bool updateStatusReadout();
  // Both key routes funnel here, so a shortcut cannot work on one and not the
  // other.
  bool handleShortcut(const juce::KeyPress &);
  juce::Component::SafePointer<juce::Component> keyListenerHost;
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
  juce::Component::SafePointer<juce::Component> focusBeforeDialog;
  void openServerBrowser();
  void closeServerBrowser();

  std::unique_ptr<ShortcutsDialog> shortcutsDialog;
  juce::Component::SafePointer<juce::DialogWindow> shortcutsDialogWindow;
  void openShortcutsDialog();
  void closeShortcutsDialog();

  juce::ToggleButton metronomeToggle;
  juce::Slider metronomeVolumeSlider;
  // Records the jam as a Ninjam archive that antiphon-stems turns into WAV
  // stems. Replaces the Save Tx / Save Rx pair, which were debug dumps of the
  // mix rather than a per-player archive, and which nobody could get stems out
  // of.
  juce::ToggleButton recordToggle;
  juce::ToggleButton testToneToggle;
  juce::TextButton syncButton;
  // Standalone only. The plugin locks to the host transport; the standalone had
  // none, so it gets its own. Sync stays DAW-only for a reason worth stating:
  // in the plugin you do not control the transport from here so you arm and
  // wait for the edge, while here pressing play IS the edge.
  juce::TextButton transportButton;
  // Offline practice: your own audio, delayed, as virtual players. Offline-only,
  // so it is disabled while connected rather than quietly doing nothing.
  juce::ToggleButton practiceToggle;
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
  // Shown only in the standalone, where the whole group above is inert: buses
  // exist so a DAW can route tracks in and record stems out, and there is no
  // DAW. Greying alone does not say *why*, so this says it in words.
  juce::Label dawOnlyNote;

  // Applies the standalone/hosted divergence. Called once at construction:
  // which controls can ever apply is fixed for the life of the instance.
  void applyHostContextToControls();
  void refreshTransportButton();

  // Local Channel Strips
  juce::OwnedArray<LocalChannelStrip> localChannelStrips;
  juce::Viewport localChannelsViewport;
  juce::Component localChannelsContainer;

  // Chat UI Controls
  //
  // The room list grows to fit its names, up to a cap: the chat below it is
  // the more valuable use of the space, and a room of twenty is a scroll
  // problem rather than a layout one.
  static constexpr int kRoomMemberLineHeight = 18;
  static constexpr int kMaxRoomMemberLines = 3;
  juce::Label roomMembersLabel;
  juce::TextEditor chatDisplay;

  // The chip: one row between the chat and its input, offering an action you
  // can take about the tempo. Never acts on its own -- a vote is always a
  // click, never a side effect of changing your DAW tempo.
  juce::Label chipLabel;
  juce::TextButton chipActionButton;
  juce::TextButton chipDismissButton;

  juce::TextEditor chatInput;

  static juce::Colour colourForChatCategory(ChatFormat::Category c);
  void updateRoomMembers();
  void updateTempoChip();
  void setChipVisible(bool shouldShow);

  // The server vote currently on offer, and the DAW tempo currently worth
  // proposing. Dismissal is remembered per value, so saying no to one proposal
  // does not silence the next, different one.
  ChatFormat::VoteState pendingVote;

  // The key the room is playing in, as last announced by anyone. Display only:
  // Ninjam has no field for it, so it rides on chat (see MusicalKey.h).
  MusicalKey::Key sessionKey;
  int dismissedVoteTarget = 0;
  bool dismissedVoteIsBpm = true;
  int dismissedDawBpm = 0;
  int chipDawBpm = 0; // non-zero when the chip is offering a DAW tempo
  juce::String lastRoomMembersText;
  // How many lines the room list currently needs, so resized() can give it the
  // height rather than cramming every player into one.
  int roomMembersLines = 1;

  // Remote Mixer Controls
  juce::Viewport remoteUsersViewport;
  juce::Component remoteUsersContainer;
  juce::OwnedArray<RemoteUserStrip> remoteUserStrips;
  // The echo strip lives in the same container as the remote players, because
  // that is exactly what it is: a player, that happens to be you, late.
  std::unique_ptr<RemoteUserStrip> echoStrip;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiphonEditor)
};
