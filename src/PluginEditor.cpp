#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ServerBrowserDialog.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  audioProcessor.ninjamClient.addListener(this);
  setLookAndFeel(&customLookAndFeel);
  setResizable(true, true);
  setResizeLimits(900, 600, 2400, 1600);
  setSize(1100, 700);

  browseButton.setButtonText("Connect...");
  browseButton.onClick = [this]() { openServerBrowser(); };
  browseButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(browseButton);

  disconnectButton.setButtonText("Disconnect");
  disconnectButton.onClick = [this]() {
    audioProcessor.ninjamClient.disconnectFromServer();
  };
  disconnectButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(disconnectButton);

  metronomeToggle.setButtonText("Metronome");
  metronomeToggle.setToggleState(audioProcessor.metronomeEnabled,
                                 juce::dontSendNotification);
  metronomeToggle.onClick = [this]() {
    audioProcessor.metronomeEnabled = metronomeToggle.getToggleState();
  };
  metronomeToggle.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(metronomeToggle);

  saveTxToggle.setButtonText("Save Tx Audio");
  saveTxToggle.setToggleState(audioProcessor.saveTxEnabled,
                              juce::dontSendNotification);
  saveTxToggle.onClick = [this]() {
    audioProcessor.saveTxEnabled = saveTxToggle.getToggleState();
  };
  saveTxToggle.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(saveTxToggle);

  saveRxToggle.setButtonText("Save Rx Audio");
  saveRxToggle.setToggleState(audioProcessor.saveRxEnabled,
                              juce::dontSendNotification);
  saveRxToggle.onClick = [this]() {
    audioProcessor.saveRxEnabled = saveRxToggle.getToggleState();
  };
  saveRxToggle.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(saveRxToggle);

  // Compact toolbar groups
  channelGroupLabel.setText("Channel:", juce::dontSendNotification);
  channelGroupLabel.setJustificationType(juce::Justification::centredRight);
  channelGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(channelGroupLabel);

  addChannelButton.setButtonText("+");
  addChannelButton.setTooltip("Add a local channel strip (routes to Input Bus 1 by default)");
  addChannelButton.onClick = [this]() { audioProcessor.addLocalChannel(); };
  addChannelButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(addChannelButton);

  inputBusGroupLabel.setText("Input bus:", juce::dontSendNotification);
  inputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  inputBusGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(inputBusGroupLabel);

  addInBusButton.setButtonText("+");
  addInBusButton.setTooltip("Add a new stereo input bus (for DAW to route another track into)");
  addInBusButton.onClick = [this]() { audioProcessor.addInputBus(); };
  addInBusButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(addInBusButton);

  removeInBusButton.setButtonText("-");
  removeInBusButton.setTooltip("Remove the last input bus (channels using it revert to bus 1)");
  removeInBusButton.onClick = [this]() { audioProcessor.removeLastInputBus(); };
  removeInBusButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(removeInBusButton);

  outputBusGroupLabel.setText("Output bus:", juce::dontSendNotification);
  outputBusGroupLabel.setJustificationType(juce::Justification::centredRight);
  outputBusGroupLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
  addAndMakeVisible(outputBusGroupLabel);

  addOutBusButton.setButtonText("+");
  addOutBusButton.setTooltip("Add a new stereo output bus (for DAW stem recording)");
  addOutBusButton.onClick = [this]() { audioProcessor.addOutputBus(); };
  addOutBusButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(addOutBusButton);

  removeOutBusButton.setButtonText("-");
  removeOutBusButton.setTooltip("Remove the last output bus (remote channels using it revert to bus 1)");
  removeOutBusButton.onClick = [this]() { audioProcessor.removeLastOutputBus(); };
  removeOutBusButton.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(removeOutBusButton);

  metronomeToggle.setRepaintsOnMouseActivity(true);

  metronomeVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  metronomeVolumeSlider.setRange(0.0, 1.0);
  metronomeVolumeSlider.setValue(audioProcessor.metronomeVolume.load(),
                                 juce::dontSendNotification);
  metronomeVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  metronomeVolumeSlider.setTooltip("Metronome volume");
  metronomeVolumeSlider.onValueChange = [this]() {
    audioProcessor.metronomeVolume.store((float)metronomeVolumeSlider.getValue());
  };
  addAndMakeVisible(metronomeVolumeSlider);

  chatToggle.setButtonText("Chat");
  chatToggle.setToggleState(audioProcessor.chatVisible.load(),
                            juce::dontSendNotification);
  chatToggle.onClick = [this]() {
    audioProcessor.chatVisible.store(chatToggle.getToggleState());
    resized();
  };
  chatToggle.setRepaintsOnMouseActivity(true);
  addAndMakeVisible(chatToggle);

  localChannelsViewport.setViewedComponent(&localChannelsContainer, false);
  addAndMakeVisible(localChannelsViewport);

  chatDisplay.setMultiLine(true);
  chatDisplay.setReadOnly(true);
  chatDisplay.setScrollbarsShown(true);
  chatDisplay.setCaretVisible(false);
  chatDisplay.setColour(juce::TextEditor::backgroundColourId,
                        juce::Colour(0xff121212));
  addAndMakeVisible(chatDisplay);

  chatInput.setMultiLine(false);
  chatInput.setReturnKeyStartsNewLine(false);
  chatInput.setTextToShowWhenEmpty(
      "Enter message or command (!vote bpm 120, /msg user text, /topic text)",
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
                 text.startsWithIgnoreCase("/kick ")) {
        audioProcessor.ninjamClient.sendAdminCommand(text.substring(1));
      } else if (text.startsWithIgnoreCase("/msg ")) {
        int firstSpace = text.indexOfChar(5, ' ');
        if (firstSpace > 0) {
          juce::String user = text.substring(5, firstSpace).trim();
          juce::String msg = text.substring(firstSpace).trim();
          audioProcessor.ninjamClient.sendPrivateMessage(user, msg);
        }
      } else if (text.startsWithChar('/')) {
        chatDisplay.insertTextAtCaret("Local: Unknown command.\n");
      } else {
        audioProcessor.ninjamClient.sendChatMessage(text);
      }
      chatInput.clear();
    }
  };
  addAndMakeVisible(chatInput);

  remoteUsersViewport.setViewedComponent(&remoteUsersContainer, false);
  addAndMakeVisible(remoteUsersViewport);

  for (const auto &msg : audioProcessor.ninjamClient.getChatLog())
    onChatMessage(msg.type, msg.username, msg.text);

  startTimerHz(30);
}

NinjamAudioProcessorEditor::~NinjamAudioProcessorEditor() {
  audioProcessor.ninjamClient.removeListener(this);
  setLookAndFeel(nullptr);
}

void NinjamAudioProcessorEditor::onChatMessage(const juce::String &type,
                                               const juce::String &username,
                                               const juce::String &text) {
  juce::String line;
  if (type == "PRIVMSG") {
    line = "[PM] <" + username + "> " + text;
  } else if (type == "MSG") {
    if (text.startsWithIgnoreCase("/me ")) {
      line = "* " + username + " " + text.substring(4);
    } else {
      line = "<" + username + "> " + text;
    }
  } else {
    line = "*** " + text;
  }
  chatDisplay.moveCaretToEnd();
  chatDisplay.insertTextAtCaret(line + "\n");
}

void NinjamAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  auto area = getLocalBounds().reduced(10);
  auto header = area.removeFromTop(80);

  g.setColour(juce::Colour(0xff0d0d1a));
  g.fillRect(header);

  const bool connected = audioProcessor.ninjamClient.isConnected();
  const juce::Colour teal(0xff00b4d8);

  auto row1 = header.removeFromTop(22);
  g.setFont(juce::FontOptions{}.withHeight(15.0f).withStyle("Bold"));
  g.setColour(teal);
  g.drawFittedText("NINJAM", row1.removeFromLeft(90),
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
    g.drawFittedText("Server:  " +
                         juce::String((double)audioProcessor.internalBpm.load(), 1) +
                         " BPM   " +
                         juce::String(audioProcessor.internalBpi.load()) + " BPI",
                     row2, juce::Justification::centredLeft, 1);
  } else {
    g.setColour(juce::Colours::darkgrey);
    g.drawFittedText("Not connected", row2, juce::Justification::centredLeft, 1);
  }

  header.removeFromTop(4);

  auto phaseBar = header.removeFromTop(8);
  g.setColour(juce::Colour(0xff1a1a2e));
  g.fillRect(phaseBar);
  if (connected && audioProcessor.internalBpi.load() > 0) {
    int bpi = audioProcessor.internalBpi.load();
    float frac = juce::jlimit(
        0.0f, 1.0f, (float)(audioProcessor.internalPhaseBeats / bpi));
    int barW = phaseBar.getWidth();
    int barX = phaseBar.getX();
    int barY = phaseBar.getY();
    int barH = phaseBar.getHeight();

    g.setColour(teal);
    g.fillRect(phaseBar.withWidth((int)(barW * frac)));

    for (int i = 1; i < bpi; ++i) {
      int lineX = barX + (int)((float)i / bpi * barW);
      bool isBarBoundary = (i % 4 == 0);
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
      float alpha = (flashBeat % 4 == 0) ? 0.50f : 0.25f;
      g.setColour(juce::Colours::white.withAlpha(bFlash * alpha));
      g.fillRect(phaseBar);
    }
  }

  header.removeFromTop(4);

  auto row3 = header.removeFromTop(20);
  g.setFont(juce::FontOptions{}.withHeight(13.0f));
#if JucePlugin_Build_Standalone
  g.setColour(juce::Colours::lightgrey);
  g.drawFittedText(
      "Phase: " + juce::String(audioProcessor.internalPhaseBeats, 2) + " / " +
          juce::String(audioProcessor.internalBpi.load()),
      row3, juce::Justification::centredLeft, 1);
#else
  const bool mismatch =
      connected &&
      std::abs(audioProcessor.hostBpm - (double)audioProcessor.internalBpm.load()) > 0.5;
  const bool pendingTransport = connected && !mismatch && !audioProcessor.hostIsPlaying;
  if (mismatch) {
    g.setColour(juce::Colours::orange);
    g.drawFittedText("BPM mismatch - set DAW to " +
                         juce::String((double)audioProcessor.internalBpm.load(), 1) + " BPM",
                     row3, juce::Justification::centredLeft, 1);
  } else if (pendingTransport) {
    g.setColour(juce::Colours::grey);
    g.drawFittedText("Start transport to begin", row3,
                     juce::Justification::centredLeft, 1);
  } else if (connected) {
    g.setColour(juce::Colours::lightgreen);
    g.drawFittedText("In sync", row3, juce::Justification::centredLeft, 1);
  } else {
    g.setColour(juce::Colours::darkgrey);
    g.drawFittedText("DAW: " + juce::String(audioProcessor.hostBpm, 1) + " BPM",
                     row3, juce::Justification::centredLeft, 1);
  }
#endif

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

void NinjamAudioProcessorEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  area.removeFromTop(80); // Status bar
  area.removeFromTop(10); // Spacing below status bar

  // Bottom toolbar
  auto toolbar = area.removeFromBottom(28);
  area.removeFromBottom(10); // spacer above toolbar

  browseButton.setBounds(toolbar.removeFromLeft(90).reduced(0, 4));
  toolbar.removeFromLeft(4);
  disconnectButton.setBounds(toolbar.removeFromLeft(82).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // Channel: [+]
  channelGroupLabel.setBounds(toolbar.removeFromLeft(62));
  toolbar.removeFromLeft(2);
  addChannelButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // Input bus: [+] [-]
  inputBusGroupLabel.setBounds(toolbar.removeFromLeft(70));
  toolbar.removeFromLeft(2);
  addInBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(2);
  removeInBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(10);

  // Output bus: [+] [-]
  outputBusGroupLabel.setBounds(toolbar.removeFromLeft(76));
  toolbar.removeFromLeft(2);
  addOutBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(2);
  removeOutBusButton.setBounds(toolbar.removeFromLeft(22).reduced(0, 4));
  toolbar.removeFromLeft(10);

  metronomeToggle.setBounds(toolbar.removeFromLeft(82).reduced(0, 4));
  toolbar.removeFromLeft(4);
  metronomeVolumeSlider.setBounds(toolbar.removeFromLeft(60).reduced(0, 6));
  toolbar.removeFromLeft(10);
  saveTxToggle.setBounds(toolbar.removeFromLeft(60).reduced(0, 4));
  toolbar.removeFromLeft(2);
  saveRxToggle.setBounds(toolbar.removeFromLeft(60).reduced(0, 4));
  toolbar.removeFromLeft(8);
  chatToggle.setBounds(toolbar.removeFromLeft(46).reduced(0, 4));

  // Section labels row
  area.removeFromTop(20);

  // Right panel: chat (conditionally visible) -- remove before elastic split
  bool showChat = audioProcessor.chatVisible.load();
  if (showChat) {
    auto rightPanel = area.removeFromRight(320);
    area.removeFromRight(10);
    chatInput.setBounds(rightPanel.removeFromBottom(24));
    rightPanel.removeFromBottom(10);
    chatDisplay.setBounds(rightPanel);
    chatDisplay.setVisible(true);
    chatInput.setVisible(true);
  } else {
    chatDisplay.setVisible(false);
    chatInput.setVisible(false);
  }

  // Elastic local/remote split
  cachedChannelPanelBounds = area;
  relayoutChannelArea();
}

void NinjamAudioProcessorEditor::openServerBrowser() {
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
    audioProcessor.ninjamClient.connectToServer(host, port, user, pass);
  };
  serverBrowser->onClose = [this]() { closeServerBrowser(); };

  addAndMakeVisible(*serverBrowser);
  serverBrowser->setBounds(getLocalBounds().reduced(20));
  serverBrowser->toFront(false);
}

void NinjamAudioProcessorEditor::closeServerBrowser() {
  if (serverBrowser) {
    removeChildComponent(serverBrowser.get());
    serverBrowser.reset();
  }
}

void NinjamAudioProcessorEditor::mouseExit(const juce::MouseEvent &) {
  repaint();
}

void NinjamAudioProcessorEditor::relayoutChannelArea() {
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

void NinjamAudioProcessorEditor::timerCallback() {
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

  repaint();

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

  relayoutChannelArea();
}
