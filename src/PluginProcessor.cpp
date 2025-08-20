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
  // We need to estimate the max buffer size we might need for one interval.
  // e.g. at 20 BPM, BPI 64, that's max ~192 seconds of audio.
  // 192s * 48000hz = ~9,216,000 samples. We'll allocate 10 million to be safe
  // and handle resizing if needed, but a robust ring-buffer is better.
  // For now, let's allocate a static heavy buffer to fit long blocks.
  captureBuffer.setSize(2, 48000 * 60 * 2); // 2 mins max
  captureWritePosition = 0;
  ninjamClient.setSampleRate(sampleRate);
}

void NinjamAudioProcessor::releaseResources() { captureBuffer.setSize(0, 0); }

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

  // 3. Metronome (simple click)
  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    double beatsPerSample = (internalBpm / 60.0) / sampleRate;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
      // Advance phase
      internalPhaseBeats += beatsPerSample;
      if (internalPhaseBeats >= internalBpi)
        internalPhaseBeats -= internalBpi;

      // Generate click if we just crossed a beat boundary (roughly)
      double fractionalBeat =
          internalPhaseBeats - std::floor(internalPhaseBeats);

      // If we just rolled over the interval
      if (internalPhaseBeats < beatsPerSample) {
        // Interval finished! Signal to send the block.
        // We'll call the `NinjamClient` asynchronously via MessageManager,
        // (this avoids allocations in processBlock). A more robust way in
        // production is to use a lock-free juce::AbstractFifo, but `callAsync`
        // is okay for now.
        if (ninjamClient.isConnected() && captureWritePosition > 0) {
          juce::AudioBuffer<float> tempBuf;
          tempBuf.makeCopyOf(captureBuffer);
          int length = captureWritePosition;

          juce::MessageManager::callAsync([this, tempBuf, length]() mutable {
            ninjamClient.processCapturedAudio(tempBuf, length);
          });
        }
        captureWritePosition = 0; // reset local pointer

        // Swap buffers for playback
        ninjamClient.swapIntervalBuffers();

        // Interval flash fired directly here, independent of beat detection
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
                       (float)internalBpm) *
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

  // 4. Capture audio to buffer
  int numSamples = buffer.getNumSamples();
  if (captureWritePosition + numSamples < captureBuffer.getNumSamples()) {
    float lGain =
        localTxMute
            ? 0.0f
            : localTxVolume * (localTxPan <= 0.0f ? 1.0f : 1.0f - localTxPan);
    float rGain =
        localTxMute
            ? 0.0f
            : localTxVolume * (localTxPan >= 0.0f ? 1.0f : 1.0f + localTxPan);

    for (int channel = 0; channel < std::min(2, totalNumInputChannels);
         channel++) {
      float gain = (channel == 0) ? lGain : rGain;
      captureBuffer.copyFrom(channel, captureWritePosition, buffer, channel, 0,
                             numSamples);
      captureBuffer.applyGain(channel, captureWritePosition, numSamples, gain);
    }
    // if mono input, copy to right array as well
    if (totalNumInputChannels == 1 && captureBuffer.getNumChannels() == 2) {
      captureBuffer.copyFrom(1, captureWritePosition, buffer, 0, 0, numSamples);
      captureBuffer.applyGain(1, captureWritePosition, numSamples, rGain);
    }
    captureWritePosition += numSamples;
  }
}

bool NinjamAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor *NinjamAudioProcessor::createEditor() {
  return new NinjamAudioProcessorEditor(*this);
}

void NinjamAudioProcessor::getStateInformation(juce::MemoryBlock &destData) {}

void NinjamAudioProcessor::setStateInformation(const void *data,
                                               int sizeInBytes) {}

void NinjamAudioProcessor::onConnected() { connectionStatus = "Connected"; }

void NinjamAudioProcessor::onDisconnected(const juce::String &error) {
  connectionStatus = "Disconnected: " + error;
}

void NinjamAudioProcessor::onServerConfig(int bpm, int bpi) {
  internalBpm = bpm;
  internalBpi = bpi;
  // We could potentially reset phase here if it's a hard reset, but usually we
  // just update
}

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new NinjamAudioProcessor();
}
