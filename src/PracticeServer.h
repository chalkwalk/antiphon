#pragma once

#include "NinjamProtocol.h"

#include <chalkwalk/ninjam/Voting.h>
#include <cstdint>
#include <JuceHeader.h>
#include <map>
#include <string>
#include <memory>
#include <vector>

// A real Ninjam server, in process, on the loopback interface.
//
// This is what makes practice mode a jam rather than a simulation of one:
// NinjamClient keys remote players purely off the wire, so a room served from
// here lights up the whole connected UI -- phase bar, remote strips, routing,
// chat, sync, recording -- with no special-casing anywhere.
//
// It is small because Ninjam servers are small. The interval grid is entirely
// client-side (every client plays each received interval starting at its own
// downbeat, PRINCIPLES 9), so there is no clock here at all. The server
// authenticates, tracks who is in the room, and relays.
//
// Not a general-purpose server, and not trying to be: no licences, no
// persistence, no anonymous-user rules, no bans. It serves a practice room.
//
// SAFETY: the listener binds 127.0.0.1 explicitly and nothing else. That is the
// property that replaces the old practice echo's "offline by construction"
// argument (DESIGN.md 6.2) now that practising means being genuinely connected
// and genuinely transmitting.
class PracticeServer : private juce::Thread {
public:
  PracticeServer();
  ~PracticeServer() override;

  bool start(int bpm = 100, int bpi = 16);
  void stop();

  int port() const { return boundPort.load(); }
  bool isListening() const { return boundPort.load() > 0; }

  // Broadcasts SERVER_CONFIG_CHANGE. Safe from any thread.
  void setConfig(int bpm, int bpi);
  int bpm() const;
  int bpi() const;

  // `!vote bpm 130`, tallied as the reference server tallies it
  // (`docs/PROTOCOL.md`, *The voting threshold*).
  //
  // OFF by default, as a stock server is, and that is not laziness about a
  // default: a vote counts every client, so in a room of one player and four
  // bots the player needs three votes and has one. Until the band can cast the
  // other two -- `ROADMAP.md`, *The band's vote policy* -- a room with voting
  // switched on is a room where every vote visibly fails, which teaches
  // something worse than a room with no voting in it.
  //
  // The mechanism is complete and tested; what is missing is the band. On the
  // day it votes, a practice room turns this on with one call.
  //
  // Percentage 1..100, or anything outside that to disable, as a server does.
  void setVoting(int thresholdPercent, int timeoutSeconds);
  int votingThreshold() const { return voteThreshold.load(); }
  int votingTimeout() const { return voteTimeout.load(); }

  void setTopic(const juce::String &topic);

  // Relayed as though `from` had typed it, so the client renders it through the
  // ordinary chat path. `from` empty means the server itself.
  void broadcastChat(const juce::String &from, const juce::String &text);

  juce::StringArray connectedUsernames() const;
  int clientCount() const;

private:
  struct Client {
    std::unique_ptr<juce::StreamingSocket> socket;
    juce::String username;
    bool authenticated = false;
    juce::uint8 challenge[8] = {};

    // Channel index -> name, as last declared by CLIENT_SET_CHANNEL_INFO.
    std::map<int, juce::String> channels;

    // Who this client has asked to hear, by CLIENT_SET_USERMASK: a bit per
    // channel index. Absent from the map and present-but-zero both mean "send
    // me nothing", which is how a bot stays deaf and why the room does not cost
    // a NinjamClient's worth of interval buffers per bot.
    std::map<std::string, juce::uint32> usermask;

    // GUID -> channel index for this client's uploads in flight. Only
    // UPLOAD_INTERVAL_BEGIN carries the channel index; the writes that follow
    // identify themselves by GUID alone, so the relay has to remember which
    // channel each one belongs to in order to honour a subscription.
    std::map<std::string, int> uploadChannel;

    // One vote of each kind, with the wall-clock second it was cast, exactly
    // as the server keeps them: a second `!vote` replaces the first and
    // refreshes its expiry, and zero means never voted.
    int voteBpm = 0;
    std::int64_t voteBpmAt = 0;
    int voteBpi = 0;
    std::int64_t voteBpiAt = 0;

    // Frames arrive split across reads and coalesced across writes, so bytes
    // accumulate here until a whole frame is present.
    juce::MemoryBlock pending;
  };

  void run() override;
  void acceptPendingConnections();
  bool readFromClient(Client &c);
  void drainFrames(Client &c);
  void handleFrame(Client &c, juce::uint8 type,
                   const chalkwalk::ninjam::ByteBuffer &payload);
  void dropClient(int index);

  // Control frames are written blocking: they are small, they always fit, and
  // losing one desynchronises the room. Audio frames go through relayAudio,
  // which drops rather than blocks -- see the comment there.
  // The Locked suffix means the caller already holds clientsMutex. The frame
  // handler runs with it held and needs to relay from inside that, so these
  // must not take it again: juce::CriticalSection is recursive and would
  // permit it, but a lock whose depth depends on the call path is a lock
  // nobody can reason about -- and TSan does not model the recursion, so it
  // reports every such acquisition.
  bool sendTo(Client &c, juce::uint8 type, const void *data, int size);
  void relayAudioLocked(const Client &from, int channelIndex, juce::uint8 type,
                        const void *data, int size);
  static bool subscribed(const Client &to, const juce::String &user,
                         int channelIndex);
  void broadcastExceptLocked(const Client *skip, juce::uint8 type,
                             const void *data, int size);

  // `text` is the whole chat line, `!vote` included. Returns false if it was
  // not a vote at all, in which case the caller relays it as ordinary chat.
  bool handleVoteLocked(Client &c, const juce::String &text);
  void announceVoteLocked(bool isBpm);
  void applyConfigLocked(int bpm, int bpi);
  void serverSayLocked(Client *only, const juce::String &text);

  void sendRoster(Client &to);
  void broadcastChannels(const juce::String &username,
                         const std::map<int, juce::String> &channels,
                         bool active, const Client *skip);
  juce::String uniqueUsername(const juce::String &wanted) const;

  juce::StreamingSocket listener;
  std::vector<std::unique_ptr<Client>> clients;
  mutable juce::CriticalSection clientsMutex;

  std::atomic<int> boundPort{0};
  std::atomic<int> serverBpm{120};
  std::atomic<int> serverBpi{8};
  std::atomic<int> voteThreshold{
      chalkwalk::ninjam::voting::kDefaultThresholdPercent};
  std::atomic<int> voteTimeout{
      chalkwalk::ninjam::voting::kDefaultTimeoutSeconds};
  juce::String roomTopic;
  mutable juce::CriticalSection stateMutex;

  juce::Random rng;
};
