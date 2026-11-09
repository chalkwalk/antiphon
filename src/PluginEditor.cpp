#include "GainUtils.h"
#include "PluginEditor.h"

#include "RoomHarmony.h"
#include "PluginProcessor.h"
#include "AccessibilityTree.h"
#include "ServerBrowserDialog.h"

AntiphonEditor::AntiphonEditor(AntiphonAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  audioProcessor.ninjamClient.addListener(this);

  // Restored before anything can speak. The editor is destroyed with the
  // window, so the setting lives on the processor; without this, closing the
  // plugin window silently undoes the one control a reader user has over how
  // much the app talks.
  announcer.setVerbosity((Announcer::Verbosity)juce::jlimit(
      0, 2, audioProcessor.announceVerbosity.load()));

  // The embedded typeface, on JUCE's DEFAULT look-and-feel. It cannot ride on
  // customLookAndFeel below: a Font naming no typeface -- which is all of ours
  // -- resolves through the default look, never the component's.
  installProductLookAndFeel();

  // Added first so keyboard focus lands on the session status before any
  // control: "where am I and is it working" is the first question.
  addAndMakeVisible(statusReadout);

  setLookAndFeel(&customLookAndFeel);

  // Without this the editor never accepts keyboard focus, so a host that only
  // forwards key events to a focused plugin view leaves every text field dead.
  setWantsKeyboardFocus(true);
  // The root the reader lands on before anything inside it. Anonymous until
  // the audit named it.
  setTitle("Antiphon");
  setDescription("Ninjam client. Session status, local channels, remote "
                 "players, chat.");
  setResizable(true, true);
  // 1080 is what the toolbar actually needs: 596 px for the session and DAW
  // groups from the left, 454 for the view and debug toggles from the right,
  // and the 10 px window margins. Below that the two runs met in the middle and
  // controls collapsed to nothing -- the previous 900 minimum was a width the
  // window could be set to but could not draw.
  setResizeLimits(1080, 600, 2400, 1600);
  setSize(1100, 700);

  browseButton.setButtonText("Connect...");
  browseButton.onClick = [this]() { openServerBrowser(); };
  browseButton.setRepaintsOnMouseActivity(true);
  browseButton.setTitle("Connect (" + Shortcuts::globalModifierName() + "O)");
  browseButton.setDescription(
      "Open the server browser to choose a server and connect. Shortcut: " +
      Shortcuts::globalModifierName() + "O");
  addAndMakeVisible(browseButton);

  disconnectButton.setButtonText("Disconnect");
  disconnectButton.onClick = [this]() {
    audioProcessor.ninjamClient.disconnectFromServer();
  };
  disconnectButton.setRepaintsOnMouseActivity(true);
  disconnectButton.setTitle("Disconnect");
  disconnectButton.setDescription("Leave the current server");
  addAndMakeVisible(disconnectButton);

  metronomeToggle.setButtonText("Metronome");
  metronomeToggle.setToggleState(audioProcessor.metronomeEnabled,
                                 juce::dontSendNotification);
  metronomeToggle.onClick = [this]() {
    audioProcessor.metronomeEnabled = metronomeToggle.getToggleState();
  };
  metronomeToggle.setRepaintsOnMouseActivity(true);
  metronomeToggle.setTitle("Metronome");
  metronomeToggle.setDescription("Play a click on every beat of the interval");
  addAndMakeVisible(metronomeToggle);

  recordToggle.setButtonText("Record");
  recordToggle.setToggleState(audioProcessor.ninjamClient.isRecordingSession(),
                              juce::dontSendNotification);
  recordToggle.onClick = [this]() {
    if (recordToggle.getToggleState()) {
      const auto parent = NinjamClient::defaultSessionDirectory();
      parent.createDirectory();
      if (audioProcessor.ninjamClient.startSessionRecording(parent)) {
        const auto dir =
            audioProcessor.ninjamClient.sessionRecordingDirectory();
        statusReadout.setStatus("Recording to " + dir.getFullPathName());
        announcer.say("Recording the session", true);
      } else {
        recordToggle.setToggleState(false, juce::dontSendNotification);
        statusReadout.setStatus("Could not start recording");
        announcer.say("Could not start recording", true);
      }
    } else {
      const auto dir = audioProcessor.ninjamClient.sessionRecordingDirectory();
      const int clips = audioProcessor.ninjamClient.sessionRecordingClipCount();
      audioProcessor.ninjamClient.stopSessionRecording();
      statusReadout.setStatus("Saved " + juce::String(clips) + " clips to " +
                              dir.getFullPathName());
      announcer.say(
          "Recording stopped, " + juce::String(clips) + " clips saved", true);
    }
  };
  recordToggle.setRepaintsOnMouseActivity(true);
  recordToggle.setTitle("Record session");
  recordToggle.setDescription(
      "Save the jam as a Ninjam archive, one file per player per interval, for "
      "converting to WAV stems with antiphon-stems");
  recordToggle.setTooltip(
      "Record the session to " +
      NinjamClient::defaultSessionDirectory().getFullPathName() +
      "\nEvery player is saved separately, exactly as sent, so the jam can be "
      "turned into stems afterwards.");
  addAndMakeVisible(recordToggle);

  testToneToggle.setButtonText("Test Tone");
  testToneToggle.setTooltip(
      "Debug: replace all local inputs with a 440 Hz tone plus short 3 kHz "
      "bursts at 0, 1/4, 1/2 and 3/4 of every interval, so transmit timing "
      "can be measured rather than listened to.");
  testToneToggle.setToggleState(audioProcessor.testToneEnabled,
                                juce::dontSendNotification);
  testToneToggle.onClick = [this]() {
    audioProcessor.testToneEnabled = testToneToggle.getToggleState();
  };
  testToneToggle.setRepaintsOnMouseActivity(true);
  testToneToggle.setTitle("Test tone");
  testToneToggle.setDescription(
      "Debug: replace all local input with a timing probe");
  addAndMakeVisible(testToneToggle);

  // The clock: two radio buttons in a group, then Play. See ClockModel.h.
  clockLabel.setText("Clock:", juce::dontSendNotification);
  clockLabel.setJustificationType(juce::Justification::centredRight);
  clockLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  clockGroup.addAndMakeVisible(clockLabel);
  clockGroup.setTitle("Clock");
  clockGroup.setFocusContainerType(
      juce::Component::FocusContainerType::focusContainer);
  const juce::String mod = Shortcuts::globalModifierName();
  syncedToDawButton.setButtonText("Synced to DAW");
  syncedToDawButton.setTitle("Synced to DAW");
  syncedToDawButton.setTooltip(
      "The jam follows your DAW's playhead: its tempo, its position, its play "
      "and stop. The default, and what almost everyone wants.\n" +
      mod + "S syncs from wherever you are; " + mod + "F switches clocks.");
  antiphonClockButton.setButtonText("Antiphon's clock");
  antiphonClockButton.setTitle("Antiphon's clock");
  antiphonClockButton.setTooltip(
      "The jam runs on Antiphon's own clock and ignores the DAW: for session "
      "view, or a DAW you are not playing from. Play starts and stops it.");
  for (auto *b : {&syncedToDawButton, &antiphonClockButton}) {
    b->setRadioGroupId(0xC10C, juce::dontSendNotification);
    b->setClickingTogglesState(false);
    b->setRepaintsOnMouseActivity(true);
    clockGroup.addAndMakeVisible(*b);
  }
  syncedToDawButton.onClick = [this]() {
    selectClockMode(ClockModel::Mode::SyncedToDaw);
  };
  antiphonClockButton.onClick = [this]() {
    selectClockMode(ClockModel::Mode::AntiphonClock);
  };
  clockGroup.onArrow = [this](int) {
    // Two choices: either arrow goes to the other one, and selects it, which
    // is how a radio group behaves everywhere else.
    const bool synced =
        audioProcessor.clockMode.load() == (int)ClockModel::Mode::SyncedToDaw;
    auto &next = synced ? antiphonClockButton : syncedToDawButton;
    if (!next.isEnabled())
      return;
    next.grabKeyboardFocus();
    selectClockMode(synced ? ClockModel::Mode::AntiphonClock
                           : ClockModel::Mode::SyncedToDaw);
  };
  addChildComponent(clockGroup);

  if (audioProcessor.hostControl != nullptr) {
    followTempoToggle =
        std::make_unique<juce::ToggleButton>("Reaper follows room tempo");
    followTempoToggle->setTitle("Reaper follows room tempo");
    followTempoToggle->setDescription(
        "When the room's tempo changes, Antiphon sets Reaper's to match");
    followTempoToggle->setTooltip(
        "Antiphon sets Reaper's project tempo to the room's when you join and "
        "whenever the room changes it, so you stay synced. Turn it off to keep "
        "your project's tempo; you then hear the room unsynced until it "
        "matches.");
    followTempoToggle->setToggleState(audioProcessor.tempoFollow.enabled,
                                      juce::dontSendNotification);
    followTempoToggle->onClick = [this]() {
      audioProcessor.setFollowRoomTempo(followTempoToggle->getToggleState());
    };
    followTempoToggle->setRepaintsOnMouseActivity(true);
    addChildComponent(*followTempoToggle);

    loopIntervalButton =
        std::make_unique<juce::TextButton>("Loop one interval");
    loopIntervalButton->setTitle("Loop one interval");
    loopIntervalButton->setDescription(
        "Set Reaper's loop to the interval playing now");
    loopIntervalButton->setTooltip(
        "Sets Reaper's loop to exactly one interval, starting at the one "
        "playing now, so a looped project stays on the room's grid.");
    loopIntervalButton->onClick = [this]() { loopOneInterval(); };
    loopIntervalButton->setRepaintsOnMouseActivity(true);
    addChildComponent(*loopIntervalButton);
  }

  transportButton.setButtonText("Play");
  transportButton.setTitle("Play");
  transportButton.setToggleable(true);
  transportButton.onClick = [this]() { toggleAntiphonPlay(); };
  transportButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(transportButton);
  refreshTransportButton();
  refreshClockControls();

  // Compact toolbar groups
  channelGroupLabel.setText("Channel:", juce::dontSendNotification);
  channelGroupLabel.setJustificationType(juce::Justification::centredRight);
  channelGroupLabel.setColour(juce::Label::textColourId,
                              juce::Colours::lightgrey);
  addAndMakeVisible(channelGroupLabel);

  addChannelButton.setButtonText("+");
  addChannelButton.setTooltip(
      "Add a local channel strip (routes to Input Bus 1 by default)");
  addChannelButton.onClick = [this]() { audioProcessor.addLocalChannel(); };
  addChannelButton.setRepaintsOnMouseActivity(true);
  addChannelButton.setTitle("Add channel");
  addChannelButton.setDescription("Add a local channel strip");
  addAndMakeVisible(addChannelButton);

  inputBusGroupLabel.setText("Input bus:", juce::dontSendNotification);
  inputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  inputBusGroupLabel.setColour(juce::Label::textColourId,
                               juce::Colours::lightgrey);
  addAndMakeVisible(inputBusGroupLabel);

  addInBusButton.setButtonText("+");
  addInBusButton.setTooltip(
      "Add a new stereo input bus (for DAW to route another track into)");
  addInBusButton.onClick = [this]() { audioProcessor.addInputBus(); };
  addInBusButton.setRepaintsOnMouseActivity(true);
  addInBusButton.setTitle("Add input bus");
  addInBusButton.setDescription(
      "Add a stereo input bus for the DAW to route a track into");
  addAndMakeVisible(addInBusButton);

  removeInBusButton.setButtonText("-");
  removeInBusButton.setTooltip(
      "Remove the last input bus (channels using it revert to bus 1)");
  removeInBusButton.onClick = [this]() { audioProcessor.removeLastInputBus(); };
  removeInBusButton.setRepaintsOnMouseActivity(true);
  removeInBusButton.setTitle("Remove input bus");
  removeInBusButton.setDescription(
      "Remove the last input bus; channels using it revert to bus 1");
  addAndMakeVisible(removeInBusButton);

  outputBusGroupLabel.setText("Output bus:", juce::dontSendNotification);
  outputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  outputBusGroupLabel.setColour(juce::Label::textColourId,
                                juce::Colours::lightgrey);
  addAndMakeVisible(outputBusGroupLabel);

  dawOnlyNote.setText("DAW only", juce::dontSendNotification);
  dawOnlyNote.setJustificationType(juce::Justification::centredLeft);
  dawOnlyNote.setColour(juce::Label::textColourId,
                        juce::Colour(AntiphonTheme::kDisabledText));
  addChildComponent(dawOnlyNote);

  addOutBusButton.setButtonText("+");
  addOutBusButton.setTooltip(
      "Add a new stereo output bus (for DAW stem recording)");
  addOutBusButton.onClick = [this]() { audioProcessor.addOutputBus(); };
  addOutBusButton.setRepaintsOnMouseActivity(true);
  addOutBusButton.setTitle("Add output bus");
  addOutBusButton.setDescription("Add a stereo output bus for recording stems");
  addAndMakeVisible(addOutBusButton);

  removeOutBusButton.setButtonText("-");
  removeOutBusButton.setTooltip(
      "Remove the last output bus (remote channels using it revert to bus 1)");
  removeOutBusButton.onClick = [this]() {
    audioProcessor.removeLastOutputBus();
  };
  removeOutBusButton.setRepaintsOnMouseActivity(true);
  removeOutBusButton.setTitle("Remove output bus");
  removeOutBusButton.setDescription(
      "Remove the last output bus; channels using it revert to bus 1");
  addAndMakeVisible(removeOutBusButton);

  metronomeToggle.setRepaintsOnMouseActivity(true);

  metronomeVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  metronomeVolumeSlider.setRange(GainUtils::kMinDb, GainUtils::kMaxDb,
                                 GainUtils::kStepDb);
  metronomeVolumeSlider.setValue(
      GainUtils::gainToDb(audioProcessor.metronomeVolume.load()),
      juce::dontSendNotification);
  metronomeVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  metronomeVolumeSlider.textFromValueFunction = [](double v) {
    return v <= GainUtils::kMinDb ? juce::String("minus infinity decibels")
                                  : juce::String(v, 1) + " dB";
  };
  metronomeVolumeSlider.setTooltip("Metronome volume in dB: -inf to +6");
  metronomeVolumeSlider.onValueChange = [this]() {
    audioProcessor.metronomeVolume.store(
        GainUtils::dbToGain(metronomeVolumeSlider.getValue()));
  };
  metronomeVolumeSlider.setTitle("Metronome volume");
  metronomeVolumeSlider.setDescription(
      "Level of the metronome click in decibels");
  addAndMakeVisible(metronomeVolumeSlider);

  cueToggle.setButtonText("DAW-stopped tone");
  cueToggle.setTitle("DAW-stopped tone");
  cueToggle.setDescription(
      "A short high tone once an interval while the DAW is stopped and the "
      "room plays on");
  cueToggle.setTooltip(
      "When the DAW stops under a synced jam, the room keeps playing on "
      "Antiphon's clock. This tone repeats once an interval until you press "
      "play or choose Antiphon's clock, so you cannot miss it. Only you hear "
      "it.");
  cueToggle.setToggleState(audioProcessor.cueEnabled.load(),
                           juce::dontSendNotification);
  cueToggle.onClick = [this]() {
    audioProcessor.cueEnabled.store(cueToggle.getToggleState());
  };
  cueToggle.setRepaintsOnMouseActivity(true);
  addChildComponent(cueToggle);

  cueVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  cueVolumeSlider.setRange(GainUtils::kMinDb, GainUtils::kMaxDb,
                           GainUtils::kStepDb);
  cueVolumeSlider.setValue(GainUtils::gainToDb(audioProcessor.cueVolume.load()),
                           juce::dontSendNotification);
  cueVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  cueVolumeSlider.textFromValueFunction =
      metronomeVolumeSlider.textFromValueFunction;
  cueVolumeSlider.setTooltip("DAW-stopped tone volume in dB: -inf to +6");
  cueVolumeSlider.onValueChange = [this]() {
    audioProcessor.cueVolume.store(
        GainUtils::dbToGain(cueVolumeSlider.getValue()));
  };
  cueVolumeSlider.setTitle("DAW-stopped tone volume");
  cueVolumeSlider.setDescription("Level of the DAW-stopped tone in decibels");
  addChildComponent(cueVolumeSlider);

  chatToggle.setButtonText("Chat");
  chatToggle.setToggleState(audioProcessor.chatVisible.load(),
                            juce::dontSendNotification);
  chatToggle.onClick = [this]() {
    audioProcessor.chatVisible.store(chatToggle.getToggleState());
    resized();
  };
  chatToggle.setRepaintsOnMouseActivity(true);
  chatToggle.setTitle("Chat panel (" + Shortcuts::globalModifierName() + "C)");
  chatToggle.setDescription("Show or hide the chat panel. Shortcut: " +
                            Shortcuts::globalModifierName() + "C");
  addAndMakeVisible(chatToggle);

  localChannelsViewport.setViewedComponent(&localChannelsContainer, false);
  localChannelsViewport.setTitle("Local channels (" +
                                 Shortcuts::globalModifierName() + "L)");
  localChannelsViewport.setDescription(
      "Scrollable list of the channels you transmit. Shortcut: " +
      Shortcuts::globalModifierName() + "L to focus section");
  addAndMakeVisible(localChannelsViewport);

  roomMembersLabel.setText("In room (0)", juce::dontSendNotification);
  roomMembersLabel.setJustificationType(juce::Justification::centredLeft);
  roomMembersLabel.setColour(juce::Label::textColourId,
                             juce::Colour(0xffb0b6c0));
  roomMembersLabel.setTitle("Players in the room");
  roomMembersLabel.setDescription(
      "Everyone on the server, including listeners with no audio channels");
  addAndMakeVisible(roomMembersLabel);

  // The chip. Hidden until there is something to decide.
  chipLabel.setJustificationType(juce::Justification::centredLeft);
  chipLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe0a030));
  addChildComponent(chipLabel);

  chipActionButton.setButtonText("Vote");
  chipActionButton.setTitle("Cast vote");
  chipActionButton.setDescription(
      "Send your vote for this tempo to the server");
  chipActionButton.onClick = [this]() {
    // A vote is only ever sent from here -- never as a side effect of the DAW
    // tempo changing.
    if (pendingVote.valid) {
      audioProcessor.ninjamClient.sendChatMessage(
          juce::String("!vote ") + (pendingVote.isBpm ? "bpm " : "bpi ") +
          juce::String(pendingVote.target));
      dismissedVoteTarget = pendingVote.target;
      dismissedVoteIsBpm = pendingVote.isBpm;
      pendingVote = {};
    } else if (keyFromChords.confident) {
      // Exactly what /key sends, so a suggestion accepted and a key typed are
      // the same message to everyone else in the room.
      audioProcessor.ninjamClient.sendChatMessage(
          MusicalKey::buildTagged(keyFromChords.key));
      dismissedKeyGuess = keyFromChords.key;
    }
    updateTempoChip();
  };
  addChildComponent(chipActionButton);

  chipDismissButton.setButtonText("Dismiss");
  chipDismissButton.setTitle("Dismiss");
  chipDismissButton.setDescription("Hide this prompt until the value changes");
  chipDismissButton.onClick = [this]() {
    if (pendingVote.valid) {
      dismissedVoteTarget = pendingVote.target;
      dismissedVoteIsBpm = pendingVote.isBpm;
      pendingVote = {};
    } else if (keyFromChords.confident) {
      dismissedKeyGuess = keyFromChords.key;
    }
    updateTempoChip();
  };
  addChildComponent(chipDismissButton);

  chatDisplay.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colour(0xff121212));
  addAndMakeVisible(chatDisplay);

  chatInput.setName("chatInput");
  chatInput.setMultiLine(false);
  chatInput.setReturnKeyStartsNewLine(false);
  chatInput.setTextToShowWhenEmpty("Message, or a command: /key Dm, /chords Am "
                                   "F C G, /bpm 120, /msg user text",
                                   juce::Colours::grey);
  chatInput.onReturnKey = [this]() {
    juce::String text = chatInput.getText().trim();
    if (text.isNotEmpty()) {
      if (text.startsWithIgnoreCase("!vote bpm ") ||
          text.startsWithIgnoreCase("!vote bpi ")) {
        audioProcessor.ninjamClient.sendChatMessage(text);
      } else if (text.startsWithIgnoreCase("/me ")) {
        audioProcessor.ninjamClient.sendChatMessage(text);
      } else if (text.startsWithIgnoreCase("/key ")) {
        // Sends the tagged form, which every other client shows as plain text
        // and which we parse back on the way in. Nothing is set locally here --
        // the message we receive is what updates the header, so what we display
        // is exactly what the room was told.
        const auto key = MusicalKey::parseName(text.substring(5).toStdString());
        if (key.valid) {
          audioProcessor.ninjamClient.sendChatMessage(
              MusicalKey::buildTagged(key));
        } else {
          const juce::String msg =
              "Local: not a key. Try /key Dm, /key F# Dorian, /key Bb major.";
          chatDisplay.appendMessage(
              msg, colourForChatCategory(ChatFormat::Category::ServerNotice));
          announcer.say(msg, true);
        }
      } else if (text.startsWithIgnoreCase("/chords ")) {
        // Degrees are resolved here and only here. What goes on the wire is
        // the absolute chart, so every bot, and every client that is not
        // Antiphon, sees chords it already understands.
        juce::String chart = text.substring(8).trim();
        if (!chart.startsWithChar('|'))
          chart = "| " + chart.replace(" ", " | ") + " |";

        Harmony::Chart parsed;
        if (Harmony::parseChart(chart.toStdString(), parsed)) {
          audioProcessor.ninjamClient.sendChatMessage(
              Harmony::chartText(parsed, sessionKey));
        } else if (!sessionKey.valid) {
          const juce::String msg =
              "Local: set a key first, and then degrees will work: /key Dm.";
          chatDisplay.appendMessage(
              msg, colourForChatCategory(ChatFormat::Category::ServerNotice));
          announcer.say(msg, true);
        } else if (Harmony::parseDegreeChart(chart.toStdString(), sessionKey,
                                             parsed)) {
          audioProcessor.ninjamClient.sendChatMessage(
              Harmony::chartText(parsed, sessionKey));
        } else {
          const juce::String msg = "Local: not chords. Try /chords Am F C G, "
                                   "or in degrees, /chords ii V I.";
          chatDisplay.appendMessage(
              msg, colourForChatCategory(ChatFormat::Category::ServerNotice));
          announcer.say(msg, true);
        }
      } else if (text.startsWithIgnoreCase("/topic ") ||
                 text.startsWithIgnoreCase("/kick ") ||
                 text.startsWithIgnoreCase("/bpm ") ||
                 text.startsWithIgnoreCase("/bpi ")) {
        // The server's own list, from the error it returns for a bad one:
        // "ADMIN requires valid parameter, i.e. topic, kick, bpm, bpi"
        // (libninjam/libninjam server/usercon.cpp:1192). bpm and bpi
        // were missing here, so an admin typing /bpm 120 got "Unknown command"
        // from us and the server never saw it. Each needs the matching
        // privilege; the server replies "No BPM permission" if you lack it.
        audioProcessor.ninjamClient.sendAdminCommand(text.substring(1));
      } else if (text.startsWithIgnoreCase("/admin ")) {
        // Escape hatch: pass anything through as an ADMIN message, so a server
        // command we do not know about does not need a code change here.
        audioProcessor.ninjamClient.sendAdminCommand(text.substring(7).trim());
      } else if (text.startsWithIgnoreCase("/msg ")) {
        int firstSpace = text.indexOfChar(5, ' ');
        if (firstSpace > 0) {
          juce::String user = text.substring(5, firstSpace).trim();
          juce::String msg = text.substring(firstSpace).trim();
          audioProcessor.ninjamClient.sendPrivateMessage(user, msg);
        }
      } else if (text.startsWithChar('/')) {
        const juce::String msg =
            "Local: unknown command. Try /key, /chords, /topic, /kick, /bpm, "
            "/bpi, /msg, /me, or /admin <anything> to pass a command straight "
            "to the server.";
        chatDisplay.appendMessage(
            msg, colourForChatCategory(ChatFormat::Category::ServerNotice));
        announcer.say(msg, true);
      } else {
        audioProcessor.ninjamClient.sendChatMessage(text);
      }
      chatInput.clear();
    }
    // Sending must not leave the keys somewhere else: the next thing typed is
    // the next message, not a transport command to the DAW.
    chatInput.grabKeyboardFocus();
  };
  chatInput.onFocusChange = [this](bool focused, FocusChangeType) {
    onChatFocusChange(focused);
  };
  chatTypingLabel.setText("Typing to chat", juce::dontSendNotification);
  chatTypingLabel.setJustificationType(juce::Justification::centredRight);
  chatTypingLabel.setFont(juce::FontOptions(11.0f));
  chatTypingLabel.setColour(juce::Label::textColourId,
                            juce::Colour(AntiphonTheme::kAccent));
  chatTypingLabel.setAccessible(false);
  chatTypingLabel.setInterceptsMouseClicks(false, false);
  addChildComponent(chatTypingLabel);
  chatInput.setTitle("Chat message (" + Shortcuts::globalModifierName() + "C)");
  chatInput.setDescription("Chat message (" + Shortcuts::globalModifierName() +
                           "C)");
  addAndMakeVisible(chatInput);

  remoteUsersViewport.setViewedComponent(&remoteUsersContainer, false);
  remoteUsersViewport.setTitle("Remote players (" +
                               Shortcuts::globalModifierName() + "R)");
  remoteUsersViewport.setDescription(
      "Scrollable list of the other players in the room. Shortcut: " +
      Shortcuts::globalModifierName() + "R to focus section");
  addAndMakeVisible(remoteUsersViewport);

  for (const auto &msg : audioProcessor.ninjamClient.getChatLog())
    onChatMessage(msg.type, msg.username, msg.text);

  applyHostContextToControls();
  setChatConnectedState(audioProcessor.ninjamClient.isConnected());
  startTimerHz(30);
}

void AntiphonEditor::onChatFocusChange(bool focused) {
  chatTypingLabel.setVisible(focused && chatInput.isShowing());
  if (focused || audioProcessor.isStandaloneApp())
    return;
  // Losing the field to another control in this window is ordinary
  // navigation, and the reader names where focus went. Losing it to the DAW
  // is the trap: the next key typed is a transport command. Checked a moment
  // later, once focus has landed somewhere.
  juce::Component::SafePointer<AntiphonEditor> self(this);
  juce::Timer::callAfterDelay(100, [self]() {
    if (self == nullptr || self->chatInput.hasKeyboardFocus(false))
      return;
    // JUCE clears the focused component when its window loses focus to
    // another, and the DAW's windows are not ours -- so nothing focused means
    // the keys left Antiphon. Our own dialogs keep it set.
    if (juce::Component::getCurrentlyFocusedComponent() == nullptr)
      self->announcer.say("Keys now go to the DAW", true);
  });
}

void AntiphonEditor::refreshTransportButton() {
  const bool playing = audioProcessor.localTransportPlaying.load();
  transportButton.setToggleState(playing, juce::dontSendNotification);
  const bool plugin = !audioProcessor.isStandaloneApp();
  const bool synced =
      audioProcessor.clockMode.load() == (int)ClockModel::Mode::SyncedToDaw;
  // Synced, the DAW's transport is the one that plays; Play would only take
  // you off it. Disabled, with the reason, rather than quietly switching.
  const bool enabled = !(plugin && synced);
  transportButton.setEnabled(enabled);
  juce::String d = playing ? "Playing on Antiphon's clock"
                           : "Stopped. Starts Antiphon's clock";
  if (!enabled)
    d = "Follows the DAW transport while synced";
  transportButton.setDescription(
      d + ". Shortcut: " + Shortcuts::globalModifierName() + "G");
  transportButton.setTooltip(
      plugin ? "Plays the jam on Antiphon's own clock, away from the DAW. "
               "Disabled while synced: the DAW's transport plays then."
             : "The standalone's own transport. It drives the interval grid "
               "the way the DAW transport does in the plugin.\nConnecting "
               "starts it for you.");
}

void AntiphonEditor::refreshClockControls() {
  if (!hasClockRow())
    return;
  const bool connected = audioProcessor.ninjamClient.isConnected();
  const bool synced =
      audioProcessor.clockMode.load() == (int)ClockModel::Mode::SyncedToDaw;
  syncedToDawButton.setToggleState(synced, juce::dontSendNotification);
  antiphonClockButton.setToggleState(!synced, juce::dontSendNotification);
  // Each join starts synced (onConnected), so a choice made offline would be
  // overwritten the moment it mattered. Say so instead of accepting it.
  const juce::String why =
      connected ? juce::String() : ". Applies while connected";
  syncedToDawButton.setEnabled(connected);
  antiphonClockButton.setEnabled(connected);
  syncedToDawButton.setDescription("1 of 2" + why);
  antiphonClockButton.setDescription("2 of 2" + why);
  clockGroup.setDescription(
      connected ? juce::String() : juce::String("Applies while connected"));
  if (loopIntervalButton != nullptr)
    loopIntervalButton->setEnabled(
        connected && audioProcessor.publishedIntervalStartPpq.load() >= 0.0);
  if (followTempoToggle != nullptr)
    followTempoToggle->setToggleState(audioProcessor.tempoFollow.enabled,
                                      juce::dontSendNotification);
}

void AntiphonEditor::selectClockMode(ClockModel::Mode m) {
  if (!audioProcessor.ninjamClient.isConnected()) {
    announcer.say("The clock applies while connected.", true);
    refreshClockControls();
    return;
  }
  // Leaving the DAW keeps playing: the jam carries on from where it was, on
  // Antiphon's clock, rather than stopping under you.
  if (m == ClockModel::Mode::AntiphonClock &&
      audioProcessor.clockMode.load() != (int)m)
    audioProcessor.localTransportPlaying.store(true);
  audioProcessor.clockMode.store((int)m);
  refreshClockControls();
  // Nothing is said here: the state changes, and the transition says where you
  // landed (SyncAnnouncements::onTransition).
}

void AntiphonEditor::toggleAntiphonPlay() {
  const bool plugin = !audioProcessor.isStandaloneApp();
  const bool connected = audioProcessor.ninjamClient.isConnected();
  if (plugin && !connected) {
    announcer.say("Play applies while connected.", true);
    return;
  }
  if (plugin &&
      audioProcessor.clockMode.load() == (int)ClockModel::Mode::SyncedToDaw) {
    // The shortcut reaches here even while the button is disabled. It means
    // "play on Antiphon's clock", and selecting that clock already plays.
    selectClockMode(ClockModel::Mode::AntiphonClock);
    return;
  }
  const bool playing = !audioProcessor.localTransportPlaying.load();
  audioProcessor.localTransportPlaying.store(playing);
  refreshTransportButton();
  // Connected, the clock's transition is announced; offline nothing else is.
  if (!connected)
    announcer.say(playing ? "Playing" : "Stopped", true);
}

void AntiphonEditor::loopOneInterval() {
  auto *host = audioProcessor.hostControl.get();
  const double start = audioProcessor.publishedIntervalStartPpq.load();
  const int bpi = audioProcessor.internalBpi.load();
  if (host == nullptr || start < 0.0 || bpi <= 0) {
    announcer.say("Antiphon can set the loop only while synced to Reaper "
                  "and playing.",
                  true);
    return;
  }
  if (!host->setLoopBeats(start, start + bpi)) {
    announcer.say("Reaper did not accept the loop.", true);
    return;
  }
  // Bars as Reaper numbers them, assuming 4/4 -- the meter is not something
  // the host tells a plugin -- so this is beats when the interval is not
  // whole bars.
  if (bpi % 4 == 0) {
    const int first = (int)std::lround(start / 4.0) + 1;
    announcer.say("Reaper loop set to one interval, bars " +
                      juce::String(first) + " to " +
                      juce::String(first + bpi / 4 - 1) + ".",
                  true);
  } else {
    const int first = (int)std::lround(start) + 1;
    announcer.say("Reaper loop set to one interval, beats " +
                      juce::String(first) + " to " +
                      juce::String(first + bpi - 1) + ".",
                  true);
  }
}

bool AntiphonEditor::ClockGroup::keyPressed(const juce::KeyPress &k) {
  if (k.getModifiers().isAnyModifierKeyDown() || !onArrow)
    return false;
  const int code = k.getKeyCode();
  if (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey) {
    onArrow(-1);
    return true;
  }
  if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey) {
    onArrow(+1);
    return true;
  }
  return false;
}

void AntiphonEditor::applyHostContextToControls() {
  // The UI is allowed to differ between the standalone and the plugin, because
  // the two genuinely can do different things. Local channels and buses exist
  // so a DAW can route tracks in and record stems out; Sync locks our interval
  // grid to the DAW transport. In the standalone none of that has a
  // counterpart, so the controls stay visible -- the shape of the app should
  // not change under you -- but disabled, and they say why.

  // AU is the same argument with a different reason: the format cannot carry a
  // bus-topology change at all (AntiphonAudioProcessor::hasFixedBusLayout), so
  // the bus controls are dead there and must look it. Everything else -- local
  // channels, Sync -- works under AU exactly as it does under VST3.
  if (audioProcessor.hasFixedBusLayout()) {
    const juce::String auWhy =
        "Fixed under Audio Unit, which cannot be told the bus layout changed. "
        "Use the VST3 or CLAP build for per-player stems";

    for (auto *b : {&addInBusButton, &removeInBusButton, &addOutBusButton,
                    &removeOutBusButton}) {
      b->setEnabled(false);
      b->setTooltip(auWhy);
      b->setDescription(b->getDescription() + ". " + auWhy);
    }

    for (auto *l : {&inputBusGroupLabel, &outputBusGroupLabel})
      l->setColour(juce::Label::textColourId,
                   juce::Colour(AntiphonTheme::kDisabledText));
    return;
  }

  if (!audioProcessor.isStandaloneApp())
    return;

  const juce::String why = "Available when Antiphon runs as a plugin in a DAW";

  for (auto *b : {&addChannelButton, &addInBusButton, &removeInBusButton,
                  &addOutBusButton, &removeOutBusButton}) {
    b->setEnabled(false);
    b->setTooltip(why);
    // The reason belongs in the accessible description too: a reader that only
    // announces "dimmed" leaves the user guessing what would un-dim it.
    b->setDescription(b->getDescription() + ". " + why);
  }

  for (auto *l :
       {&channelGroupLabel, &inputBusGroupLabel, &outputBusGroupLabel})
    l->setColour(juce::Label::textColourId,
                 juce::Colour(AntiphonTheme::kDisabledText));

  // Short forms, to make room for the note. A label is not a reader target --
  // the buttons carry the accessible names -- so nothing is lost by shortening
  // the drawn text.
  channelGroupLabel.setText("Ch:", juce::dontSendNotification);
  inputBusGroupLabel.setText("In:", juce::dontSendNotification);
  outputBusGroupLabel.setText("Out:", juce::dontSendNotification);

  dawOnlyNote.setVisible(true);
  dawOnlyNote.setTitle("Plugin only");
  dawOnlyNote.setDescription(why);
}

AntiphonEditor::~AntiphonEditor() {
  audioProcessor.ninjamClient.removeListener(this);
  if (auto *host = keyListenerHost.getComponent())
    host->removeKeyListener(this);
  setLookAndFeel(nullptr);
}

juce::Colour AntiphonEditor::colourForChatCategory(ChatFormat::Category c) {
  using C = ChatFormat::Category;
  switch (c) {
  case C::Topic:
    return juce::Colour(0xffb0b6c0);
  case C::JoinPart:
    return juce::Colour(0xff6f7787);
  case C::SelfMessage:
    return juce::Colour(AntiphonTheme::kAccent);
  case C::OtherMessage:
    return juce::Colour(0xffe6e6ea);
  case C::PrivateMessage:
    return juce::Colour(0xffcf6fd8);
  case C::Action:
    return juce::Colour(0xffd8c46a);
  case C::Voting:
    return juce::Colour(0xffe0a030);
  // Musical facts about the session get their own green, distinct from the
  // amber the voting system uses -- they are settled, not under discussion.
  case C::Key:
  case C::ChordProgression:
    return juce::Colour(0xff6fd88a);
  case C::ServerNotice:
  default:
    return juce::Colour(0xff9aa1ad);
  }
}

void AntiphonEditor::onChatMessage(const juce::String &type,
                                   const juce::String &username,
                                   const juce::String &text) {
  const auto line = ChatFormat::render(
      type, username, text, audioProcessor.ninjamClient.getSelfUsername());

  chatDisplay.appendMessage(line.text, colourForChatCategory(line.category));

  // Incoming chat is spoken, because a reader user cannot glance at it.
  // Messages from the local user are skipped: they just typed them.
  //
  // What splits `important` from the rest here is not how interesting a line
  // is -- it is whether it was AIMED at you. A private message and a vote are
  // addressed to you and actionable; the room's ordinary traffic, its joins
  // and parts and its server notices, are the background of a busy room and
  // are the first thing anybody turns down. Both tiers speak at the default
  // setting, so this changes nothing until somebody asks it to.
  switch (line.category) {
  case ChatFormat::Category::OtherMessage:
    announcer.say(username + ": " + text, false);
    break;
  case ChatFormat::Category::PrivateMessage:
    announcer.say("Private message from " + username + ": " + text, true);
    break;
  case ChatFormat::Category::Action:
    announcer.say(username + " " + text.substring(4), false);
    break;
  case ChatFormat::Category::Topic:
    announcer.say("Topic: " + text, false);
    break;
  case ChatFormat::Category::JoinPart:
  case ChatFormat::Category::ServerNotice:
    announcer.say(text, false);
    break;
  case ChatFormat::Category::Voting:
    announcer.say(text.startsWith("[voting system]")
                      ? "Vote: " + text.substring(15).trim()
                      : text,
                  true);
    break;
  case ChatFormat::Category::Key:
  case ChatFormat::Category::ChordProgression:
  case ChatFormat::Category::SelfMessage:
    break;
  }

  // A key or a chart can arrive as chat or inside a topic, tagged, as `/key`,
  // in letters or in degrees -- and what each does to the other is decided in
  // `RoomHarmony`, the one place the band consults too.
  //
  // It used to be decided here as well, and the two drifted: the band learned
  // to read `| ii | V | I |` and to carry a chart through a key change, and
  // this did neither. The chord row and the marks on the phase bar went on
  // showing the chart before, with nothing to say they were stale
  // (`PRINCIPLES` 8).
  RoomHarmony::State room;
  room.key = sessionKey;
  room.chart = sessionChart;
  room.chartFromChat = chartFromChat;

  switch (RoomHarmony::apply(text.toStdString(), room)) {
  case RoomHarmony::Change::Key: {
    sessionKey = room.key;
    sessionChart = room.chart;
    announcer.say("Key: " + MusicalKey::displayName(sessionKey) + ". " +
                      MusicalKey::scaleNotes(sessionKey),
                  true);
    // The chart moved with it, so say so rather than leaving a reader to
    // wonder whether the chords they were told still apply.
    if (!sessionChart.empty())
      announcer.say("Chords: " + Harmony::chartText(sessionChart, sessionKey),
                    true);
    updateTempoChip();
    resized();
    repaint();
    break;
  }
  case RoomHarmony::Change::Chart: {
    sessionChart = room.chart;
    chartFromChat = true;
    announcer.say("Chords: " + Harmony::chartText(sessionChart, sessionKey),
                  true);

    // Chords are evidence about the key, so this is where the guess is made.
    // It is offered on the chip and never acted on: a suggestion that set the
    // key by itself would be a client deciding something the room did not.
    keyFromChords = Harmony::inferKey(Harmony::flatten(sessionChart));
    updateTempoChip();
    resized(); // the header grows the first time a chart appears
    repaint(headerRepaintArea);
    break;
  }
  case RoomHarmony::Change::None:
    if (line.category == ChatFormat::Category::Key ||
        line.category == ChatFormat::Category::ChordProgression) {
      announcer.say(username.isNotEmpty() ? username + ": " + text : text,
                    true);
    }
    break;
  }

  // The voting system talks through chat, so this is also where a vote is
  // noticed. A settled vote clears the chip rather than offering it again.
  const auto vote = ChatFormat::parseVote(text);
  if (vote.valid) {
    if (vote.settled) {
      pendingVote = {};
    } else if (!(vote.settled) && (vote.target != dismissedVoteTarget ||
                                   vote.isBpm != dismissedVoteIsBpm)) {
      pendingVote = vote;
    }
    updateTempoChip();
  }
}

void AntiphonEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  auto area = getLocalBounds().reduced(10);
  auto header = area.removeFromTop(headerHeight());

  const bool connected = audioProcessor.ninjamClient.isConnected();
  const bool connectFailed = audioProcessor.lastConnectFailed.load();
  const bool mismatch =
      !audioProcessor.isStandaloneApp() && connected &&
      std::abs(audioProcessor.hostBpm -
               (double)audioProcessor.internalBpm.load()) > 0.5;
  const bool headerWarning = connectFailed || mismatch;
  const bool practice = audioProcessor.inPracticeRoom();
  // The whole surface is themed, so the header follows the palette rather than
  // naming a colour of its own. The warning and idle states stay as they are:
  // they mean the same thing in either room.
  juce::Colour headerBg(0xff111111); // dark grey -- idle
  if (connected)
    headerBg = juce::Colour(AntiphonTheme::current().headerBg);
  else if (headerWarning)
    headerBg = juce::Colour(0xff2a1a0a); // amber -- failed/mismatch
  g.setColour(headerBg);
  g.fillRect(header);

  const juce::Colour teal(AntiphonTheme::current().accent);

  auto row1 = header.removeFromTop(22);
  g.setFont(juce::FontOptions{}.withHeight(15.0f).withStyle("Bold"));
  g.setColour(teal);
  g.drawFittedText("ANTIPHON", row1.removeFromLeft(90),
                   juce::Justification::centredLeft, 1);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));
  // The tint is never the only signal: the line says so in words, so a screen
  // reader and a colour-blind player learn it the same way (PRINCIPLES 11).
  // The address is deliberately not shown -- 127.0.0.1 with a port is true and
  // tells you nothing about where you are.
  g.setColour(connected ? teal : juce::Colours::grey);
  g.drawFittedText(practice ? juce::String("Practice room -- your own band")
                            : audioProcessor.connectionStatus,
                   row1, juce::Justification::centredLeft, 1);

  header.removeFromTop(2);

  auto row2 = header.removeFromTop(18);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));

  // The chart in roman numerals, at the far end of the row the key is on --
  // laid out from both ends, as the toolbar is. The absolute names go on the
  // timeline below, where their position carries the timing; here it is the
  // shape of the progression, which is what a numeral is for.
  if (connected && showsChartRow() && sessionKey.valid) {
    const juce::String roman =
        Harmony::romanChartText(sessionChart, sessionKey);
    if (roman.isNotEmpty()) {
      g.setColour(juce::Colours::white.withAlpha(0.55f));
      g.drawFittedText(roman, row2.removeFromRight(320),
                       juce::Justification::centredRight, 1);
    }
  }

  if (connected) {
    g.setColour(juce::Colours::white);
    // The running tempo, not the pending one. A server change that has not
    // reached an interval boundary yet is shown as pending rather than applied,
    // because until the boundary it is not what you are playing to.
    const int activeBpm = audioProcessor.publishedActiveBpm.load();
    const int activeBpi = audioProcessor.publishedActiveBpi.load();
    const int wantBpm = audioProcessor.internalBpm.load();
    const int wantBpi = audioProcessor.internalBpi.load();
    juce::String tempoText = "Server:  " + juce::String(activeBpm) + " BPM   " +
                             juce::String(activeBpi) + " BPI";
    if (wantBpm != activeBpm || wantBpi != activeBpi)
      tempoText += "   (-> " + juce::String(wantBpm) + " / " +
                   juce::String(wantBpi) + " next interval)";
    if (sessionKey.valid)
      tempoText +=
          "   Key " + juce::String(MusicalKey::displayName(sessionKey));
    g.drawFittedText(tempoText, row2, juce::Justification::centredLeft, 1);
  } else {
    g.setColour(juce::Colours::darkgrey);
    // Names the way in. This is the moment somebody most wants to play and
    // least knows how: "Not connected" alone left the band undiscoverable
    // unless you already knew to open Browse.
    g.drawFittedText("Not connected -- Ctrl+Alt+P for a practice room", row2,
                     juce::Justification::centredLeft, 1);
  }

  header.removeFromTop(4);

  // The chart, laid along the same axis as the phase bar below it, so each
  // chord name sits at the point in the interval where it actually starts and
  // the teal fill sweeps through them. Position is the information here: it is
  // what lets you see the next change coming rather than read that it exists.
  if (showsChartRow()) {
    auto chartRow = header.removeFromTop(14);
    const int bpi = audioProcessor.publishedActiveBpi.load();
    const auto layout = Harmony::layoutChart(sessionChart, bpi);
    if (!layout.empty()) {
      const float phase = audioProcessor.publishedPhaseBeats.load();
      const int nowStep = juce::jlimit(0, layout.steps() - 1,
                                       (int)(phase * Harmony::kStepsPerBeat));
      const int nowChord = layout.stepToChord[(size_t)nowStep];

      g.setFont(juce::FontOptions{}.withHeight(12.0f));
      int previousRight = chartRow.getX();
      for (int step = 0; step < layout.steps(); ++step) {
        if (!Harmony::changesAtStep(layout, step))
          continue;

        const int idx = layout.stepToChord[(size_t)step];
        const int x =
            chartRow.getX() + (int)((float)step / (float)layout.steps() *
                                    (float)chartRow.getWidth());

        // Where the next change is, so a label never runs into its neighbour.
        int nextStep = layout.steps();
        for (int s = step + 1; s < layout.steps(); ++s)
          if (Harmony::changesAtStep(layout, s)) {
            nextStep = s;
            break;
          }
        const int room =
            (int)((float)(nextStep - step) / (float)layout.steps() *
                  (float)chartRow.getWidth());

        const bool isNow = idx == nowChord;
        // A label that will not fit is dropped rather than overlapped -- except
        // the one sounding now, which is the one you are actually reading.
        if (x < previousRight && !isNow)
          continue;

        g.setColour(isNow ? teal : juce::Colours::white.withAlpha(0.45f));
        const auto name =
            Harmony::chordName(layout.chords[(size_t)idx], sessionKey);
        g.drawFittedText(name,
                         juce::Rectangle<int>(x, chartRow.getY(),
                                              juce::jmax(24, room - 4),
                                              chartRow.getHeight()),
                         juce::Justification::centredLeft, 1);
        previousRight = x + juce::jmin(room, 6 + (int)name.length() * 7);
      }
    }
    header.removeFromTop(2);
  }

  auto phaseBar = header.removeFromTop(8);
  g.setColour(juce::Colour(0xff1a1a2e));
  g.fillRect(phaseBar);
  if (audioProcessor.publishedActiveBpi.load() > 0) {
    int bpi = audioProcessor.publishedActiveBpi.load();
    float frac = juce::jlimit(
        0.0f, 1.0f, audioProcessor.publishedPhaseBeats.load() / (float)bpi);
    int barW = phaseBar.getWidth();
    int barX = phaseBar.getX();
    int barY = phaseBar.getY();
    int barH = phaseBar.getHeight();

    g.setColour(connected ? teal : juce::Colour(0xff444444));
    g.fillRect(phaseBar.withWidth((int)((float)barW * frac)));

    if (connected) {
      // Bar ticks only when the interval divides into whole bars, matching
      // MetronomeVoice::setBeatsPerInterval -- at BPI 11 a tick every fourth
      // beat would draw a bar the click does not play.
      const bool wholeBars = (bpi % 4 == 0);
      for (int i = 1; i < bpi; ++i) {
        int lineX = barX + (int)((float)i / (float)bpi * (float)barW);
        bool isBarBoundary = wholeBars && (i % 4 == 0);
        g.setColour(isBarBoundary ? juce::Colours::white.withAlpha(0.55f)
                                  : juce::Colours::white.withAlpha(0.18f));
        int lineH = isBarBoundary ? barH : barH - 2;
        int lineY = barY + (barH - lineH) / 2;
        g.drawVerticalLine(lineX, (float)lineY, (float)(lineY + lineH));
      }

      float iFlash = audioProcessor.intervalFlashIntensity.load();
      if (iFlash > 0.0f) {
        g.setColour(juce::Colours::white.withAlpha(iFlash * 0.80f));
        g.fillRect(phaseBar);
      }
      float bFlash = audioProcessor.beatFlashIntensity.load();
      if (bFlash > 0.0f) {
        int flashBeat = audioProcessor.lastBeatCrossedIndex.load();
        float alpha = (bpi % 4 == 0 && flashBeat % 4 == 0) ? 0.50f : 0.25f;
        g.setColour(juce::Colours::white.withAlpha(bFlash * alpha));
        g.fillRect(phaseBar);
      }
    }
  }

  header.removeFromTop(4);

  auto row3 = header.removeFromTop(20);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));
  if (audioProcessor.isStandaloneApp()) {
    g.setColour(juce::Colours::lightgrey);
    g.drawFittedText(
        "Phase: " + juce::String(audioProcessor.publishedPhaseBeats.load(), 2) +
            " / " + juce::String(audioProcessor.publishedActiveBpi.load()),
        row3, juce::Justification::centredLeft, 1);
  } else {
    // Driven by the clock model rather than re-derived here, so the text
    // and the audio gating can never disagree about what is happening.
    const auto sync =
        (ClockModel::State)audioProcessor.publishedClockState.load();
    if (connected && sync != ClockModel::State::Disconnected) {
      using ClockModel::State;
      juce::Colour c = juce::Colours::orange;
      if (sync == State::Synced || sync == State::AntiphonClock)
        c = juce::Colours::lightgreen;
      else if (sync == State::Stopped)
        c = juce::Colours::yellow;
      const juce::String msg =
          SyncAnnouncements::headerText(sync, syncContext());
      g.setColour(c);
      g.drawFittedText(msg, row3, juce::Justification::centredLeft, 1);
    } else {
      g.setColour(juce::Colours::darkgrey);
      g.drawFittedText("DAW: " + juce::String(audioProcessor.hostBpm, 1) +
                           " BPM",
                       row3, juce::Justification::centredLeft, 1);
    }
  }

  // Section labels -- mirror resized() geometry
  area.removeFromBottom(28); // toolbar
  area.removeFromBottom(10); // spacer above toolbar
  area.removeFromTop(10);    // spacer below status bar

  auto labelRow = area.removeFromTop(20);
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions{}.withHeight(14.0f));

  bool showChat = audioProcessor.chatVisible.load();
  if (showChat) {
    g.drawFittedText("Chat:", labelRow.removeFromRight(320),
                     juce::Justification::left, 1);
    labelRow.removeFromRight(10);
  }
  g.drawFittedText(
      "Local Channels:", labelRow.removeFromLeft(channelAreaLocalW),
      juce::Justification::left, 1);
  labelRow.removeFromLeft(10);
  g.drawFittedText("Remote Players:", labelRow, juce::Justification::left, 1);
}

void AntiphonEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  // Covers the painted header exactly, so the spoken status and the drawn one
  // describe the same region of the window.
  const auto header = area.removeFromTop(headerHeight());
  statusReadout.setBounds(header);
  // What the 30 Hz tick repaints: the header band plus the section-label row
  // just below it, both of which paint() draws.
  headerRepaintArea = getLocalBounds().withHeight(header.getBottom() + 32);
  area.removeFromTop(10); // Spacing below status bar

  // Bottom toolbar
  auto toolbar = area.removeFromBottom(28);
  area.removeFromBottom(10); // spacer above toolbar

  // The clock row, above the toolbar: the plugin's choice of clock, Play, and
  // what Reaper lets Antiphon do. Its own row because the toolbar has no room
  // for it at the minimum width, and because it is one subject.
  if (hasClockRow()) {
    auto row = area.removeFromBottom(28);
    area.removeFromBottom(6);
    clockGroup.setVisible(true);
    clockGroup.setBounds(row.removeFromLeft(320));
    auto g = clockGroup.getLocalBounds();
    clockLabel.setBounds(g.removeFromLeft(48));
    g.removeFromLeft(4);
    syncedToDawButton.setBounds(g.removeFromLeft(126).reduced(0, 4));
    g.removeFromLeft(4);
    antiphonClockButton.setBounds(g.removeFromLeft(136).reduced(0, 4));
    row.removeFromLeft(6);
    transportButton.setBounds(row.removeFromLeft(52).reduced(0, 4));
    if (followTempoToggle != nullptr) {
      row.removeFromLeft(16);
      followTempoToggle->setVisible(true);
      followTempoToggle->setBounds(row.removeFromLeft(190).reduced(0, 4));
      row.removeFromLeft(6);
      loopIntervalButton->setVisible(true);
      loopIntervalButton->setBounds(row.removeFromLeft(130).reduced(0, 4));
    }
    cueVolumeSlider.setVisible(true);
    cueVolumeSlider.setBounds(row.removeFromRight(60).reduced(0, 6));
    row.removeFromRight(4);
    cueToggle.setVisible(true);
    cueToggle.setBounds(row.removeFromRight(140).reduced(0, 4));
  }

  // Laid out from both ends rather than left to right. Consuming only from the
  // left made the last controls run off the window -- "Save Tx Audio" was drawn
  // outside its own 60 px button -- and the failure was invisible until you
  // looked. Anchoring the right-hand group to the right edge means a window too
  // narrow for everything shows a gap in the middle instead of text spilling
  // over its border.

  // Session controls, from the left.
  const bool standalone = audioProcessor.isStandaloneApp();

  browseButton.setBounds(toolbar.removeFromLeft(90).reduced(0, 4));
  toolbar.removeFromLeft(4);
  disconnectButton.setBounds(toolbar.removeFromLeft(82).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // View and debug toggles, from the right, in reverse visual order.
  chatToggle.setBounds(toolbar.removeFromRight(52).reduced(0, 4));
  toolbar.removeFromRight(8);
  testToneToggle.setBounds(toolbar.removeFromRight(80).reduced(0, 4));
  toolbar.removeFromRight(2);
  recordToggle.setBounds(toolbar.removeFromRight(72).reduced(0, 4));
  toolbar.removeFromRight(10);
  metronomeVolumeSlider.setBounds(toolbar.removeFromRight(60).reduced(0, 6));
  toolbar.removeFromRight(4);
  metronomeToggle.setBounds(toolbar.removeFromRight(90).reduced(0, 4));
  toolbar.removeFromRight(4);
  toolbar.removeFromRight(10);

  // The DAW-only run, continuing from the left. In the standalone the note
  // prefixes the whole run rather than trailing it -- a reason is only useful
  // before the thing it explains -- and the group labels use their short forms,
  // which costs nothing when the controls are inert and buys the room the note
  // needs. The full wording stays in each control's accessible name.
  // Gated on the condition itself, not on the label's visibility: resized() runs
  // during construction, before applyHostContextToControls() has made the note
  // visible, so asking the label would silently give it no bounds.
  // The transport comes first and is a live control, so the note must not
  // prefix it -- it belongs to the inert run that follows.
  if (standalone) {
    transportButton.setBounds(toolbar.removeFromLeft(52).reduced(0, 4));
    toolbar.removeFromLeft(10);
  }

  if (standalone) {
    dawOnlyNote.setBounds(toolbar.removeFromLeft(66));
    toolbar.removeFromLeft(4);
  }

  // Channel: [+]
  channelGroupLabel.setBounds(toolbar.removeFromLeft(standalone ? 30 : 62));
  toolbar.removeFromLeft(2);
  addChannelButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // Input bus: [+] [-]
  inputBusGroupLabel.setBounds(toolbar.removeFromLeft(standalone ? 34 : 70));
  toolbar.removeFromLeft(2);
  addInBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(2);
  removeInBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // Output bus: [+] [-]
  outputBusGroupLabel.setBounds(toolbar.removeFromLeft(standalone ? 38 : 76));
  toolbar.removeFromLeft(2);
  addOutBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(2);
  removeOutBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));

  // Section labels row
  area.removeFromTop(20);

  // Right panel: chat (conditionally visible) -- remove before elastic split
  bool showChat = audioProcessor.chatVisible.load();
  if (showChat) {
    auto rightPanel = area.removeFromRight(320);
    area.removeFromRight(10);
    chatInput.setBounds(rightPanel.removeFromBottom(24));
    // Always reserved, so the history does not jump when the label appears.
    chatTypingLabel.setBounds(rightPanel.removeFromBottom(14));
    rightPanel.removeFromBottom(2);

    // The chip sits between the history and the input: close to the thing it
    // is about, and in the tab order right before where you would reply.
    if (chipLabel.isVisible()) {
      auto chipRow = rightPanel.removeFromBottom(24);
      chipDismissButton.setBounds(chipRow.removeFromRight(66).reduced(0, 2));
      chipRow.removeFromRight(4);
      chipActionButton.setBounds(chipRow.removeFromRight(62).reduced(0, 2));
      chipRow.removeFromRight(6);
      chipLabel.setBounds(chipRow);
      rightPanel.removeFromBottom(6);
    }

    roomMembersLabel.setBounds(
        rightPanel.removeFromTop(kRoomMemberLineHeight * roomMembersLines));
    rightPanel.removeFromTop(4);
    chatDisplay.setBounds(rightPanel);
    chatDisplay.setVisible(true);
    chatInput.setVisible(true);
    roomMembersLabel.setVisible(true);
  } else {
    chatDisplay.setVisible(false);
    chatInput.setVisible(false);
    chatTypingLabel.setVisible(false);
    roomMembersLabel.setVisible(false);
    setChipVisible(false);
  }

  // Elastic local/remote split
  cachedChannelPanelBounds = area;
  lastLayoutKey = -1; // the window moved; the tick must not skip the relayout
  relayoutChannelArea();
}

void AntiphonEditor::openServerBrowser() {
  if (serverBrowser)
    return;

  serverBrowser = std::make_unique<ServerBrowserDialog>();

  serverBrowser->hostInput.setText(audioProcessor.lastHost, false);
  serverBrowser->portInput.setText(juce::String(audioProcessor.lastPort),
                                   false);
  serverBrowser->usernameInput.setText(audioProcessor.lastUsername, false);
  serverBrowser->passwordInput.setText(audioProcessor.lastPassword, false);
  serverBrowser->anonymousToggle.setToggleState(audioProcessor.lastAnonymous,
                                                juce::dontSendNotification);
  serverBrowser->passwordInput.setVisible(!audioProcessor.lastAnonymous);

  serverBrowser->onConnect = [this](const juce::String &host, int port,
                                    const juce::String &user,
                                    const juce::String &pass) {
    audioProcessor.lastHost = host;
    audioProcessor.lastPort = port;
    audioProcessor.lastUsername = serverBrowser->usernameInput.getText().trim();
    audioProcessor.lastPassword = serverBrowser->passwordInput.getText();
    audioProcessor.lastAnonymous =
        serverBrowser->anonymousToggle.getToggleState();
    audioProcessor.lastConnectFailed.store(false);
    audioProcessor.ninjamClient.connectToServer(host, port, user, pass);
  };
  serverBrowser->onPractice = [this]() {
    // Start the room, then join it exactly as if it were somebody else's
    // server. The band is already in there waiting, silent, and the roster
    // line teaches how to start it.
    const int port = audioProcessor.startPracticeRoom();
    if (port <= 0) {
      if (serverBrowser != nullptr)
        serverBrowser->setStatus(
            "The practice room would not start -- no free port on this "
            "machine.");
      return;
    }
    const auto user = serverBrowser->usernameInput.getText().trim();
    audioProcessor.lastConnectFailed.store(false);
    audioProcessor.ninjamClient.connectToServer(
        PracticeRoom::host(), port, user.isEmpty() ? "you" : user, "");
    closeServerBrowser();
  };

  serverBrowser->onClose = [this]() { closeServerBrowser(); };

  focusBeforeDialog = juce::Component::getCurrentlyFocusedComponent();
  serverBrowser->setSize(700, 460);

  juce::DialogWindow::LaunchOptions opts;
  // Names both destinations. A screen reader reads this on open, and "Connect
  // to a Ninjam server" told somebody looking for the band that they were in
  // the wrong place.
  opts.dialogTitle = "Connect: a server, or the practice room";
  opts.dialogBackgroundColour = juce::Colour(0xff1a1a2e);
  opts.content.setNonOwned(serverBrowser.get());
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = false;
  serverBrowserWindow = opts.launchAsync();

  // The dialog is modal and is its own top-level window, so while it is up the
  // editor sees no keys at all -- including the audit shortcut, which is the
  // one moment you most want it.
  if (auto *w = serverBrowserWindow.getComponent())
    w->addKeyListener(this);

  // There are four ways out of this dialog -- Connect, Cancel, the X drawn in
  // the UI, and the window manager's own close button -- and only the first
  // three ran our code. Closing from the title bar left `serverBrowser` set, so
  // openServerBrowser()'s early return meant the dialog could never be opened
  // again for the life of the editor. The modal callback fires for all four.
  if (auto *w = serverBrowserWindow.getComponent())
    juce::ModalComponentManager::getInstance()->attachCallback(
        w, juce::ModalCallbackFunction::create(
               [safeThis =
                    juce::Component::SafePointer<AntiphonEditor>(this)](int) {
                 if (safeThis != nullptr)
                   safeThis->closeServerBrowser();
               }));
}

void AntiphonEditor::closeServerBrowser() {
  // Idempotent: the modal callback below fires for every route out of the
  // dialog, including the ones that already came through here.
  if (serverBrowser == nullptr && serverBrowserWindow == nullptr)
    return;

  // Do NOT delete the window. launchAsync() enters the modal state with
  // deleteWhenDismissed set (juce_DialogWindow.cpp:128), so JUCE deletes it
  // itself when ModalComponentManager::handleAsyncUpdate runs. The manager
  // holds a SafePointer and so tolerated the manual delete that used to be
  // here, but deleting a component while it is still being torn off the modal
  // stack is not something to rely on. One owner, one deletion.
  if (auto *w = serverBrowserWindow.getComponent()) {
    serverBrowserWindow = nullptr;
    w->removeKeyListener(this);
    w->exitModalState(0);
  }
  serverBrowser.reset();

  // Put focus back where it came from, so closing the dialog does not strand a
  // keyboard or screen reader user in an unrelated part of the window.
  if (auto *previous = focusBeforeDialog.getComponent();
      previous != nullptr && previous->isShowing() && previous->isEnabled())
    previous->grabKeyboardFocus();
  else if (browseButton.isShowing() && browseButton.isEnabled())
    browseButton.grabKeyboardFocus();
  else
    statusReadout.grabKeyboardFocus();
  focusBeforeDialog = nullptr;
}

void AntiphonEditor::openShortcutsDialog() {
  if (shortcutsDialog)
    return;

  shortcutsDialog = std::make_unique<ShortcutsDialog>();
  shortcutsDialog->onClose = [this]() { closeShortcutsDialog(); };

  focusBeforeDialog = juce::Component::getCurrentlyFocusedComponent();
  shortcutsDialog->setSize(600, 460);

  juce::DialogWindow::LaunchOptions opts;
  opts.dialogTitle = "Keyboard Shortcuts";
  opts.dialogBackgroundColour = juce::Colour(0xff1a1a2e);
  opts.content.setNonOwned(shortcutsDialog.get());
  opts.escapeKeyTriggersCloseButton = true;
  opts.useNativeTitleBar = true;
  opts.resizable = false;
  shortcutsDialogWindow = opts.launchAsync();

  if (auto *w = shortcutsDialogWindow.getComponent())
    w->addKeyListener(this);

  if (auto *w = shortcutsDialogWindow.getComponent())
    juce::ModalComponentManager::getInstance()->attachCallback(
        w, juce::ModalCallbackFunction::create(
               [safeThis =
                    juce::Component::SafePointer<AntiphonEditor>(this)](int) {
                 if (safeThis != nullptr)
                   safeThis->closeShortcutsDialog();
               }));
}

void AntiphonEditor::closeShortcutsDialog() {
  if (shortcutsDialog == nullptr && shortcutsDialogWindow == nullptr)
    return;

  if (auto *w = shortcutsDialogWindow.getComponent()) {
    shortcutsDialogWindow = nullptr;
    w->removeKeyListener(this);
    w->exitModalState(0);
  }
  shortcutsDialog.reset();

  if (auto *previous = focusBeforeDialog.getComponent();
      previous != nullptr && previous->isShowing() && previous->isEnabled())
    previous->grabKeyboardFocus();
  else
    statusReadout.grabKeyboardFocus();
  focusBeforeDialog = nullptr;
}

void AntiphonEditor::mouseExit(const juce::MouseEvent &) { repaint(); }

// A shortcut must not depend on which of our descendants happens to hold focus.
// JUCE delivers a key press to the focused component and then bubbles it to its
// PARENTS -- so when focus sits on an ancestor (the standalone's DocumentWindow,
// or a host window in a DAW) the editor is never asked at all, and the shortcut
// silently does nothing. Registering as a KeyListener on the top-level window
// covers that direction; keyPressed covers focus inside the editor.
void AntiphonEditor::parentHierarchyChanged() {
  auto *top = getTopLevelComponent();
  if (top == keyListenerHost.getComponent())
    return;
  if (auto *old = keyListenerHost.getComponent())
    old->removeKeyListener(this);
  keyListenerHost = nullptr;
  if (top != nullptr && top != this) {
    top->addKeyListener(this);
    keyListenerHost = top;
  }
}

bool AntiphonEditor::keyPressed(const juce::KeyPress &key, juce::Component *) {
  return handleShortcut(key);
}

void AntiphonEditor::updateSelectionHighlights() {
  selectionModel.clamp(localChannelStrips.size(), remoteUserStrips.size());

  const auto section = selectionModel.getSection();
  const int localIdx = selectionModel.getLocalIndex();
  const int remoteIdx = selectionModel.getRemoteIndex();

  for (int i = 0; i < localChannelStrips.size(); ++i) {
    localChannelStrips[i]->setSelected(
        section == Selection::Section::LocalChannels && i == localIdx);
  }

  for (int i = 0; i < remoteUserStrips.size(); ++i) {
    remoteUserStrips[i]->setSelected(
        section == Selection::Section::RemoteUsers && i == remoteIdx);
  }
}

void AntiphonEditor::announceCurrentSelection() {
  updateSelectionHighlights();
  const auto section = selectionModel.getSection();

  if (section == Selection::Section::LocalChannels) {
    const int idx = selectionModel.getLocalIndex();
    if (idx >= 0 && idx < localChannelStrips.size()) {
      auto *strip = localChannelStrips[idx];
      announcer.say("Selected Local Channel " + juce::String(idx + 1) + ": " +
                        strip->getChannelName(),
                    true);
    } else {
      announcer.say("No local channels available", true);
    }
  } else if (section == Selection::Section::RemoteUsers) {
    const int idx = selectionModel.getRemoteIndex();
    if (idx >= 0 && idx < remoteUserStrips.size()) {
      auto *strip = remoteUserStrips[idx];
      announcer.say("Selected Remote Player " + juce::String(idx + 1) + ": " +
                        strip->getUsername(),
                    true);
    } else {
      announcer.say("No remote players connected", true);
    }
  }
}

bool AntiphonEditor::keyPressed(const juce::KeyPress &key) {
  return handleShortcut(key);
}

bool AntiphonEditor::handleShortcut(const juce::KeyPress &key) {
  auto *focused = juce::Component::getCurrentlyFocusedComponent();
  const bool isTextEditor =
      (dynamic_cast<juce::TextEditor *>(focused) != nullptr);

  const bool isLeft = (key.getKeyCode() == juce::KeyPress::leftKey);
  const bool isRight = (key.getKeyCode() == juce::KeyPress::rightKey);
  const bool isUp = (key.getKeyCode() == juce::KeyPress::upKey);
  const bool isDown = (key.getKeyCode() == juce::KeyPress::downKey);

  const auto action = Shortcuts::match(
      key.getKeyCode(), key.getModifiers().isCtrlDown(),
      key.getModifiers().isAltDown(), key.getModifiers().isShiftDown(),
      isTextEditor, isLeft, isRight, isUp, isDown,
      key.getModifiers().isCommandDown());

  if (action == Shortcuts::Action::None)
    return false;

  if (action == Shortcuts::Action::FocusChat) {
    if (chatInput.isEnabled() && chatInput.isShowing()) {
      chatInput.grabKeyboardFocus();
    } else {
      announcer.say("Chat is not available until you connect", true);
    }
    return true;
  }

  if (action == Shortcuts::Action::SyncNow) {
    runSyncNow();
    return true;
  }

  if (action == Shortcuts::Action::SayStatus) {
    announcer.say(
        SyncAnnouncements::status(
            (ClockModel::State)audioProcessor.publishedClockState.load(),
            syncContext()),
        true);
    return true;
  }

  if (action == Shortcuts::Action::ToggleAntiphonPlay) {
    toggleAntiphonPlay();
    return true;
  }

  if (action == Shortcuts::Action::ToggleClockMode) {
    if (!hasClockRow()) {
      announcer.say("The standalone has only Antiphon's clock.", true);
      return true;
    }
    selectClockMode(audioProcessor.clockMode.load() ==
                            (int)ClockModel::Mode::SyncedToDaw
                        ? ClockModel::Mode::AntiphonClock
                        : ClockModel::Mode::SyncedToDaw);
    return true;
  }

  if (action == Shortcuts::Action::RetroactiveTransmit) {
    LocalChannelStrip *target = nullptr;
    for (auto *strip : localChannelStrips)
      if (strip->hasKeyboardFocus(true)) {
        target = strip;
        break;
      }
    if (target == nullptr && !localChannelStrips.isEmpty())
      target = localChannelStrips[0];

    if (target != nullptr)
      target->toggleTransmitRetroactively();
    else
      announcer.say("No channel to transmit", true);
    return true;
  }

  if (action == Shortcuts::Action::WriteAudit) {
    std::vector<const juce::Component *> roots{getTopLevelComponent()};
    if (serverBrowser != nullptr)
      roots.push_back(serverBrowser.get());
    const auto report = AccessibilityAudit::auditReport(roots);
    auto f = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                 .getChildFile("antiphon-accessibility-audit.txt");
    const bool ok = f.replaceWithText(report);
    announcer.say(ok ? "Accessibility audit written to " + f.getFullPathName()
                     : "Could not write the accessibility audit to " +
                           f.getFullPathName(),
                  true);
    statusReadout.setStatus(ok ? "Accessibility audit written to " +
                                     f.getFullPathName()
                               : "Could not write the accessibility audit");
    return true;
  }

  if (action == Shortcuts::Action::OpenConnect) {
    openServerBrowser();
    announcer.say("Opening Server Browser dialog", true);
    return true;
  }

  if (action == Shortcuts::Action::StartPracticeRoom) {
    // Straight into the room, without the dialog. The dialog is where you
    // DISCOVER it; this is for somebody who already knows and wants to play.
    if (audioProcessor.ninjamClient.isConnected()) {
      announcer.say("Already connected. Disconnect first to start a practice "
                    "room",
                    true);
      return true;
    }
    const int port = audioProcessor.startPracticeRoom();
    if (port <= 0) {
      announcer.say("The practice room would not start", true);
      return true;
    }
    const auto user = audioProcessor.lastUsername.trim();
    audioProcessor.lastConnectFailed.store(false);
    audioProcessor.ninjamClient.connectToServer(
        PracticeRoom::host(), port, user.isEmpty() ? "you" : user, "");
    announcer.say("Starting a practice room", true);
    return true;
  }

  if (action == Shortcuts::Action::ToggleMuteAll) {
    bool anyMuted = false;
    for (auto *strip : localChannelStrips) {
      if (strip->toggleMute())
        anyMuted = true;
    }
    announcer.sayToggle("", "mute all", anyMuted);
    return true;
  }

  if (action == Shortcuts::Action::CycleSpeech) {
    // All -> Important -> Off -> All. The announcement of the new setting is
    // made at the OLD one where it has to be -- a reader told "announcements
    // off" by a channel that has just been switched off would hear nothing,
    // and a control whose confirmation is the thing it suppresses is a control
    // nobody can verify they have used.
    const int next = (audioProcessor.announceVerbosity.load() + 2) % 3;
    const auto v = (Announcer::Verbosity)next;
    announcer.say("Announcements: " + Announcer::verbosityName(v), true);
    audioProcessor.announceVerbosity.store(next);
    announcer.setVerbosity(v);
    return true;
  }

  if (action == Shortcuts::Action::AnnounceShortcutsHelp) {
    openShortcutsDialog();
    announcer.say("Keyboard Shortcuts dialog opened", true);
    return true;
  }

  if (action == Shortcuts::Action::FocusLocalSection) {
    selectionModel.setSection(Selection::Section::LocalChannels);
    announceCurrentSelection();
    return true;
  }

  if (action == Shortcuts::Action::FocusRemoteSection) {
    selectionModel.setSection(Selection::Section::RemoteUsers);
    announceCurrentSelection();
    return true;
  }

  if (action == Shortcuts::Action::SelectNextLocalChannel) {
    selectionModel.selectNextLocal(localChannelStrips.size());
    announceCurrentSelection();
    return true;
  }

  if (action == Shortcuts::Action::SelectPrevLocalChannel) {
    selectionModel.selectPrevLocal(localChannelStrips.size());
    announceCurrentSelection();
    return true;
  }

  if (action == Shortcuts::Action::SelectNextRemoteUser) {
    selectionModel.selectNextRemote(remoteUserStrips.size());
    announceCurrentSelection();
    return true;
  }

  if (action == Shortcuts::Action::SelectPrevRemoteUser) {
    selectionModel.selectPrevRemote(remoteUserStrips.size());
    announceCurrentSelection();
    return true;
  }

  if (action >= Shortcuts::Action::SelectLocalChannel1 &&
      action <= Shortcuts::Action::SelectLocalChannel9) {
    const int idx = (int)action - (int)Shortcuts::Action::SelectLocalChannel1;
    selectionModel.setSection(Selection::Section::LocalChannels);
    selectionModel.setLocalIndex(idx);
    announceCurrentSelection();
    return true;
  }

  if (action >= Shortcuts::Action::SelectRemoteUser1 &&
      action <= Shortcuts::Action::SelectRemoteUser9) {
    const int idx = (int)action - (int)Shortcuts::Action::SelectRemoteUser1;
    selectionModel.setSection(Selection::Section::RemoteUsers);
    selectionModel.setRemoteIndex(idx);
    announceCurrentSelection();
    return true;
  }

  // Contextual Channel Actions: Mute, Solo, Transmit, Volume, Pan
  selectionModel.clamp(localChannelStrips.size(), remoteUserStrips.size());
  const auto section = selectionModel.getSection();

  if (section == Selection::Section::LocalChannels) {
    const int idx = selectionModel.getLocalIndex();
    if (idx >= 0 && idx < localChannelStrips.size()) {
      auto *strip = localChannelStrips[idx];
      switch (action) {
      case Shortcuts::Action::ToggleMute:
        announcer.sayToggle(strip->getChannelName(), "mute",
                            strip->toggleMute());
        break;
      case Shortcuts::Action::ToggleSolo:
        announcer.sayToggle(strip->getChannelName(), "solo",
                            strip->toggleSolo());
        break;
      case Shortcuts::Action::ToggleTransmit:
        announcer.sayToggle(strip->getChannelName(), "transmit",
                            strip->toggleTransmit());
        break;
      case Shortcuts::Action::NudgeVolumeUp:
        strip->nudgeVolume(1.0f);
        announcer.say(strip->getChannelName() + " Volume up", true);
        break;
      case Shortcuts::Action::NudgeVolumeDown:
        strip->nudgeVolume(-1.0f);
        announcer.say(strip->getChannelName() + " Volume down", true);
        break;
      case Shortcuts::Action::NudgePanLeft:
        strip->nudgePan(-0.1f);
        announcer.say(strip->getChannelName() + " Pan left", true);
        break;
      case Shortcuts::Action::NudgePanRight:
        strip->nudgePan(0.1f);
        announcer.say(strip->getChannelName() + " Pan right", true);
        break;
      default:
        break;
      }
      return true;
    }
  } else if (section == Selection::Section::RemoteUsers) {
    const int idx = selectionModel.getRemoteIndex();
    if (idx >= 0 && idx < remoteUserStrips.size()) {
      auto *userStrip = remoteUserStrips[idx];
      auto *row = userStrip->getChannelRow(0);
      if (row != nullptr) {
        switch (action) {
        case Shortcuts::Action::ToggleMute:
          announcer.sayToggle(userStrip->getUsername(), "mute",
                              row->toggleMute());
          break;
        case Shortcuts::Action::ToggleSolo:
          announcer.sayToggle(userStrip->getUsername(), "solo",
                              row->toggleSolo());
          break;
        case Shortcuts::Action::NudgeVolumeUp:
          row->nudgeVolume(1.0f);
          announcer.say(userStrip->getUsername() + " Volume up", true);
          break;
        case Shortcuts::Action::NudgeVolumeDown:
          row->nudgeVolume(-1.0f);
          announcer.say(userStrip->getUsername() + " Volume down", true);
          break;
        case Shortcuts::Action::NudgePanLeft:
          row->nudgePan(-0.1f);
          announcer.say(userStrip->getUsername() + " Pan left", true);
          break;
        case Shortcuts::Action::NudgePanRight:
          row->nudgePan(0.1f);
          announcer.say(userStrip->getUsername() + " Pan right", true);
          break;
        default:
          break;
        }
        return true;
      }
    }
  }

  return false;
}

void AntiphonEditor::updateRoomMembers() {
  const auto members = audioProcessor.ninjamClient.getRoomMembers();
  juce::String text = "In room (" + juce::String((int)members.size()) + ")";
  if (!members.empty()) {
    juce::StringArray names;
    for (const auto &m : members)
      names.add(m.channelCount > 0 ? m.username : m.username + " (no audio)");
    text += ": " + names.joinIntoString(", ");
  }

  // Only touched when it actually changed: this runs at 30 Hz, and setText on
  // a Label repaints.
  if (text == lastRoomMembersText)
    return;
  lastRoomMembersText = text;
  roomMembersLabel.setText(text, juce::dontSendNotification);
  roomMembersLabel.setDescription(text);

  // A Label wraps to as many lines as its height allows, so a one-line box
  // silently truncated the room as soon as a third player joined -- the names
  // were there, just not on screen. Measure what the text needs and ask for
  // the height, capped so a busy room cannot eat the chat.
  const int width = juce::jmax(1, roomMembersLabel.getWidth());
  const auto &font = roomMembersLabel.getFont();
  const int wanted = juce::jlimit(
      1, kMaxRoomMemberLines,
      (int)std::ceil(juce::GlyphArrangement::getStringWidth(font, text) /
                     (float)width));
  if (wanted != roomMembersLines) {
    roomMembersLines = wanted;
    resized();
  }
}

bool AntiphonEditor::showsChartRow() const {
  // Only ever a chart somebody announced. In a jam, drawing a progression the
  // room did not agree to would be a lie -- the other players are not playing
  // it -- and the practice room's band is the one case where a default chart
  // is the truth, which is why that case waits for the room to be wired in.
  return audioProcessor.ninjamClient.isConnected() && !sessionChart.empty() &&
         audioProcessor.publishedActiveBpi.load() > 0;
}

int AntiphonEditor::headerHeight() const {
  // 14 for the labels and 2 to separate them from the bar they belong to.
  return showsChartRow() ? 96 : 80;
}

void AntiphonEditor::setChipVisible(bool shouldShow) {
  if (!shouldShow)
    lastAnnouncedChip.clear();
  if (chipLabel.isVisible() == shouldShow)
    return;
  chipLabel.setVisible(shouldShow);
  chipActionButton.setVisible(shouldShow);
  chipDismissButton.setVisible(shouldShow);
  resized();
}

void AntiphonEditor::showChip(const juce::String &offer,
                              const juce::String &action,
                              const juce::String &actionTitle,
                              const juce::String &actionDescription,
                              const juce::String &offerKey) {
  chipLabel.setText(offer, juce::dontSendNotification);
  chipLabel.setTitle(offer);
  chipActionButton.setButtonText(action);
  // setButtonText does not touch the accessible title, and JUCE's button
  // handler has no getTitle() of its own -- so a non-empty title set once
  // outranks the visible text forever, and the one button this chip reuses for
  // three different offers would be announced as whichever it was built for.
  chipActionButton.setTitle(actionTitle);
  chipActionButton.setDescription(actionDescription);
  setChipVisible(true);

  // The chip is the only surface that appears without being asked for, so a
  // reader user meets it by accident or not at all. Speak both halves: the
  // offer and the button, because they are separate controls reached
  // separately. Important, not chat-class: the default verbosity is
  // Important-only, so the quiet class would leave this silent for everyone.
  const juce::String key = offerKey.isEmpty() ? offer : offerKey;
  if (key != lastAnnouncedChip) {
    lastAnnouncedChip = key;
    announcer.say(offer + ". " + actionDescription, true);
  }
}

void AntiphonEditor::updateTempoChip() {
  const bool connected = audioProcessor.ninjamClient.isConnected();
  if (!connected) {
    pendingVote = {};
    setChipVisible(false);
    return;
  }

  // A server vote in progress outranks our own suggestion: someone has already
  // started one, and offering a competing proposal would split the vote.
  if (pendingVote.valid) {
    const juce::String base = "Vote: " + juce::String(pendingVote.target) +
                              " " + (pendingVote.isBpm ? "BPM" : "BPI");
    juce::String t = base;
    if (pendingVote.needed > 0)
      t += "  (" + juce::String(pendingVote.votes) + "/" +
           juce::String(pendingVote.needed) + ")";
    // Keyed on the proposal rather than on the text: the tally ticks up while
    // the same vote is running, and that is not a new thing to interrupt for.
    showChip(t, "Vote", "Cast vote", "Add your vote for " + t, base);
    return;
  }

  // Then a key the chords imply but nobody has declared. Below a live vote,
  // because a vote is a decision in progress and this is only an observation.
  if (keyFromChords.confident && keyFromChords.key != sessionKey &&
      keyFromChords.key != dismissedKeyGuess) {
    const juce::String t =
        "These chords look like " + MusicalKey::displayName(keyFromChords.key);
    // The accessible title has to CONTAIN the visible label, not merely mean
    // the same thing: somebody driving this by voice says what they can see.
    showChip(t, "Set key", "Set key for the room",
             "Tell the room the key is " +
                 MusicalKey::displayName(keyFromChords.key));
    return;
  }

  // A DAW at a different tempo is not offered as a vote: it read as the room
  // asking you to vote, when the problem was your DAW. The sync status says
  // what to do about that instead (SyncAnnouncements.h).
  setChipVisible(false);
}

void AntiphonEditor::setChatConnectedState(bool connected) {
  // The disconnected panel used to be 0xff1e1e1e text on 0xff121212, which is
  // invisible rather than dimmed -- it read as a rendering fault, not as a
  // disabled control. It now uses the same disabled treatment as every other
  // control in the window, and says in words why it is unavailable.
  const juce::Colour bg = connected
                              ? juce::Colour(0xff0a0a0a)
                              : juce::Colour(AntiphonTheme::kDisabledFill);
  const juce::Colour text = connected
                                ? juce::Colour(0xffe0e0e0)
                                : juce::Colour(AntiphonTheme::kDisabledText);

  chatDisplay.setColour(juce::TextEditor::backgroundColourId, bg);
  chatDisplay.setColour(juce::TextEditor::textColourId, text);
  chatDisplay.setColour(juce::TextEditor::outlineColourId,
                        juce::Colour(AntiphonTheme::kDisabledEdge));
  if (!connected) {
    chatDisplay.clear();
    chatDisplay.setText("Not connected.\n\nChat becomes available once you "
                        "join a server.\n",
                        juce::dontSendNotification);
  }
  chatDisplay.repaint();

  chatInput.setEnabled(connected);
  chatInput.setColour(juce::TextEditor::backgroundColourId, bg);
  chatInput.setColour(juce::TextEditor::textColourId, text);
  chatInput.setColour(juce::TextEditor::outlineColourId,
                      juce::Colour(AntiphonTheme::kDisabledEdge));
  chatInput.setTextToShowWhenEmpty(
      connected ? "Message, or a command: /key Dm, /chords Am F C G, /bpm 120, "
                  "/msg user text"
                : "Not connected -- join a server to chat",
      juce::Colour(connected ? 0xff8a8a8a : AntiphonTheme::kDisabledText));
  chatInput.repaint();
}

void AntiphonEditor::onConnected() {
  chatDisplay.clear();
  setChatConnectedState(true);
}

void AntiphonEditor::onDisconnected(const juce::String &) {
  // The key and the chords belong to the session, not to us.
  sessionKey = {};
  sessionChart.clear();
  chartFromChat = false;
  keyFromChords = {};
  dismissedKeyGuess = {};
  setChatConnectedState(false);
  resized(); // the header gives the chart row back
}

void AntiphonEditor::updateToolbarStates() {
  const bool connected = audioProcessor.ninjamClient.isConnected();

  browseButton.setEnabled(!connected);
  disconnectButton.setEnabled(connected);

  // Practice is offline only, so the button says so by being disabled rather
  // than by failing when pressed. It also has to follow the processor rather
  // than remember what was last clicked: connecting switches practice off
  // underneath it (onConnected), and a lit toggle over a feature that is no
  // longer running is the UI telling a lie about the one thing users most
  // want to be sure of.

  // Connecting starts the standalone's transport, so Play follows the
  // processor rather than its last click.
  refreshTransportButton();

  // Everything below is DAW-only and was settled once by
  // applyHostContextToControls(). Re-enabling it here every timer tick would
  // quietly undo that.
  if (audioProcessor.isStandaloneApp())
    return;

  refreshClockControls();

  // Under AU the bus buttons were disabled once, with their reason, by
  // applyHostContextToControls(). Re-enabling them on a tick would undo that
  // silently -- the same trap the standalone guard above exists to avoid.
  if (audioProcessor.hasFixedBusLayout())
    return;

  removeInBusButton.setEnabled(audioProcessor.getBusCount(true) > 1);
  removeOutBusButton.setEnabled(audioProcessor.getBusCount(false) > 1);
}

void AntiphonEditor::relayoutChannelArea() {
  if (cachedChannelPanelBounds.isEmpty())
    return;

  auto bounds = cachedChannelPanelBounds;
  const int gap = 10;
  const int panel_w = bounds.getWidth() - gap;

  // Content needs
  int numLocal = localChannelStrips.size();
  int local_need = numLocal > 0 ? numLocal * 94 - 4 : 0;

  int remote_need = 0;
  for (auto *s : remoteUserStrips)
    remote_need += s->getPreferredWidth() + 8;
  if (!remoteUserStrips.isEmpty())
    remote_need -= 8;

  // Hard allocations: 40/60 split with 200 px floor, capped at half
  int local_hard = juce::jlimit(std::min(200, panel_w / 2), panel_w / 2,
                                (int)((float)panel_w * 0.4f));
  int remote_hard = panel_w - local_hard;

  // Elastic four-case split
  int local_w;
  int remote_w;
  if (local_need <= local_hard && remote_need <= remote_hard) {
    local_w = local_hard;
    remote_w = remote_hard;
  } else if (local_need > local_hard && remote_need <= remote_hard) {
    remote_w = remote_need;
    local_w = panel_w - remote_w;
  } else if (remote_need > remote_hard && local_need <= local_hard) {
    local_w = local_need;
    remote_w = panel_w - local_w;
  } else {
    local_w = local_hard;
    remote_w = remote_hard;
  }

  channelAreaLocalW = local_w;

  localChannelsViewport.setBounds(bounds.removeFromLeft(local_w));
  bounds.removeFromLeft(gap);
  remoteUsersViewport.setBounds(bounds);

  // Position local strips: left-flush within container
  int h = localChannelsViewport.getHeight();
  int x = 0;
  for (auto *s : localChannelStrips) {
    s->setBounds(x, 0, 90, h);
    x += 94;
  }
  localChannelsContainer.setSize(std::max(x, localChannelsViewport.getWidth()),
                                 h);

  // Position remote strips: right-flush within container
  int rh = remoteUsersViewport.getHeight();

  std::vector<RemoteUserStrip *> strips;
  for (auto *s : remoteUserStrips)
    strips.push_back(s);

  int total_remote = 0;
  for (auto *s : strips)
    total_remote += s->getPreferredWidth() + 8;
  if (!strips.empty())
    total_remote -= 8;

  int container_w = std::max(total_remote, remoteUsersViewport.getWidth());
  int right_offset = container_w - total_remote;

  int rx = 0;
  for (auto *s : strips) {
    int sw = s->getPreferredWidth();
    s->setBounds(right_offset + rx, 0, sw, rh);
    rx += sw + 8;
  }
  remoteUsersContainer.setSize(container_w, rh);
  updateSelectionHighlights();
}

void AntiphonEditor::runSyncNow() {
  if (!audioProcessor.ninjamClient.isConnected()) {
    announcer.say("Not connected.", true);
    return;
  }
  if (audioProcessor.isStandaloneApp()) {
    announcer.say(
        SyncAnnouncements::status(
            (ClockModel::State)audioProcessor.publishedClockState.load(),
            syncContext()),
        true);
    return;
  }
  const auto plan = SyncAnnouncements::syncNow(
      (ClockModel::Mode)audioProcessor.clockMode.load(), syncContext());
  if (plan.selectSynced)
    audioProcessor.clockMode.store((int)ClockModel::Mode::SyncedToDaw);
  auto *host = audioProcessor.hostControl.get();
  bool acted = plan.selectSynced;
  if (plan.setTempo && host != nullptr &&
      host->setTempo((double)audioProcessor.internalBpm.load())) {
    audioProcessor.hostTempoSetCount.fetch_add(1);
    acted = true;
  }
  if (plan.pressPlay && host != nullptr && !host->isPlaying())
    acted = host->play() || acted;
  if (plan.say.isNotEmpty())
    announcer.say(plan.say, true);
  else if (!acted)
    announcer.say(
        "Antiphon could not change the DAW. " +
            SyncAnnouncements::headerText(
                (ClockModel::State)audioProcessor.publishedClockState.load(),
                syncContext()),
        true);
}

SyncAnnouncements::Context AntiphonEditor::syncContext() const {
  SyncAnnouncements::Context c;
  c.place = audioProcessor.inPracticeRoom() ? juce::String("the practice room")
                                            : audioProcessor.lastHost;
  c.players = remoteUserStrips.size();
  // The ROOM's tempo, not the grid's: synced, the grid runs at the DAW's.
  c.roomBpm = audioProcessor.internalBpm.load();
  c.bpi = audioProcessor.internalBpi.load();
  c.dawBpm = audioProcessor.publishedHostBpm.load();
  c.dawPlaying = audioProcessor.publishedHostPlaying.load();
  c.dawHasPosition =
      (ClockModel::State)audioProcessor.publishedClockState.load() !=
      ClockModel::State::NotSyncedNoPosition;
  c.beat = (int)audioProcessor.publishedPhaseBeats.load() + 1;
  c.reaper = audioProcessor.hostControl != nullptr;
  c.reaperFollows = c.reaper && audioProcessor.tempoFollow.enabled;
  return c;
}

bool AntiphonEditor::updateStatusReadout() {
  const bool connected = audioProcessor.ninjamClient.isConnected();
  const auto sync =
      (ClockModel::State)audioProcessor.publishedClockState.load();
  const auto ctx = syncContext();

  juce::String s;
  if (!connected) {
    s = audioProcessor.lastConnectFailed.load()
            ? "Not connected. The last connection attempt failed."
            : "Not connected.";
  } else {
    s = SyncAnnouncements::status(sync, ctx) + " ";
    // The key and the chart belong in the spoken status because they are drawn
    // in the header: a reader should get what a viewer gets. The chord SOUNDING
    // is deliberately not here -- it changes several times a bar, and reading
    // state that moves on a timer is exactly what PRINCIPLES 11 refuses.
    if (sessionKey.valid)
      s << "Key " << MusicalKey::displayName(sessionKey) << ". ";
    if (showsChartRow()) {
      s << "Chords " << Harmony::chartText(sessionChart, sessionKey) << ". ";
    }
    s = s.trimEnd();
  }
  // The beat moves several times a second; the readout must not, or a reader
  // sitting on it is told the same thing over and over.
  const juce::String stable =
      s.replace(", " + SyncAnnouncements::beatText(ctx), "");
  const bool statusChanged = stable != statusReadout.getStatus();
  statusReadout.setStatus(stable);

  if (connected != lastAnnouncedConnected) {
    lastAnnouncedConnected = connected;
    if (connected) {
      joinPending = true;
      joinAtMs = juce::Time::getMillisecondCounter();
      joinTempoSets = audioProcessor.hostTempoSetCount.load();
      seenResyncCount = audioProcessor.resyncCount.load();
    } else {
      announcer.say("Disconnected", true);
    }
  }
  updateSyncAnnouncements(connected, sync, ctx);
  return statusChanged;
}

// Every sync sentence goes through here, one at a time (SyncAnnouncements.h).
// A change is said once it has held for kAnnounceHoldMs, so a tempo ramp that
// passes through the room's tempo does not chatter -- except a resync to the
// playhead, which is the answer to something you just did and is said at once.
void AntiphonEditor::updateSyncAnnouncements(
    bool connected, ClockModel::State state,
    const SyncAnnouncements::Context &ctx) {
  using ClockModel::State;
  const auto now = juce::Time::getMillisecondCounter();
  const auto hold = (juce::uint32)ClockModel::kAnnounceHoldMs;

  if (!connected) {
    joinPending = false;
    tempoChangePending = false;
    announcedClockState = State::Disconnected;
    pendingClockState = State::Disconnected;
    lastAnnouncedBpm = lastAnnouncedBpi = 0;
    return;
  }

  if (ctx.roomBpm != lastAnnouncedBpm || ctx.bpi != lastAnnouncedBpi) {
    if (!joinPending && lastAnnouncedBpm != 0) {
      tempoChangePending = true;
      tempoChangeAtMs = now;
      tempoChangeSets = audioProcessor.hostTempoSetCount.load();
      tempoPrevBpm = lastAnnouncedBpm;
      tempoPrevBpi = lastAnnouncedBpi;
    }
    lastAnnouncedBpm = ctx.roomBpm;
    lastAnnouncedBpi = ctx.bpi;
  }

  if (joinPending) {
    // Long enough for the server's tempo to arrive and Reaper to follow it.
    if (now - joinAtMs < kJoinSettleMs)
      return;
    auto c = ctx;
    c.reaperSetTempo = audioProcessor.hostTempoSetCount.load() != joinTempoSets;
    announcer.say(SyncAnnouncements::onJoin(state, c), true);
    joinPending = false;
    announcedClockState = pendingClockState = state;
    seenResyncCount = audioProcessor.resyncCount.load();
    return;
  }

  if (tempoChangePending) {
    if (now - tempoChangeAtMs < hold)
      return;
    auto c = ctx;
    c.prevRoomBpm = tempoPrevBpm;
    c.prevBpi = tempoPrevBpi;
    c.reaperSetTempo =
        audioProcessor.hostTempoSetCount.load() != tempoChangeSets;
    announcer.say(SyncAnnouncements::onRoomTempoChange(state, c), true);
    tempoChangePending = false;
    announcedClockState = pendingClockState = state;
    return;
  }

  const int resyncs = audioProcessor.resyncCount.load();
  const bool resynced = resyncs != seenResyncCount;
  seenResyncCount = resyncs;
  if (resynced && state == State::Synced &&
      announcedClockState != State::Synced) {
    announcer.say(
        SyncAnnouncements::onTransition(announcedClockState, state, ctx), true);
    announcedClockState = pendingClockState = state;
    return;
  }

  if (state == announcedClockState) {
    pendingClockState = state;
    return;
  }
  if (state != pendingClockState) {
    pendingClockState = state;
    pendingSinceMs = now;
    return;
  }
  if (now - pendingSinceMs < hold)
    return;
  announcer.say(
      SyncAnnouncements::onTransition(announcedClockState, state, ctx), true);
  announcedClockState = state;
}

void AntiphonEditor::timerCallback() {
  // Swap the whole palette when a practice room is joined or left. JUCE caches
  // colour ids, so the look-and-feel has to be told to read them again, and
  // everything on screen has to be repainted once.
  if (AntiphonTheme::isPractice() != audioProcessor.inPracticeRoom()) {
    AntiphonTheme::setPractice(audioProcessor.inPracticeRoom());
    customLookAndFeel.applyPalette();
    sendLookAndFeelChange();
    repaint();
  }

  const bool statusChanged = updateStatusReadout();

  auto decay = [](std::atomic<float> &v) {
    float f = v.load();
    if (f > 0.0f)
      v.store(std::max(0.0f, f - (1.0f / 12.0f)));
  };
  decay(audioProcessor.intervalFlashIntensity);
  decay(audioProcessor.beatFlashIntensity);

  if (++diagTickCounter >= 30) {
    diagTickCounter = 0;
    audioProcessor.ninjamClient.dumpDiagnostics();
  }

  updateToolbarStates();
  if (audioProcessor.chatVisible.load()) {
    updateRoomMembers();
    updateTempoChip();
  }
  // Only the header animates -- status text, phase bar, beat flashes. The
  // channel strips repaint their own meters, so repainting the whole editor
  // here redrew every static label, every dB scale and the whole background
  // 30 times a second. Profiling an idle, disconnected instance put
  // paintEntireComponent at 66% of all CPU and drawFittedText at 25%.
  // ...and only when the bar has stepped. The beat flashes are sampled at the
  // same moments rather than driving repaints of their own: they decay over
  // ~0.4 s while beats arrive every 0.44-0.6 s, so letting them force a repaint
  // would keep the header redrawing most of the time and undo the saving.
  const int phaseStep =
      (int)(audioProcessor.publishedPhaseBeats.load() * kPhaseStepsPerBeat);
  const bool phaseMoved = phaseStep != lastPhaseStep;
  lastPhaseStep = phaseStep;
  if (phaseMoved || statusChanged)
    repaint(headerRepaintArea);

  // Sync local channel strips to the processor's localChannels vector
  {
    juce::ScopedLock sl(audioProcessor.localChannelMutex);
    int numChannels = (int)audioProcessor.localChannels.size();
    int numStrips = localChannelStrips.size();

    // Remove extra strips (from the end)
    while (localChannelStrips.size() > numChannels) {
      localChannelsContainer.removeChildComponent(
          localChannelStrips[localChannelStrips.size() - 1]);
      localChannelStrips.removeLast();
    }

    // Add missing strips
    while (localChannelStrips.size() < numChannels) {
      int idx = localChannelStrips.size();
      auto *strip = new LocalChannelStrip(
          audioProcessor, audioProcessor.localChannels[(std::size_t)idx], idx);
      localChannelStrips.add(strip);
      localChannelsContainer.addAndMakeVisible(strip);
    }

    // Mark only the last strip as removable (only last bus can be removed)
    for (int i = 0; i < localChannelStrips.size(); ++i)
      localChannelStrips[i]->setRemovable(i == localChannelStrips.size() - 1 &&
                                          localChannelStrips.size() > 1);

    // Update peaks and bus counts; positioning is handled by relayoutChannelArea()
    int numInBuses = audioProcessor.getBusCount(true);
    for (auto *s : localChannelStrips) {
      s->updatePeaks();
      s->updateInputBusCount(numInBuses);
    }
  }

  // Sync remote user strips
  if (audioProcessor.ninjamClient.isConnected()) {
    auto users = audioProcessor.ninjamClient.getRemoteUsers();

    for (int i = remoteUserStrips.size() - 1; i >= 0; --i) {
      if (!users.contains(remoteUserStrips[i]->getName())) {
        remoteUserStrips.remove(i);
      }
    }

    for (const auto &pair : users) {
      const auto &username = pair.first;
      const auto &user = pair.second;

      RemoteUserStrip *strip = nullptr;
      for (auto *s : remoteUserStrips) {
        if (s->getName() == username) {
          strip = s;
          break;
        }
      }

      if (!strip) {
        strip = new RemoteUserStrip(audioProcessor, username);
        strip->setName(username);
        remoteUserStrips.add(strip);
        remoteUsersContainer.addAndMakeVisible(strip);
      }

      strip->updateChannels(user.channels);
    }

    int numOutBuses = audioProcessor.getBusCount(false);
    for (auto *s : remoteUserStrips)
      s->updateOutputBusCount(numOutBuses);
  } else if (!remoteUserStrips.isEmpty()) {
    remoteUserStrips.clear();
  }

  // Laying out is cheap on its own, but every setBounds it performs marks a
  // component dirty and buys another repaint. Only do it when the set of strips
  // has actually changed; resized() handles the window moving.
  const int layoutKey =
      localChannelStrips.size() * 1000 + remoteUserStrips.size() * 10;
  if (layoutKey != lastLayoutKey) {
    lastLayoutKey = layoutKey;
    relayoutChannelArea();
  }
}
