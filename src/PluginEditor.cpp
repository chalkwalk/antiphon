#include "GainUtils.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "AccessibilityTree.h"
#include "ServerBrowserDialog.h"

AntiphonEditor::AntiphonEditor(AntiphonAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  audioProcessor.ninjamClient.addListener(this);

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
  browseButton.setTitle("Connect");
  browseButton.setDescription(
      "Open the server browser to choose a server and connect");
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

  syncButton.setButtonText("Sync");
  syncButton.setTooltip(
      "Lock the jam to your DAW transport. Press this, then start the "
      "transport -- the interval grid begins on that downbeat. The timeline "
      "position is deliberately ignored, so this works in clip/session view "
      "as well as with a loop.");
  syncButton.onClick = [this]() { audioProcessor.requestSync(); };
  syncButton.setRepaintsOnMouseActivity(true);
  syncButton.setTitle("Sync");
  syncButton.setDescription(
      "Lock the interval grid to the next start of the DAW transport");
  addAndMakeVisible(syncButton);

  transportButton.onClick = [this]() {
    const bool playing = !audioProcessor.localTransportPlaying.load();
    audioProcessor.localTransportPlaying.store(playing);
    refreshTransportButton();
    announcer.say(playing ? "Transport running" : "Transport stopped", true);
  };
  transportButton.setRepaintsOnMouseActivity(true);
  addChildComponent(transportButton);
  refreshTransportButton();

  practiceToggle.setButtonText("Practice");
  practiceToggle.setToggleState(audioProcessor.ninjamClient.isPracticeEnabled(),
                                juce::dontSendNotification);
  practiceToggle.onClick = [this]() {
    const bool on = practiceToggle.getToggleState();
    const bool ok = audioProcessor.setPracticeEnabled(on);
    if (on && !ok) {
      practiceToggle.setToggleState(false, juce::dontSendNotification);
      statusReadout.setStatus("Practice needs a tempo and a sample rate");
      announcer.say("Practice could not start", true);
    } else {
      announcer.say(on ? "Practice on. Your playing comes back delayed."
                       : "Practice off",
                    true);
    }
    resized();
  };
  practiceToggle.setTitle("Practice");
  practiceToggle.setDescription(
      "Play your own audio back to you, several intervals late, as virtual "
      "players. Offline only, and never transmitted");
  practiceToggle.setTooltip(
      "A duet with your past self: what you play comes back 4, 6 and 8 "
      "intervals later as separate players you can mix.\n"
      "Offline only -- it is not available while connected, and is never "
      "transmitted.");
  practiceToggle.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(practiceToggle);

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

  chatToggle.setButtonText("Chat");
  chatToggle.setToggleState(audioProcessor.chatVisible.load(),
                            juce::dontSendNotification);
  chatToggle.onClick = [this]() {
    audioProcessor.chatVisible.store(chatToggle.getToggleState());
    resized();
  };
  chatToggle.setRepaintsOnMouseActivity(true);
  chatToggle.setTitle("Chat panel");
  chatToggle.setDescription("Show or hide the chat panel");
  addAndMakeVisible(chatToggle);

  localChannelsViewport.setViewedComponent(&localChannelsContainer, false);
  localChannelsViewport.setTitle("Local channels");
  localChannelsViewport.setDescription(
      "Scrollable list of the channels you transmit");
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
    if (chipDawBpm > 0) {
      audioProcessor.ninjamClient.sendChatMessage("!vote bpm " +
                                                  juce::String(chipDawBpm));
      dismissedDawBpm = chipDawBpm;
    } else if (pendingVote.valid) {
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
    if (chipDawBpm > 0) {
      dismissedDawBpm = chipDawBpm;
    } else if (pendingVote.valid) {
      dismissedVoteTarget = pendingVote.target;
      dismissedVoteIsBpm = pendingVote.isBpm;
      pendingVote = {};
    } else if (keyFromChords.confident) {
      dismissedKeyGuess = keyFromChords.key;
    }
    updateTempoChip();
  };
  addChildComponent(chipDismissButton);

  chatDisplay.setMultiLine(true);
  chatDisplay.setReadOnly(true);
  chatDisplay.setScrollbarsShown(true);
  chatDisplay.setCaretVisible(false);
  chatDisplay.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colour(0xff121212));
  chatDisplay.setTitle("Chat history");
  chatDisplay.setDescription("Messages from the server and the other players");
  addAndMakeVisible(chatDisplay);

  chatInput.setName("chatInput");
  chatInput.setMultiLine(false);
  chatInput.setReturnKeyStartsNewLine(false);
  chatInput.setTextToShowWhenEmpty(
      "Message, or a command: /key Dm, /chords Am F C G, /bpm 120, /msg user text",
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
        const auto key = MusicalKey::parseName(text.substring(5));
        if (key.valid) {
          audioProcessor.ninjamClient.sendChatMessage(
              MusicalKey::buildTagged(key));
        } else {
          chatDisplay.insertTextAtCaret("Local: not a key. Try /key Dm, /key "
                                        "F# Dorian, /key Bb major.\n");
        }
      } else if (text.startsWithIgnoreCase("/chords ")) {
        // Degrees are resolved here and only here. What goes on the wire is
        // the absolute chart, so every bot, and every client that is not
        // Antiphon, sees chords it already understands.
        juce::String chart = text.substring(8).trim();
        if (!chart.startsWithChar('|'))
          chart = "| " + chart.replace(" ", " | ") + " |";

        Harmony::Chart parsed;
        if (Harmony::parseChart(chart, parsed)) {
          audioProcessor.ninjamClient.sendChatMessage(
              Harmony::chartText(parsed, sessionKey.valid && sessionKey.flat));
        } else if (!sessionKey.valid) {
          chatDisplay.insertTextAtCaret(
              "Local: set a key first, and then degrees will work: /key Dm.\n");
        } else if (Harmony::parseDegreeChart(chart, sessionKey, parsed)) {
          audioProcessor.ninjamClient.sendChatMessage(
              Harmony::chartText(parsed, sessionKey.flat));
        } else {
          chatDisplay.insertTextAtCaret(
              "Local: not chords. Try /chords Am F C G, or in degrees, "
              "/chords ii V I.\n");
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
        chatDisplay.insertTextAtCaret(
            "Local: unknown command. Try /key, /chords, /topic, /kick, /bpm, "
            "/bpi, /msg, "
            "/me, or /admin <anything> to pass a command straight to the "
            "server.\n");
      } else {
        audioProcessor.ninjamClient.sendChatMessage(text);
      }
      chatInput.clear();
    }
  };
  chatInput.setTitle("Chat message");
  chatInput.setDescription(
      "Type a message or a command and press return to send");
  addAndMakeVisible(chatInput);

  remoteUsersViewport.setViewedComponent(&remoteUsersContainer, false);
  remoteUsersViewport.setTitle("Remote players");
  remoteUsersViewport.setDescription(
      "Scrollable list of the other players in the room");
  addAndMakeVisible(remoteUsersViewport);

  for (const auto &msg : audioProcessor.ninjamClient.getChatLog())
    onChatMessage(msg.type, msg.username, msg.text);

  applyHostContextToControls();
  setChatConnectedState(audioProcessor.ninjamClient.isConnected());
  startTimerHz(30);
}

void AntiphonEditor::refreshTransportButton() {
  const bool playing = audioProcessor.localTransportPlaying.load();
  transportButton.setButtonText(playing ? "Stop" : "Play");
  transportButton.setToggleState(playing, juce::dontSendNotification);
  transportButton.setTitle(playing ? "Stop the transport"
                                   : "Start the transport");
  transportButton.setDescription(
      playing ? "Stop the interval clock, the metronome and transmission"
              : "Start the interval clock, so the metronome runs and, when "
                "connected, audio is transmitted");
  transportButton.setTooltip(
      "The standalone's own transport. It drives the interval grid the way the "
      "DAW transport does in the plugin.\nConnecting starts it for you.");
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
                  &addOutBusButton, &removeOutBusButton, &syncButton}) {
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

  // The standalone gets a transport of its own where the plugin has Sync.
  transportButton.setVisible(true);

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

  // juce::TextEditor keeps a colour per inserted run, so setting the colour
  // before each insert gives per-message colour with no new widget -- and a
  // read-only TextEditor stays the best primitive for reader navigation.
  chatDisplay.setColour(juce::TextEditor::textColourId,
                        colourForChatCategory(line.category));
  chatDisplay.moveCaretToEnd();
  chatDisplay.insertTextAtCaret(line.text + "\n");

  // A key can arrive as chat or inside a topic, tagged or as `/key`; all land
  // here.
  if (const auto key = MusicalKey::parseAnnouncement(text); key.valid) {
    if (key != sessionKey) {
      sessionKey = key;
      announcer.say("Key: " + MusicalKey::displayName(key) + ". " +
                        MusicalKey::scaleNotes(key),
                    true);
      repaint(headerRepaintArea);
    }
  }

  // A chart arrives the same way, and from anyone. What the room was told is
  // what gets drawn -- nothing here invents a progression.
  if (Harmony::Chart chart; Harmony::parseChart(text, chart)) {
    const auto flat = sessionKey.valid && sessionKey.flat;
    sessionChart = std::move(chart);
    announcer.say("Chords: " + Harmony::chartText(sessionChart, flat), true);

    // Chords are evidence about the key, so this is where the guess is made.
    // It is offered on the chip and never acted on: a suggestion that set the
    // key by itself would be a client deciding something the room did not.
    keyFromChords = Harmony::inferKey(Harmony::flatten(sessionChart));
    updateTempoChip();
    resized(); // the header grows the first time a chart appears
    repaint(headerRepaintArea);
  }

  // The voting system talks through chat, so this is also where a vote is
  // noticed. A settled vote clears the chip rather than offering it again.
  const auto vote = ChatFormat::parseVote(text);
  if (vote.valid) {
    if (vote.settled) {
      pendingVote = {};
    } else if (!(vote.settled) && !(vote.target == dismissedVoteTarget &&
                                    vote.isBpm == dismissedVoteIsBpm)) {
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
  juce::Colour headerBg =
      connected       ? juce::Colour(0xff0d0d1a)  // navy -- normal
      : headerWarning ? juce::Colour(0xff2a1a0a)  // amber -- failed/mismatch
                      : juce::Colour(0xff111111); // dark grey -- idle
  g.setColour(headerBg);
  g.fillRect(header);

  const juce::Colour teal(0xff00b4d8);

  auto row1 = header.removeFromTop(22);
  g.setFont(juce::FontOptions{}.withHeight(15.0f).withStyle("Bold"));
  g.setColour(teal);
  g.drawFittedText("ANTIPHON", row1.removeFromLeft(90),
                   juce::Justification::centredLeft, 1);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));
  g.setColour(connected ? teal : juce::Colours::grey);
  g.drawFittedText(audioProcessor.connectionStatus, row1,
                   juce::Justification::centredLeft, 1);

  header.removeFromTop(2);

  auto row2 = header.removeFromTop(18);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));

  // The chart in roman numerals, at the far end of the row the key is on --
  // laid out from both ends, as the toolbar is. The absolute names go on the
  // timeline below, where their position carries the timing; here it is the
  // shape of the progression, which is what a numeral is for.
  if (connected && showsChartRow() && sessionKey.valid) {
    const auto roman = Harmony::romanChartText(sessionChart, sessionKey);
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
      tempoText += "   Key " + MusicalKey::displayName(sessionKey);
    g.drawFittedText(tempoText, row2, juce::Justification::centredLeft, 1);
  } else {
    g.setColour(juce::Colours::darkgrey);
    g.drawFittedText("Not connected", row2, juce::Justification::centredLeft,
                     1);
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
      const int nowStep = juce::jlimit(
          0, layout.steps() - 1, (int)(phase * Harmony::kStepsPerBeat));
      const int nowChord = layout.stepToChord[(size_t)nowStep];

      g.setFont(juce::FontOptions{}.withHeight(12.0f));
      int previousRight = chartRow.getX();
      for (int step = 0; step < layout.steps(); ++step) {
        if (!Harmony::changesAtStep(layout, step))
          continue;

        const int idx = layout.stepToChord[(size_t)step];
        const int x = chartRow.getX() +
                      (int)((float)step / (float)layout.steps() *
                            (float)chartRow.getWidth());

        // Where the next change is, so a label never runs into its neighbour.
        int nextStep = layout.steps();
        for (int s = step + 1; s < layout.steps(); ++s)
          if (Harmony::changesAtStep(layout, s)) {
            nextStep = s;
            break;
          }
        const int room = (int)((float)(nextStep - step) / (float)layout.steps() *
                               (float)chartRow.getWidth());

        const bool isNow = idx == nowChord;
        // A label that will not fit is dropped rather than overlapped -- except
        // the one sounding now, which is the one you are actually reading.
        if (x < previousRight && !isNow)
          continue;

        g.setColour(isNow ? teal : juce::Colours::white.withAlpha(0.45f));
        const auto name = Harmony::chordName(layout.chords[(size_t)idx],
                                             sessionKey.valid && sessionKey.flat);
        g.drawFittedText(name,
                         juce::Rectangle<int>(x, chartRow.getY(),
                                              juce::jmax(24, room - 4),
                                              chartRow.getHeight()),
                         juce::Justification::centredLeft, 1);
        previousRight = x + juce::jmin(room, 6 + name.length() * 7);
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
    g.fillRect(phaseBar.withWidth((int)(barW * frac)));

    if (connected) {
      // Bar ticks only when the interval divides into whole bars, matching
      // MetronomeVoice::setBeatsPerInterval -- at BPI 11 a tick every fourth
      // beat would draw a bar the click does not play.
      const bool wholeBars = (bpi % 4 == 0);
      for (int i = 1; i < bpi; ++i) {
        int lineX = barX + (int)((float)i / bpi * barW);
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
    // Driven by the sync state machine rather than re-derived here, so the text
    // and the audio gating can never disagree about what is happening.
    const auto sync =
        (SyncState::State)audioProcessor.publishedSyncState.load();
    if (connected && sync != SyncState::State::Disconnected) {
      juce::Colour c = juce::Colours::grey;
      juce::String msg = SyncState::describe(sync);
      switch (sync) {
      case SyncState::State::TempoMismatch:
        c = juce::Colours::orange;
        msg = "DAW tempo does not match - set the DAW to " +
              juce::String(audioProcessor.internalBpm.load()) + " BPM";
        break;
      case SyncState::State::ReadyToSync:
        c = juce::Colours::yellow;
        msg = "Tempo matches - press Sync to arm";
        break;
      case SyncState::State::WaitingForPlay:
        c = juce::Colours::yellow;
        msg = "Armed - start the DAW transport to join";
        break;
      case SyncState::State::Running:
        c = juce::Colours::lightgreen;
        msg = "In sync";
        break;
      default:
        break;
      }
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
  practiceToggle.setBounds(toolbar.removeFromRight(76).reduced(0, 4));
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
  if (standalone)
    transportButton.setBounds(toolbar.removeFromLeft(52).reduced(0, 4));
  else
    syncButton.setBounds(toolbar.removeFromLeft(52).reduced(0, 4));
  toolbar.removeFromLeft(10);

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
    rightPanel.removeFromBottom(6);

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
  serverBrowser->onClose = [this]() { closeServerBrowser(); };

  focusBeforeDialog = juce::Component::getCurrentlyFocusedComponent();
  serverBrowser->setSize(700, 460);

  juce::DialogWindow::LaunchOptions opts;
  opts.dialogTitle = "Connect to a Ninjam server";
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

bool AntiphonEditor::keyPressed(const juce::KeyPress &key) {
  return handleShortcut(key);
}

bool AntiphonEditor::handleShortcut(const juce::KeyPress &key) {
  // Ctrl+Alt combinations, chosen to stay clear of the shortcuts a DAW claims.
  // Documented in docs/ACCESSIBILITY.md; every one of these is also reachable by
  // tabbing to the control and pressing it, so none of them is the only route.
  // Match the key CODE, never the text character: with Ctrl held, X11 hands
  // JUCE a control character, so Ctrl+Alt+A arrives as 0x01 and comparing
  // against 'A' silently never fires. See Shortcuts.h.
  const auto action = Shortcuts::match(
      key.getKeyCode(), key.getModifiers().isCtrlDown(),
      key.getModifiers().isAltDown(), key.getModifiers().isShiftDown());
  if (action == Shortcuts::Action::None)
    return false;

  if (action == Shortcuts::Action::FocusChat) {
    if (chatInput.isEnabled() && chatInput.isShowing()) {
      chatInput.grabKeyboardFocus();
      announcer.say("Chat message box", true);
    } else {
      announcer.say("Chat is not available until you connect", true);
    }
    return true;
  }

  if (action == Shortcuts::Action::ArmSync) {
    if (syncButton.isEnabled()) {
      audioProcessor.requestSync();
      announcer.say("Sync armed. Start the DAW transport to join.", true);
    } else {
      announcer.say("Sync is not available yet", true);
    }
    return true;
  }

  if (action == Shortcuts::Action::RetroactiveTransmit) {
    // Applies to the strip the focus is in, so it behaves like holding that
    // strip's own TX button. Falls back to the first channel when focus is
    // elsewhere, which is the common single-channel case.
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
    // The audit this project relies on instead of a screen reader nobody here
    // can run. Writes beside the other debug dumps.
    // Audit every root we own, not just this one. The connect dialog is a
    // separate top-level window, so walking the editor never reached it and
    // none of its controls had ever been checked.
    std::vector<const juce::Component *> roots{getTopLevelComponent()};
    if (serverBrowser != nullptr)
      roots.push_back(serverBrowser.get());
    const auto report = AccessibilityAudit::auditReport(roots);
    auto f = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                 .getChildFile("antiphon-accessibility-audit.txt");
    const bool ok = f.replaceWithText(report);
    // Name the file and admit failure: this shortcut has no other visible
    // effect, so "nothing happened" and "it worked" looked identical.
    announcer.say(ok ? "Accessibility audit written to " + f.getFullPathName()
                     : "Could not write the accessibility audit to " +
                           f.getFullPathName(),
                  true);
    statusReadout.setStatus(ok ? "Accessibility audit written to " +
                                     f.getFullPathName()
                               : "Could not write the accessibility audit");
    return true;
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
  if (chipLabel.isVisible() == shouldShow)
    return;
  chipLabel.setVisible(shouldShow);
  chipActionButton.setVisible(shouldShow);
  chipDismissButton.setVisible(shouldShow);
  resized();
}

void AntiphonEditor::updateTempoChip() {
  const bool connected = audioProcessor.ninjamClient.isConnected();
  if (!connected) {
    pendingVote = {};
    chipDawBpm = 0;
    setChipVisible(false);
    return;
  }

  // A server vote in progress outranks our own suggestion: someone has already
  // started one, and offering a competing proposal would split the vote.
  if (pendingVote.valid) {
    chipDawBpm = 0;
    juce::String t = "Vote: " + juce::String(pendingVote.target) + " " +
                     (pendingVote.isBpm ? "BPM" : "BPI");
    if (pendingVote.needed > 0)
      t += "  (" + juce::String(pendingVote.votes) + "/" +
           juce::String(pendingVote.needed) + ")";
    chipLabel.setText(t, juce::dontSendNotification);
    chipLabel.setTitle(t);
    chipActionButton.setButtonText("Vote");
    chipActionButton.setDescription("Add your vote for " + t);
    setChipVisible(true);
    return;
  }

  // Then a key the chords imply but nobody has declared. Below a live vote,
  // because a vote is a decision in progress and this is only an observation;
  // above the DAW tempo, because it is about the music rather than the setup.
  if (keyFromChords.confident && keyFromChords.key != sessionKey &&
      keyFromChords.key != dismissedKeyGuess) {
    chipDawBpm = 0;
    const juce::String t = "These chords look like " +
                           MusicalKey::displayName(keyFromChords.key);
    chipLabel.setText(t, juce::dontSendNotification);
    chipLabel.setTitle(t);
    chipActionButton.setButtonText("Set key");
    chipActionButton.setDescription("Tell the room the key is " +
                                    MusicalKey::displayName(keyFromChords.key));
    setChipVisible(true);
    return;
  }

  // Otherwise: the DAW is at a different tempo from the server. This only
  // offers the vote -- changing your DAW tempo never casts one.
  const int serverBpm = audioProcessor.publishedActiveBpm.load();
  const int hostBpm = (int)std::lround(audioProcessor.hostBpm);
  // A tempo the server would refuse is not worth offering: an out-of-range
  // `!vote` is answered with a complaint about the command's parameters, which
  // tells a player nothing about the real problem. A DAW at 30 BPM is ordinary.
  const bool worthProposing =
      !audioProcessor.isStandaloneApp() && hostBpm > 0 && serverBpm > 0 &&
      hostBpm != serverBpm && hostBpm != dismissedDawBpm &&
      ChatFormat::isVotableBpm(hostBpm);
  if (worthProposing) {
    chipDawBpm = hostBpm;
    const juce::String t = "Your DAW is at " + juce::String(hostBpm) + " BPM";
    chipLabel.setText(t, juce::dontSendNotification);
    chipLabel.setTitle(t);
    chipActionButton.setButtonText("Propose");
    chipActionButton.setDescription("Vote to change the server tempo to " +
                                    juce::String(hostBpm) + " BPM");
    setChipVisible(true);
    return;
  }

  chipDawBpm = 0;
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
      connected
          ? "Message, or a command: /key Dm, /chords Am F C G, /bpm 120, /msg user text"
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
  const bool practiceOn = audioProcessor.ninjamClient.isPracticeEnabled();
  if (practiceToggle.getToggleState() != practiceOn)
    practiceToggle.setToggleState(practiceOn, juce::dontSendNotification);
  practiceToggle.setEnabled(!connected);
  practiceToggle.setTooltip(
      connected ? juce::String("Practice is offline only -- disconnect to use "
                               "it. Nothing you play in practice is ever sent.")
                : juce::String("A duet with your past self: what you play "
                               "comes back a chosen number of intervals "
                               "later. Offline only, never transmitted."));
  practiceToggle.setDescription(
      connected
          ? juce::String("Practice mode, unavailable while connected to a "
                         "server. It is offline only and is never transmitted")
          : juce::String("Play your own audio back to you, delayed, as virtual "
                         "players. Offline only, and never transmitted"));

  // Everything below is DAW-only and was settled once by
  // applyHostContextToControls(). Re-enabling it here every timer tick would
  // quietly undo that.
  if (audioProcessor.isStandaloneApp())
    return;

  const auto sync = (SyncState::State)audioProcessor.publishedSyncState.load();
  syncButton.setEnabled(sync == SyncState::State::ReadyToSync ||
                        sync == SyncState::State::WaitingForPlay ||
                        sync == SyncState::State::Running);

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
                                (int)(panel_w * 0.4f));
  int remote_hard = panel_w - local_hard;

  // Elastic four-case split
  int local_w, remote_w;
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

  // The echo strip shares this area with the remote players, because that is
  // what it is: a player who happens to be you, late. It has to be laid out
  // here with them -- being in the container is not enough, and a strip with no
  // bounds is invisible in a way that looks exactly like the feature not
  // working.
  std::vector<RemoteUserStrip *> strips;
  for (auto *s : remoteUserStrips)
    strips.push_back(s);
  if (echoStrip != nullptr)
    strips.push_back(echoStrip.get());

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
}

bool AntiphonEditor::updateStatusReadout() {
  const bool connected = audioProcessor.ninjamClient.isConnected();
  const auto sync = (SyncState::State)audioProcessor.publishedSyncState.load();
  // The running tempo, so the spoken status and the drawn one agree.
  const int bpm = audioProcessor.publishedActiveBpm.load();
  const int bpi = audioProcessor.publishedActiveBpi.load();

  juce::String s;
  if (!connected) {
    s = audioProcessor.lastConnectFailed.load()
            ? "Not connected. The last connection attempt failed."
            : "Not connected.";
  } else {
    s << "Connected to " << audioProcessor.lastHost << " as "
      << (audioProcessor.lastUsername.isNotEmpty() ? audioProcessor.lastUsername
                                                   : juce::String("anonymous"))
      << ". " << bpm << " BPM, " << bpi << " beats per interval. ";

    // The key and the chart belong in the spoken status because they are drawn
    // in the header: a reader should get what a viewer gets. The chord SOUNDING
    // is deliberately not here -- it changes several times a bar, and reading
    // state that moves on a timer is exactly what PRINCIPLES 11 refuses.
    if (sessionKey.valid)
      s << "Key " << MusicalKey::displayName(sessionKey) << ". ";
    if (showsChartRow()) {
      s << "Chords " << Harmony::chartText(sessionChart, sessionKey.flat)
        << ". ";
    }

    s << (audioProcessor.isStandaloneApp()
              ? juce::String("Running.")
              : juce::String(SyncState::describe(sync)) + ".");
  }
  const bool statusChanged = s != statusReadout.getStatus();
  statusReadout.setStatus(s);

  // Announce only the transitions, never the steady state -- this runs 30 times
  // a second.
  if (connected != lastAnnouncedConnected) {
    lastAnnouncedConnected = connected;
    announcer.say(connected ? "Connected" : "Disconnected", true);
  }
  if (connected && (bpm != lastAnnouncedBpm || bpi != lastAnnouncedBpi)) {
    const bool first = lastAnnouncedBpm == 0;
    lastAnnouncedBpm = bpm;
    lastAnnouncedBpi = bpi;
    if (!first)
      announcer.say("Tempo now " + juce::String(bpm) + " BPM, " +
                        juce::String(bpi) + " beats per interval",
                    true);
  }
  if (connected && !audioProcessor.isStandaloneApp()) {
    const juce::String syncText = SyncState::describe(sync);
    if (syncText != lastAnnouncedSyncState) {
      lastAnnouncedSyncState = syncText;
      announcer.say(syncText, true);
    }
  }
  return statusChanged;
}

void AntiphonEditor::timerCallback() {
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
      if (users.find(remoteUserStrips[i]->getName()) == users.end()) {
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
    if (echoStrip != nullptr) {
      remoteUsersContainer.removeChildComponent(echoStrip.get());
      echoStrip.reset();
    }
  } else if (audioProcessor.ninjamClient.isPracticeEnabled()) {
    // Offline with practice on: the echo taps take the mixer's remote half,
    // because that is what they are -- players, who happen to be you, late.
    if (!remoteUserStrips.isEmpty())
      remoteUserStrips.clear();
    const auto taps = audioProcessor.ninjamClient.getEchoTaps();
    if (!taps.empty()) {
      if (echoStrip == nullptr) {
        echoStrip = std::make_unique<RemoteUserStrip>(audioProcessor,
                                                      "Echo (you, delayed)");
        echoStrip->setName("Echo");
        remoteUsersContainer.addAndMakeVisible(echoStrip.get());
      }
      echoStrip->updateEchoTaps(taps,
                                audioProcessor.ninjamClient.maxEchoDelay());
      echoStrip->updateOutputBusCount(audioProcessor.getBusCount(false));
    }
  } else if (!remoteUserStrips.isEmpty() || echoStrip != nullptr) {
    remoteUserStrips.clear();
    if (echoStrip != nullptr) {
      remoteUsersContainer.removeChildComponent(echoStrip.get());
      echoStrip.reset();
    }
  }

  // Laying out is cheap on its own, but every setBounds it performs marks a
  // component dirty and buys another repaint. Only do it when the set of strips
  // has actually changed; resized() handles the window moving.
  // The echo strip has to be in the key. Without it, switching practice on
  // while no players are present leaves the counts unchanged, the relayout is
  // skipped, and the strip appears with no bounds -- invisible, and looking
  // exactly like the feature not working.
  const int layoutKey = localChannelStrips.size() * 1000 +
                        remoteUserStrips.size() * 10 +
                        (echoStrip != nullptr ? 1 : 0);
  if (layoutKey != lastLayoutKey) {
    lastLayoutKey = layoutKey;
    relayoutChannelArea();
  }
}
