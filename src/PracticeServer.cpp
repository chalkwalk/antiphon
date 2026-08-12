#include "PracticeServer.h"

#include "SocketWrite.h"

PracticeServer::PracticeServer() : juce::Thread("PracticeServer") {}

PracticeServer::~PracticeServer() { stop(); }

bool PracticeServer::start(int bpmIn, int bpiIn) {
  serverBpm = bpmIn;
  serverBpi = bpiIn;

  // 127.0.0.1 explicitly, never INADDR_ANY. The room must not be reachable from
  // anywhere but this machine -- see the class comment.
  if (!listener.createListener(0, "127.0.0.1"))
    return false;

  const int p = listener.getBoundPort();
  if (p <= 0) {
    listener.close();
    return false;
  }
  boundPort = p;
  startThread();
  return true;
}

void PracticeServer::stop() {
  signalThreadShouldExit();
  listener.close();
  {
    juce::ScopedLock sl(clientsMutex);
    for (auto &c : clients)
      if (c->socket)
        c->socket->close();
  }
  // A thread that misses this deadline is one whose WaitableEvents ~Thread() is
  // about to destroy underneath it. FakeNinjamServer learned this the hard way.
  if (!stopThread(2000)) {
    std::fprintf(stderr, "PracticeServer: thread did not exit within 2000ms; "
                         "destroying it now is unsafe\n");
    std::fflush(stderr);
  }
  {
    juce::ScopedLock sl(clientsMutex);
    clients.clear();
  }
  boundPort = 0;
}

int PracticeServer::bpm() const { return serverBpm.load(); }
int PracticeServer::bpi() const { return serverBpi.load(); }

void PracticeServer::setConfig(int bpmIn, int bpiIn) {
  serverBpm = bpmIn;
  serverBpi = bpiIn;
  auto p = NinjamProtocol::buildServerConfig(bpmIn, bpiIn);
  broadcastExcept(nullptr, 0x02, p.getData(), (int)p.getSize());
}

void PracticeServer::setTopic(const juce::String &topic) {
  {
    juce::ScopedLock sl(stateMutex);
    roomTopic = topic;
  }
  auto p = NinjamProtocol::buildChat("TOPIC", {}, topic);
  broadcastExcept(nullptr, 0xC0, p.getData(), (int)p.getSize());
}

void PracticeServer::broadcastChat(const juce::String &from,
                                   const juce::String &text) {
  auto p = NinjamProtocol::buildChat("MSG", from, text);
  broadcastExcept(nullptr, 0xC0, p.getData(), (int)p.getSize());
}

int PracticeServer::clientCount() const {
  juce::ScopedLock sl(clientsMutex);
  return (int)clients.size();
}

juce::StringArray PracticeServer::connectedUsernames() const {
  juce::ScopedLock sl(clientsMutex);
  juce::StringArray names;
  for (const auto &c : clients)
    if (c->authenticated)
      names.add(c->username);
  return names;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

bool PracticeServer::sendTo(Client &c, juce::uint8 type, const void *data,
                            int size) {
  if (!c.socket || !c.socket->isConnected())
    return false;

  juce::uint8 header[NinjamProtocol::kHeaderSize];
  NinjamProtocol::writeFrameHeader(header, type, (juce::uint32)size);
  if (SocketWrite::noSigPipe(*c.socket, header, NinjamProtocol::kHeaderSize) !=
      NinjamProtocol::kHeaderSize)
    return false;
  if (size > 0 && SocketWrite::noSigPipe(*c.socket, data, size) != size)
    return false;
  return true;
}

void PracticeServer::broadcastExcept(const Client *skip, juce::uint8 type,
                                     const void *data, int size) {
  juce::ScopedLock sl(clientsMutex);
  for (auto &c : clients) {
    if (c.get() == skip || !c->authenticated)
      continue;
    sendTo(*c, type, data, size);
  }
}

bool PracticeServer::subscribed(const Client &to, const juce::String &user,
                                int channelIndex) {
  // Channel indices at or above 32 have no bit; buildUsermask drops them
  // rather than shifting past the width of the mask.
  if (channelIndex < 0 || channelIndex >= 32)
    return false;
  auto it = to.usermask.find(user);
  if (it == to.usermask.end())
    return false;
  return (it->second & (1u << channelIndex)) != 0;
}

void PracticeServer::relayAudio(const Client &from, int channelIndex,
                                juce::uint8 type, const void *data, int size) {
  juce::ScopedLock sl(clientsMutex);
  for (auto &c : clients) {
    if (c.get() == &from || !c->authenticated)
      continue;

    // Not subscribed: send nothing. This is the whole reason bots are cheap --
    // a deaf bot never causes an interval buffer to be allocated at the far
    // end, and a room of four costs one client's worth of memory, not five.
    // Note that an unsubscribed client sends a mask of zero rather than
    // omitting the entry, so presence in the map is not consent.
    if (!subscribed(*c, from.username, channelIndex))
      continue;

    // An audio frame is large enough to fill a socket buffer, and a blocking
    // write here would stall the whole room. Dropping is safe where blocking is
    // not: interval delivery is all-or-nothing, so a client that misses part of
    // an interval simply does not play it -- exactly what happens on a real
    // network under loss.
    if (c->socket == nullptr || c->socket->waitUntilReady(false, 0) <= 0)
      continue;

    sendTo(*c, type, data, size);
  }
}

// ---------------------------------------------------------------------------
// Room bookkeeping
// ---------------------------------------------------------------------------

juce::String PracticeServer::uniqueUsername(const juce::String &wanted) const {
  // Caller holds clientsMutex. Two players with one name would collide in
  // NinjamClient's (username, channelIndex) slot key and mix into each other.
  const juce::String base = wanted.isEmpty() ? juce::String("player") : wanted;
  juce::String candidate = base;
  int suffix = 1;
  bool clash = true;
  while (clash) {
    clash = false;
    for (const auto &c : clients)
      if (c->authenticated && c->username == candidate) {
        clash = true;
        break;
      }
    if (clash)
      candidate = base + juce::String(++suffix);
  }
  return candidate;
}

void PracticeServer::sendRoster(Client &to) {
  // Caller holds clientsMutex.
  std::vector<NinjamProtocol::UserInfoEntry> entries;
  for (const auto &c : clients) {
    if (c.get() == &to || !c->authenticated)
      continue;
    for (const auto &[idx, name] : c->channels) {
      NinjamProtocol::UserInfoEntry e;
      e.active = true;
      e.channelIndex = idx;
      e.username = c->username;
      e.channelName = name;
      entries.push_back(std::move(e));
    }
  }
  if (entries.empty())
    return;

  auto p = NinjamProtocol::buildUserInfo(entries);
  sendTo(to, 0x03, p.getData(), (int)p.getSize());
}

void PracticeServer::broadcastChannels(
    const juce::String &username,
    const std::map<int, juce::String> &channels, bool active,
    const Client *skip) {
  // Caller holds clientsMutex.
  std::vector<NinjamProtocol::UserInfoEntry> entries;
  for (const auto &[idx, name] : channels) {
    NinjamProtocol::UserInfoEntry e;
    e.active = active;
    e.channelIndex = idx;
    e.username = username;
    e.channelName = name;
    entries.push_back(std::move(e));
  }
  if (entries.empty())
    return;

  auto p = NinjamProtocol::buildUserInfo(entries);
  for (auto &other : clients) {
    if (other.get() == skip || !other->authenticated)
      continue;
    sendTo(*other, 0x03, p.getData(), (int)p.getSize());
  }
}

// ---------------------------------------------------------------------------
// The thread
// ---------------------------------------------------------------------------

void PracticeServer::run() {
  while (!threadShouldExit()) {
    acceptPendingConnections();

    bool didWork = false;
    {
      juce::ScopedLock sl(clientsMutex);
      for (int i = (int)clients.size() - 1; i >= 0; --i) {
        auto &c = *clients[(size_t)i];
        if (c.socket == nullptr || !c.socket->isConnected()) {
          dropClient(i);
          continue;
        }
        if (c.socket->waitUntilReady(true, 0) <= 0)
          continue;
        if (!readFromClient(c)) {
          dropClient(i);
          continue;
        }
        didWork = true;
        drainFrames(c);
      }
    }

    // Poll rather than block: waitForNextConnection waits forever and closing
    // the listener from another thread does not reliably wake it, which is the
    // same trap NinjamClient::readFull and FakeNinjamServer both hit.
    if (!didWork)
      wait(5);
  }
}

void PracticeServer::acceptPendingConnections() {
  if (listener.waitUntilReady(true, 0) <= 0)
    return;

  auto *accepted = listener.waitForNextConnection();
  if (accepted == nullptr)
    return;

  auto client = std::make_unique<Client>();
  client->socket.reset(accepted);
  SocketWrite::prepare(*client->socket);
  for (int i = 0; i < 8; ++i)
    client->challenge[i] = (juce::uint8)rng.nextInt(256);

  auto p = NinjamProtocol::buildAuthChallenge(client->challenge);
  // The server speaks first.
  sendTo(*client, 0x00, p.getData(), (int)p.getSize());

  juce::ScopedLock sl(clientsMutex);
  clients.push_back(std::move(client));
}

void PracticeServer::dropClient(int index) {
  // Caller holds clientsMutex.
  auto &c = *clients[(size_t)index];
  if (c.authenticated) {
    broadcastChannels(c.username, c.channels, false, &c);

    // PART, not a MSG saying so: NinjamClient only removes a name from
    // roomMembers on a real PART (NinjamClient.cpp:652), and a bot that leaves
    // when its owner does needs that to be accurate.
    auto part = NinjamProtocol::buildChat("PART", c.username);
    for (auto &other : clients) {
      if (other.get() == &c || !other->authenticated)
        continue;
      sendTo(*other, 0xC0, part.getData(), (int)part.getSize());
    }
  }
  clients.erase(clients.begin() + index);
}

bool PracticeServer::readFromClient(Client &c) {
  char buf[8192];
  const int got = c.socket->read(buf, (int)sizeof(buf), false);
  if (got <= 0)
    return false;
  c.pending.append(buf, (size_t)got);
  return true;
}

void PracticeServer::drainFrames(Client &c) {
  // Caller holds clientsMutex.
  size_t offset = 0;
  while (true) {
    const size_t avail = c.pending.getSize() - offset;
    if (avail < (size_t)NinjamProtocol::kHeaderSize)
      break;

    const auto *base = static_cast<const juce::uint8 *>(c.pending.getData());
    NinjamProtocol::FrameHeader frame;
    if (!NinjamProtocol::readFrameHeader(base + offset, frame)) {
      // Oversized length: the stream is desynchronised and cannot be recovered.
      c.socket->close();
      return;
    }

    const size_t total = (size_t)NinjamProtocol::kHeaderSize + frame.length;
    if (avail < total)
      break;

    juce::MemoryBlock payload;
    if (frame.length > 0)
      payload.append(base + offset + NinjamProtocol::kHeaderSize, frame.length);

    offset += total;
    handleFrame(c, frame.type, payload);
  }

  if (offset > 0)
    c.pending.removeSection(0, offset);
}

void PracticeServer::handleFrame(Client &c, juce::uint8 type,
                                 const juce::MemoryBlock &payload) {
  // Caller holds clientsMutex.
  switch (type) {
  case 0x80: { // CLIENT_AUTH_USER
    NinjamProtocol::AuthUser au;
    if (!NinjamProtocol::parseAuthUser(payload, au)) {
      c.socket->close();
      return;
    }

    // Any password is accepted: this room is on the loopback interface and
    // exists to be walked into. Rejecting one would only be theatre.
    c.username = uniqueUsername(au.username);
    c.authenticated = true;

    // The cap matters: the reference client stores it as m_max_localch and
    // silently refuses to transmit on any channel index at or above it, so a
    // reply without it gets no audio at all (njclient.cpp:1096).
    auto reply = NinjamProtocol::buildAuthReply(true, {}, 32);
    sendTo(c, 0x01, reply.getData(), (int)reply.getSize());

    auto cfg =
        NinjamProtocol::buildServerConfig(serverBpm.load(), serverBpi.load());
    sendTo(c, 0x02, cfg.getData(), (int)cfg.getSize());

    juce::String topic;
    {
      juce::ScopedLock sl(stateMutex);
      topic = roomTopic;
    }
    if (topic.isNotEmpty()) {
      auto t = NinjamProtocol::buildChat("TOPIC", {}, topic);
      sendTo(c, 0xC0, t.getData(), (int)t.getSize());
    }

    // Who is already here, then tell everyone else who just arrived. JOIN and
    // PART are how the far end maintains room membership for players who have
    // no audio channels at all.
    for (const auto &other : clients) {
      if (other.get() == &c || !other->authenticated)
        continue;
      auto j = NinjamProtocol::buildChat("JOIN", other->username);
      sendTo(c, 0xC0, j.getData(), (int)j.getSize());
    }

    auto joined = NinjamProtocol::buildChat("JOIN", c.username);
    for (auto &other : clients) {
      if (other.get() == &c || !other->authenticated)
        continue;
      sendTo(*other, 0xC0, joined.getData(), (int)joined.getSize());
    }

    sendRoster(c);
    return;
  }

  case 0x81: { // CLIENT_SET_USERMASK
    std::vector<NinjamProtocol::UsermaskEntry> masks;
    NinjamProtocol::parseUsermask(payload, masks);
    for (const auto &m : masks)
      c.usermask[m.username] = m.mask;
    return;
  }

  case 0x82: { // CLIENT_SET_CHANNEL_INFO
    std::vector<NinjamProtocol::ChannelInfoEntry> chans;
    if (!NinjamProtocol::parseChannelInfo(payload, chans))
      return;

    // A channel that has gone is announced as inactive before the map forgets
    // it, or the far end keeps a strip for a channel nobody is sending on.
    std::map<int, juce::String> departed;
    for (const auto &[idx, name] : c.channels)
      if ((size_t)idx >= chans.size())
        departed[idx] = name;
    if (!departed.empty())
      broadcastChannels(c.username, departed, false, &c);

    c.channels.clear();
    for (size_t i = 0; i < chans.size(); ++i)
      c.channels[(int)i] = chans[i].name;

    broadcastChannels(c.username, c.channels, true, &c);
    return;
  }

  case 0x83: { // UPLOAD_INTERVAL_BEGIN -> DOWNLOAD_INTERVAL_BEGIN
    NinjamProtocol::IntervalBegin begin;
    if (!NinjamProtocol::parseIntervalBegin(payload, begin))
      return;

    c.uploadChannel[begin.guidHex] = begin.channelIndex;
    auto out = NinjamProtocol::buildIntervalBegin(
        begin.guid, begin.estimatedSize, begin.fourcc, begin.channelIndex,
        c.username);
    relayAudio(c, begin.channelIndex, 0x04, out.getData(), (int)out.getSize());
    return;
  }

  case 0x84: { // UPLOAD_INTERVAL_WRITE -> DOWNLOAD_INTERVAL_WRITE
    NinjamProtocol::IntervalWrite w;
    if (!NinjamProtocol::parseIntervalWrite(payload, w))
      return;

    // A write for a GUID we never saw a begin for cannot be attributed to a
    // channel, so it cannot be filtered, so it is dropped.
    auto it = c.uploadChannel.find(w.guidHex);
    if (it == c.uploadChannel.end())
      return;
    const int channelIndex = it->second;
    if (w.isFinal)
      c.uploadChannel.erase(it);

    // The 0x84 and 0x05 payloads are byte-identical, so this is a forward.
    relayAudio(c, channelIndex, 0x05, payload.getData(), (int)payload.getSize());
    return;
  }

  case 0xC0: { // CHAT_MESSAGE
    NinjamProtocol::Chat chat;
    if (!NinjamProtocol::parseChat(payload, chat))
      return;

    if (chat.type == "MSG") {
      auto out = NinjamProtocol::buildChat("MSG", c.username, chat.p1);
      for (auto &other : clients) {
        if (!other->authenticated)
          continue;
        sendTo(*other, 0xC0, out.getData(), (int)out.getSize());
      }
      return;
    }

    if (chat.type == "PRIVMSG") {
      auto out = NinjamProtocol::buildChat("PRIVMSG", c.username, chat.p2);
      for (auto &other : clients)
        if (other->authenticated && other->username == chat.p1)
          sendTo(*other, 0xC0, out.getData(), (int)out.getSize());
      return;
    }

    if (chat.type == "TOPIC") {
      {
        juce::ScopedLock sl(stateMutex);
        roomTopic = chat.p2;
      }
      auto out = NinjamProtocol::buildChat("TOPIC", c.username, chat.p2);
      for (auto &other : clients)
        if (other->authenticated)
          sendTo(*other, 0xC0, out.getData(), (int)out.getSize());
      return;
    }
    return;
  }

  case 0xFD: // KEEP_ALIVE -- nothing to do, the read itself proved liveness.
  default:
    return;
  }
}
