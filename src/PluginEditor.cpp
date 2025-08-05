#include "PluginEditor.h"
#include "PluginProcessor.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  audioProcessor.ninjamClient.addListener(this);
  setLookAndFeel(&customLookAndFeel);
  setResizable(true, true);
  setResizeLimits(900, 600, 2400, 1600);
  setSize(1000, 700);

  serverInput.setText("ninbot.com");
  addAndMakeVisible(serverInput);

  usernameInput.setText("anonymous");
  addAndMakeVisible(usernameInput);

  connectButton.setButtonText("Connect");
  connectButton.onClick = [this]() {
    audioProcessor.ninjamClient.connectToServer(serverInput.getText(), 2049,
                                                usernameInput.getText(), "");
  };
  addAndMakeVisible(connectButton);

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

  // Top Header Row
  auto headerRow = area.removeFromTop(80);

  g.setColour(juce::Colours::white);
  g.setFont(24.0f);
  g.drawFittedText("Ninjam JUCE Plugin", headerRow.removeFromTop(30),
                   juce::Justification::centred, 1);

  g.setFont(16.0f);
  juce::String connInfo = "Connection: " + audioProcessor.connectionStatus;
  g.drawFittedText(connInfo, headerRow.removeFromTop(20),
                   juce::Justification::left, 1);

  juce::String hostInfo = "Host Sync: ";
  hostInfo += audioProcessor.hostIsPlaying ? "Playing" : "Stopped";
  hostInfo += " | BPM: " + juce::String(audioProcessor.hostBpm, 1);
  hostInfo += " | PPQ: " + juce::String(audioProcessor.hostPpqPosition, 2);
  g.drawFittedText(hostInfo, headerRow.removeFromTop(20),
                   juce::Justification::left, 1);

  area.removeFromTop(10); // spacing

  // Sync Info Row
  auto syncRow = area.removeFromTop(30);
  juce::String internalInfo = "Metronome: ";
  internalInfo += "BPM: " + juce::String(audioProcessor.internalBpm, 1);
  internalInfo += " | BPI: " + juce::String(audioProcessor.internalBpi);
  internalInfo +=
      " | Phase: " + juce::String(audioProcessor.internalPhaseBeats, 2);
  g.drawFittedText(internalInfo, syncRow.removeFromLeft(350),
                   juce::Justification::left, 1);

  if (std::abs(audioProcessor.hostBpm - audioProcessor.internalBpm) > 0.1) {
    g.setColour(juce::Colours::red);
    g.drawFittedText("WARNING: Host BPM != Server BPM!",
                     syncRow.removeFromLeft(300), juce::Justification::left, 1);
  }

  area.removeFromTop(
      50); // space for the connection inputs and toggles mapped in resized()

  // Main Panels
  g.setColour(juce::Colours::white);
  g.setFont(14.0f);

  auto leftPanel = area.removeFromLeft(350);
  g.drawFittedText("Chat & Commands:", leftPanel.removeFromTop(20),
                   juce::Justification::left, 1);

  area.removeFromLeft(20); // gap

  auto rightPanel = area;
  g.drawFittedText("Local Transmit:", rightPanel.removeFromTop(20),
                   juce::Justification::left, 1);
  rightPanel.removeFromTop(50); // local mixer sliders
  g.drawFittedText("Remote Users:", rightPanel.removeFromTop(20),
                   juce::Justification::left, 1);
}

void NinjamAudioProcessorEditor::resized() {
  auto area = getLocalBounds().reduced(10);
  area.removeFromTop(80); // Title and host sync
  area.removeFromTop(10); // Spacing

  auto syncRow = area.removeFromTop(30);
  syncRow.removeFromLeft(350); // Metronome text
  syncRow.removeFromLeft(300); // Warning text

  auto controlsRow = area.removeFromTop(40);
  serverInput.setBounds(controlsRow.removeFromLeft(150).reduced(0, 8));
  controlsRow.removeFromLeft(10);
  usernameInput.setBounds(controlsRow.removeFromLeft(120).reduced(0, 8));
  controlsRow.removeFromLeft(10);
  connectButton.setBounds(controlsRow.removeFromLeft(100).reduced(0, 8));
  controlsRow.removeFromLeft(10);
  disconnectButton.setBounds(controlsRow.removeFromLeft(100).reduced(0, 8));

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

  rightPanel.removeFromTop(30); // "Remote Users" label plus gap

  auto toggleRow = rightPanel.removeFromBottom(40);
  metronomeToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));
  saveTxToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));
  saveRxToggle.setBounds(toggleRow.removeFromLeft(120).reduced(0, 8));

  remoteUsersViewport.setBounds(rightPanel);
}

void NinjamAudioProcessorEditor::timerCallback() {
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
