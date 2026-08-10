#include "FakeNinjamServer.h"

FakeNinjamServer::FakeNinjamServer() : juce::Thread("FakeNinjamServer") {}

FakeNinjamServer::~FakeNinjamServer() { stop(); }

void FakeNinjamServer::setChallenge(const juce::uint8 c[8]) {
  juce::ScopedLock sl(stateMutex);
  memcpy(challenge, c, 8);
}

void FakeNinjamServer::setEchoUsername(juce::String u) {
  juce::ScopedLock sl(stateMutex);
  echoUsername = std::move(u);
}

bool FakeNinjamServer::start(int bpm, int bpi) {
  {
    juce::ScopedLock sl(stateMutex);
    serverBpm = bpm;
    serverBpi = bpi;
  }
  if (!listener.createListener(0, "127.0.0.1"))
    return false;
  boundPort = listener.getBoundPort();
  if (boundPort <= 0)
    return false;
  startThread();
  return true;
}

void FakeNinjamServer::stop() {
  signalThreadShouldExit();
  listener.close();
  {
    juce::ScopedLock sl(clientMutex);
    if (client)
      client->close();
  }
  // The return used to be ignored. Worth checking: a thread that misses this
  // deadline is one whose WaitableEvents ~Thread() is about to destroy out from
  // under it, which is how this hung CI for a day. See ROADMAP.md.
  if (!stopThread(2000)) {
    std::fprintf(stderr, "FakeNinjamServer: thread did not exit within 2000ms; "
                         "destroying it now is unsafe\n");
    std::fflush(stderr);
  }
  juce::ScopedLock sl(clientMutex);
  client.reset();
}

bool FakeNinjamServer::hasClient() const {
  juce::ScopedLock sl(clientMutex);
  return client != nullptr && client->isConnected();
}

bool FakeNinjamServer::send(juce::uint8 type, const void *data, int size) {
  juce::ScopedLock sl(clientMutex);
  if (!client || !client->isConnected())
    return false;

  juce::uint8 header[NinjamProtocol::kHeaderSize];
  NinjamProtocol::writeFrameHeader(header, type, (juce::uint32)size);
  if (client->write(header, NinjamProtocol::kHeaderSize) !=
      NinjamProtocol::kHeaderSize)
    return false;
  if (size > 0 && client->write(data, size) != size)
    return false;
  return true;
}

bool FakeNinjamServer::sendRaw(juce::uint8 type, const void *data, int size) {
  return send(type, data, size);
}

bool FakeNinjamServer::readExactly(void *dest, int numBytes) {
  auto *d = static_cast<char *>(dest);
  int got = 0;
  while (got < numBytes) {
    if (threadShouldExit())
      return false;
    juce::StreamingSocket *s = nullptr;
    {
      juce::ScopedLock sl(clientMutex);
      s = client.get();
    }
    if (!s || !s->isConnected())
      return false;
    const int r = s->read(d + got, numBytes - got, true);
    if (r <= 0)
      return false;
    got += r;
  }
  return true;
}

void FakeNinjamServer::run() {
  // Polled rather than blocking. waitForNextConnection() waits forever, and
  // closing the listener from another thread does not reliably wake it -- the
  // same trap the client had, fixed there for the same reason
  // (NinjamClient::readFull). It matters more than a slow exit: a thread that
  // outlives stopThread's timeout is one this object is then destroyed on top
  // of. Poll, and honour threadShouldExit between polls.
  juce::StreamingSocket *accepted = nullptr;
  while (!threadShouldExit()) {
    const int ready = listener.waitUntilReady(true, 50);
    if (ready < 0)
      return;
    if (ready > 0) {
      accepted = listener.waitForNextConnection();
      break;
    }
  }
  if (accepted == nullptr || threadShouldExit()) {
    delete accepted;
    return;
  }
  {
    juce::ScopedLock sl(clientMutex);
    client.reset(accepted);
  }

  // The server speaks first: SERVER_AUTH_CHALLENGE.
  {
    juce::MemoryBlock ch;
    {
      juce::ScopedLock sl(stateMutex);
      ch.append(challenge, 8);
    }
    const juce::uint32 caps = 0, version = 0x00020000;
    ch.append(&caps, 4);
    ch.append(&version, 4);
    const char noLicence[1] = {0};
    ch.append(noLicence, 1);
    send(0x00, ch.getData(), (int)ch.getSize());
  }

  while (!threadShouldExit()) {
    juce::StreamingSocket *s = nullptr;
    {
      juce::ScopedLock sl(clientMutex);
      s = client.get();
    }
    if (!s || !s->isConnected())
      break;

    const int ready = s->waitUntilReady(true, 50);
    if (ready < 0)
      break;
    if (ready == 0)
      continue;

    juce::uint8 header[NinjamProtocol::kHeaderSize];
    if (!readExactly(header, NinjamProtocol::kHeaderSize))
      break;

    NinjamProtocol::FrameHeader frame;
    if (!NinjamProtocol::readFrameHeader(header, frame))
      break;

    juce::MemoryBlock payload;
    if (frame.length > 0) {
      payload.setSize(frame.length, true);
      if (!readExactly(payload.getData(), (int)frame.length))
        break;
    }

    {
      juce::ScopedLock sl(stateMutex);
      received.add({frame.type, payload});
    }
    handleClientMessage(frame.type, payload);
  }
}

void FakeNinjamServer::handleClientMessage(juce::uint8 type,
                                           const juce::MemoryBlock &payload) {
  if (type == 0x80) {
    // CLIENT_AUTH_USER -> grant or deny. The reply must carry the channel cap:
    // the reference client stores it as m_max_localch and silently refuses to
    // transmit on any channel index at or above it, so a reply of just the
    // flag byte gets no audio at all from a stock client.
    auto reply = NinjamProtocol::buildAuthReply(grantAccess.load(), {}, 32);
    send(0x01, reply.getData(), (int)reply.getSize());
    if (!grantAccess.load())
      return;

    int bpm, bpi;
    {
      juce::ScopedLock sl(stateMutex);
      bpm = serverBpm;
      bpi = serverBpi;
    }
    sendServerConfig(bpm, bpi);
    return;
  }

  if (!echoEnabled.load())
    return;

  if (type == 0x83) {
    // UPLOAD_INTERVAL_BEGIN -> DOWNLOAD_INTERVAL_BEGIN with a username appended.
    NinjamProtocol::IntervalBegin begin;
    if (!NinjamProtocol::parseIntervalBegin(payload, begin))
      return;
    juce::String user;
    {
      juce::ScopedLock sl(stateMutex);
      user = echoUsername;
    }
    auto echoed = NinjamProtocol::buildIntervalBegin(
        begin.guid, begin.estimatedSize, begin.fourcc, begin.channelIndex,
        user);
    send(0x04, echoed.getData(), (int)echoed.getSize());
    return;
  }

  if (type == 0x84) {
    // UPLOAD_INTERVAL_WRITE and DOWNLOAD_INTERVAL_WRITE payloads are identical.
    send(0x05, payload.getData(), (int)payload.getSize());
    NinjamProtocol::IntervalWrite w;
    if (NinjamProtocol::parseIntervalWrite(payload, w) && w.isFinal)
      uploadsCompleted.fetch_add(1);
  }
}

void FakeNinjamServer::sendServerConfig(int bpm, int bpi) {
  {
    juce::ScopedLock sl(stateMutex);
    serverBpm = bpm;
    serverBpi = bpi;
  }
  juce::uint8 p[4];
  p[0] = (juce::uint8)(bpm & 0xFF);
  p[1] = (juce::uint8)((bpm >> 8) & 0xFF);
  p[2] = (juce::uint8)(bpi & 0xFF);
  p[3] = (juce::uint8)((bpi >> 8) & 0xFF);
  send(0x02, p, 4);
}

void FakeNinjamServer::sendUserInfo(const juce::String &user, int chIdx,
                                    const juce::String &chName, bool active) {
  juce::MemoryBlock p;
  const juce::uint8 head[6] = {(juce::uint8)(active ? 1 : 0),
                               (juce::uint8)chIdx,
                               0,
                               0,  // volume, LE int16
                               0,  // pan
                               0}; // flags
  p.append(head, 6);
  p.append(user.toRawUTF8(), (size_t)user.getNumBytesAsUTF8() + 1);
  p.append(chName.toRawUTF8(), (size_t)chName.getNumBytesAsUTF8() + 1);
  send(0x03, p.getData(), (int)p.getSize());
}

void FakeNinjamServer::sendChat(const juce::String &type,
                                const juce::String &p1,
                                const juce::String &p2) {
  auto b = NinjamProtocol::buildChat(type, p1, p2);
  send(0xC0, b.getData(), (int)b.getSize());
}

int FakeNinjamServer::countReceived(juce::uint8 type) const {
  juce::ScopedLock sl(stateMutex);
  int n = 0;
  for (const auto &r : received)
    if (r.type == type)
      ++n;
  return n;
}

juce::Array<FakeNinjamServer::Received>
FakeNinjamServer::messagesOfType(juce::uint8 type) const {
  juce::ScopedLock sl(stateMutex);
  juce::Array<Received> out;
  for (const auto &r : received)
    if (r.type == type)
      out.add(r);
  return out;
}

juce::MemoryBlock FakeNinjamServer::lastPayloadOfType(juce::uint8 type) const {
  juce::ScopedLock sl(stateMutex);
  for (int i = received.size() - 1; i >= 0; --i)
    if (received.getReference(i).type == type)
      return received.getReference(i).payload;
  return {};
}

void FakeNinjamServer::clearReceived() {
  juce::ScopedLock sl(stateMutex);
  received.clear();
}
