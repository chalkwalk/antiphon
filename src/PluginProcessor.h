#pragma once

#include "NinjamClient.h"
#include <JuceHeader.h>
#include <memory>
#include <vector>

class NinjamAudioProcessor : public juce::AudioProcessor,
                             public NinjamClientListener {
public:
  struct LocalChannel {
    juce::String name{"Local Instrument"};
    std::atomic<bool> isMono{false};
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
    std::atomic<bool> muted{false};
    std::atomic<bool> xmitEnabled{true};
    std::atomic<float> peakL{0.0f};
    std::atomic<float> peakR{0.0f};
    std::atomic<bool> isValid{true};

    juce::AbstractFifo fifo{1};
    juce::AudioBuffer<float> ring;
  };

  NinjamAudioProcessor();
  ~NinjamAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  bool canAddBus(bool isInput) const override { return isInput; }
  bool canRemoveBus(bool isInput) const override {
    return isInput && getBusCount(true) > 1;
  }
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

  int addLocalChannel();
  void removeLastLocalChannel();

  NinjamClient ninjamClient;
  juce::String connectionStatus = "Disconnected";

  juce::CriticalSection localChannelMutex;
  std::vector<std::shared_ptr<LocalChannel>> localChannels;

  // Host Sync State
  double hostBpm = 120.0;
  double hostPpqPosition = 0.0;
  bool hostIsPlaying = false;

  // Internal Metronome State (atomic: written on message thread, read on audio+UI threads)
  std::atomic<int> internalBpm{120};
  std::atomic<int> internalBpi{16};
  double internalPhaseBeats = 0.0; // audio thread only

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
  std::atomic<int> lastBeatCrossedIndex{-1};

private:
  int lastTimestampedBeat = -1;
  int intervalSyncCooldown = 0;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessor)
};
