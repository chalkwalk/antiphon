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
                                         int samplesPerBlock) {}

void NinjamAudioProcessor::releaseResources() {}

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
  if (auto *playHead = getPlayHead()) {
    juce::Optional<juce::AudioPlayHead::PositionInfo> info =
        playHead->getPosition();
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
      if (fractionalBeat < 0.05) // First 5% of a beat is a click
      {
        // Alternate click frequency for downbeat (interval boundary)
        float freq = (std::floor(internalPhaseBeats) == 0.0) ? 880.0f : 440.0f;
        float posInBeat =
            (float)fractionalBeat * 20.0f; // Scale to 0..1 over the 5% window
        float envelope = 1.0f - posInBeat;
        float clickSample =
            std::sin(posInBeat * juce::MathConstants<float>::twoPi * freq /
                     (float)internalBpm) *
            envelope * 0.1f;

        for (int channel = 0; channel < std::min(2, totalNumOutputChannels);
             channel++)
          buffer.addSample(channel, sample, clickSample);
      }
    }
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
