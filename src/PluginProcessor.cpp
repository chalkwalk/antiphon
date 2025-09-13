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
  if (!addBus(true)) return -1;
  updateHostDisplay();
  return getBusCount(true) - 1;
}

void NinjamAudioProcessor::removeLastLocalChannel() {
  if (getBusCount(true) <= 1) return;
  removeBus(true);
  updateHostDisplay();
}

void NinjamAudioProcessor::prepareToPlay(double sampleRate, int) {
  int numBuses = getBusCount(true);
  juce::ScopedLock sl(localChannelMutex);
  while ((int)localChannels.size() < numBuses)
    localChannels.push_back(std::make_shared<LocalChannel>());
  while ((int)localChannels.size() > numBuses) {
    localChannels.back()->isValid.store(false);
    localChannels.pop_back();
  }
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
  if (layouts.outputBuses.isEmpty() ||
      layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
    return false;
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

  // 1. Host Sync
  if (auto *ph = getPlayHead()) {
    auto info = ph->getPosition();
    if (info.hasValue()) {
      hostIsPlaying = info->getIsPlaying();
      if (info->getBpm().hasValue()) hostBpm = *info->getBpm();
      if (info->getPpqPosition().hasValue()) hostPpqPosition = *info->getPpqPosition();
    }
  }

  // 2. Clear any output channels that don't have corresponding input
  {
    int totalIn = getTotalNumInputChannels();
    for (int i = totalIn; i < totalNumOutputChannels; ++i)
      buffer.clear(i, 0, buffer.getNumSamples());
  }

  // 2b. Per-channel peaks (raw input, before remote audio is mixed in)
  {
    juce::ScopedLock sl(localChannelMutex);
    int ns = buffer.getNumSamples();
    for (int ci = 0; ci < (int)localChannels.size(); ++ci) {
      auto &lc = *localChannels[ci];
      auto *bus = getBus(true, ci);
      if (!bus) continue;
      int offset = bus->getChannelIndexInProcessBlockBuffer(0);
      int busCh = bus->getNumberOfChannels();

      float pL = 0.0f, pR = 0.0f;
      if (offset < buffer.getNumChannels()) {
        const float *p = buffer.getReadPointer(offset);
        for (int s = 0; s < ns; ++s) pL = std::max(pL, std::abs(p[s]));
      }
      if (offset + 1 < buffer.getNumChannels() && busCh > 1) {
        const float *p = buffer.getReadPointer(offset + 1);
        for (int s = 0; s < ns; ++s) pR = std::max(pR, std::abs(p[s]));
      } else {
        pR = pL;
      }
      float vol = lc.volume.load(), pan = lc.pan.load();
      bool muted = lc.muted.load();
      float lG = muted ? 0.0f : vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
      float rG = muted ? 0.0f : vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
      lc.peakL.store(pL * lG);
      lc.peakR.store(pR * rG);
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

  // 3. Server interval sync
  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    if (intervalSyncCooldown > 0) {
      intervalSyncCooldown = std::max(0, intervalSyncCooldown - buffer.getNumSamples());
      // Always clear in this block: BEGINs that arrived during cooldown (or in
      // the same block that crosses to 0) must not trigger a mid-interval swap.
      ninjamClient.intervalBeginSignal.store(false);
    }

    bool swappedBySignal = false;
    if (ninjamClient.isConnected() && intervalSyncCooldown == 0 &&
        ninjamClient.intervalBeginSignal.exchange(false)) {
      fireCaptureLambdas();
      ninjamClient.swapIntervalBuffers();
      internalPhaseBeats = 0.0;
      lastTimestampedBeat = -1;
      intervalFlashIntensity.store(1.0f);
      int bpm = internalBpm.load(), bpi = internalBpi.load();
      intervalSyncCooldown = (bpm > 0 && bpi > 0)
          ? (int)(sampleRate * 60.0 / bpm * bpi / 2) : 48000;
      swappedBySignal = true;
    }

    // 4. Metronome click + fallback interval boundary detection
    int bpm = internalBpm.load();
    int bpi = internalBpi.load();
    double beatsPerSample = (bpm / 60.0) / sampleRate;
    bool needsFallbackSwap = false;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
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
              envelope * amp;
          for (int ch = 0; ch < std::min(2, totalNumOutputChannels); ++ch)
            buffer.addSample(ch, sample, clickSample);
        }
      }
    }

    // Fallback swap fires after the per-sample loop (never inside it)
    if (needsFallbackSwap) {
      fireCaptureLambdas();
      ninjamClient.swapIntervalBuffers();
      ninjamClient.intervalBeginSignal.store(false);
      int bpm2 = internalBpm.load(), bpi2 = internalBpi.load();
      intervalSyncCooldown = (bpm2 > 0 && bpi2 > 0)
          ? (int)(sampleRate * 60.0 / bpm2 * bpi2 / 2) : 48000;
    }
  }

  if (ninjamClient.isConnected())
    ninjamClient.getDecodedAudio(buffer);

  ninjamClient.setSaveTx(saveTxEnabled);
  ninjamClient.setSaveRx(saveRxEnabled);

  // 5. Capture raw input into per-channel ring buffers
  {
    juce::ScopedLock sl(localChannelMutex);
    int ns = buffer.getNumSamples();
    for (int ci = 0; ci < (int)localChannels.size(); ++ci) {
      auto &lc = *localChannels[ci];
      auto *bus = getBus(true, ci);
      if (!bus) continue;
      int offset = bus->getChannelIndexInProcessBlockBuffer(0);
      int busCh = bus->getNumberOfChannels();

      float vol = lc.volume.load(), pan = lc.pan.load();
      bool muted = lc.muted.load();
      float lG = muted ? 0.0f : vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
      float rG = muted ? 0.0f : vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
      bool mono = lc.isMono.load();

      if (lc.fifo.getFreeSpace() >= ns) {
        int s1, n1, s2, n2;
        lc.fifo.prepareToWrite(ns, s1, n1, s2, n2);
        for (int ch = 0; ch < 2; ++ch) {
          float gain = (ch == 0) ? lG : rG;
          int srcOff = (mono || ch >= busCh) ? offset : offset + ch;
          if (srcOff < buffer.getNumChannels()) {
            if (n1 > 0) {
              lc.ring.copyFrom(ch, s1, buffer, srcOff, 0, n1);
              lc.ring.applyGain(ch, s1, n1, gain);
            }
            if (n2 > 0) {
              lc.ring.copyFrom(ch, s2, buffer, srcOff, n1, n2);
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
    xml.setAttribute("lastHost",      lastHost);
    xml.setAttribute("lastPort",      lastPort);
    xml.setAttribute("lastUsername",  lastUsername);
    xml.setAttribute("lastPassword",  obfuscate(lastPassword));
    xml.setAttribute("lastAnonymous", lastAnonymous);

    juce::ScopedLock sl(localChannelMutex);
    for (int i = 0; i < (int)localChannels.size(); ++i) {
        auto &lc = *localChannels[i];
        auto *ch = xml.createNewChildElement("LocalChannel");
        ch->setAttribute("idx",    i);
        ch->setAttribute("name",   lc.name);
        ch->setAttribute("mono",   lc.isMono.load());
        ch->setAttribute("volume", (double)lc.volume.load());
        ch->setAttribute("pan",    (double)lc.pan.load());
        ch->setAttribute("muted",  lc.muted.load());
        ch->setAttribute("xmit",   lc.xmitEnabled.load());
    }
    copyXmlToBinary(xml, destData);
}

void NinjamAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("NinjamState")) return;

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

    // Restore per-channel settings (fall back gracefully if no LocalChannel children)
    juce::ScopedLock sl(localChannelMutex);
    for (auto *ch : xml->getChildIterator()) {
        if (!ch->hasTagName("LocalChannel")) continue;
        int idx = ch->getIntAttribute("idx", 0);
        if (idx < 0 || idx >= (int)localChannels.size()) continue;
        auto &lc = *localChannels[idx];
        lc.name = ch->getStringAttribute("name", "Local Instrument");
        lc.isMono.store(ch->getBoolAttribute("mono", false));
        lc.volume.store((float)ch->getDoubleAttribute("volume", 1.0));
        lc.pan.store((float)ch->getDoubleAttribute("pan", 0.0));
        lc.muted.store(ch->getBoolAttribute("muted", false));
        lc.xmitEnabled.store(ch->getBoolAttribute("xmit", true));
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
