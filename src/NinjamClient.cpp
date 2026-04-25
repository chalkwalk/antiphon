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
  if (shouldSave && !isSavingTx.load()) {
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
  } else if (!shouldSave && isSavingTx.load()) {
    txOggFile.reset();
    txWavWriter.reset();
  }
  isSavingTx.store(shouldSave);
}

void NinjamClient::setSaveRx(bool shouldSave) {
  juce::ScopedLock sl(rxFileMutex);
  if (shouldSave && !isSavingRx.load()) {
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
  } else if (!shouldSave && isSavingRx.load()) {
    rxOggFile.reset();
    rxWavWriter.reset();
  }
  isSavingRx.store(shouldSave);
}

void NinjamClient::closeDebugCaptureFiles() {
  {
    juce::ScopedLock sl(txFileMutex);
    txOggFile.reset();
    txWavWriter.reset(); // destroying the writer is what writes the WAV header
  }
  juce::ScopedLock sl(rxFileMutex);
  rxOggFile.reset();
  rxWavWriter.reset();
}

void NinjamClient::reopenDebugCaptureFiles() {
  // setSaveTx/Rx only act on a change, so the flag is cleared first to make the
  // reopen a change again.
  if (isSavingTx.exchange(false))
    setSaveTx(true);
  if (isSavingRx.exchange(false))
    setSaveRx(true);
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
  //
  // The slots are marked draining rather than freed here. The audio thread may
  // be inside the mix holding pointers into them, and this thread has no way to
  // know; it hands them back on its next pass and the memory is reclaimed when
  // the slot is next claimed. If the audio thread never runs again, the
  // destructor gets it.
  guidToInterval.clear();
  for (auto &slot : streamSlots)
    if (slot.state.load(std::memory_order_acquire) != StreamSlot::kFree)
      slot.state.store(StreamSlot::kDraining, std::memory_order_release);
  {
    juce::ScopedLock sl(usersMutex);
    slotIndexByKey.clear();
    remoteUsers.clear();
    roomMembers.clear();
  }
  soloMask.fetch_and(~kRemoteSolo, std::memory_order_relaxed);

  // Finalise the debug dumps so tx.wav and rx.wav are readable now rather than
  // when the plugin is closed. The toggles keep their state; a later connect
  // reopens them.
  closeDebugCaptureFiles();

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
    // Reopen the debug dumps if their toggles survived a previous disconnect.
    reopenDebugCaptureFiles();
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
    std::vector<std::pair<juce::String, int>> departed;
    {
      juce::ScopedLock sl(usersMutex);
      for (const auto &e : entries) {
        if (e.active) {
          // Membership is not removed when the channels go: a player who drops
          // to zero channels is still in the room, just not in the mixer.
          if (roomMembers.insert(e.username).second)
            changed = true;
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
            if (user.channels.erase(e.channelIndex) > 0) {
              changed = true;
              departed.emplace_back(e.username, e.channelIndex);
            }
            if (user.channels.empty())
              remoteUsers.erase(it);
          }
        }
      }
    }

    for (const auto &d : departed)
      releaseStreamSlot(d.first, d.second);

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

    const int slotIndex = acquireStreamSlot(begin.username, begin.channelIndex);
    if (slotIndex < 0) {
      juce::Logger::writeToLog("[rx] no free stream slot for " +
                               begin.username + " channel " +
                               juce::String(begin.channelIndex));
      return true;
    }
    auto &slot = streamSlots[(std::size_t)slotIndex];

    // Reclaim whatever the audio thread has finished with before allocating
    // another few megabytes.
    drainRetired(slot);

    auto interval = std::make_unique<DecodedInterval>();
    interval->guid = begin.guidHex;
    int bufferSamples = static_cast<int>(
        sampleRate * 60.0 / std::max(1, serverBpm) * serverBpi * 1.5f);
    interval->buffer.setSize(2, bufferSamples);
    interval->buffer.clear();
    // Before the interval is shared with the audio thread; see DecodedInterval.
    interval->publishWritePointers();

    DecodedInterval *raw = interval.get();
    slot.owned.push_back(std::move(interval));
    if (!slot.ready.push(raw)) {
      // The audio thread is not keeping up, or is not running. Drop the
      // interval whole rather than block: it was never made visible, so undoing
      // the ownership is just a pop.
      slot.owned.pop_back();
      return true;
    }

    PendingDownload pd;
    pd.target = raw;
    pd.slotIndex = slotIndex;
    pd.username = begin.username;
    pd.channelIndex = begin.channelIndex;
    pd.decoder = std::make_unique<VorbisDecoder>();
    guidToInterval[begin.guidHex] = std::move(pd);

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

      // No lock at all: guidToInterval is touched only by this thread, and the
      // audio thread never looks at it. The decoded samples reach the audio
      // thread the way they always have, by the atomic writePos, published
      // after the samples are in the buffer.
      PendingDownload *pdPtr = nullptr;
      auto found = guidToInterval.find(guid);
      if (found != guidToInterval.end())
        pdPtr = &found->second;

      if (pdPtr != nullptr) {
        auto &pd = *pdPtr;
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
                    float *writePtr = interval.channelWritePtr[(size_t)ch];
                    if (writePtr == nullptr) continue;
                    writePtr += writePos;
                    for (int s = 0; s < toCopy; ++s)
                      writePtr[s] = decodedBlock[s * out_nch + ch];
                  }
                  if (out_nch == 1 && interval.buffer.getNumChannels() == 2 &&
                      interval.channelWritePtr[0] != nullptr &&
                      interval.channelWritePtr[1] != nullptr) {
                    memcpy(interval.channelWritePtr[1] + writePos,
                           interval.channelWritePtr[0] + writePos,
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
                      if (auto *dst = interval.channelWritePtr[(size_t)ch])
                        memcpy(dst + writePos, dstBuf.getData(),
                               (size_t)numOut * sizeof(float));
                    }
                    if (out_nch == 1 && interval.buffer.getNumChannels() == 2 &&
                        interval.channelWritePtr[0] != nullptr &&
                        interval.channelWritePtr[1] != nullptr) {
                      memcpy(interval.channelWritePtr[1] + writePos,
                             interval.channelWritePtr[0] + writePos,
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
          guidToInterval.erase(guid);
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
        if (parsed.p1.isNotEmpty()) {
          juce::ScopedLock sl(usersMutex);
          roomMembers.insert(parsed.p1);
        }
      } else if (msg.type == "PART") {
        msg.username = "Server";
        msg.text = parsed.p1 + " left";
        if (parsed.p1.isNotEmpty()) {
          juce::ScopedLock sl(usersMutex);
          roomMembers.erase(parsed.p1);
        }
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

int NinjamClient::acquireStreamSlot(const juce::String &username,
                                    int channelIndex) {
  const auto key = std::make_pair(username, channelIndex);

  // Whatever the UI has already set for this channel. The channel strip exists
  // as soon as USER_INFO_CHANGE names the channel, which is before the first
  // interval arrives, so a fader moved in that window would otherwise be lost
  // the moment the slot was claimed.
  RemoteUserChannel seed;
  {
    juce::ScopedLock sl(usersMutex);
    auto existing = slotIndexByKey.find(key);
    if (existing != slotIndexByKey.end())
      return existing->second;

    auto uit = remoteUsers.find(username);
    if (uit != remoteUsers.end()) {
      auto cit = uit->second.channels.find(channelIndex);
      if (cit != uit->second.channels.end())
        seed = cit->second;
    }
  }

  for (int i = 0; i < kMaxStreams; ++i) {
    auto &slot = streamSlots[(std::size_t)i];
    if (slot.state.load(std::memory_order_acquire) != StreamSlot::kFree)
      continue;

    // kFree is published by the audio thread only after it has handed back
    // every interval it held, so freeing them here cannot pull the buffer out
    // from under a mix in progress.
    drainRetired(slot);
    slot.owned.clear();

    slot.username = username;
    slot.channelIndex = channelIndex;
    slot.volume.store(seed.volume, std::memory_order_relaxed);
    slot.pan.store(seed.pan, std::memory_order_relaxed);
    slot.muted.store(seed.isMuted, std::memory_order_relaxed);
    slot.soloed.store(seed.isSoloed, std::memory_order_relaxed);
    slot.outputBus.store(seed.outputBusIndex, std::memory_order_relaxed);
    slot.recvEnabled.store(seed.recvEnabled, std::memory_order_relaxed);
    slot.peakLevel.store(0.0f, std::memory_order_relaxed);
    slot.muteRamp.prepare(sampleRate);
    // Starts wherever the channel should already be, so claiming a slot for a
    // channel you had muted does not fade it in.
    {
      const bool mutedNow =
          seed.isMuted || !seed.recvEnabled ||
          (isAnySoloActive() && !seed.isSoloed);
      slot.muteRamp.jumpTo(mutedNow ? 0.0f : 1.0f);
    }
    // Everything above must be visible to the audio thread before it sees the
    // slot go live.
    slot.state.store(StreamSlot::kLive, std::memory_order_release);

    {
      juce::ScopedLock sl(usersMutex);
      slotIndexByKey[key] = i;
    }
    return i;
  }

  return -1;
}

void NinjamClient::releaseStreamSlot(const juce::String &username,
                                     int channelIndex) {
  juce::ScopedLock sl(usersMutex);
  auto it = slotIndexByKey.find(std::make_pair(username, channelIndex));
  if (it == slotIndexByKey.end())
    return;

  // Only marked. The audio thread may be mid-block holding pointers into this
  // slot; it is the one that completes the transition to kFree, and the memory
  // is reclaimed when the slot is next claimed.
  streamSlots[(std::size_t)it->second].state.store(StreamSlot::kDraining,
                                                   std::memory_order_release);
  slotIndexByKey.erase(it);
}

void NinjamClient::drainRetired(StreamSlot &slot) {
  while (DecodedInterval *iv = slot.retired.pop()) {
    for (auto it = slot.owned.begin(); it != slot.owned.end(); ++it) {
      if (it->get() == iv) {
        slot.owned.erase(it);
        break;
      }
    }
  }
}

void NinjamClient::drainAllRetired() {
  for (auto &slot : streamSlots)
    drainRetired(slot);
}

void NinjamClient::releaseSlotOnAudioThread(StreamSlot &slot) {
  // Sized so none of these pushes can fail; see StreamSlot.
  if (slot.fadeOut != nullptr) {
    slot.retired.push(slot.fadeOut);
    slot.fadeOut = nullptr;
  }
  if (slot.current != nullptr) {
    slot.retired.push(slot.current);
    slot.current = nullptr;
  }
  while (DecodedInterval *iv = slot.ready.pop())
    slot.retired.push(iv);

  slot.readPos = 0;
  slot.fadeOutPos = 0;
  slot.fadeTotal = 0;
  slot.fadeRemaining = 0;
  // Publishes the hand-back: the next claimer may free everything above.
  slot.state.store(StreamSlot::kFree, std::memory_order_release);
}

void NinjamClient::swapIntervalBuffers() {
  constexpr int kFadeSamples = 256;
  for (auto &slot : streamSlots) {
    const int st = slot.state.load(std::memory_order_acquire);
    if (st == StreamSlot::kFree)
      continue;
    if (st == StreamSlot::kDraining) {
      releaseSlotOnAudioThread(slot);
      continue;
    }

    // A swap arriving mid-fade ends the fade; whatever it was reading is
    // finished with.
    if (slot.fadeOut != nullptr) {
      slot.retired.push(slot.fadeOut);
      slot.fadeOut = nullptr;
      slot.fadeRemaining = 0;
    }

    if (slot.current != nullptr) {
      const int unplayed = slot.current->writePos.load() - slot.readPos;
      if (unplayed > 0) {
        diagSwapsBeforeConsumed.fetch_add(1);
        diagSamplesDroppedOnSwap.fetch_add(unplayed);
        const int fadeLen = std::min(unplayed, kFadeSamples);
        slot.fadeOut = slot.current;
        slot.fadeOutPos = slot.readPos;
        slot.fadeTotal = fadeLen;
        slot.fadeRemaining = fadeLen;
      } else {
        slot.retired.push(slot.current);
      }
      slot.current = nullptr;
    }

    if (DecodedInterval *next = slot.ready.pop()) {
      slot.current = next;
      slot.readPos = 0;
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
  std::map<juce::String, RemoteUser> copy;
  {
    juce::ScopedLock sl(usersMutex);
    copy = remoteUsers;
    // Meter levels live in the slots, where the audio thread can publish them
    // without touching this map. Folded in here so the UI still sees one
    // coherent picture of a channel.
    for (const auto &[key, index] : slotIndexByKey) {
      auto uit = copy.find(key.first);
      if (uit == copy.end())
        continue;
      auto cit = uit->second.channels.find(key.second);
      if (cit == uit->second.channels.end())
        continue;
      cit->second.peakLevel = streamSlots[(std::size_t)index].peakLevel.load(
          std::memory_order_relaxed);
    }
  }
  return copy;
}

// Applies a change to both the UI's picture of the channel and, if the channel
// is playing, the atomic the audio thread actually reads. Callers hold
// usersMutex; the audio thread never does.
template <typename ApplyToChannel, typename ApplyToSlot>
void NinjamClient::updateChannelParam(const juce::String &username,
                                      int channelIndex, ApplyToChannel toChannel,
                                      ApplyToSlot toSlot) {
  juce::ScopedLock sl(usersMutex);
  auto uit = remoteUsers.find(username);
  if (uit != remoteUsers.end()) {
    auto cit = uit->second.channels.find(channelIndex);
    if (cit != uit->second.channels.end())
      toChannel(cit->second);
  }
  auto sit = slotIndexByKey.find(std::make_pair(username, channelIndex));
  if (sit != slotIndexByKey.end())
    toSlot(streamSlots[(std::size_t)sit->second]);
}

std::vector<NinjamClient::RoomMember> NinjamClient::getRoomMembers() const {
  std::vector<RoomMember> out;
  juce::ScopedLock sl(usersMutex);
  out.reserve(roomMembers.size());
  for (const auto &name : roomMembers) {
    RoomMember m;
    m.username = name;
    auto it = remoteUsers.find(name);
    m.channelCount = it != remoteUsers.end() ? (int)it->second.channels.size() : 0;
    out.push_back(m);
  }
  return out;
}

juce::String NinjamClient::getSelfUsername() const {
  juce::ScopedLock sl(usersMutex);
  return currentUsername;
}

void NinjamClient::setRemoteUserVolume(const juce::String &username,
                                       int channelIndex, float volume) {
  updateChannelParam(
      username, channelIndex,
      [volume](RemoteUserChannel &c) { c.volume = volume; },
      [volume](StreamSlot &s) {
        s.volume.store(volume, std::memory_order_relaxed);
      });
}

void NinjamClient::setRemoteUserPan(const juce::String &username,
                                    int channelIndex, float pan) {
  updateChannelParam(
      username, channelIndex, [pan](RemoteUserChannel &c) { c.pan = pan; },
      [pan](StreamSlot &s) { s.pan.store(pan, std::memory_order_relaxed); });
}

void NinjamClient::setRemoteUserMute(const juce::String &username,
                                     int channelIndex, bool mute) {
  updateChannelParam(
      username, channelIndex, [mute](RemoteUserChannel &c) { c.isMuted = mute; },
      [mute](StreamSlot &s) {
        s.muted.store(mute, std::memory_order_relaxed);
      });
}

void NinjamClient::setRemoteUserSolo(const juce::String &username,
                                     int channelIndex, bool solo) {
  updateChannelParam(
      username, channelIndex, [solo](RemoteUserChannel &c) { c.isSoloed = solo; },
      [solo](StreamSlot &s) {
        s.soloed.store(solo, std::memory_order_relaxed);
      });

  // The remote half of the global solo bus (njclient.cpp:1750). Recomputed
  // rather than counted, so it cannot drift out of step with the channels.
  bool anyRemoteSolo = false;
  {
    juce::ScopedLock sl(usersMutex);
    for (const auto &[uname, user] : remoteUsers) {
      for (const auto &[chIdx, ch] : user.channels)
        if (ch.isSoloed) { anyRemoteSolo = true; break; }
      if (anyRemoteSolo) break;
    }
  }
  const int bits = soloMask.load(std::memory_order_relaxed);
  soloMask.store(anyRemoteSolo ? (bits | kRemoteSolo) : (bits & ~kRemoteSolo),
                 std::memory_order_relaxed);
}

void NinjamClient::setRemoteUserRecv(const juce::String &username,
                                     int channelIndex, bool recv) {
  // Recv both tells the server to stop sending and silences what we already
  // have. The server-side change cannot be immediate -- an interval may already
  // be in flight -- so without the local half the control would appear to do
  // nothing for a second or two. Making recv part of the mute decision needs no
  // handover: once the server does stop, there is nothing left to mute and the
  // condition is still true.
  updateChannelParam(
      username, channelIndex,
      [recv](RemoteUserChannel &c) { c.recvEnabled = recv; },
      [recv](StreamSlot &s) {
        s.recvEnabled.store(recv, std::memory_order_relaxed);
      });
  sendUserMask();
}

void NinjamClient::setRemoteUserOutputBus(const juce::String &username,
                                          int channelIndex, int busIdx) {
  updateChannelParam(
      username, channelIndex,
      [busIdx](RemoteUserChannel &c) { c.outputBusIndex = busIdx; },
      [busIdx](StreamSlot &s) {
        s.outputBus.store(busIdx, std::memory_order_relaxed);
      });
}

void NinjamClient::sendUserMask() {
  std::vector<std::pair<juce::String, juce::uint32>> masks;
  {
    juce::ScopedLock sl(usersMutex);
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
  // No lock, no allocation, no map traversal: a walk over a fixed array whose
  // entries are never created or destroyed (PRINCIPLES section 7).
  int numSamples = buffer.getNumSamples();
  int dstChannels = buffer.getNumChannels();

  // The global bus, so a local solo silences remote players too.
  const bool anySolo = isAnySoloActive();

  for (auto &slot : streamSlots) {
    const int slotState = slot.state.load(std::memory_order_acquire);
    if (slotState == StreamSlot::kFree)
      continue;
    if (slotState == StreamSlot::kDraining) {
      releaseSlotOnAudioThread(slot);
      continue;
    }
    const float vol = slot.volume.load(std::memory_order_relaxed);
    const float pan = slot.pan.load(std::memory_order_relaxed);
    const bool isSoloed = slot.soloed.load(std::memory_order_relaxed);
    const int outBusIdx = slot.outputBus.load(std::memory_order_relaxed);

    // njclient.cpp:1388. When any solo is active it *replaces* the mute
    // decision rather than combining with it, so a channel that is both muted
    // and soloed is heard. We used to AND the two, so mute won and solo could
    // not bring a muted channel back.
    //
    // Recv sits upstream of all of it: if we have asked the server to stop
    // sending a channel there is no signal to un-mute, so not even solo can
    // recover it. The reference reaches the same place by deleting the decode
    // state outright (njclient.cpp:1710).
    bool isMuted = anySolo ? !isSoloed
                           : slot.muted.load(std::memory_order_relaxed);
    if (!slot.recvEnabled.load(std::memory_order_relaxed))
      isMuted = true;

    // Mute is a gain, not a branch: cutting the mix in and out between blocks
    // is a step discontinuity, which is a click.
    slot.muteRamp.setTarget(isMuted ? 0.0f : 1.0f);

    // Everything that mixes lives in here so that the ramp below advances for
    // the whole block on every path, including the ones that mix nothing. A
    // ramp that only moved on blocks carrying audio would drift out of step
    // with the clock, and mute would take longer than 5 ms whenever a stream
    // underran.
    [&] {
    if (slot.current == nullptr && slot.fadeOut == nullptr)
      return;

    float lGain = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    float rGain = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    int maxBus = std::max(0, dstChannels / 2 - 1);
    int busIdx = juce::jlimit(0, maxBus, outBusIdx);

    // Crossfade region: mix the old interval's tail (fade-out) with the new
    // interval's head (fade-in) for the first fadeCopy samples of this block.
    int fadeCopy = 0;
    if (slot.fadeOut && slot.fadeRemaining > 0) {
      fadeCopy = std::min(numSamples, slot.fadeRemaining);
      int elapsed = slot.fadeTotal - slot.fadeRemaining;

      {
        int oldChannels = slot.fadeOut->buffer.getNumChannels();
        for (int ch = 0; ch < std::min(oldChannels, 2); ++ch) {
          int dstCh = busIdx * 2 + ch;
          if (dstCh >= dstChannels) continue;
          float chGain = (ch == 0) ? lGain : rGain;
          const float *src =
              slot.fadeOut->buffer.getReadPointer(ch, slot.fadeOutPos);
          float *dst = buffer.getWritePointer(dstCh);
          for (int s = 0; s < fadeCopy; ++s) {
            float g = (float)(slot.fadeRemaining - s) / slot.fadeTotal;
            dst[s] += src[s] * chGain * g * slot.muteRamp.gainAt(s);
          }
        }

        if (slot.current) {
          int newAvail = slot.current->writePos.load() - slot.readPos;
          int newFadeCopy = std::min(fadeCopy, newAvail);
          if (newFadeCopy > 0) {
            int newChannels = slot.current->buffer.getNumChannels();
            for (int ch = 0; ch < std::min(newChannels, 2); ++ch) {
              int dstCh = busIdx * 2 + ch;
              if (dstCh >= dstChannels) continue;
              float chGain = (ch == 0) ? lGain : rGain;
              const float *src =
                  slot.current->buffer.getReadPointer(ch, slot.readPos);
              float *dst = buffer.getWritePointer(dstCh);
              for (int s = 0; s < newFadeCopy; ++s) {
                float g = (float)(elapsed + s) / slot.fadeTotal;
                dst[s] += src[s] * chGain * g * slot.muteRamp.gainAt(s);
              }
            }
            // readPos advances whatever the mute gain is, so a muted stream
            // stays in time and un-muting lands where it should.
            slot.readPos += newFadeCopy;
          }
        }
      }

      slot.fadeOutPos += fadeCopy;
      slot.fadeRemaining -= fadeCopy;
      if (slot.fadeRemaining <= 0) {
        slot.retired.push(slot.fadeOut);
        slot.fadeOut = nullptr;
        slot.fadeRemaining = 0;
      }
    }

    // Post-fade region: normal mix from current at full gain.
    if (!slot.current)
      return;

    auto &iv = *slot.current;
    int avail = iv.writePos.load() - slot.readPos;
    int remainingSamples = numSamples - fadeCopy;
    if (remainingSamples <= 0)
      return;
    if (avail <= 0) {
      if (!iv.finalReceived.load())
        diagUnderrunBlocks.fetch_add(1);
      return;
    }

    int toCopy = std::min(remainingSamples, avail);
    int srcChannels = slot.current->buffer.getNumChannels();

    // Metered before the mute gain, so the meter keeps showing that a player is
    // playing even while you have them muted.
    float peak = 0.0f;
    for (int ch = 0; ch < std::min(srcChannels, 2); ++ch) {
      float chGain = (ch == 0) ? lGain : rGain;
      const float *src =
          slot.current->buffer.getReadPointer(ch, slot.readPos);
      for (int s = 0; s < toCopy; ++s)
        peak = std::max(peak, std::abs(src[s]) * chGain);
    }
    slot.peakLevel.store(peak, std::memory_order_relaxed);

    // addFrom took a constant gain, which is why mute used to be a branch and
    // therefore a click. The ramp has to be read per sample, at the offset
    // within the block that this region occupies.
    for (int ch = 0; ch < std::min(srcChannels, 2); ++ch) {
      int dstCh = busIdx * 2 + ch;
      if (dstCh >= dstChannels) continue;
      const float chGain = (ch == 0) ? lGain : rGain;
      const float *src = slot.current->buffer.getReadPointer(ch, slot.readPos);
      float *dst = buffer.getWritePointer(dstCh) + fadeCopy;
      for (int s = 0; s < toCopy; ++s)
        dst[s] += src[s] * chGain * slot.muteRamp.gainAt(fadeCopy + s);
    }

    slot.readPos += toCopy;
    }();

    slot.muteRamp.advance(numSamples);
  }

  // Checked before the lock is even considered, so the normal path takes no
  // lock on the audio thread at all. Save Rx is a debug capture: when it is on
  // the audio thread does file I/O and that is accepted, because writing the
  // mix as the audio thread sees it is the whole point of the toggle. It is
  // never on in ordinary use.
  if (isSavingRx.load(std::memory_order_relaxed)) {
    juce::ScopedLock fl(rxFileMutex);
    if (isSavingRx.load(std::memory_order_relaxed) && rxWavWriter != nullptr)
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
