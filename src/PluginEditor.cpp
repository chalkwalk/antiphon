#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ServerBrowserDialog.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  audioProcessor.ninjamClient.addListener(this);
  setLookAndFeel(&customLookAndFeel);
  setResizable(true, true);
  setResizeLimits(900, 600, 2400, 1600);
  setSize(1000, 700);

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

  localVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  localVolumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  localVolumeSlider.setRange(0.0, 2.0, 0.01);
  localVolumeSlider.setValue(audioProcessor.localTxVolume,
                             juce::dontSendNotification);
  localVolumeSlider.onValueChange = [this]() {
    audioProcessor.localTxVolume = (float)localVolumeSlider.getValue();
  };
  addAndMakeVisible(localVolumeSlider);

  localPanSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  localPanSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  localPanSlider.setRange(-1.0, 1.0, 0.01);
  localPanSlider.setValue(audioProcessor.localTxPan,
                          juce::dontSendNotification);
  localPanSlider.onValueChange = [this]() {
    audioProcessor.localTxPan = (float)localPanSlider.getValue();
  };
  addAndMakeVisible(localPanSlider);

  localMuteButton.setButtonText("Local Mute");
  localMuteButton.setToggleState(audioProcessor.localTxMute,
                                 juce::dontSendNotification);
  localMuteButton.onClick = [this]() {
    audioProcessor.localTxMute = localMuteButton.getToggleState();
  };
  addAndMakeVisible(localMuteButton);

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

  // Restore existing chat log
  for (const auto &msg : audioProcessor.ninjamClient.getChatLog()) {
    onChatMessage(msg.type, msg.username, msg.text);
  }

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
    // JOIN, PART, TOPIC, etc.
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

  // Status bar background
  g.setColour(juce::Colour(0xff0d0d1a));
  g.fillRect(header);

  const bool connected = audioProcessor.ninjamClient.isConnected();
  const juce::Colour teal(0xff00b4d8);

  // Row 1: title + connection status
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

  // Row 2: server BPM / BPI
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
    g.drawFittedText("Not connected", row2, juce::Justification::centredLeft,
                     1);
  }

  header.removeFromTop(4);

  // Phase progress bar
  auto phaseBar = header.removeFromTop(8);
  g.setColour(juce::Colour(0xff1a1a2e));
  g.fillRect(phaseBar);
  if (connected && audioProcessor.internalBpi.load() > 0) {
    int bpi = audioProcessor.internalBpi.load();
    float frac = juce::jlimit(
        0.0f, 1.0f,
        (float)(audioProcessor.internalPhaseBeats / bpi));
    int barW = phaseBar.getWidth();
    int barX = phaseBar.getX();
    int barY = phaseBar.getY();
    int barH = phaseBar.getHeight();

    // Teal fill
    g.setColour(teal);
    g.fillRect(phaseBar.withWidth((int)(barW * frac)));

    // Beat and bar gridlines
    for (int i = 1; i < bpi; ++i) {
      int lineX = barX + (int)((float)i / bpi * barW);
      bool isBarBoundary = (i % 4 == 0);
      g.setColour(isBarBoundary ? juce::Colours::white.withAlpha(0.55f)
                                : juce::Colours::white.withAlpha(0.18f));
      int lineH = isBarBoundary ? barH : barH - 2;
      int lineY = barY + (barH - lineH) / 2;
      g.drawVerticalLine(lineX, (float)lineY, (float)(lineY + lineH));
    }

    // Flash overlays -- drawn last so they sit on top of fill and gridlines
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

  // Row 3: sync state
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
  const bool pendingTransport =
      connected && !mismatch && !audioProcessor.hostIsPlaying;
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
    g.drawFittedText("DAW: " + juce::String(audioProcessor.hostBpm, 1) +
                         " BPM",
                     row3, juce::Justification::centredLeft, 1);
  }
#endif

  area.removeFromTop(10); // spacing
  area.removeFromTop(38); // space for connect/disconnect buttons in resized()

  // Main Panels
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions{}.withHeight(14.0f));

  auto leftPanel = area.removeFromLeft(350);
  g.drawFittedText("Chat & Commands:", leftPanel.removeFromTop(20),
                   juce::Justification::left, 1);

  area.removeFromLeft(20); // gap

  auto rightPanel = area;
  g.drawFittedText("Local Transmit:", rightPanel.removeFromTop(20),
                   juce::Justification::left, 1);
  rightPanel.removeFromTop(40); // local mixer sliders

  // Local TX VU bars (L and R)
  auto drawVuBar = [&](juce::Rectangle<int> bounds, float peak) {
    g.setColour(juce::Colour(0xff111122));
    g.fillRect(bounds);
    float frac = juce::jlimit(0.0f, 1.0f, peak);
    if (frac > 0.0f) {
      auto fill = bounds.removeFromBottom((int)(bounds.getHeight() * frac));
      g.setColour(frac > 0.7f ? juce::Colour(0xffe08000) : juce::Colour(0xff00c040));
      g.fillRect(fill);
    }
  };
  drawVuBar(localVuBoundsL, localDecayedPeakL);
  drawVuBar(localVuBoundsR, localDecayedPeakR);

  rightPanel.removeFromTop(10);
  g.drawFittedText("Remote Users:", rightPanel.removeFromTop(20),
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

  area.removeFromTop(10); // Spacing

  // Left Panel (Chat)
  auto leftPanel = area.removeFromLeft(350);
  leftPanel.removeFromTop(20); // "Chat & Commands" label
  chatInput.setBounds(leftPanel.removeFromBottom(24));
  leftPanel.removeFromBottom(10); // gap
  chatDisplay.setBounds(leftPanel);

  area.removeFromLeft(20); // Center gap

  // Right Panel (Mixer)
  auto rightPanel = area;
  rightPanel.removeFromTop(20); // "Local Transmit" label

  auto localMixerRow = rightPanel.removeFromTop(40);
  localVolumeSlider.setBounds(localMixerRow.removeFromLeft(120).reduced(0, 10));
  localMixerRow.removeFromLeft(10);
  localPanSlider.setBounds(localMixerRow.removeFromLeft(100).reduced(0, 10));
  localMixerRow.removeFromLeft(10);
  localMuteButton.setBounds(localMixerRow.removeFromLeft(90).reduced(0, 10));
  localMixerRow.removeFromLeft(8);
  localVuBoundsL = localMixerRow.removeFromLeft(8).reduced(0, 6);
  localMixerRow.removeFromLeft(2);
  localVuBoundsR = localMixerRow.removeFromLeft(8).reduced(0, 6);

  rightPanel.removeFromTop(30); // "Remote Users" label plus gap

  auto toggleRow = rightPanel.removeFromBottom(40);
  metronomeToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));
  saveTxToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));
  saveRxToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));

  remoteUsersViewport.setBounds(rightPanel);
}

void NinjamAudioProcessorEditor::openServerBrowser() {
  if (serverBrowser) return; // already open

  serverBrowser = std::make_unique<ServerBrowserDialog>();

  // Pre-populate from saved state
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
  // Decay flash intensities (message thread; audio thread only writes 1.0f)
  auto decay = [](std::atomic<float> &v) {
    float f = v.load();
    if (f > 0.0f) v.store(std::max(0.0f, f - (1.0f / 12.0f)));
  };
  decay(audioProcessor.intervalFlashIntensity);
  decay(audioProcessor.beatFlashIntensity);

  // Decay local TX peaks (read atomics, apply decay, store back for paint())
  localDecayedPeakL = std::max(localDecayedPeakL * 0.92f, audioProcessor.localTxPeakL.load());
  localDecayedPeakR = std::max(localDecayedPeakR * 0.92f, audioProcessor.localTxPeakR.load());

  repaint();

  if (audioProcessor.ninjamClient.isConnected()) {
    auto users = audioProcessor.ninjamClient.getRemoteUsers();

    // Check for removals
    for (int i = remoteUserStrips.size() - 1; i >= 0; --i) {
      if (users.find(remoteUserStrips[i]->getName()) == users.end()) {
        remoteUserStrips.remove(i);
      }
    }

    // Check for additions/updates
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

    // Layout
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
