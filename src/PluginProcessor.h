#pragma once

#include "GainRamp.h"
#include "IntervalClock.h"
#include "ClockModel.h"
#include "HostControl.h"
#include "IntervalProbe.h"
#include "PlayheadGrid.h"
#include "TransmitSpans.h"
#include "CueVoice.h"
#include "MetronomeVoice.h"
#include "NinjamClient.h"
#include "PracticeRoom.h"
#include <JuceHeader.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include <array>
#include <map>
#include <memory>
#include <vector>

class AntiphonAudioProcessor
    : public juce::AudioProcessor,
      public NinjamClientListener,
      public clap_juce_extensions::clap_juce_audio_processor_capabilities,
      private juce::Timer {
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
  bool isStandaloneApp() const { return wrapperType == wrapperType_Standalone; }

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  // AU cannot be told that our bus topology changed. JUCE's AU wrapper acts on
  // exactly three ChangeDetails flags -- latency, parameter info and program
  // (juce_audio_plugin_client_AU_1.mm, AudioProcessorChangedUpdater::update) --
  // and drops the busLayoutChanged flag that
  // patches/juce-bus-layout-change-notification.patch adds for VST3 and CLAP.
  // AU's own bus-count property is host-driven only: a host may call
  // SetBusCount, but a plugin has no way to announce one. Adding a bus anyway
  // would succeed internally and route nowhere, so under AU the layout is
  // fixed at the default one stereo in, one stereo out, and the editor says so.
  bool hasFixedBusLayout() const {
    return wrapperType == wrapperType_AudioUnit;
  }

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
  bool canAddBus(bool) const override { return !hasFixedBusLayout(); }
  bool canRemoveBus(bool isInput) const override {
    if (hasFixedBusLayout())
      return false;
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

  // The practice room: a server on loopback and a band of bots on it.
  //
  // Owned here rather than by the editor, because a room must outlive a closed
  // window -- shutting the plugin UI mid-jam would otherwise take the band with
  // it. Started by the connect dialog, stopped when we leave.
  //
  // It is a DESTINATION, not a mode. Everything past connectToServer is the
  // ordinary connected path: phase bar, remote strips, routing, chat, sync,
  // recording, stems all work without knowing the far side is local.
  PracticeRoom practiceRoom;

  // Start the room and return the port to connect to, or 0 if it would not
  // start. The caller connects; this deliberately does not, so that the one
  // connect path in the editor stays the only one.
  int startPracticeRoom();
  void stopPracticeRoom();
  bool inPracticeRoom() const { return practiceRoom.isRunning(); }
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
  // Whether the host reports a position at all. Without one there is no
  // timeline to lock to and the free-running clock is all there is.
  bool hostHasPpq = false;
  bool hostIsPlaying = false;
  bool hostLooping = false;
  double hostLoopStartPpq = 0.0;
  double hostLoopEndPpq = 0.0;
  // Audio thread only: which grid ran last block, so a change of source can
  // continue the position rather than restart it.
  ClockModel::GridSource lastGrid = ClockModel::GridSource::None;
  // Audio thread only: where the playhead was at the end of the last block, to
  // tell a loop wrap or a jump from ordinary playing.
  double lastBlockEndPpq = 0.0;
  bool lastBlockEndValid = false;
  // Audio thread only: the interval position at the end of the last block.
  double lastPhaseBeats = 0.0;
  // Audio thread only: samples captured into the interval being built. Every
  // upload is full length, so a jump pads it rather than leaving it short.
  int samplesCapturedThisInterval = 0;
  // Audio thread only: the room BPM the grid ran at last block.
  int lastClockBpm = 0;
  PlayheadGrid::Position playheadPosition;
  // Audio thread only: the edge that joins a room, so the transmit ring can be
  // emptied of anything captured before it.
  bool wasInJam = false;

  // Internal Metronome State (atomic: written on message thread, read on audio+UI threads)
  std::atomic<int> internalBpm{120};
  std::atomic<int> internalBpi{16};

  // Sample-exact beat/interval grid. Audio thread only; the UI reads the
  // published phase below rather than touching the clock.
  IntervalClock intervalClock;
  MetronomeVoice metronomeVoice;
  // The DAW-stopped cue (CueVoice.h). Armed only when the DAW stopped under a
  // synced jam: the one surprise worth repeating. Audio thread only.
  CueVoice cueVoice;
  bool cueArmed = false;
  ClockModel::State cueLastState = ClockModel::State::Disconnected;
  std::vector<IntervalClock::Event> clockEvents; // reused, never reallocated
  juce::AudioBuffer<float> metronomeScratch;
  // Copy of every input channel taken before the output is cleared or mixed
  // into; JUCE aliases input and output buses in one buffer.
  juce::AudioBuffer<float> inputSnapshot;
  std::vector<IntervalClock::BlockSegment> captureSegments;

  // Synced to the DAW, or not -- see ClockModel.h. The machine is the audio
  // thread's; the mode is the player's choice, written by the UI, and starts
  // as Synced to DAW on every join rather than being saved with the project.
  ClockModel::Machine clockMachine;            // audio thread only
  std::atomic<int> clockMode{0};               // ClockModel::Mode
  std::atomic<int> publishedClockState{0};     // ClockModel::State, for the UI
  // Bumped every time the position is re-taken from the playhead (entering
  // Synced, or a jump while synced), so the UI can say where you landed.
  std::atomic<int> resyncCount{0};
  // Bumped when the grid applies a new room BPM, which is when a host that
  // follows the room's tempo should be told.
  std::atomic<int> gridTempoChangeCount{0};
  // Millisecond counter at the last room tempo change Antiphon is setting in
  // the DAW itself; the DAW lags by a message-thread hop, which is not a desync.
  std::atomic<juce::uint32> roomTempoChangedMs{0};
  static constexpr juce::uint32 kTempoGraceMs = 250;
  // Published for the UI, which must not read the audio thread's doubles.
  std::atomic<double> publishedHostBpm{0.0};
  std::atomic<bool> publishedHostPlaying{false};

  // Reaper, and only Reaper, lets a plugin set its tempo and press its play
  // button (HostControl.h). Null in every other host. Message thread only.
  std::unique_ptr<HostControl> hostControl;
  TempoFollow tempoFollow; // .enabled persisted as "followRoomTempo"
  // Set when hostControl exists and follows the room, so the audio thread
  // can open the grace period at the moment it applies a new tempo.
  std::atomic<bool> hostFollowsTempo{false};
  // Bumped each time Antiphon set the DAW's tempo itself, so the UI can say so.
  std::atomic<int> hostTempoSetCount{0};
  void setFollowRoomTempo(bool on);

  juce::VST3ClientExtensions *getVST3ClientExtensions() override {
    return vst3Extensions.get();
  }

  // Antiphon's own transport: the standalone's only one, and the plugin's Play
  // on Antiphon's clock. In the standalone, connecting turns it on, so nobody
  // joins a room to silence.
  std::atomic<bool> localTransportPlaying{false};


  // Interval phase in beats, published for the UI phase bar.
  std::atomic<float> publishedPhaseBeats{0.0f};
  // Where the current interval began, in the host's own beats, while synced to
  // the playhead; negative otherwise. "Loop one interval" loops it.
  std::atomic<double> publishedIntervalStartPpq{-1.0};
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
  int lastPort{2049};
  juce::String lastUsername{""};
  juce::String lastPassword{""};
  bool lastAnonymous{true};

  // Debug / Integration Testing Features
  std::atomic<bool> metronomeEnabled{true};
  std::atomic<bool> cueEnabled{true};     // persisted as "cue"
  std::atomic<float> cueVolume{0.5f};     // persisted as "cueVolume"
  std::atomic<float> metronomeVolume{1.0f};
  std::atomic<bool> lastConnectFailed{false};
  std::atomic<bool> saveTxEnabled{false};
  std::atomic<bool> saveRxEnabled{false};
  std::atomic<bool> testToneEnabled{false};
  std::atomic<bool> chatVisible{true};

  // How much the app says out loud, as Announcer::Verbosity. Lives here rather
  // than in the editor because the editor is destroyed with the window and a
  // setting somebody chose must not be one they choose again every time.
  //
  // Defaults to All, which is what the app already did: every incoming chat
  // line, every join and part. That was built and used by a screen reader
  // user, so it is not a default to quietly change -- what was missing was the
  // ability to turn it DOWN, not a different opinion about where it should
  // start.
  std::atomic<int> announceVerbosity{2};

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
  // The same, for the DAW-stopped cue, at cueVolume.
  void renderCue(juce::AudioBuffer<float> &buffer, int startSample, int count,
                 int totalNumOutputChannels);

  // Overwrites every input bus with a 440 Hz sine plus a full-scale one-sample
  // impulse at the top of each interval. The impulse's offset within an
  // archived interval is the transmit alignment error, in samples.
  void injectTestTone(int numSamples);

  // Captures [startSample, startSample + count) of the input snapshot into
  // every local channel's ring. Called once per segment of a block so that a
  // transmitted interval ends on the exact sample, not on a block boundary.
  void captureInputRange(int startSample, int count);
  // Zeros into every channel's transmit ring: the hole a forward jump tears in
  // an interval, or the padding that keeps an upload full length.
  void captureSilence(int count);

  int64_t testToneSample = 0;
  ninjam::IntervalProbe testProbe;

  std::atomic<bool> phaseResetPending{false};

  // Watches for the moments the DAW's tempo should follow the room's: joining,
  // and the grid applying a room tempo change. Message thread.
  void timerCallback() override;
  void adoptReaper(std::function<void *(const char *)> resolve);
  std::unique_ptr<juce::VST3ClientExtensions> vst3Extensions;
  bool followWasConnected = false;
  int followSeenTempoChanges = 0;
  bool hasConnectedSinceLastAttempt{false};
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiphonAudioProcessor)
};
