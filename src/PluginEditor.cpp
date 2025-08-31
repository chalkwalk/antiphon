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
  addAndMakeVisible(browseButton);

  disconnectButton.setButtonText("Disconnect");
  disconnectButton.onClick = [this]() {
    audioProcessor.ninjamClient.disconnectFromServer();
  };
  addAndMakeVisible(disconnectButton);

  metronomeToggle.setButtonText("Metronome");
  metronomeToggle.setToggleState(audioProcessor.metronomeEnabled,
                                 juce::dontSendNotification);
  metronomeToggle.onClick = [this]() {
    audioProcessor.metronomeEnabled = metronomeToggle.getToggleState();
  };
  addAndMakeVisible(metronomeToggle);

  saveTxToggle.setButtonText("Save Tx Audio");
  saveTxToggle.setToggleState(audioProcessor.saveTxEnabled,
                              juce::dontSendNotification);
  saveTxToggle.onClick = [this]() {
    audioProcessor.saveTxEnabled = saveTxToggle.getToggleState();
  };
  addAndMakeVisible(saveTxToggle);

  saveRxToggle.setButtonText("Save Rx Audio");
  saveRxToggle.setToggleState(audioProcessor.saveRxEnabled,
                              juce::dontSendNotification);
  saveRxToggle.onClick = [this]() {
    audioProcessor.saveRxEnabled = saveRxToggle.getToggleState();
  };
  addAndMakeVisible(saveRxToggle);

  addChannelButton.setButtonText("+ Channel");
  addChannelButton.onClick = [this]() {
    audioProcessor.addLocalChannel();
    // prepareToPlay will be called by the host; timerCallback will sync strips
  };
  addAndMakeVisible(addChannelButton);

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

  // Section labels
  area.removeFromTop(10);
  area.removeFromTop(28); // controls row
  area.removeFromTop(10);

  auto leftPanel = area.removeFromLeft(350);
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions{}.withHeight(14.0f));
  g.drawFittedText("Chat & Commands:", leftPanel.removeFromTop(20),
                   juce::Justification::left, 1);

  area.removeFromLeft(10);

  auto midPanel = area.removeFromLeft(430);
  g.drawFittedText("Local Channels:", midPanel.removeFromTop(20),
                   juce::Justification::left, 1);

  area.removeFromLeft(10);

  g.drawFittedText("Remote Users:", area.removeFromTop(20),
                   juce::Justification::left, 1);
}

void NinjamAudioProcessorEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  area.removeFromTop(80); // Status bar
  area.removeFromTop(10); // Spacing

  auto controlsRow = area.removeFromTop(28);
  browseButton.setBounds(controlsRow.removeFromLeft(120).reduced(0, 4));
  controlsRow.removeFromLeft(8);
  disconnectButton.setBounds(controlsRow.removeFromLeft(100).reduced(0, 4));
  controlsRow.removeFromLeft(8);
  addChannelButton.setBounds(controlsRow.removeFromLeft(100).reduced(0, 4));

  area.removeFromTop(10);

  // Left panel: Chat
  auto leftPanel = area.removeFromLeft(350);
  leftPanel.removeFromTop(20); // label
  chatInput.setBounds(leftPanel.removeFromBottom(24));
  leftPanel.removeFromBottom(10);
  chatDisplay.setBounds(leftPanel);

  area.removeFromLeft(10);

  // Middle panel: Local channel strips
  auto midPanel = area.removeFromLeft(430);
  midPanel.removeFromTop(20); // label

  auto toggleRow = midPanel.removeFromBottom(30);
  metronomeToggle.setBounds(toggleRow.removeFromLeft(110).reduced(0, 4));
  saveTxToggle.setBounds(toggleRow.removeFromLeft(110).reduced(0, 4));
  saveRxToggle.setBounds(toggleRow.removeFromLeft(110).reduced(0, 4));

  localChannelsViewport.setBounds(midPanel);

  area.removeFromLeft(10);

  // Right panel: Remote users
  area.removeFromTop(20); // label
  remoteUsersViewport.setBounds(area);
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

void NinjamAudioProcessorEditor::timerCallback() {
  auto decay = [](std::atomic<float> &v) {
    float f = v.load();
    if (f > 0.0f) v.store(std::max(0.0f, f - (1.0f / 12.0f)));
  };
  decay(audioProcessor.intervalFlashIntensity);
  decay(audioProcessor.beatFlashIntensity);

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

    // Update peaks and layout
    int stripH = 44;
    int y = 0;
    for (auto *s : localChannelStrips) {
      s->updatePeaks();
      s->setBounds(0, y, localChannelsViewport.getWidth() - 20, stripH);
      y += stripH + 4;
    }
    localChannelsContainer.setSize(
        localChannelsViewport.getWidth() - 20,
        std::max(y, localChannelsViewport.getHeight()));
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

    int y = 0;
    int stripHeight = 60;
    for (auto *s : remoteUserStrips) {
      s->setBounds(0, y, remoteUsersViewport.getWidth() - 20, stripHeight);
      y += stripHeight + 5;
    }
    remoteUsersContainer.setSize(remoteUsersViewport.getWidth() - 20,
                                 std::max(y, remoteUsersViewport.getHeight()));
  } else if (!remoteUserStrips.isEmpty()) {
    remoteUserStrips.clear();
    remoteUsersContainer.setSize(remoteUsersViewport.getWidth() - 20,
                                 remoteUsersViewport.getHeight());
  }
}
