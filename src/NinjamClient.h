#pragma once
#include "utils/vorbisencdec.h"
#include <JuceHeader.h>

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

  void run() override;

  bool isConnected() const { return connectionState == 3; }

  void processCapturedAudio(juce::AudioBuffer<float> &buffer, int numSamples);
  void getDecodedAudio(juce::AudioBuffer<float> &buffer);

  void setSampleRate(double sr) { sampleRate = sr; }
  void setServerBpm(int bpm) { serverBpm = bpm; }
  void setServerBpi(int bpi) { serverBpi = bpi; }

  void swapIntervalBuffers();

  void setRemoteUserVolume(const juce::String &username, int channelIndex,
                           float volume);
  void setRemoteUserPan(const juce::String &username, int channelIndex,
                        float pan);
  void setRemoteUserMute(const juce::String &username, int channelIndex,
                         bool mute);
  void setRemoteUserSolo(const juce::String &username, int channelIndex,
                         bool solo);

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

  struct RemoteUserChannel {
    int channelIndex = 0;
    juce::String channelName;
    float volume = 0.5f; // Default -6dB
    float pan = 0.0f;
    bool isMuted = false;
    bool isSoloed = false;
    float peakLevel = 0.0f; // written audio thread (downloadMutex), read UI thread
  };

  struct RemoteUser {
    juce::String username;
    std::map<int, RemoteUserChannel> channels;
  };

  std::map<juce::String, RemoteUser> getRemoteUsers() const;

  struct IntervalBuffer {
    juce::AudioBuffer<float> frontBuffer;
    juce::AudioBuffer<float> backBuffer;
    int frontReadPosition = 0;
    int frontWritePosition = 0; // valid samples in frontBuffer after last swap
    int backWritePosition = 0;
  };

  // Structure to hold an incoming remote stream
  struct RemoteChannel {
    std::unique_ptr<VorbisDecoder> decoder;
    juce::String channelName;
    bool isActive = false;
    juce::String username;
    int channelIndex = 0;
    IntervalBuffer intervalBuffer;
    // Per-channel resamplers used when remote sample rate != local sample rate
    juce::LagrangeInterpolator resamplerL, resamplerR;
  };

  // Set to true on every DOWNLOAD_INTERVAL_BEGIN; audio thread reads and clears it
  // to align its swap phase to the server's interval boundary.
  std::atomic<bool> intervalBeginSignal{false};

private:
  juce::ListenerList<NinjamClientListener> listeners;
  std::unique_ptr<juce::StreamingSocket> socket;

  juce::CriticalSection downloadMutex;
  // Key is the 16-byte GUID as a hex string
  std::map<juce::String, RemoteChannel> activeDownloads;
  std::map<juce::String, RemoteUser> remoteUsers;

  juce::CriticalSection chatMutex;
  juce::Array<ChatMessage> chatLog;

  double sampleRate = 48000.0;
  int serverBpm = 120;
  int serverBpi = 16;

  juce::String currentHost;
  int currentPort = 2049;
  juce::String currentUsername;
  juce::String currentPassword;

  bool handleMessage(juce::uint8 type, const juce::MemoryBlock &payload);
  void sendAuthRequest(const juce::MemoryBlock &challenge);
  void sendChannelInfo();

  bool readFull(void *dest, int numBytes);
  bool writeFull(juce::uint8 type, const void *payload, int numBytes);

  std::atomic<int> connectionState{
      0}; // 0=disconnected, 1=connecting, 2=authenticating, 3=connected

  int64 lastKeepAliveTime = 0;

  bool isSavingTx = false;
  bool isSavingRx = false;
  std::unique_ptr<juce::FileOutputStream> txOggFile;
  std::unique_ptr<juce::AudioFormatWriter> txWavWriter;
  std::unique_ptr<juce::FileOutputStream> rxOggFile;
  std::unique_ptr<juce::AudioFormatWriter> rxWavWriter;
  juce::WavAudioFormat wavFormat;

  juce::CriticalSection txFileMutex;
  juce::CriticalSection rxFileMutex;
};
