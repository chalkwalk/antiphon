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

void NinjamAudioProcessor::setCurrentProgram(int index) {}

const juce::String NinjamAudioProcessor::getProgramName(int index) {
  return {};
}

void NinjamAudioProcessor::changeProgramName(int index,
                                             const juce::String &newName) {}

void NinjamAudioProcessor::prepareToPlay(double sampleRate,
                                         int samplesPerBlock) {
  int ringSize = (int)sampleRate * 30; // 30 s covers any normal BPM/BPI combo
  captureRingBuffer.setSize(2, ringSize);
  captureRingBuffer.clear();
  captureFifo.setTotalSize(ringSize);
  ninjamClient.setSampleRate(sampleRate);
}

void NinjamAudioProcessor::releaseResources() {
  captureFifo.reset();
  captureRingBuffer.setSize(0, 0);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool NinjamAudioProcessor::isBusesLayoutSupported(
    const BusesLayout &layouts) const {
  // Support up to 8 channels input / output, dynamic allocation.
  const int numInputChannels = layouts.getMainInputChannels();
  const int numOutputChannels = layouts.getMainOutputChannels();

  // We allow mono, stereo, up to 8 surround layouts, basically any layout with
  // <= 8 channels.
  if (numInputChannels > 8 || numOutputChannels > 8)
    return false;

  // For now we just require that if they give us inputs, they give us outputs.
  if (numInputChannels > 0 && numOutputChannels == 0)
    return false;

  return true;
}
#endif

void NinjamAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                        juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;
  auto totalNumInputChannels = getTotalNumInputChannels();
  auto totalNumOutputChannels = getTotalNumOutputChannels();

  // 1. Host Sync
  if (auto *currentPlayHead = getPlayHead()) {
    juce::Optional<juce::AudioPlayHead::PositionInfo> info =
        currentPlayHead->getPosition();
    if (info.hasValue()) {
      hostIsPlaying = info->getIsPlaying();
      if (info->getBpm().hasValue())
        hostBpm = *info->getBpm();
      if (info->getPpqPosition().hasValue())
        hostPpqPosition = *info->getPpqPosition();
    }
  }

  // 2. Audio Pass-through
  for (int i = 0; i < totalNumOutputChannels; ++i) {
    if (i < totalNumInputChannels) {
      // Only need to copy if the buffers are distinct (which they might be if
      // we dynamically create output buses without matching inputs) But usually
      // in JUCE processBlock happens in place if input == output. We'll leave
      // it in place, but ensure extra outputs are cleared.
    } else {
      buffer.clear(i, 0, buffer.getNumSamples());
    }
  }

  // 2b. Local TX peak (raw input before remote audio or metronome are added)
  {
    int ns = buffer.getNumSamples();
    float pL = 0.0f, pR = 0.0f;
    if (totalNumInputChannels > 0 && buffer.getNumChannels() > 0) {
      const float *ch0 = buffer.getReadPointer(0);
      for (int s = 0; s < ns; ++s) pL = std::max(pL, std::abs(ch0[s]));
    }
    if (totalNumInputChannels > 1 && buffer.getNumChannels() > 1) {
      const float *ch1 = buffer.getReadPointer(1);
      for (int s = 0; s < ns; ++s) pR = std::max(pR, std::abs(ch1[s]));
    } else {
      pR = pL;
    }
    float vol = localTxVolume.load();
    float pan = localTxPan.load();
    bool muted = localTxMute.load();
    float lG = muted ? 0.0f : vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    float rG = muted ? 0.0f : vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);
    localTxPeakL.store(pL * lG);
    localTxPeakR.store(pR * rG);
  }

  // 3. Server interval sync -- align phase and swap buffers when a
  //    DOWNLOAD_INTERVAL_BEGIN arrives, so our swap matches the server's
  //    actual interval boundary rather than our free-running internal phase.
  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    if (intervalSyncCooldown > 0) {
      intervalSyncCooldown = std::max(0, intervalSyncCooldown - buffer.getNumSamples());
      if (intervalSyncCooldown > 0)
        ninjamClient.intervalBeginSignal.store(false); // discard stale signals during cooldown
    }

    bool swappedBySignal = false;
    if (ninjamClient.isConnected() && intervalSyncCooldown == 0 &&
        ninjamClient.intervalBeginSignal.exchange(false)) {
      int length = captureFifo.getNumReady();
      if (length > 0) {
        juce::MessageManager::callAsync([this, length]() {
          juce::AudioBuffer<float> buf(captureRingBuffer.getNumChannels(), length);
          int s1, n1, s2, n2;
          captureFifo.prepareToRead(length, s1, n1, s2, n2);
          for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
            if (n1 > 0) buf.copyFrom(ch, 0,  captureRingBuffer, ch, s1, n1);
            if (n2 > 0) buf.copyFrom(ch, n1, captureRingBuffer, ch, s2, n2);
          }
          captureFifo.finishedRead(n1 + n2);
          ninjamClient.processCapturedAudio(buf, length);
        });
      } else {
        captureFifo.reset();
      }
      ninjamClient.swapIntervalBuffers();
      internalPhaseBeats = 0.0;
      lastTimestampedBeat = -1;
      intervalFlashIntensity.store(1.0f);
      // Cooldown: ignore further signals for half an interval to prevent
      // double-swaps from multiple channels' DOWNLOAD_INTERVAL_BEGIN messages.
      int bpm = internalBpm.load(), bpi = internalBpi.load();
      intervalSyncCooldown = (bpm > 0 && bpi > 0)
          ? (int)(sampleRate * 60.0 / bpm * bpi / 2)
          : 48000;
      swappedBySignal = true;
    }

  // 4. Metronome (simple click) + fallback interval swap
    int bpm = internalBpm.load();
    int bpi = internalBpi.load();
    double beatsPerSample = (bpm / 60.0) / sampleRate;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
      // Advance phase
      internalPhaseBeats += beatsPerSample;
      if (internalPhaseBeats >= bpi)
        internalPhaseBeats -= bpi;

      // Generate click if we just crossed a beat boundary (roughly)
      double fractionalBeat =
          internalPhaseBeats - std::floor(internalPhaseBeats);

      // If we just rolled over the interval
      if (internalPhaseBeats < beatsPerSample) {
        if (!swappedBySignal) {
          // Fallback swap: server signal didn't fire this buffer (e.g. disconnected
          // or signal arrived in the previous buffer). Keep audio moving.
          int length = captureFifo.getNumReady();
          if (ninjamClient.isConnected() && length > 0) {
            juce::MessageManager::callAsync([this, length]() {
              juce::AudioBuffer<float> buf(captureRingBuffer.getNumChannels(), length);
              int s1, n1, s2, n2;
              captureFifo.prepareToRead(length, s1, n1, s2, n2);
              for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
                if (n1 > 0) buf.copyFrom(ch, 0,  captureRingBuffer, ch, s1, n1);
                if (n2 > 0) buf.copyFrom(ch, n1, captureRingBuffer, ch, s2, n2);
              }
              captureFifo.finishedRead(n1 + n2);
              ninjamClient.processCapturedAudio(buf, length);
            });
          } else {
            captureFifo.reset();
          }
          ninjamClient.swapIntervalBuffers();
          // Mirror the signal-triggered path: drain any pending server signal
          // and set a cooldown so a delayed DOWNLOAD_INTERVAL_BEGIN arriving in
          // the next block doesn't re-swap and clobber the just-populated front buffer.
          ninjamClient.intervalBeginSignal.store(false);
          intervalSyncCooldown = (bpm > 0 && bpi > 0)
              ? (int)(sampleRate * 60.0 / bpm * bpi / 2)
              : 48000;
          swappedBySignal = true;
        }
        intervalFlashIntensity.store(1.0f);
      }

      if (fractionalBeat < 0.05) // First 5% of a beat
      {
        int currentBeat = (int)std::floor(internalPhaseBeats);
        if (currentBeat != lastTimestampedBeat) {
          lastTimestampedBeat = currentBeat;
          lastBeatCrossedIndex.store(currentBeat);
          if (currentBeat != 0) // beat 0 uses intervalFlashIntensity instead
            beatFlashIntensity.store(1.0f);
        }

        if (metronomeEnabled) {
          float freq, amp;
          if (currentBeat == 0) {
            freq = 880.0f; amp = 0.10f; // interval boundary
          } else if (currentBeat % 4 == 0) {
            freq = 660.0f; amp = 0.06f; // bar boundary
          } else {
            freq = 440.0f; amp = 0.03f; // regular beat
          }
          float posInBeat = (float)fractionalBeat * 20.0f;
          float envelope = 1.0f - posInBeat;
          float clickSample =
              std::sin(posInBeat * juce::MathConstants<float>::twoPi * freq /
                       (float)bpm) *
              envelope * amp;

          for (int channel = 0; channel < std::min(2, totalNumOutputChannels);
               channel++)
            buffer.addSample(channel, sample, clickSample);
        }
      }
    }
  }

  if (ninjamClient.isConnected()) {
    ninjamClient.getDecodedAudio(buffer);
  }

  ninjamClient.setSaveTx(saveTxEnabled);
  ninjamClient.setSaveRx(saveRxEnabled);

  // 4. Capture audio into ring buffer
  int numSamples = buffer.getNumSamples();
  if (captureFifo.getFreeSpace() >= numSamples) {
    float lGain =
        localTxMute
            ? 0.0f
            : localTxVolume * (localTxPan <= 0.0f ? 1.0f : 1.0f - localTxPan);
    float rGain =
        localTxMute
            ? 0.0f
            : localTxVolume * (localTxPan >= 0.0f ? 1.0f : 1.0f + localTxPan);

    int s1, n1, s2, n2;
    captureFifo.prepareToWrite(numSamples, s1, n1, s2, n2);

    for (int ch = 0; ch < captureRingBuffer.getNumChannels(); ++ch) {
      float gain = (ch == 0) ? lGain : rGain;
      int srcCh = (ch == 1 && totalNumInputChannels == 1) ? 0 : ch;
      if (srcCh < buffer.getNumChannels()) {
        if (n1 > 0) {
          captureRingBuffer.copyFrom(ch, s1, buffer, srcCh, 0, n1);
          captureRingBuffer.applyGain(ch, s1, n1, gain);
        }
        if (n2 > 0) {
          captureRingBuffer.copyFrom(ch, s2, buffer, srcCh, n1, n2);
          captureRingBuffer.applyGain(ch, s2, n2, gain);
        }
      }
    }
    captureFifo.finishedWrite(n1 + n2);
  }
}

bool NinjamAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *NinjamAudioProcessor::createEditor() {
  return new NinjamAudioProcessorEditor(*this);
}

// XOR-based obfuscation so passwords aren't plain text in DAW project state.
// Not cryptographically secure -- just opaque on casual inspection.
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
    xml.setAttribute("localVolume",   (double)localTxVolume.load());
    xml.setAttribute("localPan",      (double)localTxPan.load());
    xml.setAttribute("localMute",     localTxMute.load());
    xml.setAttribute("metronome",     metronomeEnabled.load());
    xml.setAttribute("lastHost",      lastHost);
    xml.setAttribute("lastPort",      lastPort);
    xml.setAttribute("lastUsername",  lastUsername);
    xml.setAttribute("lastPassword",  obfuscate(lastPassword));
    xml.setAttribute("lastAnonymous", lastAnonymous);
    copyXmlToBinary(xml, destData);
}

void NinjamAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("NinjamState")) return;
    localTxVolume.store((float)xml->getDoubleAttribute("localVolume",  1.0));
    localTxPan.store   ((float)xml->getDoubleAttribute("localPan",     0.0));
    localTxMute.store  (xml->getBoolAttribute("localMute",    false));
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
}

void NinjamAudioProcessor::onConnected() { connectionStatus = "Connected"; }

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
