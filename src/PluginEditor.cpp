#include "PluginEditor.h"
#include "PluginProcessor.h"

NinjamAudioProcessorEditor::NinjamAudioProcessorEditor(NinjamAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setSize(800, 600);

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

  remoteUsersViewport.setViewedComponent(&remoteUsersContainer, false);
  addAndMakeVisible(remoteUsersViewport);

  startTimerHz(30);
}

NinjamAudioProcessorEditor::~NinjamAudioProcessorEditor() {}

void NinjamAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  g.setFont(24.0f);
  g.drawFittedText("Ninjam JUCE Plugin", 0, 10, getWidth(), 30,
                   juce::Justification::centred, 1);

  g.setFont(16.0f);
  juce::String connInfo = "Connection: " + audioProcessor.connectionStatus;
  g.drawFittedText(connInfo, 10, 50, getWidth() - 20, 30,
                   juce::Justification::left, 1);

  juce::String hostInfo = "Host Sync: ";
  hostInfo += audioProcessor.hostIsPlaying ? "Playing" : "Stopped";
  hostInfo += " | BPM: " + juce::String(audioProcessor.hostBpm, 1);
  hostInfo += " | PPQ: " + juce::String(audioProcessor.hostPpqPosition, 2);
  g.drawFittedText(hostInfo, 10, 80, getWidth() - 20, 30,
                   juce::Justification::left, 1);

  juce::String internalInfo = "Metronome: ";
  internalInfo += "BPM: " + juce::String(audioProcessor.internalBpm, 1);
  internalInfo += " | BPI: " + juce::String(audioProcessor.internalBpi);
  internalInfo +=
      " | Phase: " + juce::String(audioProcessor.internalPhaseBeats, 2);
  g.drawFittedText(internalInfo, 10, 110, getWidth() - 20, 30,
                   juce::Justification::left, 1);

  if (std::abs(audioProcessor.hostBpm - audioProcessor.internalBpm) > 0.1) {
    g.setColour(juce::Colours::red);
    g.drawFittedText("WARNING: Host BPM does not match Server BPM!", 10, 140,
                     getWidth() - 20, 30, juce::Justification::left, 1);
  }

  g.setColour(juce::Colours::white);
  g.setFont(14.0f);
  g.drawFittedText("Local Transmit:", 600, 180, 150, 20,
                   juce::Justification::left, 1);
  g.drawFittedText("Remote Mixer:", 10, 260, 200, 20, juce::Justification::left,
                   1);
}

void NinjamAudioProcessorEditor::resized() {
  serverInput.setBounds(10, 180, 200, 24);
  usernameInput.setBounds(220, 180, 150, 24);
  connectButton.setBounds(380, 180, 100, 24);
  disconnectButton.setBounds(490, 180, 100, 24);

  metronomeToggle.setBounds(10, 220, 150, 24);
  saveTxToggle.setBounds(170, 220, 150, 24);
  saveRxToggle.setBounds(330, 220, 150, 24);

  localVolumeSlider.setBounds(600, 200, 150, 20);
  localPanSlider.setBounds(600, 220, 150, 20);
  localMuteButton.setBounds(600, 240, 150, 20);

  remoteUsersViewport.setBounds(10, 280, getWidth() - 20, getHeight() - 290);
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
