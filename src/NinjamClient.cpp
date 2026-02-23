#include "NinjamClient.h"
#include "NinjamProtocol.h"

NinjamClient::NinjamClient() : juce::Thread("NinjamClientThread") {}

NinjamClient::~NinjamClient() {
  // Clear the flag before anything else, so any callAsync lambda still queued
  // on the message thread becomes a no-op rather than a use-after-free.
  aliveFlag->store(false);
  disconnectFromServer();
}

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
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    txWavWriter.reset(wavFormat.createWriterFor(
        new juce::FileOutputStream(wavF), rate, 2, 32, strArr, 0));
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
    const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    rxWavWriter.reset(wavFormat.createWriterFor(
        new juce::FileOutputStream(wavF), rate, 2, 32, strArr, 0));
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
  // A frame is a header write followed by a payload write, and writeFull is
  // called from three threads: the network thread (keep-alive, usermask,
  // channel info), the message thread (interval uploads) and the UI thread
  // (chat, recv toggles). Without this lock two frames can interleave, so the
  // server reads one message's header followed by another's payload, desyncs,
  // and drops the connection. It shows up as a random disconnect while
  // playing, which is easy to blame on the network.
  juce::ScopedLock sl(writeMutex);

  if (!socket || !socket->isConnected())
    return false;

  juce::uint8 header[NinjamProtocol::kHeaderSize];
  NinjamProtocol::writeFrameHeader(header, type, (juce::uint32)numBytes);

  if (socket->write(header, NinjamProtocol::kHeaderSize) !=
      NinjamProtocol::kHeaderSize)
    return false;
  if (numBytes > 0 && socket->write(payload, numBytes) != numBytes)
    return false;

  return true;
}

void NinjamClient::run() {
  socket = std::make_unique<juce::StreamingSocket>();

  if (!socket->connect(currentHost, currentPort, 2000)) {
    connectionState = 0;
    callAsyncIfAlive([this]() {
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

    juce::uint8 header[NinjamProtocol::kHeaderSize];
    if (!readFull(header, NinjamProtocol::kHeaderSize))
      break;

    // A length above the sanity cap means the stream is desynchronised; there
    // is no way to resynchronise, so drop the connection.
    NinjamProtocol::FrameHeader frame;
    if (!NinjamProtocol::readFrameHeader(header, frame))
      break;

    juce::MemoryBlock payload;
    if (frame.length > 0) {
      payload.setSize(frame.length, true);
      if (!readFull(payload.getData(), static_cast<int>(frame.length)))
        break;
    }

    if (!handleMessage(frame.type, payload))
      break;
  }

  connectionState = 0;
  if (socket)
    socket->close();

  // Drop all per-session state. Without this a reconnect shows the previous
  // session's users, and their orphaned channel streams keep being swapped
  // every interval, silently, forever.
  {
    juce::ScopedLock sl(downloadMutex);
    guidToInterval.clear();
    channelStreams.clear();
    remoteUsers.clear();
  }

  callAsyncIfAlive([this]() {
    listeners.call(&NinjamClientListener::onDisconnected, "Connection closed");
  });
}

bool NinjamClient::handleMessage(juce::uint8 type,
                                 const juce::MemoryBlock &payload) {
  // A malformed message is dropped rather than treated as fatal: the framing
  // layer already resynchronised, so the connection stays usable.
  auto malformed = [type]() {
    juce::Logger::writeToLog("[protocol] dropped malformed message type 0x" +
                             juce::String::toHexString((int)type));
    return true;
  };

  // SERVER_AUTH_CHALLENGE
  if (type == 0x00) {
    NinjamProtocol::AuthChallenge c;
    if (!NinjamProtocol::parseAuthChallenge(payload, c))
      return malformed();
    sendAuthRequest(c.challenge);
  }
  // SERVER_AUTH_REPLY
  else if (type == 0x01) {
    NinjamProtocol::AuthReply reply;
    if (!NinjamProtocol::parseAuthReply(payload, reply))
      return malformed();
    if (!reply.granted)
      return false;

    connectionState = 3;
    sendChannelInfo();
    callAsyncIfAlive(
        [this]() { listeners.call(&NinjamClientListener::onConnected); });
  }
  // SERVER_CONFIG_CHANGE
  else if (type == 0x02) {
    NinjamProtocol::ServerConfig cfg;
    if (!NinjamProtocol::parseServerConfig(payload, cfg))
      return malformed();

    // Update immediately so buffer sizing in DOWNLOAD_INTERVAL_BEGIN is correct.
    serverBpm = cfg.bpm;
    serverBpi = cfg.bpi;

    const int bpm = cfg.bpm, bpi = cfg.bpi;
    callAsyncIfAlive([this, bpm, bpi]() {
      listeners.call(&NinjamClientListener::onServerConfig, bpm, bpi);
    });
  }
  // USER_INFO_CHANGE
  else if (type == 0x03) {
    std::vector<NinjamProtocol::UserInfoEntry> entries;
    const bool wellFormed = NinjamProtocol::parseUserInfo(payload, entries);

    bool changed = false;
    {
      juce::ScopedLock sl(downloadMutex);
      for (const auto &e : entries) {
        if (e.active) {
          if (remoteUsers.find(e.username) == remoteUsers.end())
            remoteUsers[e.username] = RemoteUser{e.username, {}};

          auto &user = remoteUsers[e.username];
          if (user.channels.find(e.channelIndex) == user.channels.end()) {
            RemoteUserChannel newChan;
            newChan.channelIndex = e.channelIndex;
            newChan.channelName = e.channelName;
            user.channels[e.channelIndex] = newChan;
            changed = true;
          } else if (user.channels[e.channelIndex].channelName !=
                     e.channelName) {
            user.channels[e.channelIndex].channelName = e.channelName;
            changed = true;
          }
        } else {
          auto it = remoteUsers.find(e.username);
          if (it != remoteUsers.end()) {
            auto &user = it->second;
            if (user.channels.erase(e.channelIndex) > 0)
              changed = true;
            if (user.channels.empty())
              remoteUsers.erase(it);
          }
        }
      }
    }

    sendUserMask();

    if (changed) {
      callAsyncIfAlive([this]() {
        listeners.call(&NinjamClientListener::onUserInfoChange);
      });
    }

    if (!wellFormed)
      return malformed();
  }
  // SERVER_DOWNLOAD_INTERVAL_BEGIN
  else if (type == 0x04) {
    NinjamProtocol::IntervalBegin begin;
    if (!NinjamProtocol::parseIntervalBegin(payload, begin))
      return malformed();

    // Non-audio fourCCs (notably Jamtaba's JTBv video) are ignored outright.
    // Queueing them would create a channel stream that swaps forever in
    // silence and shows up as a phantom channel in the mixer.
    if (!begin.isOggAudio())
      return true;

    auto interval = std::make_shared<DecodedInterval>();
    interval->guid = begin.guidHex;
    int bufferSamples = static_cast<int>(
        sampleRate * 60.0 / std::max(1, serverBpm) * serverBpi * 1.5f);
    interval->buffer.setSize(2, bufferSamples);
    interval->buffer.clear();

    PendingDownload pd;
    pd.target = interval;
    pd.username = begin.username;
    pd.channelIndex = begin.channelIndex;
    pd.decoder = std::make_unique<VorbisDecoder>();

    juce::ScopedLock sl(downloadMutex);
    guidToInterval[begin.guidHex] = std::move(pd);

    auto key = std::make_pair(begin.username, begin.channelIndex);
    auto &stream = channelStreams[key];
    stream.username = begin.username;
    stream.channelIndex = begin.channelIndex;
    stream.queue.push_back(interval);

    // Signal the audio thread that a new server interval has started.
    // compare_exchange prevents re-signalling if the audio thread hasn't
    // processed the previous signal yet (avoids double-swap for multi-channel
    // sessions where several DOWNLOAD_INTERVAL_BEGIN messages arrive per boundary).
    bool expected = false;
    intervalBeginSignal.compare_exchange_strong(expected, true);
  }
  // SERVER_DOWNLOAD_INTERVAL_WRITE
  else if (type == 0x05) {
    NinjamProtocol::IntervalWrite write;
    if (!NinjamProtocol::parseIntervalWrite(payload, write))
      return malformed();
    {
      const juce::String &guid = write.guidHex;
      const juce::uint8 flags = write.isFinal ? 1 : 0;

      juce::ScopedLock sl(downloadMutex);
      auto it = guidToInterval.find(guid);
      if (it != guidToInterval.end()) {
        auto &pd = it->second;
        auto &interval = *pd.target;

        int oggDataSize = write.audioSize;
        if (oggDataSize > 0 && pd.decoder != nullptr) {
          const char *oggData = static_cast<const char *>(write.audioData);
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
    NinjamProtocol::Chat parsed;
    if (!NinjamProtocol::parseChat(payload, parsed))
      return malformed();

    if (parsed.type.isNotEmpty()) {
      ChatMessage msg;
      msg.type = parsed.type;
      if (msg.type == "MSG" || msg.type == "PRIVMSG") {
        msg.username = parsed.p1;
        msg.text = parsed.p2;
      } else if (msg.type == "TOPIC") {
        msg.username = "Server";
        msg.text = "Topic: " + parsed.p2;
      } else if (msg.type == "JOIN") {
        msg.username = "Server";
        msg.text = parsed.p1 + " joined";
      } else if (msg.type == "PART") {
        msg.username = "Server";
        msg.text = parsed.p1 + " left";
      } else {
        msg.username = "Server";
        msg.text = "[" + msg.type + "] " + parsed.p1 + " " + parsed.p2;
      }

      {
        juce::ScopedLock sl(chatMutex);
        chatLog.add(msg);
        if (chatLog.size() > 100)
          chatLog.remove(0); // keep history bounded
      }

      callAsyncIfAlive(
          [this, type = msg.type, user = msg.username, text = msg.text]() {
            listeners.call(&NinjamClientListener::onChatMessage, type, user,
                           text);
          });
    }
  }
  return true;
}

void NinjamClient::sendAuthRequest(const juce::uint8 challenge[8]) {
  juce::uint8 hash[20];
  NinjamProtocol::computeAuthHash(currentUsername, currentPassword, challenge,
                                  hash);
  auto packet = NinjamProtocol::buildAuthUser(hash, currentUsername);
  writeFull(0x80, packet.getData(), static_cast<int>(packet.getSize()));
}

void NinjamClient::sendChannelInfo() {
  juce::StringArray names;
  {
    juce::ScopedLock sl(channelInfoMutex);
    names = storedChannelNames;
  }
  auto payload = NinjamProtocol::buildChannelInfo(names);
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

  juce::uint8 guid[16];
  for (int i = 0; i < 16; i++)
    guid[i] = (juce::uint8)(juce::Random::getSystemRandom().nextInt(256));

  int numCh = mono ? 1 : buffer.getNumChannels();
  const char fourcc[4] = {'O', 'G', 'G', 'v'};
  auto beginPacket =
      NinjamProtocol::buildIntervalBegin(guid, 0, fourcc, channelIndex);
  writeFull(0x83, beginPacket.getData(),
            static_cast<int>(beginPacket.getSize()));

  // The stream must declare the rate the audio is actually at, or every
  // listener resamples it -- a 44.1 kHz session sent as 48 kHz plays back
  // 8.8% sharp.
  const int encodeRate = sampleRate > 0.0 ? (int)sampleRate : 48000;
  VorbisEncoder encoder(encodeRate, numCh, 128,
                        juce::Random::getSystemRandom().nextInt());

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

      auto writePacket =
          NinjamProtocol::buildIntervalWrite(guid, false, oggData, avail);
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

    auto writePacket =
        NinjamProtocol::buildIntervalWrite(guid, true, oggData, avail);
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
  int swaps = diagSwaps.exchange(0);
  int earlySwaps = diagSwapsBeforeConsumed.exchange(0);
  int dropped = diagSamplesDroppedOnSwap.exchange(0);
  int underruns = diagUnderrunBlocks.exchange(0);
  int lastSamples = diagLastIntervalSamples.load();
  int lastExpected = diagLastIntervalExpected.load();
  if (swaps == 0 && earlySwaps == 0 && underruns == 0)
    return;
  juce::Logger::writeToLog(
      juce::String::formatted(
          "[diag] swaps=%d early=%d droppedSmp=%d underrunBlocks=%d "
          "lastInterval=%d/%d",
          swaps, earlySwaps, dropped, underruns,
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
  std::vector<std::pair<juce::String, juce::uint32>> masks;
  {
    juce::ScopedLock sl(downloadMutex);
    for (auto &[uname, user] : remoteUsers) {
      juce::uint32 mask = 0;
      for (auto &[chIdx, ch] : user.channels)
        if (chIdx >= 0 && chIdx < 32 && ch.recvEnabled)
          mask |= (1u << chIdx);
      masks.emplace_back(uname, mask);
    }
  }
  if (masks.empty())
    return;

  auto payload = NinjamProtocol::buildUsermask(masks);
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
  auto msgBlock = NinjamProtocol::buildChat("MSG", text);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}

void NinjamClient::sendAdminCommand(const juce::String &command) {
  if (!isConnected())
    return;
  auto msgBlock = NinjamProtocol::buildChat("ADMIN", command);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}

void NinjamClient::sendPrivateMessage(const juce::String &username,
                                      const juce::String &text) {
  if (!isConnected())
    return;
  auto msgBlock = NinjamProtocol::buildChat("PRIVMSG", username, text);
  writeFull(0xC0, msgBlock.getData(), static_cast<int>(msgBlock.getSize()));
}
