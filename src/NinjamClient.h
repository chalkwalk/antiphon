#pragma once
#include "GainRamp.h"
#include "SessionWriter.h"
#include "SpscRing.h"
#include "VorbisCodec.h"
#include <chalkwalk/ninjam/Bytes.h>
#include <JuceHeader.h>
#include <array>
#include <deque>
#include <memory>
#include <set>
#include <vector>

class NinjamClientListener {
public:
  virtual ~NinjamClientListener() = default;
  virtual void onConnected() {}
  virtual void onDisconnected(const juce::String &) {}
  virtual void onServerConfig(int, int) {}
  virtual void onUserInfoChange() {}

  // Somebody joined or left, carrying WHO rather than only that something
  // changed.
  //
  // This exists because `getRoomMembers()` cannot answer the question. The set
  // is maintained on the network thread the instant a JOIN or PART arrives,
  // while listener callbacks are dispatched to the message thread afterwards --
  // so a listener asking "is this person here now?" is asking about a list that
  // may already have moved on. A player who joins and leaves inside one
  // message-thread gap is, from the listener's side, someone who was never
  // there at all.
  //
  // The event carries the fact instead, and a fact does not go stale.
  virtual void onRoomMembershipChange(const juce::String & /*username*/,
                                      bool /*joined*/) {}
  virtual void onChatMessage(const juce::String &type,
                             const juce::String &username,
                             const juce::String &text) {}

  // A remote interval, complete and decoded, at our sample rate.
  //
  // ON THE NETWORK THREAD, unlike every other callback here, which are posted
  // to the message thread. Posting this one would mean copying an interval of
  // audio per listener per interval to hand it over, and the only caller wants
  // to measure it and keep a number. So it is delivered where it is decoded
  // and the samples are valid only for the duration of the call.
  //
  // Nothing in the plugin listens to this. It exists for the bots, whose
  // `BotClient::Listener` has the matching call: a bot could send audio and
  // never receive any, which is half an interface for something meant to
  // become a partner that reacts to what you play.
  virtual void onIntervalReceived(const juce::String &username,
                                  int channelIndex, const float *left,
                                  const float *right, int numSamples) {
    (void)username;
    (void)channelIndex;
    (void)left;
    (void)right;
    (void)numSamples;
  }
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

  // Records the session as a Ninjam archive: the Ogg bytes exactly as sent and
  // received, plus the manifest, in the server's own format. antiphon-stems
  // turns the result into WAV stems.
  //
  // Public servers do not all archive, and those that do do not all let you
  // have the files, so this is the only way to be sure of getting stems out of
  // a jam you played.
  // Where sessions go unless told otherwise: a folder beside your music, not
  // buried in application support, because these are recordings you will want
  // to find and drag into a DAW.
  static juce::File defaultSessionDirectory();

  // Names the session `<date>_<time>_<server>.ninjam`, so a directory of them
  // sorts chronologically and each says where it came from.
  bool startSessionRecording(const juce::File &parentDir);
  void stopSessionRecording();
  bool isRecordingSession() const { return sessionWriter.isActive(); }
  juce::File sessionRecordingDirectory() const {
    return sessionWriter.directory();
  }
  int sessionRecordingClipCount() const { return sessionWriter.clipCount(); }

  // Which local interval transfers are happening in. Incremented by the audio
  // thread at each boundary. Uploads and downloads for the same musical
  // interval occur in the same one -- you send your interval N while receiving
  // everyone else's -- so one counter keeps every stem on the same timeline.
  std::atomic<int> sessionIntervalIndex{0};

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

  // How an encode is allowed to spend its time.
  //
  // Encoding an interval is a few hundred milliseconds of Vorbis, and running
  // it flat out is one contiguous burst of compute on whichever thread called
  // -- the shape ROADMAP.md's *The interval boundary is a compute spike*
  // exists to get rid of. `spreadOverSeconds` stretches the block loop to
  // roughly that duration by sleeping between blocks, so the same work lands
  // as a thin smear instead. Zero means flat out, which is what a caller with
  // its own schedule wants: the band's bots are already staggered a slice
  // apart and pacing them again would run one bot's encode into the next
  // bot's slice.
  //
  // `abort` is checked between blocks so stopping does not have to wait out a
  // spread. An aborted encode leaves an upload the server has a BEGIN for and
  // no final flush, which is only reachable on the way to disconnecting.
  struct EncodePacing {
    double spreadOverSeconds = 0.0;
    std::function<bool()> abort;
  };

  // Flat out, on the calling thread. What the band's bots use.
  void processCapturedAudio(juce::AudioBuffer<float> &buffer, int numSamples,
                            int channelIndex, bool mono);

  // Paced, and abortable.
  void processCapturedAudio(juce::AudioBuffer<float> &buffer, int numSamples,
                            int channelIndex, bool mono,
                            const EncodePacing &pacing);

  // The same thing, off this thread and paced across the interval.
  //
  // For the LOCAL player, whose capture arrives on the message thread from the
  // `callAsync` in `processBlock`. Encoding there froze the UI for the length
  // of an interval's Vorbis, every interval, in every room -- meters and phase
  // bar included. The cheap half of the handoff (draining the FIFO, applying
  // the transmit spans) has to stay on the message thread because the FIFO
  // wants emptying promptly; only the expensive half moves.
  //
  // Takes the buffer by value: the job outlives the caller's stack frame.
  void enqueueCapturedAudio(juce::AudioBuffer<float> buffer, int numSamples,
                            int channelIndex, bool mono);
  void getDecodedAudio(juce::AudioBuffer<float> &buffer);

  // Whether a channel is subscribed to the moment it is first seen. Set before
  // connecting.
  //
  // A client that only transmits wants none of it: an unsubscribed channel
  // never causes the server to send an interval, so it never causes one to be
  // allocated here. That is what keeps a practice room of bots costing one
  // client's worth of interval buffers rather than one per bot. Turning recv
  // off after the fact would leave a window in which audio arrives anyway.
  void setDefaultRecvEnabled(bool enabled) { defaultRecvEnabled = enabled; }

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
  // (justinfrankel/ninjam njclient.cpp:2948 and :1967). Remote players are
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

  // Everyone in the room, including players with no audio channels.
  //
  // getRemoteUsers() is the mixer's view and deliberately drops a user the
  // moment their channel map empties -- a strip with no channels is not a
  // mixer entry. But a listener with no channels is still in the room, and
  // "who am I playing with?" is a question the mixer cannot answer. Membership
  // is tracked separately, from JOIN and PART and from any username that
  // appears in USER_INFO_CHANGE.
  struct RoomMember {
    juce::String username;
    int channelCount = 0;
  };
  std::vector<RoomMember> getRoomMembers() const;

  // What we connected as, so the UI can tell your own messages from everyone
  // else's. Empty until a connection is attempted.
  juce::String getSelfUsername() const;

  // Whether anything, anywhere, is soloed -- a bitmask, exactly as the
  // reference client keeps it (`m_issoloactive`, njclient.cpp:1750 and :1886).
  //
  // Solo is ONE global bus spanning local and remote channels: soloing a local
  // channel silences remote players, and soloing a remote channel silences your
  // local monitor. Both of the reference's mix decisions test the combined
  // value (:1307 for the local monitor, :1388 for remote playback). We used to
  // keep two independent solo buses, which meant solo did not do the one thing
  // solo is for -- hearing that channel on its own.
  enum SoloBits { kRemoteSolo = 1, kLocalSolo = 2 };
  std::atomic<int> soloMask{0};
  bool isAnySoloActive() const {
    return soloMask.load(std::memory_order_relaxed) != 0;
  }
  // Called by the processor when a local channel's solo changes; the client
  // owns the mask because both mixes have to agree on it.
  void setLocalSoloActive(bool active) {
    const int bits = soloMask.load(std::memory_order_relaxed);
    soloMask.store(active ? (bits | kLocalSolo) : (bits & ~kLocalSolo),
                   std::memory_order_relaxed);
  }

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
  // THREAD-SAFE, and it has to be. Every `listeners.call` is posted through
  // `callAsyncIfAlive` and so runs on the message thread, but `addListener`
  // and `removeListener` are called from wherever a listener happens to be
  // built or destroyed -- a bot reaped by `PracticeRoom::reapPartedBots` on
  // the pump thread destroys its `NinjamBotClient`, which removes itself
  // from here.
  //
  // The stock `ListenerList` locks `listeners->getLock()` in both `remove()`
  // and the callback loop, but its default array uses `DummyCriticalSection`,
  // so both locks are no-ops and the two race on the list's iterator
  // bookkeeping. TSan reported it against `PracticeRoomTests` for as long as
  // anyone has looked. `ThreadSafeListenerList` is the same class with a real
  // `CriticalSection`, which makes those two existing locks mean something.
  //
  // The callback loop holds that lock, so a listener callback must never block
  // on something a remover holds. Nothing does today: the listeners are the
  // processor, the editor, `NinjamBotClient` and the tests, and none of them
  // touches `PracticeRoom::botsMutex` -- which is the lock the reap path holds
  // when it removes.
  juce::ThreadSafeListenerList<NinjamClientListener> listeners;
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
    // Upstream of everything: with Recv off we have asked the server to stop
    // sending this channel, so there is no signal for solo to recover.
    std::atomic<bool> recvEnabled{true};
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
    // Audio thread only. Mute is applied as a ramped gain rather than as a
    // branch, so switching it cannot click.
    GainRamp muteRamp;

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

  // The slots are the network's alone now. They were shared with the practice
  // echo, which owned a range past kMaxStreams so that "one non-audio owner per
  // slot" held statically; with the echo gone the network thread is the only
  // non-audio owner there is, and the ranges collapse to one.
  static constexpr int kTotalSlots = kMaxStreams;
  std::array<StreamSlot, (std::size_t)kTotalSlots> streamSlots;

  void swapSlotRange(int first, int last);
  void mixSlotRange(int first, int last, juce::AudioBuffer<float> &buffer);

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
  // Room membership, which outlives a user's channels. See getRoomMembers.
  std::set<juce::String> roomMembers;

  juce::CriticalSection chatMutex;
  juce::Array<ChatMessage> chatLog;

  double sampleRate = 48000.0;
  int serverBpm = 120;
  int serverBpi = 16;

  std::atomic<bool> defaultRecvEnabled{true};

  juce::String currentHost;
  int currentPort = 2049;
  juce::String currentUsername;
  juce::String currentPassword;

  template <typename ApplyToChannel, typename ApplyToSlot>
  void updateChannelParam(const juce::String &username, int channelIndex,
                          ApplyToChannel toChannel, ApplyToSlot toSlot);

  bool handleMessage(juce::uint8 type,
                     const chalkwalk::ninjam::ByteBuffer &payload);
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

  // The remote half of the global solo bus.
  void recomputeRemoteSolo();

  SessionWriter sessionWriter;

  // Encodes intervals off the message thread, one at a time, in order.
  //
  // A `juce::Thread` like the client itself rather than a `std::thread`, so
  // there is one idiom for "a thread this class owns".
  class EncodeWorker final : public juce::Thread {
  public:
    explicit EncodeWorker(NinjamClient &o)
        : juce::Thread("Antiphon encode"), owner(o) {}
    ~EncodeWorker() override { stopThread(2000); }

    void post(juce::AudioBuffer<float> &&buffer, int numSamples,
              int channelIndex, bool mono, double spreadOverSeconds);
    void discardPending();
    int pendingCount() const;

    void run() override;

  private:
    struct Job {
      juce::AudioBuffer<float> buffer;
      int numSamples = 0;
      int channelIndex = 0;
      bool mono = false;
      double spreadOverSeconds = 0.0;
    };

    NinjamClient &owner;
    mutable juce::CriticalSection jobMutex;
    std::deque<Job> jobs;
    juce::WaitableEvent wake;
  };

  EncodeWorker encodeWorker{*this};

  // Intervals dropped because the encoder could not keep up. Not expected to
  // move: an interval takes a fraction of itself to encode, so a backlog means
  // something is badly wrong and dropping is better than growing without
  // bound.
  std::atomic<int> diagEncodeDrops{0};

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
