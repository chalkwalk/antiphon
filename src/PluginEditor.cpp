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
  browseButton.setDescription("Open the server browser to choose a server and connect");
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

  saveTxToggle.setButtonText("Save Tx");
  saveTxToggle.setToggleState(audioProcessor.saveTxEnabled,
                              juce::dontSendNotification);
  saveTxToggle.onClick = [this]() {
    audioProcessor.saveTxEnabled = saveTxToggle.getToggleState();
    audioProcessor.applyDebugCaptureSettings();
  };
  saveTxToggle.setRepaintsOnMouseActivity(true);
  saveTxToggle.setTitle("Save transmitted audio");
  saveTxToggle.setDescription("Debug: write the audio you send to a file on the desktop");
  addAndMakeVisible(saveTxToggle);

  saveRxToggle.setButtonText("Save Rx");
  saveRxToggle.setToggleState(audioProcessor.saveRxEnabled,
                              juce::dontSendNotification);
  saveRxToggle.onClick = [this]() {
    audioProcessor.saveRxEnabled = saveRxToggle.getToggleState();
    audioProcessor.applyDebugCaptureSettings();
  };
  saveRxToggle.setRepaintsOnMouseActivity(true);
  saveRxToggle.setTitle("Save received audio");
  saveRxToggle.setDescription("Debug: write the audio you receive to a file on the desktop");
  addAndMakeVisible(saveRxToggle);

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
  testToneToggle.setDescription("Debug: replace all local input with a timing probe");
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
  syncButton.setDescription("Lock the interval grid to the next start of the DAW transport");
  addAndMakeVisible(syncButton);

  // Compact toolbar groups
  channelGroupLabel.setText("Channel:", juce::dontSendNotification);
  channelGroupLabel.setJustificationType(juce::Justification::centredRight);
  channelGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(channelGroupLabel);

  addChannelButton.setButtonText("+");
  addChannelButton.setTooltip("Add a local channel strip (routes to Input Bus 1 by default)");
  addChannelButton.onClick = [this]() { audioProcessor.addLocalChannel(); };
  addChannelButton.setRepaintsOnMouseActivity(true);
  addChannelButton.setTitle("Add channel");
  addChannelButton.setDescription("Add a local channel strip");
  addAndMakeVisible(addChannelButton);

  inputBusGroupLabel.setText("Input bus:", juce::dontSendNotification);
  inputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  inputBusGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(inputBusGroupLabel);

  addInBusButton.setButtonText("+");
  addInBusButton.setTooltip("Add a new stereo input bus (for DAW to route another track into)");
  addInBusButton.onClick = [this]() { audioProcessor.addInputBus(); };
  addInBusButton.setRepaintsOnMouseActivity(true);
  addInBusButton.setTitle("Add input bus");
  addInBusButton.setDescription("Add a stereo input bus for the DAW to route a track into");
  addAndMakeVisible(addInBusButton);

  removeInBusButton.setButtonText("-");
  removeInBusButton.setTooltip("Remove the last input bus (channels using it revert to bus 1)");
  removeInBusButton.onClick = [this]() { audioProcessor.removeLastInputBus(); };
  removeInBusButton.setRepaintsOnMouseActivity(true);
  removeInBusButton.setTitle("Remove input bus");
  removeInBusButton.setDescription("Remove the last input bus; channels using it revert to bus 1");
  addAndMakeVisible(removeInBusButton);

  outputBusGroupLabel.setText("Output bus:", juce::dontSendNotification);
  outputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  outputBusGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(outputBusGroupLabel);

  dawOnlyNote.setText("DAW only", juce::dontSendNotification);
  dawOnlyNote.setJustificationType(juce::Justification::centredLeft);
  dawOnlyNote.setColour(juce::Label::textColourId,
                        juce::Colour(AntiphonTheme::kDisabledText));
  addChildComponent(dawOnlyNote);

  addOutBusButton.setButtonText("+");
  addOutBusButton.setTooltip("Add a new stereo output bus (for DAW stem recording)");
  addOutBusButton.onClick = [this]() { audioProcessor.addOutputBus(); };
  addOutBusButton.setRepaintsOnMouseActivity(true);
  addOutBusButton.setTitle("Add output bus");
  addOutBusButton.setDescription("Add a stereo output bus for recording stems");
  addAndMakeVisible(addOutBusButton);

  removeOutBusButton.setButtonText("-");
  removeOutBusButton.setTooltip("Remove the last output bus (remote channels using it revert to bus 1)");
  removeOutBusButton.onClick = [this]() { audioProcessor.removeLastOutputBus(); };
  removeOutBusButton.setRepaintsOnMouseActivity(true);
  removeOutBusButton.setTitle("Remove output bus");
  removeOutBusButton.setDescription("Remove the last output bus; channels using it revert to bus 1");
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
  metronomeVolumeSlider.setDescription("Level of the metronome click in decibels");
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
  roomMembersLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b6c0));
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
  chipActionButton.setDescription("Send your vote for this tempo to the server");
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
      "Message, or a command: /bpm 120, /bpi 16, /topic text, /msg user text",
      juce::Colours::grey);
  chatInput.onReturnKey = [this]() {
    juce::String text = chatInput.getText().trim();
    if (text.isNotEmpty()) {
      if (text.startsWithIgnoreCase("!vote bpm ") ||
          text.startsWithIgnoreCase("!vote bpi ")) {
        audioProcessor.ninjamClient.sendChatMessage(text);
      } else if (text.startsWithIgnoreCase("/me ")) {
        audioProcessor.ninjamClient.sendChatMessage(text);
      } else if (text.startsWithIgnoreCase("/topic ") ||
                 text.startsWithIgnoreCase("/kick ") ||
                 text.startsWithIgnoreCase("/bpm ") ||
                 text.startsWithIgnoreCase("/bpi ")) {
        // The server's own list, from the error it returns for a bad one:
        // "ADMIN requires valid parameter, i.e. topic, kick, bpm, bpi"
        // (references/libninjam/ninjam/server/usercon.cpp:1192). bpm and bpi
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
            "Local: unknown command. Try /topic, /kick, /bpm, /bpi, /msg, /me, "
            "or /admin <anything> to pass a command straight to the server.\n");
      } else {
        audioProcessor.ninjamClient.sendChatMessage(text);
      }
      chatInput.clear();
    }
  };
  chatInput.setTitle("Chat message");
  chatInput.setDescription("Type a message or a command and press return to send");
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

void AntiphonEditor::applyHostContextToControls() {
  // The UI is allowed to differ between the standalone and the plugin, because
  // the two genuinely can do different things. Local channels and buses exist
  // so a DAW can route tracks in and record stems out; Sync locks our interval
  // grid to the DAW transport. In the standalone none of that has a
  // counterpart, so the controls stay visible -- the shape of the app should
  // not change under you -- but disabled, and they say why.
  if (!audioProcessor.isStandaloneApp())
    return;

  const juce::String why =
      "Available when Antiphon runs as a plugin in a DAW";

  for (auto *b : {&addChannelButton, &addInBusButton, &removeInBusButton,
                  &addOutBusButton, &removeOutBusButton, &syncButton}) {
    b->setEnabled(false);
    b->setTooltip(why);
    // The reason belongs in the accessible description too: a reader that only
    // announces "dimmed" leaves the user guessing what would un-dim it.
    b->setDescription(b->getDescription() + ". " + why);
  }

  for (auto *l : {&channelGroupLabel, &inputBusGroupLabel, &outputBusGroupLabel})
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
  case C::Topic:          return juce::Colour(0xffb0b6c0);
  case C::JoinPart:       return juce::Colour(0xff6f7787);
  case C::SelfMessage:    return juce::Colour(AntiphonTheme::kAccent);
  case C::OtherMessage:   return juce::Colour(0xffe6e6ea);
  case C::PrivateMessage: return juce::Colour(0xffcf6fd8);
  case C::Action:         return juce::Colour(0xffd8c46a);
  case C::Voting:         return juce::Colour(0xffe0a030);
  case C::ServerNotice:
  default:                return juce::Colour(0xff9aa1ad);
  }
}

void AntiphonEditor::onChatMessage(const juce::String &type,
                                               const juce::String &username,
                                               const juce::String &text) {
  const auto line = ChatFormat::render(type, username, text,
                                       audioProcessor.ninjamClient.getSelfUsername());

  // juce::TextEditor keeps a colour per inserted run, so setting the colour
  // before each insert gives per-message colour with no new widget -- and a
  // read-only TextEditor stays the best primitive for reader navigation.
  chatDisplay.setColour(juce::TextEditor::textColourId,
                        colourForChatCategory(line.category));
  chatDisplay.moveCaretToEnd();
  chatDisplay.insertTextAtCaret(line.text + "\n");

  // The voting system talks through chat, so this is also where a vote is
  // noticed. A settled vote clears the chip rather than offering it again.
  const auto vote = ChatFormat::parseVote(text);
  if (vote.valid) {
    if (vote.settled) {
      pendingVote = {};
    } else if (!(vote.settled) &&
               !(vote.target == dismissedVoteTarget &&
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
  auto header = area.removeFromTop(80);

  const bool connected      = audioProcessor.ninjamClient.isConnected();
  const bool connectFailed  = audioProcessor.lastConnectFailed.load();
  const bool mismatch = !audioProcessor.isStandaloneApp() && connected &&
      std::abs(audioProcessor.hostBpm - (double)audioProcessor.internalBpm.load()) > 0.5;
  const bool headerWarning = connectFailed || mismatch;
  juce::Colour headerBg = connected      ? juce::Colour(0xff0d0d1a)   // navy -- normal
                        : headerWarning  ? juce::Colour(0xff2a1a0a)   // amber -- failed/mismatch
                                         : juce::Colour(0xff111111);  // dark grey -- idle
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
    g.drawFittedText(tempoText, row2, juce::Justification::centredLeft, 1);
  } else {
    g.setColour(juce::Colours::darkgrey);
    g.drawFittedText("Not connected", row2, juce::Justification::centredLeft, 1);
  }

  header.removeFromTop(4);

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
        "Phase: " +
            juce::String(audioProcessor.publishedPhaseBeats.load(), 2) + " / " +
            juce::String(audioProcessor.publishedActiveBpi.load()),
        row3, juce::Justification::centredLeft, 1);
  } else {
    // Driven by the sync state machine rather than re-derived here, so the text
    // and the audio gating can never disagree about what is happening.
    const auto sync = (SyncState::State)audioProcessor.publishedSyncState.load();
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
      g.drawFittedText("DAW: " + juce::String(audioProcessor.hostBpm, 1) + " BPM",
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
  g.drawFittedText("Local Channels:", labelRow.removeFromLeft(channelAreaLocalW),
                   juce::Justification::left, 1);
  labelRow.removeFromLeft(10);
  g.drawFittedText("Remote Players:", labelRow, juce::Justification::left, 1);
}

void AntiphonEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  // Covers the painted header exactly, so the spoken status and the drawn one
  // describe the same region of the window.
  const auto header = area.removeFromTop(80);
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
  browseButton.setBounds(toolbar.removeFromLeft(90).reduced(0, 4));
  toolbar.removeFromLeft(4);
  disconnectButton.setBounds(toolbar.removeFromLeft(82).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // View and debug toggles, from the right, in reverse visual order.
  chatToggle.setBounds(toolbar.removeFromRight(52).reduced(0, 4));
  toolbar.removeFromRight(8);
  testToneToggle.setBounds(toolbar.removeFromRight(80).reduced(0, 4));
  toolbar.removeFromRight(2);
  saveRxToggle.setBounds(toolbar.removeFromRight(68).reduced(0, 4));
  toolbar.removeFromRight(2);
  saveTxToggle.setBounds(toolbar.removeFromRight(68).reduced(0, 4));
  toolbar.removeFromRight(10);
  metronomeVolumeSlider.setBounds(toolbar.removeFromRight(60).reduced(0, 6));
  toolbar.removeFromRight(4);
  metronomeToggle.setBounds(toolbar.removeFromRight(90).reduced(0, 4));
  toolbar.removeFromRight(10);

  // The DAW-only run, continuing from the left. In the standalone the note
  // prefixes the whole run rather than trailing it -- a reason is only useful
  // before the thing it explains -- and the group labels use their short forms,
  // which costs nothing when the controls are inert and buys the room the note
  // needs. The full wording stays in each control's accessible name.
  // Gated on the condition itself, not on the label's visibility: resized() runs
  // during construction, before applyHostContextToControls() has made the note
  // visible, so asking the label would silently give it no bounds.
  const bool standalone = audioProcessor.isStandaloneApp();
  if (standalone) {
    dawOnlyNote.setBounds(toolbar.removeFromLeft(66));
    toolbar.removeFromLeft(4);
  }

  syncButton.setBounds(toolbar.removeFromLeft(52).reduced(0, 4));
  toolbar.removeFromLeft(10);

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

    roomMembersLabel.setBounds(rightPanel.removeFromTop(18));
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
  if (serverBrowser) return;

  serverBrowser = std::make_unique<ServerBrowserDialog>();

  serverBrowser->hostInput.setText(audioProcessor.lastHost, false);
  serverBrowser->portInput.setText(juce::String(audioProcessor.lastPort), false);
  serverBrowser->usernameInput.setText(audioProcessor.lastUsername, false);
  serverBrowser->passwordInput.setText(audioProcessor.lastPassword, false);
  serverBrowser->anonymousToggle.setToggleState(audioProcessor.lastAnonymous,
                                                juce::dontSendNotification);
  serverBrowser->passwordInput.setVisible(!audioProcessor.lastAnonymous);

  serverBrowser->onConnect = [this](const juce::String &host, int port,
                                    const juce::String &user,
                                    const juce::String &pass) {
    audioProcessor.lastHost      = host;
    audioProcessor.lastPort      = port;
    audioProcessor.lastUsername  = serverBrowser->usernameInput.getText().trim();
    audioProcessor.lastPassword  = serverBrowser->passwordInput.getText();
    audioProcessor.lastAnonymous = serverBrowser->anonymousToggle.getToggleState();
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
               [safeThis = juce::Component::SafePointer<AntiphonEditor>(this)](
                   int) {
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

void AntiphonEditor::mouseExit(const juce::MouseEvent &) {
  repaint();
}

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

bool AntiphonEditor::keyPressed(const juce::KeyPress &key,
                                juce::Component *) {
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
  const auto action = Shortcuts::match(key.getKeyCode(),
                                       key.getModifiers().isCtrlDown(),
                                       key.getModifiers().isAltDown());
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
      names.add(m.channelCount > 0
                    ? m.username
                    : m.username + " (no audio)");
    text += ": " + names.joinIntoString(", ");
  }

  // Only touched when it actually changed: this runs at 30 Hz, and setText on
  // a Label repaints.
  if (text == lastRoomMembersText)
    return;
  lastRoomMembersText = text;
  roomMembersLabel.setText(text, juce::dontSendNotification);
  roomMembersLabel.setDescription(text);
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

  // Otherwise: the DAW is at a different tempo from the server. This only
  // offers the vote -- changing your DAW tempo never casts one.
  const int serverBpm = audioProcessor.publishedActiveBpm.load();
  const int hostBpm = (int)std::lround(audioProcessor.hostBpm);
  const bool worthProposing = !audioProcessor.isStandaloneApp() &&
                              hostBpm > 0 && serverBpm > 0 &&
                              hostBpm != serverBpm && hostBpm != dismissedDawBpm;
  if (worthProposing) {
    chipDawBpm = hostBpm;
    const juce::String t =
        "Your DAW is at " + juce::String(hostBpm) + " BPM";
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
  const juce::Colour bg =
      connected ? juce::Colour(0xff0a0a0a)
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
      connected ? "Message, or a command: /bpm 120, /bpi 16, /topic text, /msg user text"
                : "Not connected -- join a server to chat",
      juce::Colour(connected ? 0xff8a8a8a : AntiphonTheme::kDisabledText));
  chatInput.repaint();
}

void AntiphonEditor::onConnected() {
  chatDisplay.clear();
  setChatConnectedState(true);
}

void AntiphonEditor::onDisconnected(const juce::String &) {
  setChatConnectedState(false);
}

void AntiphonEditor::updateToolbarStates() {
  const bool connected = audioProcessor.ninjamClient.isConnected();

  browseButton.setEnabled(!connected);
  disconnectButton.setEnabled(connected);

  // Everything below is DAW-only and was settled once by
  // applyHostContextToControls(). Re-enabling it here every timer tick would
  // quietly undo that.
  if (audioProcessor.isStandaloneApp())
    return;

  const int inBuses  = audioProcessor.getBusCount(true);
  const int outBuses = audioProcessor.getBusCount(false);

  const auto sync = (SyncState::State)audioProcessor.publishedSyncState.load();
  syncButton.setEnabled(sync == SyncState::State::ReadyToSync ||
                        sync == SyncState::State::WaitingForPlay ||
                        sync == SyncState::State::Running);

  removeInBusButton.setEnabled(inBuses > 1);
  removeOutBusButton.setEnabled(outBuses > 1);
}

void AntiphonEditor::relayoutChannelArea() {
  if (cachedChannelPanelBounds.isEmpty()) return;

  auto bounds = cachedChannelPanelBounds;
  const int gap = 10;
  const int panel_w = bounds.getWidth() - gap;

  // Content needs
  int numLocal = localChannelStrips.size();
  int local_need = numLocal > 0 ? numLocal * 94 - 4 : 0;

  int remote_need = 0;
  for (auto *s : remoteUserStrips) remote_need += s->getPreferredWidth() + 8;
  if (!remoteUserStrips.isEmpty()) remote_need -= 8;

  // Hard allocations: 40/60 split with 200 px floor, capped at half
  int local_hard = juce::jlimit(std::min(200, panel_w / 2),
                                panel_w / 2,
                                (int)(panel_w * 0.4f));
  int remote_hard = panel_w - local_hard;

  // Elastic four-case split
  int local_w, remote_w;
  if (local_need <= local_hard && remote_need <= remote_hard) {
    local_w  = local_hard;
    remote_w = remote_hard;
  } else if (local_need > local_hard && remote_need <= remote_hard) {
    remote_w = remote_need;
    local_w  = panel_w - remote_w;
  } else if (remote_need > remote_hard && local_need <= local_hard) {
    local_w  = local_need;
    remote_w = panel_w - local_w;
  } else {
    local_w  = local_hard;
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
  localChannelsContainer.setSize(
      std::max(x, localChannelsViewport.getWidth()), h);

  // Position remote strips: right-flush within container
  int rh = remoteUsersViewport.getHeight();
  int total_remote = 0;
  for (auto *s : remoteUserStrips) total_remote += s->getPreferredWidth() + 8;
  if (!remoteUserStrips.isEmpty()) total_remote -= 8;

  int container_w = std::max(total_remote, remoteUsersViewport.getWidth());
  int right_offset = container_w - total_remote;

  int rx = 0;
  for (auto *s : remoteUserStrips) {
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
    if (f > 0.0f) v.store(std::max(0.0f, f - (1.0f / 12.0f)));
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
      auto *strip = new LocalChannelStrip(audioProcessor,
                                          audioProcessor.localChannels[idx]);
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
        if (s->getName() == username) { strip = s; break; }
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
      localChannelStrips.size() * 1000 + remoteUserStrips.size();
  if (layoutKey != lastLayoutKey) {
    lastLayoutKey = layoutKey;
    relayoutChannelArea();
  }
}
