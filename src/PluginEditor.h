#pragma once

#include "LocalChannelStrip.h"
#include "NinjamClient.h"
#include "NinjamLookAndFeel.h"
#include "PluginProcessor.h"
#include "RemoteUserStrip.h"
#include <JuceHeader.h>

class ServerBrowserDialog;

class NinjamAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::Timer,
                                   public NinjamClientListener {
public:
  NinjamAudioProcessorEditor(NinjamAudioProcessor &);
  ~NinjamAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;
  void timerCallback() override;

  // NinjamClientListener
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override;

private:
  NinjamAudioProcessor &audioProcessor;
  NinjamLookAndFeel customLookAndFeel;
  juce::TooltipWindow tooltipWindow{this, 700};
  int diagTickCounter = 0;

  juce::TextButton browseButton;
  juce::TextButton disconnectButton;

  std::unique_ptr<ServerBrowserDialog> serverBrowser;
  void openServerBrowser();
  void closeServerBrowser();

  juce::ToggleButton metronomeToggle;
  juce::ToggleButton saveTxToggle;
  juce::ToggleButton saveRxToggle;
  juce::ToggleButton chatToggle;

  // Local Channel Strips
  juce::OwnedArray<LocalChannelStrip> localChannelStrips;
  juce::TextButton addChannelButton;
  juce::TextButton addInputBusButton;
  juce::TextButton removeInputBusButton;
  juce::TextButton addOutputBusButton;
  juce::TextButton removeOutputBusButton;
  juce::Viewport localChannelsViewport;
  juce::Component localChannelsContainer;

  // Chat UI Controls
  juce::TextEditor chatDisplay;
  juce::TextEditor chatInput;

  // Remote Mixer Controls
  juce::Viewport remoteUsersViewport;
  juce::Component remoteUsersContainer;
  juce::OwnedArray<RemoteUserStrip> remoteUserStrips;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessorEditor)
};
