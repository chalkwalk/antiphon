#pragma once

#include <memory>
#include <string>
#include <vector>

// The room, as a bot needs it.
//
// A bot is an ordinary NINJAM client, but almost none of a client is a bot's
// business. It never plays anybody back -- it is deaf by construction, so that
// an unsubscribed client never causes the server to send it an interval -- so
// it needs no mixer, no playback queue, no interval delay and no audio device.
// What is left is small enough to write down: connect, hear what is said, say
// something, know who is here, and put an interval on the wire.
//
// THIRTEEN CALLS OUT AND SIX BACK, measured against what `PracticeBot` actually
// used rather than designed from what a client can do.
//
// The point of the interface is that the bots do not know what is under it.
// Antiphon supplies an adapter over its own `NinjamClient`; a standalone
// `jambot` would supply a smaller one over a socket, and neither is visible
// from here. Without this, the bots and the plugin's client are one lump: the
// bots cannot leave, and the client cannot be replaced.
//
// JUCE-FREE and `std::string`, deliberately. This interface IS the line the
// bots are extracted along, so it must not carry a type from either side of it.

namespace BotClient {

// Somebody in the room. Membership outlives channels: a player who has joined
// but published nothing is present and has to be counted as present.
struct Member {
  std::string username;
  int channelCount = 0;
};

// One of a player's channels, carrying only what a bot decides with: whether
// to subscribe, and what to call it.
struct Channel {
  int index = 0;
  std::string name;
  bool recvEnabled = false;
};

struct Peer {
  std::string username;
  std::vector<Channel> channels;
};

// What the room tells a bot. Every one has a default, because a bot that only
// wants chat should not have to write five empty overrides.
class Listener {
public:
  virtual ~Listener() = default;

  virtual void onConnected() {}
  virtual void onDisconnected(const std::string &reason) { (void)reason; }

  // The server's tempo and interval length. Not a request -- it has already
  // happened, and every client in the room got the same message.
  virtual void onServerConfig(int bpm, int bpi) { (void)bpm; (void)bpi; }

  // Somebody's channels changed. Coarse on purpose: it says look again.
  virtual void onUserInfoChange() {}

  // A JOIN or a PART. Distinct from `onUserInfoChange` because an event does
  // not go stale -- a player who joins and leaves between two scans of the
  // member list was, as far as any scan can tell, never there.
  virtual void onRoomMembershipChange(const std::string &username, bool joined) {
    (void)username;
    (void)joined;
  }

  // `type` is the server's: "MSG" for the room, "PRIVMSG" for one person,
  // "TOPIC" and so on. Passed through rather than parsed, because what counts
  // as addressed to you is the bot's question and not the transport's.
  virtual void onChatMessage(const std::string &type,
                             const std::string &username,
                             const std::string &text) {
    (void)type;
    (void)username;
    (void)text;
  }
};

class Client {
public:
  virtual ~Client() = default;

  virtual void addListener(Listener *listener) = 0;
  virtual void removeListener(Listener *listener) = 0;

  // Before connecting: what we will send, and at what rate.
  virtual void setSampleRate(double sampleRate) = 0;
  virtual void setChannels(const std::vector<std::string> &names) = 0;

  // Deaf by default is what keeps a room of bots costing one client's worth of
  // interval buffers rather than one per bot: an unsubscribed client never
  // causes the server to send it an interval, so it never allocates one.
  virtual void setDefaultRecvEnabled(bool enabled) = 0;

  virtual void connect(const std::string &host, int port,
                       const std::string &username,
                       const std::string &password) = 0;

  // Terminal. A bot that reconnects is a bot nobody can get rid of, and these
  // can be pointed at a real server -- so the absence of a retry is a feature
  // and belongs in the interface rather than in one implementation of it.
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  virtual std::vector<Member> members() const = 0;
  virtual std::vector<Peer> peers() const = 0;
  virtual void setRecv(const std::string &username, int channelIndex,
                       bool enabled) = 0;

  virtual void sendChat(const std::string &text) = 0;
  virtual void sendPrivate(const std::string &to, const std::string &text) = 0;

  // One interval of audio, interleaved as separate channel pointers. `right`
  // may be null for a mono voice.
  //
  // Called from whatever thread the caller conducts on, never an audio thread:
  // encoding an interval allocates, and a bot has no real-time obligation
  // because nothing is waiting on it.
  virtual void transmit(const float *left, const float *right,
                        int numSamples) = 0;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace BotClient
