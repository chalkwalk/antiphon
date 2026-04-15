#pragma once
#include "SpscRing.h"
#include "VorbisCodec.h"
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

class NinjamClientListener {
public:
  virtual ~NinjamClientListener() = default;
  virtual void onConnected() {}
  virtual void onDisconnected(const juce::String &) {}
  virtual void onServerConfig(int, int) {}
  virtual void onUserInfoChange() {}
  virtual void onChatMessage(const juce::String &type,
                             const juce::String &username,
                             const juce::String &text) {}
};

class NinjamClient : public juce::Thread {
public:
  NinjamClient();
  ~NinjamClient() override;

  void addListener(NinjamClientListener *listener);
  void removeListener(NinjamClientListener *listener);

  void connectToServer(const juce::String &host, int port,
                       const juce::String &username,
                       const juce::String &password);
  void disconnectFromServer();

  void setSaveTx(bool shouldSave);
  void setSaveRx(bool shouldSave);

  // Finalises the debug dumps without changing the toggles.
  //
  // A WAV file is only readable once its writer is destroyed and the header
  // written, so leaving the toggle on held tx.wav and rx.wav open and unusable
  // for the whole life of the plugin. Called on disconnect; a later connect
  // reopens them if the toggle is still set.
  void closeDebugCaptureFiles();
  void reopenDebugCaptureFiles();

  void run() override;

  bool isConnected() const { return connectionState == 3; }

  void processCapturedAudio(juce::AudioBuffer<float> &buffer, int numSamples,
                            int channelIndex, bool mono);
  void getDecodedAudio(juce::AudioBuffer<float> &buffer);

  void setSampleRate(double sr) { sampleRate = sr; }
  void setServerBpm(int bpm) { serverBpm = bpm; }
  void setServerBpi(int bpi) { serverBpi = bpi; }

  void swapIntervalBuffers();

  // Update the local channel names sent to the server via CLIENT_SET_CHANNEL_INFO (0x82).
  // Safe to call from any thread. Sends immediately if connected.
  void updateChannelInfo(const juce::StringArray &names);

  void setRemoteUserVolume(const juce::String &username, int channelIndex,
                           float volume);
  void setRemoteUserPan(const juce::String &username, int channelIndex,
                        float pan);
  void setRemoteUserMute(const juce::String &username, int channelIndex,
                         bool mute);
  void setRemoteUserSolo(const juce::String &username, int channelIndex,
                         bool solo);
  void setRemoteUserRecv(const juce::String &username, int channelIndex,
                         bool recv);
  void setRemoteUserOutputBus(const juce::String &username, int channelIndex,
                              int busIdx);

  struct ChatMessage {
    juce::String type;
    juce::String username;
    juce::String text;
  };

  void sendChatMessage(const juce::String &text);
  void sendAdminCommand(const juce::String &command);
  void sendPrivateMessage(const juce::String &username,
                          const juce::String &text);

  juce::Array<ChatMessage> getChatLog() const;

  // Default gain applied to every remote channel: 0.25 linear, -12.04 dB.
  //
  // This matches the reference client, which is what ReaNINJAM and the other
  // canonical clients are built on: RemoteUser_Channel defaults to volume 0.25
  // and RemoteUser to 1.0, and they are multiplied at mix time
  // (references/ninjam/ninjam/njclient.cpp:2948 and :1967). Remote players are
  // deliberately quieter than your own signal. Confirmed by measurement
  // against the real reference client, which plays our transmitted tone back
  // at 0.253 of the level we sent.
  //
  // Do not "fix" this to unity: it would make us 12 dB louder than everyone
  // else in the same session. The UI fader initialises from this too, so the
  // two cannot drift apart.
  static constexpr float kDefaultRemoteChannelVolume = 0.25f;

  struct RemoteUserChannel {
    int channelIndex = 0;
    juce::String channelName;
    float volume = kDefaultRemoteChannelVolume;
    float pan = 0.0f;
    bool isMuted = false;
    bool isSoloed = false;
    bool recvEnabled = true;
    float peakLevel = 0.0f;
    int outputBusIndex = 0;
  };

  struct RemoteUser {
    juce::String username;
    std::map<int, RemoteUserChannel> channels;
  };

  std::map<juce::String, RemoteUser> getRemoteUsers() const;

  // Set to true on DOWNLOAD_INTERVAL_BEGIN. The audio thread drains it and
  // never acts on it: the local metronome is the sole authority for interval
  // swaps (PRINCIPLES 9). Driving swaps from this signal instead let network
  // jitter yank the interval clock mid-interval, discarding seconds of
  // un-played audio. Kept only so the diagnostic counters stay meaningful.
  std::atomic<bool> intervalBeginSignal{false};

  // Diagnostics for the playback path. All counters are sticky totals; the
  // dumper logs deltas.
  std::atomic<int> diagSwaps{0};
  std::atomic<int> diagSwapsBeforeConsumed{0};
  std::atomic<int> diagUnderrunBlocks{0};
  std::atomic<int> diagSamplesDroppedOnSwap{0};
  std::atomic<int> diagLastIntervalSamples{0};
  std::atomic<int> diagLastIntervalExpected{0};
  void dumpDiagnostics();

private:
  juce::ListenerList<NinjamClientListener> listeners;
  std::unique_ptr<juce::StreamingSocket> socket;

  // One decoded interval of audio from a remote channel.
  // writePos is atomically incremented by the network thread after each decode
  // batch; the audio thread reads [0, writePos) without the lock.
  struct DecodedInterval {
    juce::String guid;
    juce::AudioBuffer<float> buffer;
    std::atomic<int> writePos{0};
    std::atomic<bool> finalReceived{false};

    // Channel write pointers, taken once before the interval is shared with the
    // audio thread.
    //
    // AudioBuffer::getWritePointer sets the buffer's `isClear` flag, and
    // AudioBuffer::addFrom on the audio thread reads it. Calling
    // getWritePointer from the network thread during decode is therefore a data
    // race on that flag -- confirmed by TSan -- even though the decoded samples
    // themselves are published safely by writePos. Taking the pointers up front
    // leaves isClear false for the buffer's whole life and nobody writes it
    // again.
    std::array<float *, 2> channelWritePtr{{nullptr, nullptr}};

    // Call after setSize()/clear() and before sharing.
    void publishWritePointers() {
      for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
        channelWritePtr[(size_t)ch] = buffer.getWritePointer(ch);
    }
  };

  // One remote channel's playback slot.
  //
  // Fixed in number and never created or destroyed while the client lives, so
  // the audio thread walks the whole array without a lock and can never observe
  // a half-built or freed entry. That is the entire point: the mix used to walk
  // a std::map under downloadMutex, which the network thread also held, so
  // every block was one decode away from a priority inversion (PRINCIPLES 7).
  //
  // Every field below is owned by exactly one thread. The ownership is the
  // design, so it is spelled out field by field -- if you add one, say who
  // writes it.
  struct StreamSlot {
    // Free -> Live -> Draining -> Free. The network thread makes the first two
    // transitions, the audio thread the third: only the audio thread knows when
    // it has let go of the interval pointers. Read with acquire, so a Live
    // slot's parameters are visible as soon as its state is.
    enum State { kFree = 0, kLive = 1, kDraining = 2 };
    std::atomic<int> state{kFree};

    // UI writes, audio thread reads.
    std::atomic<float> volume{kDefaultRemoteChannelVolume};
    std::atomic<float> pan{0.0f};
    std::atomic<bool> muted{false};
    std::atomic<bool> soloed{false};
    std::atomic<int> outputBus{0};
    // Audio thread writes, UI reads. Was a plain float in the shared map, and
    // was therefore a race in every build that ever ran.
    std::atomic<float> peakLevel{0.0f};

    // Ownership travels in a circle and never comes to rest on the audio
    // thread: the network thread pushes a decoded interval to `ready`, the
    // audio thread pops it, plays it, and pushes it to `retired` for the
    // network thread to destroy. Freeing an interval means freeing a
    // multi-megabyte AudioBuffer, which must never happen inside the callback.
    //
    // `retired` is sized above ready.capacity() + 2 -- the most the audio thread
    // can ever hold at once being `current` and `fadeOut` -- so handing one back
    // cannot fail, and the audio thread is never left holding a pointer it has
    // no way to return.
    SpscRing<DecodedInterval, 8> ready;
    SpscRing<DecodedInterval, 24> retired;

    // Audio thread only. `fadeOut` keeps the previous interval alive for a short
    // crossfade across the swap boundary, masking the discontinuity left by
    // un-played tail samples.
    DecodedInterval *current = nullptr;
    int readPos = 0;
    DecodedInterval *fadeOut = nullptr;
    int fadeOutPos = 0;
    int fadeTotal = 0;
    int fadeRemaining = 0;

    // Network thread only. The audio thread must never touch `username`:
    // copying a juce::String touches a refcount, and comparing one can read
    // memory the network thread is rewriting.
    juce::String username;
    int channelIndex = 0;
    std::vector<std::unique_ptr<DecodedInterval>> owned;
  };

  // Well past any real session: sixteen players with four channels each.
  // Running out drops the channel and says so, rather than growing an array the
  // audio thread is walking.
  static constexpr int kMaxStreams = 64;
  std::array<StreamSlot, (std::size_t)kMaxStreams> streamSlots;

  int acquireStreamSlot(const juce::String &username, int channelIndex);
  void releaseStreamSlot(const juce::String &username, int channelIndex);
  void drainRetired(StreamSlot &slot);
  void drainAllRetired();
  // Audio thread: hand every interval back and mark the slot reusable.
  void releaseSlotOnAudioThread(StreamSlot &slot);

  // Routes DOWNLOAD_INTERVAL_WRITE chunks to the correct DecodedInterval.
  // Erased when the final chunk arrives (flags & 1). `target` is borrowed --
  // the slot's `owned` list holds the interval.
  struct PendingDownload {
    DecodedInterval *target = nullptr;
    int slotIndex = -1;
    std::unique_ptr<VorbisDecoder> decoder;
    juce::LagrangeInterpolator resamplerL, resamplerR;
    juce::String username;
    int channelIndex = 0;
  };

  // Network thread only, start to finish: inserted on 0x04, erased on the final
  // 0x05 chunk, cleared on disconnect. The audio thread never looks at it, so it
  // needs no lock.
  std::map<juce::String, PendingDownload> guidToInterval;

  // The UI's view of the room, and the map from a channel to its slot. Shared
  // between the network thread and the message thread; the audio thread never
  // takes this lock, which is the whole reason it can be a lock at all.
  juce::CriticalSection usersMutex;
  std::map<juce::String, RemoteUser> remoteUsers;
  std::map<std::pair<juce::String, int>, int> slotIndexByKey;

  juce::CriticalSection chatMutex;
  juce::Array<ChatMessage> chatLog;

  double sampleRate = 48000.0;
  int serverBpm = 120;
  int serverBpi = 16;

  juce::String currentHost;
  int currentPort = 2049;
  juce::String currentUsername;
  juce::String currentPassword;

  template <typename ApplyToChannel, typename ApplyToSlot>
  void updateChannelParam(const juce::String &username, int channelIndex,
                          ApplyToChannel toChannel, ApplyToSlot toSlot);

  bool handleMessage(juce::uint8 type, const juce::MemoryBlock &payload);
  void sendAuthRequest(const juce::uint8 challenge[8]);
  void sendChannelInfo();
  void sendUserMask();

  bool readFull(void *dest, int numBytes);
  bool writeFull(juce::uint8 type, const void *payload, int numBytes);

  std::atomic<int> connectionState{
      0}; // 0=disconnected, 1=connecting, 2=authenticating, 3=connected

  int64 lastKeepAliveTime = 0;

  // Atomic because the audio thread reads isSavingRx to decide whether to take
  // rxFileMutex at all. Plain bools here were also a data race: the message
  // thread wrote them while the audio thread read them.
  std::atomic<bool> isSavingTx{false};
  std::atomic<bool> isSavingRx{false};
  std::unique_ptr<juce::FileOutputStream> txOggFile;
  std::unique_ptr<juce::AudioFormatWriter> txWavWriter;
  std::unique_ptr<juce::FileOutputStream> rxOggFile;
  std::unique_ptr<juce::AudioFormatWriter> rxWavWriter;
  juce::WavAudioFormat wavFormat;

  juce::CriticalSection txFileMutex;
  juce::CriticalSection rxFileMutex;
  juce::CriticalSection channelInfoMutex;
  // Serialises whole frames onto the socket; see writeFull.
  juce::CriticalSection writeMutex;

  // Listener callbacks are bounced to the message thread with callAsync, which
  // means a message can still be sitting in the queue when this object is
  // destroyed -- closing a plugin shortly after a disconnect, for instance.
  // The queued lambda would then run against a dangling `this`. Every such
  // lambda captures a copy of this flag and checks it first; the destructor
  // clears it before tearing anything down.
  //
  // This is airtight as long as the destructor runs on the message thread
  // (it does in the plugin: the processor is destroyed there), because the
  // check and the destruction cannot then interleave.
  std::shared_ptr<std::atomic<bool>> aliveFlag{
      std::make_shared<std::atomic<bool>>(true)};

  template <typename Fn> void callAsyncIfAlive(Fn &&fn) {
    auto alive = aliveFlag;
    juce::MessageManager::callAsync(
        [alive, fn = std::forward<Fn>(fn)]() mutable {
          if (alive->load())
            fn();
        });
  }
  juce::StringArray storedChannelNames{"Instrument"};
};
