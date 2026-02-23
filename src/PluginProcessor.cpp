#include "PluginProcessor.h"
#include "ChannelMix.h"
#include "PluginEditor.h"

AntiphonAudioProcessor::AntiphonAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
#endif
{
  ninjamClient.addListener(this);
  localChannels.push_back(std::make_shared<LocalChannel>());
  if (!isStandaloneApp()) metronomeEnabled = false;
}

AntiphonAudioProcessor::~AntiphonAudioProcessor() {
  ninjamClient.removeListener(this);
}

const juce::String AntiphonAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool AntiphonAudioProcessor::acceptsMidi() const { return false; }
bool AntiphonAudioProcessor::producesMidi() const { return false; }
bool AntiphonAudioProcessor::isMidiEffect() const { return false; }
double AntiphonAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int AntiphonAudioProcessor::getNumPrograms() { return 1; }
int AntiphonAudioProcessor::getCurrentProgram() { return 0; }
void AntiphonAudioProcessor::setCurrentProgram(int) {}
const juce::String AntiphonAudioProcessor::getProgramName(int) { return {}; }
void AntiphonAudioProcessor::changeProgramName(int, const juce::String &) {}

int AntiphonAudioProcessor::addLocalChannel() {
  juce::ScopedLock sl(localChannelMutex);
  auto ch = std::make_shared<LocalChannel>();
  int ringSize = (int)getSampleRate() * 30;
  if (ringSize > 0) {
    ch->ring.setSize(2, ringSize);
    ch->fifo.setTotalSize(ringSize);
  }
  localChannels.push_back(ch);
  return (int)localChannels.size() - 1;
}

void AntiphonAudioProcessor::removeLastLocalChannel() {
  juce::ScopedLock sl(localChannelMutex);
  if (localChannels.size() <= 1) return;
  localChannels.back()->isValid.store(false);
  localChannels.pop_back();
}

int AntiphonAudioProcessor::addInputBus() {
  if (!addBus(true)) return -1;
  updateHostDisplay();
  return getBusCount(true) - 1;
}

void AntiphonAudioProcessor::removeLastInputBus() {
  if (getBusCount(true) <= 1) return;
  int removedIdx = getBusCount(true) - 1;
  {
    juce::ScopedLock sl(localChannelMutex);
    for (auto &lc : localChannels)
      if (lc->inputBusIndex.load() == removedIdx)
        lc->inputBusIndex.store(0);
  }
  removeBus(true);
  updateHostDisplay();
}

int AntiphonAudioProcessor::addOutputBus() {
  if (!addBus(false)) return -1;
  updateHostDisplay();
  return getBusCount(false) - 1;
}

void AntiphonAudioProcessor::removeLastOutputBus() {
  if (getBusCount(false) <= 1) return;
  int removedIdx = getBusCount(false) - 1;
  auto users = ninjamClient.getRemoteUsers();
  for (auto &[uname, user] : users)
    for (auto &[chIdx, ch] : user.channels)
      if (ch.outputBusIndex == removedIdx)
        ninjamClient.setRemoteUserOutputBus(uname, chIdx, 0);
  removeBus(false);
  updateHostDisplay();
}

void AntiphonAudioProcessor::setRemoteUserOutputBus(const juce::String &username,
                                                  int channelIndex, int busIdx) {
  ninjamClient.setRemoteUserOutputBus(username, channelIndex, busIdx);
  savedRemoteRoutings[{username, channelIndex}] = busIdx;
}

void AntiphonAudioProcessor::onUserInfoChange() {
  auto users = ninjamClient.getRemoteUsers();
  for (auto &[uname, user] : users) {
    for (auto &[chIdx, ch] : user.channels) {
      auto it = savedRemoteRoutings.find({uname, chIdx});
      if (it != savedRemoteRoutings.end())
        ninjamClient.setRemoteUserOutputBus(uname, chIdx, it->second);
    }
  }
}

void AntiphonAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {
  juce::ScopedLock sl(localChannelMutex);
  int ringSize = (int)sampleRate * 30;
  for (auto &lc : localChannels) {
    lc->ring.setSize(2, ringSize);
    lc->ring.clear();
    lc->fifo.setTotalSize(ringSize);
  }
  ninjamClient.setSampleRate(sampleRate);

  intervalClock.prepare(sampleRate);
  intervalClock.setTempo(internalBpm.load(), internalBpi.load());
  metronomeVoice.prepare(sampleRate);
  metronomeScratch.setSize(1, juce::jmax(samplesPerBlock, 1));

  // Sized here so processBlock never allocates. Generous on channels so that
  // adding an input bus at runtime does not outgrow it before the host calls
  // prepareToPlay again.
  inputSnapshot.setSize(juce::jmax(2, getTotalNumInputChannels() + 8),
                        juce::jmax(samplesPerBlock, 1));
  inputSnapshot.clear();
  captureSegments.reserve(8);

  // Preallocated so advance() never allocates on the audio thread. Two events
  // per beat is already generous; the vector only ever grows here.
  clockEvents.reserve((size_t)juce::jmax(64, internalBpi.load() * 2 + 4));
}

void AntiphonAudioProcessor::injectTestTone(int numSamples) {
  const double sr = getSampleRate();
  const int intervalLen = intervalClock.samplesPerInterval();
  if (sr <= 0.0 || intervalLen <= 0 || inputSnapshot.getNumSamples() < numSamples)
    return;

  const auto &probe = testProbe;
  const int64_t startPos = intervalClock.samplePosInInterval();

  // Written into the input snapshot, so the monitor mix and the capture both
  // see it exactly as if it had arrived on every input bus.
  for (int i = 0; i < numSamples; ++i) {
    const int64_t pos = (startPos + i) % intervalLen;
    const float v =
        probe.sampleAt(pos, intervalLen, testToneSample + i, sr);
    for (int ch = 0; ch < inputSnapshot.getNumChannels(); ++ch)
      inputSnapshot.setSample(ch, i, v);
  }
  testToneSample += numSamples;
}

void AntiphonAudioProcessor::captureInputRange(int startSample, int count) {
  if (count <= 0)
    return;

  juce::ScopedLock sl(localChannelMutex);
  const int numInBuses = getBusCount(true);

  // Null for a channel index the snapshot does not have, which ChannelMix
  // treats as absent rather than reading past the end.
  auto sourcePointer = [this](int ch) -> const float * {
    return ch >= 0 && ch < inputSnapshot.getNumChannels()
               ? inputSnapshot.getReadPointer(ch)
               : nullptr;
  };

  for (auto &lcPtr : localChannels) {
    auto &lc = *lcPtr;
    const int busIdx = juce::jlimit(0, numInBuses - 1, lc.inputBusIndex.load());
    auto *bus = getBus(true, busIdx);
    if (!bus)
      continue;
    const int offset = bus->getChannelIndexInProcessBlockBuffer(0);
    const int busCh = bus->getNumberOfChannels();

    // Gain is vol*pan only -- mute and solo are monitor-only and must not
    // affect what other players hear.
    const auto gains =
        ChannelMix::panGains(lc.volume.load(), lc.pan.load());
    const bool mono = lc.isMono.load();

    if (lc.fifo.getFreeSpace() < count)
      continue;

    const float *srcL = sourcePointer(offset);
    const float *srcR = busCh > 1 ? sourcePointer(offset + 1) : nullptr;

    int s1, n1, s2, n2;
    lc.fifo.prepareToWrite(count, s1, n1, s2, n2);
    if (n1 > 0)
      ChannelMix::write(lc.ring.getWritePointer(0, s1),
                        lc.ring.getWritePointer(1, s1), srcL, srcR, mono,
                        startSample, n1, gains);
    if (n2 > 0)
      ChannelMix::write(lc.ring.getWritePointer(0, s2),
                        lc.ring.getWritePointer(1, s2), srcL, srcR, mono,
                        startSample + n1, n2, gains);
    lc.fifo.finishedWrite(n1 + n2);
  }
}

void AntiphonAudioProcessor::renderMetronome(juce::AudioBuffer<float> &buffer,
                                           int startSample, int count,
                                           float gain,
                                           int totalNumOutputChannels) {
  if (count <= 0 || gain <= 0.0f || !metronomeVoice.isActive())
    return;
  if (metronomeScratch.getNumSamples() < count)
    return; // block larger than prepareToPlay promised; skip rather than allocate

  metronomeScratch.clear(0, 0, count);
  metronomeVoice.render(metronomeScratch.getWritePointer(0), count, gain);

  for (int ch = 0; ch < std::min(2, totalNumOutputChannels); ++ch)
    buffer.addFrom(ch, startSample, metronomeScratch, 0, 0, count);
}

void AntiphonAudioProcessor::releaseResources() {
  juce::ScopedLock sl(localChannelMutex);
  for (auto &lc : localChannels) {
    lc->fifo.reset();
    lc->ring.setSize(0, 0);
  }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AntiphonAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  if (layouts.outputBuses.isEmpty()) return false;
  for (auto &ch : layouts.outputBuses)
    if (ch != juce::AudioChannelSet::stereo()) return false;
  for (auto &ch : layouts.inputBuses)
    if (ch != juce::AudioChannelSet::stereo() &&
        ch != juce::AudioChannelSet::mono())
      return false;
  return true;
}
#endif

void AntiphonAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;
  auto totalNumOutputChannels = std::min(getTotalNumOutputChannels(), buffer.getNumChannels());
  int ns = buffer.getNumSamples();

  // 1. Host Sync
  if (auto *ph = getPlayHead()) {
    auto info = ph->getPosition();
    if (info.hasValue()) {
      hostIsPlaying = info->getIsPlaying();
      if (info->getBpm().hasValue()) hostBpm = *info->getBpm();
      if (info->getPpqPosition().hasValue()) hostPpqPosition = *info->getPpqPosition();
    }
  }

  // 2. Snapshot every input channel before anything overwrites it.
  //    JUCE aliases input and output buses in the same buffer, so clearing the
  //    output (step 3) and mixing remote audio into it (step 7) both destroy
  //    input data. Only bus 0 used to be snapshotted, which left input bus 1
  //    onwards reading whatever had just been written to the matching output
  //    bus. Everything downstream now reads this copy instead of `buffer`.
  //    Allocated in prepareToPlay, never on the audio thread.
  const int numInCh =
      juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
  if (inputSnapshot.getNumSamples() >= ns &&
      inputSnapshot.getNumChannels() >= numInCh) {
    for (int ch = 0; ch < numInCh; ++ch)
      inputSnapshot.copyFrom(ch, 0, buffer, ch, 0, ns);
  }

  // 3. Clear output channels 0/1 (and any trailing unused outputs).
  for (int i = 0; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, ns);

  // 3b. Debug test-tone injection. Replaces every input bus with a known
  //     signal so that a server-side session archive can be measured rather
  //     than listened to. Done here, after the output clear, so both the
  //     monitor pass and the capture pass see it. The interval position is
  //     read before the clock advances, so it is the position at the start of
  //     this block.
  if (testToneEnabled.load())
    injectTestTone(ns);

  // 4. Monitor mix pass -- mix all local input buses into output 0/1.
  {
    juce::ScopedLock sl(localChannelMutex);

    bool anyMonitorSolo = false;
    for (auto &lcPtr : localChannels)
      if (lcPtr->monitorSolo.load()) { anyMonitorSolo = true; break; }

    int numInBuses = getBusCount(true);
    for (int ci = 0; ci < (int)localChannels.size(); ++ci) {
      auto &lc = *localChannels[ci];
      int busIdx = juce::jlimit(0, numInBuses - 1, lc.inputBusIndex.load());
      auto *bus = getBus(true, busIdx);
      if (!bus) continue;
      int offset = bus->getChannelIndexInProcessBlockBuffer(0);
      int busCh = bus->getNumberOfChannels();

      bool muted = lc.muted.load();
      bool solo  = lc.monitorSolo.load();
      float monitorFactor = (muted ? 0.0f : 1.0f) *
                            (anyMonitorSolo && !solo ? 0.0f : 1.0f);
      const auto txGains =
          ChannelMix::panGains(lc.volume.load(), lc.pan.load());
      const ChannelMix::Frame monGains{txGains.left * monitorFactor,
                                       txGains.right * monitorFactor};
      bool mono = lc.isMono.load();

      // Every bus reads the snapshot: the live buffer no longer holds valid
      // input for any bus once the output has been cleared.
      auto snapshotPointer = [this](int ch) -> const float * {
        return ch >= 0 && ch < inputSnapshot.getNumChannels()
                   ? inputSnapshot.getReadPointer(ch)
                   : nullptr;
      };
      const float *srcL = snapshotPointer(offset);
      const float *srcR = busCh > 1 ? snapshotPointer(offset + 1) : nullptr;

      // Metered after mono summing, so a mono channel shows the single signal
      // it actually transmits rather than an unrelated stereo pair. Scaled by
      // monitor gain, so the VU shows what you hear.
      const auto p =
          ChannelMix::peaks(srcL, srcR, mono, 0, ns, monGains);
      lc.peakL.store(p.left);
      lc.peakR.store(p.right);

      if (totalNumOutputChannels < 2) continue;

      ChannelMix::addInto(buffer.getWritePointer(0), buffer.getWritePointer(1),
                          srcL, srcR, mono, ns, monGains);
    }
  }

  // Helper: fire per-channel callAsync at interval boundary
  auto fireCaptureLambdas = [this]() {
    juce::ScopedLock sl(localChannelMutex);
    for (int ci = 0; ci < (int)localChannels.size(); ++ci) {
      auto &lcPtr = localChannels[ci];
      if (!lcPtr->xmitEnabled.load()) { lcPtr->fifo.reset(); continue; }
      int length = lcPtr->fifo.getNumReady();
      if (ninjamClient.isConnected() && length > 0) {
        bool mono = lcPtr->isMono.load();
        auto capturedPtr = lcPtr;
        juce::MessageManager::callAsync([this, capturedPtr, length, ci, mono]() {
          if (!capturedPtr->isValid.load()) return;
          juce::AudioBuffer<float> buf(2, length);
          int s1, n1, s2, n2;
          capturedPtr->fifo.prepareToRead(length, s1, n1, s2, n2);
          for (int ch = 0; ch < 2; ++ch) {
            if (n1 > 0) buf.copyFrom(ch, 0,  capturedPtr->ring, ch, s1, n1);
            if (n2 > 0) buf.copyFrom(ch, n1, capturedPtr->ring, ch, s2, n2);
          }
          capturedPtr->fifo.finishedRead(n1 + n2);
          ninjamClient.processCapturedAudio(buf, length, ci, mono);
        });
      } else {
        lcPtr->fifo.reset();
      }
    }
  };

  // 5. Local interval metronome (sole authority for swap timing).
  // Per njclient's design, DOWNLOAD_INTERVAL_BEGIN is only used to queue
  // incoming audio, never to drive the local clock. Network jitter would
  // otherwise yank the interval clock mid-interval and discard seconds of
  // un-played audio.
  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    // Drain any pending signal so the diagnostic counter stays at 0.
    ninjamClient.intervalBeginSignal.store(false);

    // 6. Metronome click + interval boundary detection.
    // IntervalClock owns the grid; it is sample-exact and every interval is
    // the same length, matching the reference client's integer arithmetic.
    intervalClock.setTempo(internalBpm.load(), internalBpi.load());

    // Reset phase on disconnect (flag set by onDisconnected on message thread)
    if (phaseResetPending.exchange(false)) {
      intervalClock.reset();
      metronomeVoice.reset();
      intervalFlashIntensity.store(0.0f);
      beatFlashIntensity.store(0.0f);
    }

    // Sync state: decides whether we are actually in the jam, and resets the
    // interval clock on the transport-start edge we were armed for.
    SyncState::Inputs si;
    si.connected = ninjamClient.isConnected();
    si.transportPlaying = hostIsPlaying;
    si.syncRequested = syncRequested.exchange(false);
    si.hasTransport = !isStandaloneApp() && getPlayHead() != nullptr;
    // Compare at whole-BPM resolution; the server only carries an integer.
    si.tempoMatches =
        !si.hasTransport || (int)std::lround(hostBpm) == internalBpm.load();

    if (syncState.update(si)) {
      intervalClock.reset();
      metronomeVoice.reset();
    }
    publishedSyncState.store((int)syncState.get());

    // Everything below is gated on being in step, not merely connected: before
    // the user has synced we neither transmit nor play, so a jam never starts
    // out of phase with their DAW.
    const bool isConnected = syncState.isRunning();

    if (isConnected) {
      clockEvents.clear();
      intervalClock.advance(ns, clockEvents);

      const float metroGain =
          metronomeEnabled.load() ? metronomeVolume.load() : 0.0f;
      int renderedTo = 0;

      for (const auto &e : clockEvents) {
        // Render any click still sounding up to this event before retriggering.
        renderMetronome(buffer, renderedTo, e.sampleOffset - renderedTo,
                        metroGain, totalNumOutputChannels);
        renderedTo = e.sampleOffset;

        if (e.type == IntervalClock::Event::Type::IntervalStart) {
          intervalFlashIntensity.store(1.0f);
        } else {
          lastBeatCrossedIndex.store(e.beatIndex);
          if (e.beatIndex != 0)
            beatFlashIntensity.store(1.0f);
          metronomeVoice.trigger(e.beatIndex);
        }
      }

      renderMetronome(buffer, renderedTo, ns - renderedTo, metroGain,
                      totalNumOutputChannels);
    }

    publishedPhaseBeats.store((float)intervalClock.phaseBeats());

    // Capture, split at the interval boundary.
    //
    // The samples before the boundary belong to the interval that is ending
    // and must be in the buffer we transmit; the samples after it start the
    // next one. Capturing whole blocks and flushing at the boundary instead
    // rounds every transmitted interval up to a multiple of the block size --
    // measured against the reference client as roughly +1.3 ms of stretch at
    // each interval seam (work item #27).
    if (isConnected) {
      IntervalClock::splitAtIntervalStarts(clockEvents, ns, captureSegments);
      for (const auto &seg : captureSegments) {
        if (seg.count > 0)
          captureInputRange(seg.start, seg.count);
        if (seg.closesInterval) {
          fireCaptureLambdas();
          ninjamClient.swapIntervalBuffers();
          ninjamClient.diagSwaps.fetch_add(1);
          ninjamClient.intervalBeginSignal.store(false);
        }
      }
    }
  }

  // 7. Remote audio mix
  if (ninjamClient.isConnected())
    ninjamClient.getDecodedAudio(buffer);

  ninjamClient.setSaveTx(saveTxEnabled);
  ninjamClient.setSaveRx(saveRxEnabled);

}

bool AntiphonAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *AntiphonAudioProcessor::createEditor() {
  return new AntiphonEditor(*this);
}

// XOR-based obfuscation so passwords aren't plain text in DAW project state.
static const uint8_t kObfKey[] = {
    0x4e, 0x6a, 0x6d, 0x50, 0x77, 0x64, 0x21, 0x38,
    0x2a, 0x5e, 0x71, 0x33, 0x2f, 0x56, 0x9c, 0xb1
};

static juce::String obfuscate(const juce::String &s) {
    auto utf8 = s.toRawUTF8();
    int len = (int)s.getNumBytesAsUTF8();
    juce::MemoryBlock buf(len);
    for (int i = 0; i < len; ++i)
        static_cast<uint8_t *>(buf.getData())[i] =
            static_cast<uint8_t>(utf8[i]) ^ kObfKey[i % sizeof(kObfKey)];
    return juce::Base64::toBase64(buf.getData(), buf.getSize());
}

static juce::String deobfuscate(const juce::String &b64) {
    juce::MemoryOutputStream out;
    if (!juce::Base64::convertFromBase64(out, b64))
        return {};
    auto *data = static_cast<const uint8_t *>(out.getData());
    int len = (int)out.getDataSize();
    juce::HeapBlock<char> buf(len + 1);
    for (int i = 0; i < len; ++i)
        buf[i] = static_cast<char>(data[i] ^ kObfKey[i % sizeof(kObfKey)]);
    buf[len] = 0;
    return juce::String::fromUTF8(buf, len);
}

void AntiphonAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
    juce::XmlElement xml("NinjamState");
    xml.setAttribute("metronome",     metronomeEnabled.load());
    xml.setAttribute("metronomeVol",  (double)metronomeVolume.load());
    xml.setAttribute("chatVisible",   chatVisible.load());
    xml.setAttribute("lastHost",      lastHost);
    xml.setAttribute("lastPort",      lastPort);
    xml.setAttribute("lastUsername",  lastUsername);
    xml.setAttribute("lastPassword",  obfuscate(lastPassword));
    xml.setAttribute("lastAnonymous", lastAnonymous);

    {
      juce::ScopedLock sl(localChannelMutex);
      for (int i = 0; i < (int)localChannels.size(); ++i) {
          auto &lc = *localChannels[i];
          auto *ch = xml.createNewChildElement("LocalChannel");
          ch->setAttribute("idx",      i);
          ch->setAttribute("name",     lc.name);
          ch->setAttribute("mono",     lc.isMono.load());
          ch->setAttribute("volume",   (double)lc.volume.load());
          ch->setAttribute("pan",      (double)lc.pan.load());
          ch->setAttribute("muted",    lc.muted.load());
          ch->setAttribute("solo",     lc.monitorSolo.load());
          ch->setAttribute("xmit",     lc.xmitEnabled.load());
          ch->setAttribute("inputBus", lc.inputBusIndex.load());
      }
    }
    for (auto &[key, busIdx] : savedRemoteRoutings) {
        auto *rr = xml.createNewChildElement("RemoteRouting");
        rr->setAttribute("username", key.first);
        rr->setAttribute("ch",       key.second);
        rr->setAttribute("outputBus", busIdx);
    }
    copyXmlToBinary(xml, destData);
}

void AntiphonAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("NinjamState")) return;

    chatVisible.store(xml->getBoolAttribute("chatVisible", true));
    metronomeVolume.store((float)xml->getDoubleAttribute("metronomeVol", 1.0));
    metronomeEnabled.store(
        xml->getBoolAttribute("metronome", isStandaloneApp()));
    lastHost      = xml->getStringAttribute("lastHost",      "ninbot.com");
    lastPort      = xml->getIntAttribute   ("lastPort",      2049);
    lastUsername  = xml->getStringAttribute("lastUsername",  "");
    lastPassword  = deobfuscate(xml->getStringAttribute("lastPassword", ""));
    lastAnonymous = xml->getBoolAttribute  ("lastAnonymous", true);

    savedRemoteRoutings.clear();
    for (auto *child : xml->getChildIterator()) {
        if (child->hasTagName("RemoteRouting")) {
            juce::String uname = child->getStringAttribute("username");
            int chIdx = child->getIntAttribute("ch", 0);
            int bus   = child->getIntAttribute("outputBus", 0);
            savedRemoteRoutings[{uname, chIdx}] = bus;
        }
    }

    // Restore per-channel settings; create channels if needed
    juce::ScopedLock sl(localChannelMutex);
    for (auto *ch : xml->getChildIterator()) {
        if (!ch->hasTagName("LocalChannel")) continue;
        int idx = ch->getIntAttribute("idx", 0);
        while (idx >= (int)localChannels.size())
            localChannels.push_back(std::make_shared<LocalChannel>());
        auto &lc = *localChannels[idx];
        lc.name = ch->getStringAttribute("name", "Instrument");
        lc.isMono.store(ch->getBoolAttribute("mono", false));
        lc.volume.store((float)ch->getDoubleAttribute("volume", 1.0));
        lc.pan.store((float)ch->getDoubleAttribute("pan", 0.0));
        lc.muted.store(ch->getBoolAttribute("muted", false));
        lc.monitorSolo.store(ch->getBoolAttribute("solo", false));
        lc.xmitEnabled.store(ch->getBoolAttribute("xmit", true));
        lc.inputBusIndex.store(ch->getIntAttribute("inputBus", 0));
    }
}

void AntiphonAudioProcessor::sendChannelInfoToServer() {
  juce::StringArray names;
  {
    juce::ScopedLock sl(localChannelMutex);
    for (const auto &ch : localChannels)
      names.add(ch->name);
  }
  ninjamClient.updateChannelInfo(names);
}

void AntiphonAudioProcessor::onConnected() {
  connectionStatus = "Connected";
  hasConnectedSinceLastAttempt = true;
  lastConnectFailed.store(false);
  sendChannelInfoToServer();
}

void AntiphonAudioProcessor::onDisconnected(const juce::String &error) {
  if (!hasConnectedSinceLastAttempt)
    lastConnectFailed.store(true);
  hasConnectedSinceLastAttempt = false;
  connectionStatus = error.isEmpty() ? "Disconnected" : "Disconnected: " + error;
  phaseResetPending.store(true);
}

void AntiphonAudioProcessor::onServerConfig(int bpm, int bpi) {
  internalBpm.store(bpm);
  internalBpi.store(bpi);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new AntiphonAudioProcessor();
}
