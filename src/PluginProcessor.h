#pragma once

#include "GainRamp.h"
#include "IntervalClock.h"
#include "RunGate.h"
#include "IntervalProbe.h"
#include "SyncState.h"
#include "TransmitSpans.h"
#include "MetronomeVoice.h"
#include "NinjamClient.h"
#include <JuceHeader.h>
#include <array>
#include <map>
#include <memory>
#include <vector>

class AntiphonAudioProcessor : public juce::AudioProcessor,
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
    // Set by the capture pass whenever it writes with transmit on, cleared at
    // the interval boundary. An interval is sent if transmit was on for any
    // part of it -- the parts it was off for are already silence in the ring.
    // Reading xmitEnabled at the boundary instead would throw away an interval
    // you played most of and then switched off during.
    // Where transmit was on within the interval being captured, and within the
    // one being handed off. Double-buffered because the audio thread starts
    // recording the next interval the moment the boundary passes, while the
    // handoff is still reading the previous one.
    TransmitSpans spans[2];
    std::atomic<int> writeSpanIndex{0};
    // Audio thread only. The monitor gain, ramped so mute and solo cannot
    // click.
    GainRamp monitorRamp;

    juce::AbstractFifo fifo{1};
    juce::AudioBuffer<float> ring;

    // Returns a recycled channel to a fresh one's state. Deliberately does not
    // touch the ring's size: the audio thread may still hold a pointer to this
    // object from before it was removed, and reallocating the buffer under it
    // is the one thing that would turn this design into a crash. The ring is
    // only ever sized in prepareToPlay, which the host does not run
    // concurrently with processBlock.
    void resetForReuse() {
      name = "Instrument";
      isMono.store(false);
      volume.store(1.0f);
      pan.store(0.0f);
      muted.store(false);
      monitorSolo.store(false);
      xmitEnabled.store(true);
      peakL.store(0.0f);
      peakR.store(0.0f);
      inputBusIndex.store(0);
      writeSpanIndex.store(0);
      spans[0].beginInterval(false);
      spans[1].beginInterval(false);
      fifo.reset();
      ring.clear();
      isValid.store(true);
    }
  };

  AntiphonAudioProcessor();
  ~AntiphonAudioProcessor() override;

  // True only when this instance really is the Standalone app.
  //
  // Deliberately NOT the JucePlugin_Build_Standalone macro. That macro is a
  // project-level flag meaning "Standalone is one of the FORMATS", and this
  // file is compiled once into the shared code that the VST3 and CLAP also
  // link against -- so the macro is 1 in every format. Using it here compiled
  // the DAW sync flow out of the plugin entirely: hasTransport was forced
  // false, tempo was never compared, and the state machine ran on connect.
  bool isStandaloneApp() const {
    return wrapperType == wrapperType_Standalone;
  }

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
  // Recomputes the local half of the global solo bus. Called whenever a local
  // solo changes; the client owns the mask because the remote mix has to agree
  // with the monitor mix about whether anything is soloed.
  // Applies the transmit state to the whole interval so far, as though it had
  // been that way from the start.
  //
  // `pressSampleInInterval` and `pressIntervalIndex` are taken at the moment
  // the button went down, not when the hold timer expired: the plain toggle
  // already set the state from the press onwards, so rewriting the part before
  // it leaves the interval uniform. Returns false if the interval boundary has
  // passed since the press -- that interval has already gone out, so there is
  // nothing left to rewrite.
  bool applyRetroactiveTransmit(int channelIndex, bool on,
                                juce::int64 pressIntervalIndex);

  // Which interval we are in, so a press can be matched to it later.
  juce::int64 currentIntervalIndex() const {
    return publishedIntervalIndex.load();
  }

  void refreshLocalSoloState();
  void sendChannelInfoToServer();
  // Message thread only: opens or closes the debug dump files to match the
  // saveTx/saveRx flags.
  void applyDebugCaptureSettings();

  NinjamClient ninjamClient;
  juce::String connectionStatus = "Disconnected";

  // The UI's view of the local channels. Message thread only -- the audio
  // thread must never take this lock, because the things that hold it are
  // exactly the things that are slow: addLocalChannel allocates a 30-second
  // ring (about 11 MB at 96 kHz), setStateInformation parses XML, and the
  // editor's 30 Hz timer constructs channel strips under it.
  juce::CriticalSection localChannelMutex;
  std::vector<std::shared_ptr<LocalChannel>> localChannels;

  // What the audio thread actually walks: raw pointers to the same objects,
  // published by publishLocalChannels() once the vector above is settled.
  //
  // Safe without a lock because a LocalChannel is never destroyed while the
  // processor lives. Removing a channel moves it to `spareLocalChannels` and
  // shrinks the count; a later add takes it back out. So a pointer the audio
  // thread is holding across a block can go stale in the sense of no longer
  // being in use, but never in the sense of dangling.
  static constexpr int kMaxLocalChannels = 32;
  std::array<LocalChannel *, (std::size_t)kMaxLocalChannels> audioChannels{};
  std::atomic<int> audioChannelCount{0};
  std::vector<std::shared_ptr<LocalChannel>> spareLocalChannels;

  // Call with localChannelMutex held, after any change to localChannels.
  void publishLocalChannels();

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
  // Copy of every input channel taken before the output is cleared or mixed
  // into; JUCE aliases input and output buses in one buffer.
  juce::AudioBuffer<float> inputSnapshot;
  std::vector<IntervalClock::BlockSegment> captureSegments;

  // DAW sync. We lock the interval grid to the transport START rather than the
  // timeline: a clip/session view advances a timeline that is musically
  // meaningless, and jogging during a live jam is not a real use case.
  SyncState syncState;                      // audio thread only
  std::atomic<int> publishedSyncState{0};   // SyncState::State, for the UI
  std::atomic<bool> syncRequested{false};   // set by the Sync button

  void requestSync() { syncRequested.store(true); }

  // The standalone's own transport. The plugin uses the host's; the standalone
  // had none, so its grid ran purely on being connected and there was nothing
  // to start or stop. Connecting turns it on, so existing behaviour is
  // unchanged and nobody joins a room to silence.
  std::atomic<bool> localTransportPlaying{false};

  // Whichever transport applies to this build.
  bool transportIsPlaying() const {
    return isStandaloneApp() ? localTransportPlaying.load() : hostIsPlaying;
  }

  // Offline practice: your own audio played back to you, delayed. Offline-only,
  // which is what makes "never transmitted" true by construction.
  std::atomic<bool> practiceEnabled{false};

  // Builds the echo history at the current interval length, which only the
  // processor knows. Message thread: this allocates.
  bool setPracticeEnabled(bool on);

  // Interval phase in beats, published for the UI phase bar.
  std::atomic<float> publishedPhaseBeats{0.0f};
  // Counts intervals since the clock was reset, so a press can be matched to
  // the interval it happened in.
  std::atomic<juce::int64> publishedIntervalIndex{0};

  // The tempo the interval clock is *actually running*, which is not the same
  // as internalBpm/internalBpi.
  //
  // A server BPM or BPI change takes effect at the next interval boundary, so
  // the current interval plays out at its original length -- that is what keeps
  // us aligned with every other client. internalBpi changes the instant the
  // server says so, and the UI used to read it directly: on a 16 -> 11 change
  // the phase bar re-divided immediately while the phase still ran to the old
  // interval, so it read "15.50 / 11" and drew past its own end. These follow
  // the clock, so the display changes exactly when the audio does.
  std::atomic<int> publishedActiveBpm{120};
  std::atomic<int> publishedActiveBpi{16};

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
  void injectTestTone(int numSamples);

  // Captures [startSample, startSample + count) of the input snapshot into
  // every local channel's ring. Called once per segment of a block so that a
  // transmitted interval ends on the exact sample, not on a block boundary.
  void captureInputRange(int startSample, int count);

  int64_t testToneSample = 0;
  ninjam::IntervalProbe testProbe;

  std::atomic<bool> phaseResetPending{false};
  bool hasConnectedSinceLastAttempt{false};
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiphonAudioProcessor)
};
