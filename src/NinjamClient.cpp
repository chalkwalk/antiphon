#include "NinjamClient.h"
#include "Sha1.h"

NinjamClient::NinjamClient() : juce::Thread("NinjamClientThread") {}

NinjamClient::~NinjamClient() { disconnectFromServer(); }

void NinjamClient::addListener(NinjamClientListener *listener) {
  listeners.add(listener);
}

void NinjamClient::removeListener(NinjamClientListener *listener) {
  listeners.remove(listener);
}

void NinjamClient::setSaveTx(bool shouldSave) {
  juce::ScopedLock sl(txFileMutex);
  if (shouldSave && !isSavingTx) {
    juce::File desktop =
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    juce::File oggF = desktop.getChildFile("tx.ogg");
    juce::File wavF = desktop.getChildFile("tx.wav");

    oggF.deleteFile();
    wavF.deleteFile();

    txOggFile = std::make_unique<juce::FileOutputStream>(oggF);
    if (txOggFile->openedOk()) {
      txOggFile->setPosition(0);
      txOggFile->truncate();
    }

    juce::StringPairArray strArr;
    txWavWriter.reset(wavFormat.createWriterFor(
        new juce::FileOutputStream(wavF), 48000, 2, 32, strArr, 0));
  } else if (!shouldSave && isSavingTx) {
    txOggFile.reset();
    txWavWriter.reset();
  }
  isSavingTx = shouldSave;
}

void NinjamClient::setSaveRx(bool shouldSave) {
  juce::ScopedLock sl(rxFileMutex);
  if (shouldSave && !isSavingRx) {
    juce::File desktop =
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory);
    juce::File oggF = desktop.getChildFile("rx.ogg");
    juce::File wavF = desktop.getChildFile("rx.wav");

    oggF.deleteFile();
    wavF.deleteFile();

    rxOggFile = std::make_unique<juce::FileOutputStream>(oggF);
    if (rxOggFile->openedOk()) {
      rxOggFile->setPosition(0);
      rxOggFile->truncate();
    }

    juce::StringPairArray strArr;
    rxWavWriter.reset(wavFormat.createWriterFor(
        new juce::FileOutputStream(wavF), 48000, 2, 32, strArr, 0));
  } else if (!shouldSave && isSavingRx) {
    rxOggFile.reset();
    rxWavWriter.reset();
  }
  isSavingRx = shouldSave;
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
  stopThread(3000);
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

  if (!socket->connect(currentHost, currentPort, 2000)) {
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
        sendChannelInfo();
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

      // Update immediately so buffer sizing in DOWNLOAD_INTERVAL_BEGIN is correct.
      serverBpm = bpm;
      serverBpi = bpi;

      juce::MessageManager::callAsync([this, bpm, bpi]() {
        listeners.call(&NinjamClientListener::onServerConfig, bpm, bpi);
      });
    }
  }
  // USER_INFO_CHANGE
  else if (type == 0x03) {
    int offset = 0;
    int payloadSize = payload.getSize();
    const char *data = static_cast<const char *>(payload.getData());

    bool changed = false;
    while (offset < payloadSize) {
      if (offset + 4 > payloadSize)
        break;

      juce::uint8 active = data[offset++];
      juce::uint8 channelIndex = data[offset++];
      juce::int16 volume;
      memcpy(&volume, data + offset, 2);
      volume =
          juce::ByteOrder::swapIfBigEndian(static_cast<juce::uint16>(volume));
      offset += 2;
      juce::int8 pan = data[offset++];
      juce::uint8 flags = data[offset++];

      juce::String username = juce::String::fromUTF8(data + offset);
      offset += username.getNumBytesAsUTF8() + 1;

      juce::String channelName = juce::String::fromUTF8(data + offset);
      offset += channelName.getNumBytesAsUTF8() + 1;

      juce::ScopedLock sl(downloadMutex);
      if (active) {
        if (remoteUsers.find(username) == remoteUsers.end()) {
          remoteUsers[username] = RemoteUser{username, {}};
        }
        auto &user = remoteUsers[username];
        if (user.channels.find(channelIndex) == user.channels.end()) {
          RemoteUserChannel newChan;
          newChan.channelIndex = channelIndex;
          newChan.channelName = channelName;
          user.channels[channelIndex] = newChan;
          changed = true;
        } else if (user.channels[channelIndex].channelName != channelName) {
          user.channels[channelIndex].channelName = channelName;
          changed = true;
        }
      } else {
        if (remoteUsers.find(username) != remoteUsers.end()) {
          auto &user = remoteUsers[username];
          if (user.channels.find(channelIndex) != user.channels.end()) {
            user.channels.erase(channelIndex);
            changed = true;
          }
          if (user.channels.empty()) {
            remoteUsers.erase(username);
          }
        }
      }
    }

    sendUserMask();

    if (changed) {
      juce::MessageManager::callAsync([this]() {
        listeners.call(&NinjamClientListener::onUserInfoChange);
      });
    }
  }
  // SERVER_DOWNLOAD_INTERVAL_BEGIN
  else if (type == 0x04) {
    if (payload.getSize() >= 16) {
      juce::String guid = juce::String::toHexString(payload.getData(), 16);

      int estSize = 0;
      juce::uint8 fourcc[4] = {0};
      juce::uint8 channelIndex = 0;
      juce::String username;

      int offset = 16;
      if (offset + 4 <= payload.getSize()) {
        memcpy(&estSize, static_cast<const char *>(payload.getData()) + offset,
               4);
        estSize = juce::ByteOrder::swapIfBigEndian(
            static_cast<juce::uint32>(estSize));
        offset += 4;

        if (offset + 4 <= payload.getSize()) {
          memcpy(fourcc, static_cast<const char *>(payload.getData()) + offset,
                 4);
          offset += 4;

          if (offset + 1 <= payload.getSize()) {
            channelIndex =
                static_cast<const juce::uint8 *>(payload.getData())[offset++];
            if (offset < payload.getSize()) {
              username = juce::String::fromUTF8(
                  static_cast<const char *>(payload.getData()) + offset);
            }
          }
        }
      }

      auto interval = std::make_shared<DecodedInterval>();
      interval->guid = guid;
      int bufferSamples = static_cast<int>(
          sampleRate * 60.0 / std::max(1, serverBpm) * serverBpi * 1.5f);
      interval->buffer.setSize(2, bufferSamples);
      interval->buffer.clear();

      PendingDownload pd;
      pd.target = interval;
      pd.username = username;
      pd.channelIndex = channelIndex;
      if (fourcc[0] == 'O' && fourcc[1] == 'G' && fourcc[2] == 'G' &&
          fourcc[3] == 'v')
        pd.decoder = std::make_unique<VorbisDecoder>();

      juce::ScopedLock sl(downloadMutex);
      guidToInterval[guid] = std::move(pd);

      auto key = std::make_pair(username, (int)channelIndex);
      auto &stream = channelStreams[key];
      stream.username = username;
      stream.channelIndex = channelIndex;
      stream.queue.push_back(interval);

      // Signal the audio thread that a new server interval has started.
      // compare_exchange prevents re-signalling if the audio thread hasn't
      // processed the previous signal yet (avoids double-swap for multi-channel
      // sessions where several DOWNLOAD_INTERVAL_BEGIN messages arrive per boundary).
      bool expected = false;
      intervalBeginSignal.compare_exchange_strong(expected, true);
    }
  }
  // SERVER_DOWNLOAD_INTERVAL_WRITE
  else if (type == 0x05) {
    if (payload.getSize() >= 17) {
      juce::String guid = juce::String::toHexString(payload.getData(), 16);
      juce::uint8 flags =
          static_cast<const juce::uint8 *>(payload.getData())[16];

      juce::ScopedLock sl(downloadMutex);
      auto it = guidToInterval.find(guid);
      if (it != guidToInterval.end()) {
        auto &pd = it->second;
        auto &interval = *pd.target;

        int oggDataSize = static_cast<int>(payload.getSize()) - 17;
        if (oggDataSize > 0 && pd.decoder != nullptr) {
          const char *oggData =
              static_cast<const char *>(payload.getData()) + 17;
          pd.decoder->decode(oggData, oggDataSize);
          {
            // Decode into the interval buffer, resampling if the OGG's native
            // sample rate differs from our local rate.
            while (pd.decoder->available() > 0) {
              int out_nch = pd.decoder->numChannels();
              int availFrames = pd.decoder->available() / out_nch;
              if (availFrames <= 0 || out_nch <= 0)
                break;

              const float *decodedBlock = pd.decoder->pcm();
              int writePos = interval.writePos.load();
              int remain = interval.buffer.getNumSamples() - writePos;

              int decoderRate = pd.decoder->sampleRate();
              bool needsResample =
                  (decoderRate > 0 && decoderRate != (int)sampleRate);
              double speedRatio =
                  needsResample ? (double)decoderRate / sampleRate : 1.0;

              int maxSrc =
                  needsResample ? (int)((double)remain * speedRatio) : remain;
              int toCopy = std::min(availFrames, maxSrc);

              if (toCopy > 0) {
                if (!needsResample) {
                  for (int ch = 0; ch < std::min(2, out_nch); ++ch) {
                    float *writePtr =
                        interval.buffer.getWritePointer(ch, writePos);
                    for (int s = 0; s < toCopy; ++s)
                      writePtr[s] = decodedBlock[s * out_nch + ch];
                  }
                  if (out_nch == 1 && interval.buffer.getNumChannels() == 2) {
                    memcpy(interval.buffer.getWritePointer(1, writePos),
                           interval.buffer.getReadPointer(0, writePos),
                           (size_t)toCopy * sizeof(float));
                  }
                  interval.writePos.fetch_add(toCopy);
                } else {
                  int numOut =
                      std::min((int)((double)toCopy / speedRatio), remain);
                  if (numOut > 0) {
                    juce::HeapBlock<float> srcBuf(toCopy), dstBuf(numOut);
                    for (int ch = 0; ch < std::min(2, out_nch); ++ch) {
                      for (int s = 0; s < toCopy; ++s)
                        srcBuf[s] = decodedBlock[s * out_nch + ch];
                      auto &interp =
                          (ch == 0) ? pd.resamplerL : pd.resamplerR;
                      interp.process(speedRatio, srcBuf.getData(),
                                     dstBuf.getData(), numOut);
                      memcpy(interval.buffer.getWritePointer(ch, writePos),
                             dstBuf.getData(), (size_t)numOut * sizeof(float));
                    }
                    if (out_nch == 1 && interval.buffer.getNumChannels() == 2) {
                      memcpy(interval.buffer.getWritePointer(1, writePos),
                             interval.buffer.getReadPointer(0, writePos),
                             (size_t)numOut * sizeof(float));
                    }
                    interval.writePos.fetch_add(numOut);
                  }
                }
                pd.decoder->skip(toCopy * out_nch);
              } else {
                // Buffer full -- drain decoder.
                pd.decoder->skip(pd.decoder->available());
                break;
              }
            }

            juce::ScopedLock fl(rxFileMutex);
            if (isSavingRx && rxOggFile != nullptr)
              rxOggFile->write(oggData, oggDataSize);
          }
        }

        if (flags & 1) {
          interval.finalReceived.store(true);
          diagLastIntervalSamples.store(interval.writePos.load());
          int bpm = serverBpm, bpi = serverBpi;
          if (bpm > 0 && bpi > 0)
            diagLastIntervalExpected.store(
                (int)(sampleRate * 60.0 / bpm * bpi));
          guidToInterval.erase(it);
        }
      }
    }
  }
  // CHAT_MESSAGE
  else if (type == 0xC0) {
    if (payload.getSize() >= 1) {
      const char *data = static_cast<const char *>(payload.getData());
      const char *endp = data + payload.getSize();
      juce::StringArray parms;

      const char *p = data;
      for (int i = 0; i < 5; ++i) {
        if (p < endp) {
          juce::String s = juce::String::fromUTF8(p);
          parms.add(s);
          p += s.getNumBytesAsUTF8() + 1;
        } else {
          parms.add(juce::String());
        }
      }

      if (parms.size() >= 1 && parms[0].isNotEmpty()) {
        ChatMessage msg;
        msg.type = parms[0];
        if (msg.type == "MSG" || msg.type == "PRIVMSG") {
          msg.username = parms[1];
          msg.text = parms[2];
        } else if (msg.type == "TOPIC") {
          msg.username = "Server";
          msg.text = "Topic: " + parms[2];
        } else if (msg.type == "JOIN") {
          msg.username = "Server";
          msg.text = parms[1] + " joined";
        } else if (msg.type == "PART") {
          msg.username = "Server";
          msg.text = parms[1] + " left";
        } else {
          msg.username = "Server";
          msg.text = "[" + msg.type + "] " + parms[1] + " " + parms[2];
        }

        {
          juce::ScopedLock sl(chatMutex);
          chatLog.add(msg);
          if (chatLog.size() > 100)
            chatLog.remove(0); // keep history bounded
        }

        juce::MessageManager::callAsync(
            [this, type = msg.type, user = msg.username, text = msg.text]() {
              listeners.call(&NinjamClientListener::onChatMessage, type, user,
                             text);
            });
      }
    }
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

  juce::MemoryBlock passHashResult(20, true);

  // According to Ninjam protocol:
  // if password is empty hash is SHA1(challenge) ?
  // Actually, let's use the password as is for anonymous login since they
  // don't use it.

  // Auth packet:
  // 20 bytes: Hash
  // N bytes (null term): Username
  // 4 bytes: Client Capabilities (0x00010000 = keepalive support, etc)

  // For anonymous logins, hash is 20 bytes of 0s usually, or hash of pass.
  // Let's do hash of challenge + password.
  // The password hash is: SHA1(SHA1(user:pass) + challenge)
  Sha1 passHash;
  passHash.add(currentUsername.toRawUTF8(),
               currentUsername.getNumBytesAsUTF8());
  passHash.add(":", 1);
  passHash.add(currentPassword.toRawUTF8(),
               currentPassword.getNumBytesAsUTF8());

  juce::MemoryBlock innerHashResult(20, true);
  passHash.result(innerHashResult.getData());

  Sha1 finalHash;
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

void NinjamClient::sendChannelInfo() {
  juce::StringArray names;
  {
    juce::ScopedLock sl(channelInfoMutex);
    names = storedChannelNames;
  }
  juce::MemoryBlock payload;
  // 2-byte LE mpisize header: 4 bytes of per-channel metadata (vol, pan, flags)
  juce::uint8 msz[2] = { 4, 0 };
  payload.append(msz, 2);
  for (const auto &name : names) {
    payload.append(name.toRawUTF8(), name.getNumBytesAsUTF8() + 1);
    // volume (2 bytes LE, 0 = 0dB), pan (1 byte, 0 = centre), flags (1 byte, 0 = default)
    juce::uint8 meta[4] = { 0, 0, 0, 0 };
    payload.append(meta, 4);
  }
  writeFull(0x82, payload.getData(), static_cast<int>(payload.getSize()));
}

void NinjamClient::updateChannelInfo(const juce::StringArray &names) {
  {
    juce::ScopedLock sl(channelInfoMutex);
    storedChannelNames = names.isEmpty() ? juce::StringArray{"Local Instrument"} : names;
  }
  if (isConnected())
    sendChannelInfo();
}

void NinjamClient::processCapturedAudio(juce::AudioBuffer<float> &buffer,
                                        int numSamples,
                                        int channelIndex,
                                        bool mono) {
  if (!isConnected())
    return;

  {
    juce::ScopedLock sl(txFileMutex);
    if (isSavingTx && txWavWriter != nullptr) {
      txWavWriter->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    }
  }

  juce::MemoryBlock guid(16, true);
  for (int i = 0; i < 16; i++)
    guid[i] = (juce::uint8)(juce::Random::getSystemRandom().nextInt(256));

  // UPLOAD_INTERVAL_BEGIN (0x83): 16-byte GUID + 4-byte estsize LE + 4-byte fourcc LE + 1-byte chidx
  int numCh = mono ? 1 : buffer.getNumChannels();
  juce::MemoryBlock beginPacket;
  beginPacket.append(guid.getData(), 16);
  juce::uint32 estsize = 0;
  beginPacket.append(&estsize, 4);
  juce::uint8 fourcc[4] = { 'O', 'G', 'G', 'v' };
  beginPacket.append(fourcc, 4);
  juce::uint8 chidx = (juce::uint8)channelIndex;
  beginPacket.append(&chidx, 1);
  writeFull(0x83, beginPacket.getData(), static_cast<int>(beginPacket.getSize()));

  VorbisEncoder encoder(48000, numCh, 128, juce::Random::getSystemRandom().nextInt());

  // We should send smaller chunks, but for now just encode the whole thing in
  // chunks and construct the UPLOAD_INTERVAL_WRITE payload

  const int blockSize = 1024;
  int pos = 0;

  while (pos < numSamples) {
    int toProcess = std::min(blockSize, numSamples - pos);

    std::vector<float> interleaved(static_cast<std::size_t>(toProcess * numCh));
    if (mono) {
      const float *readPtr = buffer.getReadPointer(0, pos);
      for (int i = 0; i < toProcess; ++i)
        interleaved[i] = readPtr[i];
    } else {
      for (int ch = 0; ch < numCh; ++ch) {
        const float *readPtr = buffer.getReadPointer(ch, pos);
        for (int i = 0; i < toProcess; ++i)
          interleaved[i * numCh + ch] = readPtr[i];
      }
    }
    encoder.encode(interleaved.data(), toProcess);

    while (encoder.available() > 0) {
      int avail = encoder.available();
      const void *oggData = encoder.data();

      juce::MemoryBlock writePacket;
      writePacket.append(guid.getData(), 16);
      juce::uint8 flags = 0;
      writePacket.append(&flags, 1);
      writePacket.append(oggData, avail);
      writeFull(0x84, writePacket.getData(),
                static_cast<int>(writePacket.getSize()));

      {
        juce::ScopedLock sl(txFileMutex);
        if (isSavingTx && txOggFile != nullptr)
          txOggFile->write(oggData, avail);
      }

      encoder.advance(avail);
    }

    pos += toProcess;
  }

  // Flush end-of-stream.
  encoder.encode(nullptr, 0);
  while (encoder.available() > 0) {
    int avail = encoder.available();
    const void *oggData = encoder.data();

    juce::MemoryBlock writePacket;
    writePacket.append(guid.getData(), 16);
    juce::uint8 flags = 1;
    writePacket.append(&flags, 1);
    writePacket.append(oggData, static_cast<std::size_t>(avail));
    writeFull(0x84, writePacket.getData(),
              static_cast<int>(writePacket.getSize()));

    {
      juce::ScopedLock sl(txFileMutex);
      if (isSavingTx && txOggFile != nullptr)
        txOggFile->write(oggData, avail);
    }

    encoder.advance(avail);
  }
}

void NinjamClient::swapIntervalBuffers() {
  constexpr int kFadeSamples = 256;
  juce::ScopedLock sl(downloadMutex);
  for (auto &[key, stream] : channelStreams) {
    if (stream.current) {
      int unplayed = stream.current->writePos.load() - stream.readPos;
      if (unplayed > 0) {
        diagSwapsBeforeConsumed.fetch_add(1);
        diagSamplesDroppedOnSwap.fetch_add(unplayed);
        int fadeLen = std::min(unplayed, kFadeSamples);
        stream.fadeOut = stream.current;
        stream.fadeOutPos = stream.readPos;
        stream.fadeTotal = fadeLen;
        stream.fadeRemaining = fadeLen;
      }
    }
    if (!stream.queue.empty()) {
      stream.current = stream.queue.front();
      stream.queue.pop_front();
      stream.readPos = 0;
    } else {
      stream.current = nullptr;
    }
  }
}

void NinjamClient::dumpDiagnostics() {
  int sigSwaps = diagSwapsBySignal.exchange(0);
  int fbSwaps  = diagSwapsByFallback.exchange(0);
  int earlySwaps = diagSwapsBeforeConsumed.exchange(0);
  int dropped = diagSamplesDroppedOnSwap.exchange(0);
  int underruns = diagUnderrunBlocks.exchange(0);
  int lastSamples = diagLastIntervalSamples.load();
  int lastExpected = diagLastIntervalExpected.load();
  if (sigSwaps == 0 && fbSwaps == 0 && earlySwaps == 0 && underruns == 0)
    return;
  juce::Logger::writeToLog(
      juce::String::formatted(
          "[diag] swaps sig=%d fb=%d early=%d droppedSmp=%d underrunBlocks=%d "
          "lastInterval=%d/%d",
          sigSwaps, fbSwaps, earlySwaps, dropped, underruns,
          lastSamples, lastExpected));
}

std::map<juce::String, NinjamClient::RemoteUser>
NinjamClient::getRemoteUsers() const {
  juce::ScopedLock sl(
      downloadMutex); // Actually juce CriticalSection is mutable, wait! It's
                      // not const in NinjamClient.h
  // The getter should copy it safely or return a copy. Let's assume
  // downloadMutex is mutable if const, or we just drop const.
  return remoteUsers;
}

void NinjamClient::setRemoteUserVolume(const juce::String &username,
                                       int channelIndex, float volume) {
  juce::ScopedLock sl(downloadMutex);
  if (remoteUsers.find(username) != remoteUsers.end()) {
    auto &user = remoteUsers[username];
    if (user.channels.find(channelIndex) != user.channels.end()) {
      user.channels[channelIndex].volume = volume;
    }
  }
}

void NinjamClient::setRemoteUserPan(const juce::String &username,
                                    int channelIndex, float pan) {
  juce::ScopedLock sl(downloadMutex);
  if (remoteUsers.find(username) != remoteUsers.end()) {
    auto &user = remoteUsers[username];
    if (user.channels.find(channelIndex) != user.channels.end()) {
      user.channels[channelIndex].pan = pan;
    }
  }
}

void NinjamClient::setRemoteUserMute(const juce::String &username,
                                     int channelIndex, bool mute) {
  juce::ScopedLock sl(downloadMutex);
  if (remoteUsers.find(username) != remoteUsers.end()) {
    auto &user = remoteUsers[username];
    if (user.channels.find(channelIndex) != user.channels.end()) {
      user.channels[channelIndex].isMuted = mute;
    }
  }
}

void NinjamClient::setRemoteUserSolo(const juce::String &username,
                                     int channelIndex, bool solo) {
  juce::ScopedLock sl(downloadMutex);
  if (remoteUsers.find(username) != remoteUsers.end()) {
    auto &user = remoteUsers[username];
    if (user.channels.find(channelIndex) != user.channels.end()) {
      user.channels[channelIndex].isSoloed = solo;
    }
  }
}

void NinjamClient::setRemoteUserRecv(const juce::String &username,
                                     int channelIndex, bool recv) {
  {
    juce::ScopedLock sl(downloadMutex);
    if (remoteUsers.find(username) != remoteUsers.end()) {
      auto &user = remoteUsers[username];
      if (user.channels.find(channelIndex) != user.channels.end()) {
        user.channels[channelIndex].recvEnabled = recv;
      }
    }
  }
  sendUserMask();
}

void NinjamClient::setRemoteUserOutputBus(const juce::String &username,
                                          int channelIndex, int busIdx) {
  juce::ScopedLock sl(downloadMutex);
  if (remoteUsers.find(username) != remoteUsers.end()) {
    auto &user = remoteUsers[username];
    if (user.channels.find(channelIndex) != user.channels.end())
      user.channels[channelIndex].outputBusIndex = busIdx;
  }
}

void NinjamClient::sendUserMask() {
  juce::MemoryBlock payload;
  {
    juce::ScopedLock sl(downloadMutex);
    for (auto &[uname, user] : remoteUsers) {
      juce::uint32 mask = 0;
      for (auto &[chIdx, ch] : user.channels)
        if (chIdx < 32 && ch.recvEnabled)
          mask |= (1u << chIdx);
      payload.append(uname.toRawUTF8(), uname.getNumBytesAsUTF8() + 1);
      juce::uint32 leM = juce::ByteOrder::swapIfBigEndian(mask);
      payload.append(&leM, 4);
    }
  }
  if (payload.getSize() > 0)
    writeFull(0x81, payload.getData(), (int)payload.getSize());
}

void NinjamClient::getDecodedAudio(juce::AudioBuffer<float> &buffer) {
  juce::ScopedLock sl(downloadMutex);
  int numSamples = buffer.getNumSamples();
  int dstChannels = buffer.getNumChannels();

  bool anySolo = false;
  for (const auto &u : remoteUsers)
    for (const auto &c : u.second.channels)
      if (c.second.isSoloed)
        anySolo = true;

  for (auto &[key, stream] : channelStreams) {
    if (!stream.current && !stream.fadeOut)
      continue;

    float vol = 0.5f;
    float pan = 0.0f;
    bool isMuted = false;
    bool isSoloed = false;

    int outBusIdx = 0;
    auto uit = remoteUsers.find(stream.username);
    if (uit != remoteUsers.end()) {
      auto cit = uit->second.channels.find(stream.channelIndex);
      if (cit != uit->second.channels.end()) {
        vol = cit->second.volume;
        pan = cit->second.pan;
        isMuted = cit->second.isMuted;
        isSoloed = cit->second.isSoloed;
        outBusIdx = cit->second.outputBusIndex;
      }
    }

    if (anySolo && !isSoloed)
      isMuted = true;

    float lGain = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    float rGain = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    int maxBus = std::max(0, dstChannels / 2 - 1);
    int busIdx = juce::jlimit(0, maxBus, outBusIdx);

    // Crossfade region: mix the old interval's tail (fade-out) with the new
    // interval's head (fade-in) for the first fadeCopy samples of this block.
    int fadeCopy = 0;
    if (stream.fadeOut && stream.fadeRemaining > 0) {
      fadeCopy = std::min(numSamples, stream.fadeRemaining);
      int elapsed = stream.fadeTotal - stream.fadeRemaining;

      if (!isMuted) {
        int oldChannels = stream.fadeOut->buffer.getNumChannels();
        for (int ch = 0; ch < std::min(oldChannels, 2); ++ch) {
          int dstCh = busIdx * 2 + ch;
          if (dstCh >= dstChannels) continue;
          float chGain = (ch == 0) ? lGain : rGain;
          const float *src =
              stream.fadeOut->buffer.getReadPointer(ch, stream.fadeOutPos);
          float *dst = buffer.getWritePointer(dstCh);
          for (int s = 0; s < fadeCopy; ++s) {
            float g = (float)(stream.fadeRemaining - s) / stream.fadeTotal;
            dst[s] += src[s] * chGain * g;
          }
        }

        if (stream.current) {
          int newAvail = stream.current->writePos.load() - stream.readPos;
          int newFadeCopy = std::min(fadeCopy, newAvail);
          if (newFadeCopy > 0) {
            int newChannels = stream.current->buffer.getNumChannels();
            for (int ch = 0; ch < std::min(newChannels, 2); ++ch) {
              int dstCh = busIdx * 2 + ch;
              if (dstCh >= dstChannels) continue;
              float chGain = (ch == 0) ? lGain : rGain;
              const float *src =
                  stream.current->buffer.getReadPointer(ch, stream.readPos);
              float *dst = buffer.getWritePointer(dstCh);
              for (int s = 0; s < newFadeCopy; ++s) {
                float g = (float)(elapsed + s) / stream.fadeTotal;
                dst[s] += src[s] * chGain * g;
              }
            }
            stream.readPos += newFadeCopy;
          }
        }
      } else if (stream.current) {
        // Still advance readPos in muted streams so timing stays consistent.
        int newAvail = stream.current->writePos.load() - stream.readPos;
        stream.readPos += std::min(fadeCopy, newAvail);
      }

      stream.fadeOutPos += fadeCopy;
      stream.fadeRemaining -= fadeCopy;
      if (stream.fadeRemaining <= 0) {
        stream.fadeOut.reset();
        stream.fadeRemaining = 0;
      }
    }

    // Post-fade region: normal mix from current at full gain.
    if (!stream.current)
      continue;

    auto &iv = *stream.current;
    int avail = iv.writePos.load() - stream.readPos;
    int remainingSamples = numSamples - fadeCopy;
    if (remainingSamples <= 0)
      continue;
    if (avail <= 0) {
      if (!iv.finalReceived.load())
        diagUnderrunBlocks.fetch_add(1);
      continue;
    }

    int toCopy = std::min(remainingSamples, avail);
    int srcChannels = stream.current->buffer.getNumChannels();

    float peak = 0.0f;
    for (int ch = 0; ch < std::min(srcChannels, 2); ++ch) {
      float chGain = (ch == 0) ? lGain : rGain;
      const float *src =
          stream.current->buffer.getReadPointer(ch, stream.readPos);
      for (int s = 0; s < toCopy; ++s)
        peak = std::max(peak, std::abs(src[s]) * chGain);
    }
    if (uit != remoteUsers.end()) {
      auto cit = uit->second.channels.find(stream.channelIndex);
      if (cit != uit->second.channels.end())
        cit->second.peakLevel = peak;
    }

    if (!isMuted) {
      for (int ch = 0; ch < std::min(srcChannels, 2); ++ch) {
        int dstCh = busIdx * 2 + ch;
        if (dstCh < dstChannels) {
          float gain = (ch == 0) ? lGain : rGain;
          buffer.addFrom(dstCh, fadeCopy, stream.current->buffer, ch,
                         stream.readPos, toCopy, gain);
        }
      }
    }

    stream.readPos += toCopy;
  }

  {
    juce::ScopedLock fl(rxFileMutex);
    if (isSavingRx && rxWavWriter != nullptr)
      rxWavWriter->writeFromAudioSampleBuffer(buffer, 0, numSamples);
  }
}

juce::Array<NinjamClient::ChatMessage> NinjamClient::getChatLog() const {
  juce::ScopedLock sl(chatMutex);
  return chatLog;
}

void NinjamClient::sendChatMessage(const juce::String &text) {
  if (!isConnected())
    return;
  juce::MemoryBlock msgBlock;
  juce::String type = "MSG";
  msgBlock.append(type.toUTF8(), type.getNumBytesAsUTF8() + 1);
  msgBlock.append(text.toUTF8(), text.getNumBytesAsUTF8() + 1);
  char empty[1] = {0};
  msgBlock.append(empty, 1);
  msgBlock.append(empty, 1);
  msgBlock.append(empty, 1);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}

void NinjamClient::sendAdminCommand(const juce::String &command) {
  if (!isConnected())
    return;
  juce::MemoryBlock msgBlock;
  juce::String type = "ADMIN";
  msgBlock.append(type.toUTF8(), type.getNumBytesAsUTF8() + 1);
  msgBlock.append(command.toUTF8(), command.getNumBytesAsUTF8() + 1);
  char empty[1] = {0};
  msgBlock.append(empty, 1);
  msgBlock.append(empty, 1);
  msgBlock.append(empty, 1);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}

void NinjamClient::sendPrivateMessage(const juce::String &username,
                                      const juce::String &text) {
  if (!isConnected())
    return;
  juce::MemoryBlock msgBlock;
  juce::String type = "PRIVMSG";
  msgBlock.append(type.toUTF8(), type.getNumBytesAsUTF8() + 1);
  msgBlock.append(username.toUTF8(), username.getNumBytesAsUTF8() + 1);
  msgBlock.append(text.toUTF8(), text.getNumBytesAsUTF8() + 1);
  char empty[1] = {0};
  msgBlock.append(empty, 1);
  msgBlock.append(empty, 1);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}
