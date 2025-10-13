#include "PluginProcessor.h"
#include "PluginEditor.h"

NinjamAudioProcessor::NinjamAudioProcessor()
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
#if !JucePlugin_Build_Standalone
  metronomeEnabled = false;
#endif
}

NinjamAudioProcessor::~NinjamAudioProcessor() {
  ninjamClient.removeListener(this);
}

const juce::String NinjamAudioProcessor::getName() const {
  return JucePlugin_Name;
}

bool NinjamAudioProcessor::acceptsMidi() const { return false; }
bool NinjamAudioProcessor::producesMidi() const { return false; }
bool NinjamAudioProcessor::isMidiEffect() const { return false; }
double NinjamAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int NinjamAudioProcessor::getNumPrograms() { return 1; }
int NinjamAudioProcessor::getCurrentProgram() { return 0; }
void NinjamAudioProcessor::setCurrentProgram(int) {}
const juce::String NinjamAudioProcessor::getProgramName(int) { return {}; }
void NinjamAudioProcessor::changeProgramName(int, const juce::String &) {}

int NinjamAudioProcessor::addLocalChannel() {
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

void NinjamAudioProcessor::removeLastLocalChannel() {
  juce::ScopedLock sl(localChannelMutex);
  if (localChannels.size() <= 1) return;
  localChannels.back()->isValid.store(false);
  localChannels.pop_back();
}

int NinjamAudioProcessor::addInputBus() {
  if (!addBus(true)) return -1;
  updateHostDisplay();
  return getBusCount(true) - 1;
}

void NinjamAudioProcessor::removeLastInputBus() {
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

int NinjamAudioProcessor::addOutputBus() {
  if (!addBus(false)) return -1;
  updateHostDisplay();
  return getBusCount(false) - 1;
}

void NinjamAudioProcessor::removeLastOutputBus() {
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

void NinjamAudioProcessor::setRemoteUserOutputBus(const juce::String &username,
                                                  int channelIndex, int busIdx) {
  ninjamClient.setRemoteUserOutputBus(username, channelIndex, busIdx);
  savedRemoteRoutings[{username, channelIndex}] = busIdx;
}

void NinjamAudioProcessor::onUserInfoChange() {
  auto users = ninjamClient.getRemoteUsers();
  for (auto &[uname, user] : users) {
    for (auto &[chIdx, ch] : user.channels) {
      auto it = savedRemoteRoutings.find({uname, chIdx});
      if (it != savedRemoteRoutings.end())
        ninjamClient.setRemoteUserOutputBus(uname, chIdx, it->second);
    }
  }
}

void NinjamAudioProcessor::prepareToPlay(double sampleRate, int) {
  juce::ScopedLock sl(localChannelMutex);
  int ringSize = (int)sampleRate * 30;
  for (auto &lc : localChannels) {
    lc->ring.setSize(2, ringSize);
    lc->ring.clear();
    lc->fifo.setTotalSize(ringSize);
  }
  ninjamClient.setSampleRate(sampleRate);
}

void NinjamAudioProcessor::releaseResources() {
  juce::ScopedLock sl(localChannelMutex);
  for (auto &lc : localChannels) {
    lc->fifo.reset();
    lc->ring.setSize(0, 0);
  }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NinjamAudioProcessor::isBusesLayoutSupported(
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

void NinjamAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &) {
  juce::ScopedNoDenormals noDenormals;
  auto totalNumOutputChannels = getTotalNumOutputChannels();
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

  // 2. Snapshot bus 0 input -- output channels 0/1 alias bus 0 in JUCE, so we
  //    must capture it before clearing/overwriting the output.
  juce::AudioBuffer<float> bus0Snapshot;
  {
    auto *bus0 = getBus(true, 0);
    if (bus0) {
      int off = bus0->getChannelIndexInProcessBlockBuffer(0);
      int nch = bus0->getNumberOfChannels();
      bus0Snapshot.setSize(nch, ns, false, false, true);
      for (int ch = 0; ch < nch; ++ch)
        bus0Snapshot.copyFrom(ch, 0, buffer, off + ch, 0, ns);
    }
  }

  // 3. Clear output channels 0/1 (and any trailing unused outputs).
  for (int i = 0; i < totalNumOutputChannels; ++i)
    buffer.clear(i, 0, ns);

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

      float vol = lc.volume.load(), pan = lc.pan.load();
      bool muted = lc.muted.load();
      bool solo  = lc.monitorSolo.load();
      float monitorFactor = (muted ? 0.0f : 1.0f) *
                            (anyMonitorSolo && !solo ? 0.0f : 1.0f);
      float lG = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan) * monitorFactor;
      float rG = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan) * monitorFactor;
      bool mono = lc.isMono.load();

      // Compute peaks on raw input (reflects what you're sending), display
      // scaled by monitor gain so the VU shows what you hear.
      {
        const float *rawL = (busIdx == 0 && bus0Snapshot.getNumChannels() > 0)
            ? bus0Snapshot.getReadPointer(0)
            : (offset     < buffer.getNumChannels() ? buffer.getReadPointer(offset)     : nullptr);
        const float *rawR = (busIdx == 0 && bus0Snapshot.getNumChannels() > 1)
            ? bus0Snapshot.getReadPointer(1)
            : (offset + 1 < buffer.getNumChannels() && busCh > 1 ? buffer.getReadPointer(offset + 1) : rawL);
        float pL = 0.0f, pR = 0.0f;
        if (rawL) for (int s = 0; s < ns; ++s) pL = std::max(pL, std::abs(rawL[s]));
        if (rawR) for (int s = 0; s < ns; ++s) pR = std::max(pR, std::abs(rawR[s]));
        lc.peakL.store(pL * lG);
        lc.peakR.store(pR * rG);
      }

      if (totalNumOutputChannels < 2) continue;

      // Source: bus 0 from snapshot, buses 1+ from live buffer (not aliased).
      const juce::AudioBuffer<float> &src = (busIdx == 0) ? bus0Snapshot : buffer;
      int srcOff0 = (busIdx == 0) ? 0 : offset;
      int srcOff1 = (busIdx == 0) ? (bus0Snapshot.getNumChannels() > 1 ? 1 : 0)
                                  : (busCh > 1 ? offset + 1 : offset);

      // mono flag: both output channels read from the first source channel only.
      // This matches the transmit capture behavior.
      int readL = srcOff0;
      int readR = (mono || busCh == 1) ? srcOff0 : srcOff1;
      if (readL < src.getNumChannels())
        buffer.addFrom(0, 0, src, readL, 0, ns, lG);
      if (readR < src.getNumChannels())
        buffer.addFrom(1, 0, src, readR, 0, ns, rG);
    }
  }

  // Helper: fire per-channel callAsync at interval boundary
  auto fireCaptureLambdas = [this]() {
    juce::ScopedLock sl(localChannelMutex);
    for (auto &lcPtr : localChannels) {
      if (!lcPtr->xmitEnabled.load()) { lcPtr->fifo.reset(); continue; }
      int length = lcPtr->fifo.getNumReady();
      if (ninjamClient.isConnected() && length > 0) {
        juce::String name = lcPtr->name;
        bool mono = lcPtr->isMono.load();
        auto capturedPtr = lcPtr;
        juce::MessageManager::callAsync([this, capturedPtr, length, name, mono]() {
          if (!capturedPtr->isValid.load()) return;
          juce::AudioBuffer<float> buf(2, length);
          int s1, n1, s2, n2;
          capturedPtr->fifo.prepareToRead(length, s1, n1, s2, n2);
          for (int ch = 0; ch < 2; ++ch) {
            if (n1 > 0) buf.copyFrom(ch, 0,  capturedPtr->ring, ch, s1, n1);
            if (n2 > 0) buf.copyFrom(ch, n1, capturedPtr->ring, ch, s2, n2);
          }
          capturedPtr->fifo.finishedRead(n1 + n2);
          ninjamClient.processCapturedAudio(buf, length, name, mono);
        });
      } else {
        lcPtr->fifo.reset();
      }
    }
  };

  // 5. Local interval metronome (sole authority for swap timing).
  // Per njclient's design, DOWNLOAD_INTERVAL_BEGIN is only used to queue
  // incoming audio, never to drive the local clock. Network jitter would
  // otherwise yank internalPhaseBeats mid-interval and discard seconds of
  // un-played audio.
  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    // Drain any pending signal so the diagnostic counter stays at 0.
    ninjamClient.intervalBeginSignal.store(false);

    bool swappedBySignal = false;

    // 6. Metronome click + interval boundary detection
    int bpm = internalBpm.load();
    int bpi = internalBpi.load();
    double beatsPerSample = (bpm / 60.0) / sampleRate;
    bool needsFallbackSwap = false;

    for (int sample = 0; sample < ns; ++sample) {
      internalPhaseBeats += beatsPerSample;
      if (internalPhaseBeats >= bpi)
        internalPhaseBeats -= bpi;

      double fractionalBeat = internalPhaseBeats - std::floor(internalPhaseBeats);

      if (internalPhaseBeats < beatsPerSample) {
        if (!swappedBySignal) {
          needsFallbackSwap = true;
          swappedBySignal = true;
        }
        intervalFlashIntensity.store(1.0f);
      }

      if (fractionalBeat < 0.05) {
        int currentBeat = (int)std::floor(internalPhaseBeats);
        if (currentBeat != lastTimestampedBeat) {
          lastTimestampedBeat = currentBeat;
          lastBeatCrossedIndex.store(currentBeat);
          if (currentBeat != 0)
            beatFlashIntensity.store(1.0f);
        }

        if (metronomeEnabled) {
          float freq, amp;
          if (currentBeat == 0) {
            freq = 880.0f; amp = 0.10f;
          } else if (currentBeat % 4 == 0) {
            freq = 660.0f; amp = 0.06f;
          } else {
            freq = 440.0f; amp = 0.03f;
          }
          float posInBeat = (float)fractionalBeat * 20.0f;
          float envelope = 1.0f - posInBeat;
          float clickSample =
              std::sin(posInBeat * juce::MathConstants<float>::twoPi * freq /
                       (float)bpm) *
              envelope * amp * metronomeVolume.load();
          for (int ch = 0; ch < std::min(2, totalNumOutputChannels); ++ch)
            buffer.addSample(ch, sample, clickSample);
        }
      }
    }

    // Fallback swap fires after the per-sample loop (never inside it)
    if (needsFallbackSwap) {
      juce::Logger::writeToLog(
          juce::String::formatted("[diag] swap FALLBACK phase=%.3f bpi=%d",
                                  internalPhaseBeats, internalBpi.load()));
      fireCaptureLambdas();
      ninjamClient.swapIntervalBuffers();
      ninjamClient.diagSwapsByFallback.fetch_add(1);
      ninjamClient.intervalBeginSignal.store(false);
      int bpm2 = internalBpm.load(), bpi2 = internalBpi.load();
      intervalSyncCooldown = (bpm2 > 0 && bpi2 > 0)
          ? (int)(sampleRate * 60.0 / bpm2 * bpi2 / 2) : 48000;
    }
  }

  // 7. Remote audio mix
  if (ninjamClient.isConnected())
    ninjamClient.getDecodedAudio(buffer);

  ninjamClient.setSaveTx(saveTxEnabled);
  ninjamClient.setSaveRx(saveRxEnabled);

  // 8. Capture raw input into per-channel ring buffers.
  //    Gain is vol*pan only (no mute factor -- mute is monitor-only).
  //    Bus 0 is read from bus0Snapshot because output 0/1 was overwritten above.
  {
    juce::ScopedLock sl(localChannelMutex);
    int numInBusesCap = getBusCount(true);
    for (int ci = 0; ci < (int)localChannels.size(); ++ci) {
      auto &lc = *localChannels[ci];
      int busIdx = juce::jlimit(0, numInBusesCap - 1, lc.inputBusIndex.load());
      auto *bus = getBus(true, busIdx);
      if (!bus) continue;
      int offset = bus->getChannelIndexInProcessBlockBuffer(0);
      int busCh = bus->getNumberOfChannels();

      float vol = lc.volume.load(), pan = lc.pan.load();
      float lG = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
      float rG = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
      bool mono = lc.isMono.load();

      const juce::AudioBuffer<float> &src = (busIdx == 0) ? bus0Snapshot : buffer;
      int srcBase = (busIdx == 0) ? 0 : offset;
      int srcCh   = (busIdx == 0) ? bus0Snapshot.getNumChannels() : busCh;

      if (lc.fifo.getFreeSpace() >= ns) {
        int s1, n1, s2, n2;
        lc.fifo.prepareToWrite(ns, s1, n1, s2, n2);
        for (int ch = 0; ch < 2; ++ch) {
          float gain = (ch == 0) ? lG : rG;
          int srcOff = (mono || ch >= srcCh) ? srcBase : srcBase + ch;
          if (srcOff < src.getNumChannels()) {
            if (n1 > 0) {
              lc.ring.copyFrom(ch, s1, src, srcOff, 0, n1);
              lc.ring.applyGain(ch, s1, n1, gain);
            }
            if (n2 > 0) {
              lc.ring.copyFrom(ch, s2, src, srcOff, n1, n2);
              lc.ring.applyGain(ch, s2, n2, gain);
            }
          }
        }
        lc.fifo.finishedWrite(n1 + n2);
      }
    }
  }
}

bool NinjamAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *NinjamAudioProcessor::createEditor() {
  return new NinjamAudioProcessorEditor(*this);
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

void NinjamAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {
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

void NinjamAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("NinjamState")) return;

    chatVisible.store(xml->getBoolAttribute("chatVisible", true));
    metronomeVolume.store((float)xml->getDoubleAttribute("metronomeVol", 1.0));
    metronomeEnabled.store(xml->getBoolAttribute("metronome",
#if JucePlugin_Build_Standalone
        true
#else
        false
#endif
    ));
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

void NinjamAudioProcessor::sendChannelInfoToServer() {
  juce::StringArray names;
  {
    juce::ScopedLock sl(localChannelMutex);
    for (const auto &ch : localChannels)
      names.add(ch->name);
  }
  ninjamClient.updateChannelInfo(names);
}

void NinjamAudioProcessor::onConnected() {
  connectionStatus = "Connected";
  sendChannelInfoToServer();
}

void NinjamAudioProcessor::onDisconnected(const juce::String &error) {
  connectionStatus = "Disconnected: " + error;
}

void NinjamAudioProcessor::onServerConfig(int bpm, int bpi) {
  internalBpm.store(bpm);
  internalBpi.store(bpi);
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NinjamAudioProcessor();
}
