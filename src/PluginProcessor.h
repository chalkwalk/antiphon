#pragma once

#include "NinjamClient.h"
#include <JuceHeader.h>

class NinjamAudioProcessor : public juce::AudioProcessor,
                             public NinjamClientListener {
public:
  NinjamAudioProcessor();
  ~NinjamAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  void onConnected() override;
  void onDisconnected(const juce::String &error) override;
  void onServerConfig(int bpm, int bpi) override;

  NinjamClient ninjamClient;
  juce::String connectionStatus = "Disconnected";

  // Capture Buffer
  juce::AudioBuffer<float> captureBuffer;
  int captureWritePosition = 0;

  // Host Sync State
  double hostBpm = 120.0;
  double hostPpqPosition = 0.0;
  bool hostIsPlaying = false;

  // Internal Metronome State
  double internalBpm = 120.0;
  int internalBpi = 16;
  double internalPhaseBeats = 0.0; // Position in the interval (0 to BPI)
  // Local Transmit Mixer State
  std::atomic<float> localTxVolume{1.0f};
  std::atomic<float> localTxPan{0.0f};
  std::atomic<bool> localTxMute{false};

  // Last-used connection settings (persisted via getStateInformation)
  juce::String lastHost{"ninbot.com"};
  int          lastPort{2049};
  juce::String lastUsername{""};
  juce::String lastPassword{""};
  bool         lastAnonymous{true};

  // Debug / Integration Testing Features
  std::atomic<bool> metronomeEnabled{true};
  std::atomic<bool> saveTxEnabled{false};
  std::atomic<bool> saveRxEnabled{false};

  // Flash state (written audio thread, decayed + read UI thread)
  std::atomic<float> intervalFlashIntensity{0.0f};
  std::atomic<float> beatFlashIntensity{0.0f};
  std::atomic<int> lastBeatCrossedIndex{-1}; // 0 = interval, N%4==0 = bar, else beat

private:
  int lastTimestampedBeat = -1;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessor)
};
