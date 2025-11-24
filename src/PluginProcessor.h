#pragma once

#include "IntervalClock.h"
#include "MetronomeVoice.h"
#include "NinjamClient.h"
#include <JuceHeader.h>
#include <map>
#include <memory>
#include <vector>

class NinjamAudioProcessor : public juce::AudioProcessor,
                             public NinjamClientListener {
public:
  struct LocalChannel {
    juce::String name{"Instrument"};
    std::atomic<bool> isMono{false};
    std::atomic<float> volume{1.0f};
    std::atomic<float> pan{0.0f};
    std::atomic<bool> muted{false};       // monitor mute only
    std::atomic<bool> monitorSolo{false}; // monitor solo only
    std::atomic<bool> xmitEnabled{true};
    std::atomic<float> peakL{0.0f};
    std::atomic<float> peakR{0.0f};
    std::atomic<bool> isValid{true};
    std::atomic<int> inputBusIndex{0};

    juce::AbstractFifo fifo{1};
    juce::AudioBuffer<float> ring;
  };

  NinjamAudioProcessor();
  ~NinjamAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  bool canAddBus(bool) const override { return true; }
  bool canRemoveBus(bool isInput) const override {
    return isInput ? getBusCount(true) > 1 : getBusCount(false) > 1;
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
  void onUserInfoChange() override;

  int addLocalChannel();
  void removeLastLocalChannel();
  int addInputBus();
  void removeLastInputBus();
  int addOutputBus();
  void removeLastOutputBus();
  void setRemoteUserOutputBus(const juce::String &username, int channelIndex,
                              int busIdx);
  void sendChannelInfoToServer();

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

  // Sample-exact beat/interval grid. Audio thread only; the UI reads the
  // published phase below rather than touching the clock.
  IntervalClock intervalClock;
  MetronomeVoice metronomeVoice;
  std::vector<IntervalClock::Event> clockEvents; // reused, never reallocated
  juce::AudioBuffer<float> metronomeScratch;

  // Interval phase in beats, published for the UI phase bar.
  std::atomic<float> publishedPhaseBeats{0.0f};

  // Last-used connection settings (persisted via getStateInformation)
  juce::String lastHost{"ninbot.com"};
  int          lastPort{2049};
  juce::String lastUsername{""};
  juce::String lastPassword{""};
  bool         lastAnonymous{true};

  // Debug / Integration Testing Features
  std::atomic<bool> metronomeEnabled{true};
  std::atomic<float> metronomeVolume{1.0f};
  std::atomic<bool> lastConnectFailed{false};
  std::atomic<bool> saveTxEnabled{false};
  std::atomic<bool> saveRxEnabled{false};
  std::atomic<bool> testToneEnabled{false};
  std::atomic<bool> chatVisible{true};

  // Flash state (written audio thread, decayed + read UI thread)
  std::atomic<float> intervalFlashIntensity{0.0f};
  std::atomic<float> beatFlashIntensity{0.0f};
  std::atomic<int> lastBeatCrossedIndex{-1};

  // Saved output bus routing for remote channels, keyed by (username, channelIndex).
  // Persisted in state and reapplied when users rejoin.
  std::map<std::pair<juce::String, int>, int> savedRemoteRoutings;

private:
  // Adds `count` samples of the current click, starting at `startSample`, into
  // the first two output channels. Renders once into scratch so the voice is
  // advanced exactly once per sample regardless of channel count.
  void renderMetronome(juce::AudioBuffer<float> &buffer, int startSample,
                       int count, float gain, int totalNumOutputChannels);

  // Overwrites every input bus with a 440 Hz sine plus a full-scale one-sample
  // impulse at the top of each interval. The impulse's offset within an
  // archived interval is the transmit alignment error, in samples.
  void injectTestTone(juce::AudioBuffer<float> &buffer,
                      juce::AudioBuffer<float> &bus0Snapshot, int numSamples);

  double testTonePhase = 0.0;
  juce::AudioBuffer<float> toneScratch;

  std::atomic<bool> phaseResetPending{false};
  bool hasConnectedSinceLastAttempt{false};
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NinjamAudioProcessor)
};
