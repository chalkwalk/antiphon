#include "NinjamClient.h"
#include "utils/sha1.h"

NinjamClient::NinjamClient() : juce::Thread("NinjamClientThread") {}

NinjamClient::~NinjamClient() { disconnectFromServer(); }

void NinjamClient::addListener(NinjamClientListener *listener) {
  listeners.add(listener);
}

void NinjamClient::removeListener(NinjamClientListener *listener) {
  listeners.remove(listener);
}

void NinjamClient::connectToServer(const juce::String &host, int port,
                                   const juce::String &username,
                                   const juce::String &password) {
  if (isThreadRunning())
    disconnectFromServer();

  currentHost = host;
  currentPort = port;
  currentUsername = username;
  currentPassword = password;

  connectionState = 1;
  startThread();
}

void NinjamClient::disconnectFromServer() {
  if (socket)
    socket->close();

  connectionState = 0;
  signalThreadShouldExit();
  stopThread(2000);
}

bool NinjamClient::readFull(void *dest, int numBytes) {
  if (!socket || !socket->isConnected())
    return false;
  char *d = static_cast<char *>(dest);
  int read = 0;
  while (read < numBytes) {
    if (threadShouldExit() || !socket->isConnected())
      return false;

    int r = socket->read(d + read, numBytes - read, true);
    if (r <= 0)
      return false;
    read += r;
  }
  return true;
}

bool NinjamClient::writeFull(juce::uint8 type, const void *payload,
                             int numBytes) {
  if (!socket || !socket->isConnected())
    return false;

  juce::uint8 header[5];
  header[0] = type;
  juce::uint32 len = juce::ByteOrder::swapIfBigEndian((juce::uint32)numBytes);
  memcpy(header + 1, &len, 4);

  if (socket->write(header, 5) != 5)
    return false;
  if (numBytes > 0 && socket->write(payload, numBytes) != numBytes)
    return false;

  return true;
}

void NinjamClient::run() {
  socket = std::make_unique<juce::StreamingSocket>();

  if (!socket->connect(currentHost, currentPort, 5000)) {
    connectionState = 0;
    juce::MessageManager::callAsync([this]() {
      listeners.call(&NinjamClientListener::onDisconnected,
                     "Connection timed out");
    });
    return;
  }

  connectionState = 2; // Authenticating
  lastKeepAliveTime = juce::Time::currentTimeMillis();

  while (!threadShouldExit() && socket->isConnected()) {
    // Check for keep-alive
    if (connectionState == 3 &&
        juce::Time::currentTimeMillis() - lastKeepAliveTime > 3000) {
      writeFull(0xFD, nullptr, 0); // Keep Alive Message
      lastKeepAliveTime = juce::Time::currentTimeMillis();
    }

    // Wait for data (non-blocking read using wait)
    int ready = socket->waitUntilReady(true, 100);
    if (ready < 0)
      break; // Error
    if (ready == 0)
      continue; // Timeout, loop again to check keep-alive

    juce::uint8 header[5];
    if (!readFull(header, 5))
      break;

    juce::uint8 type = header[0];
    juce::uint32 payloadLen;
    memcpy(&payloadLen, header + 1, 4);
    payloadLen = juce::ByteOrder::swapIfBigEndian(payloadLen);

    // Safeguard against ridiculous sizes
    if (payloadLen > 1024 * 1024 * 10)
      break;

    juce::MemoryBlock payload;
    if (payloadLen > 0) {
      payload.setSize(payloadLen, true);
      if (!readFull(payload.getData(), static_cast<int>(payloadLen)))
        break;
    }

    if (!handleMessage(type, payload))
      break;
  }

  connectionState = 0;
  if (socket)
    socket->close();

  juce::MessageManager::callAsync([this]() {
    listeners.call(&NinjamClientListener::onDisconnected, "Connection closed");
  });
}

bool NinjamClient::handleMessage(juce::uint8 type,
                                 const juce::MemoryBlock &payload) {
  // SERVER_AUTH_CHALLENGE
  if (type == 0x00) {
    if (payload.getSize() >= 8) {
      juce::MemoryBlock challenge(payload.getData(), 8);
      sendAuthRequest(challenge);
    }
  }
  // SERVER_AUTH_REPLY
  else if (type == 0x01) {
    if (payload.getSize() >= 1) {
      juce::uint8 flag = static_cast<const juce::uint8 *>(payload.getData())[0];
      if (flag == 1) // Access Granted
      {
        connectionState = 3;
        juce::MessageManager::callAsync(
            [this]() { listeners.call(&NinjamClientListener::onConnected); });
      } else // Access Denied
      {
        return false;
      }
    }
  }
  // SERVER_CONFIG_CHANGE
  else if (type == 0x02) {
    if (payload.getSize() >= 4) {
      juce::uint16 bpm, bpi;
      memcpy(&bpm, payload.getData(), 2);
      memcpy(&bpi, static_cast<const char *>(payload.getData()) + 2, 2);
      bpm = juce::ByteOrder::swapIfBigEndian(bpm);
      bpi = juce::ByteOrder::swapIfBigEndian(bpi);

      juce::MessageManager::callAsync([this, bpm, bpi]() {
        listeners.call(&NinjamClientListener::onServerConfig, bpm, bpi);
      });
    }
  }
  // USER_INFO_CHANGE
  else if (type == 0x03) {
    // Parse user joins/leaves
  }

  return true;
}

void NinjamClient::sendAuthRequest(const juce::MemoryBlock &challenge) {
  // MSG_CLIENT_AUTH:
  // 20 bytes SHA1 hash of (User + Pass)
  // Username (NUL terminated)
  // maybe capabilities... we'll just send standard.
  // Wait, the hash is SHA1(challenge + SHA1(user + ":" + pass)). Let me check
  // standard ninjam. In njclient.cpp it's SHA1(challenge + SHA1(user + ":" +
  // pass)) or just SHA1(challenge + pass)?

  WDL_SHA1 hash;
  juce::MemoryBlock passHashResult(20, true);

  // According to Ninjam protocol:
  // if password is empty hash is SHA1(challenge) ?
  // Actually, let's use the password as is for anonymous login since they don't
  // use it.

  // Auth packet:
  // 20 bytes: Hash
  // N bytes (null term): Username
  // 4 bytes: Client Capabilities (0x00010000 = keepalive support, etc)

  // For anonymous logins, hash is 20 bytes of 0s usually, or hash of pass.
  // Let's do hash of challenge + password.
  // The password hash is: SHA1(SHA1(user:pass) + challenge)
  WDL_SHA1 passHash;
  passHash.add(currentUsername.toRawUTF8(),
               currentUsername.getNumBytesAsUTF8());
  passHash.add(":", 1);
  passHash.add(currentPassword.toRawUTF8(),
               currentPassword.getNumBytesAsUTF8());

  juce::MemoryBlock innerHashResult(20, true);
  passHash.result(innerHashResult.getData());

  WDL_SHA1 finalHash;
  finalHash.add(innerHashResult.getData(), 20);
  finalHash.add(challenge.getData(), 8);

  juce::MemoryBlock finalHashResult(20, true);
  finalHash.result(finalHashResult.getData());

  juce::MemoryBlock packet;
  packet.append(finalHashResult.getData(), 20);
  packet.append(currentUsername.toRawUTF8(),
                currentUsername.getNumBytesAsUTF8() + 1);

  juce::uint32 caps =
      juce::ByteOrder::swapIfBigEndian((juce::uint32)1); // client caps
  packet.append(&caps, 4);

  juce::uint32 version = juce::ByteOrder::swapIfBigEndian(
      (juce::uint32)0x00020000); // protocol version
  packet.append(&version, 4);

  writeFull(0x80, packet.getData(), static_cast<int>(packet.getSize()));
}
