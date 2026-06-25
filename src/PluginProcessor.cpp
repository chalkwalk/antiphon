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
  // Every mutation of localChannels must be followed by this, or the audio
  // thread keeps walking the previous list. prepareToPlay would cover this one
  // in practice, but "it happens to be published later" is not a rule anyone
  // can follow.
  publishLocalChannels();
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

void AntiphonAudioProcessor::publishLocalChannels() {
  // Caller holds localChannelMutex. The pointers are written first and the
  // count released afterwards, so the audio thread -- which reads the count
  // with acquire and then indexes below it -- never sees a slot that has not
  // been filled in.
  const int n =
      juce::jmin((int)localChannels.size(), kMaxLocalChannels);
  for (int i = 0; i < n; ++i)
    audioChannels[(std::size_t)i] = localChannels[(std::size_t)i].get();
  audioChannelCount.store(n, std::memory_order_release);
}

int AntiphonAudioProcessor::addLocalChannel() {
  const int ringSize = (int)getSampleRate() * 30;

  // Built before the lock is taken. This allocation is the reason the audio
  // thread must not share this lock: it is megabytes, and it used to happen
  // with the lock held.
  std::shared_ptr<LocalChannel> ch;
  {
    juce::ScopedLock sl(localChannelMutex);
    if ((int)localChannels.size() >= kMaxLocalChannels)
      return -1;
    if (!spareLocalChannels.empty()) {
      ch = spareLocalChannels.back();
      spareLocalChannels.pop_back();
    }
  }

  if (ch == nullptr) {
    ch = std::make_shared<LocalChannel>();
    if (ringSize > 0) {
      ch->ring.setSize(2, ringSize);
      ch->fifo.setTotalSize(ringSize);
    }
  } else {
    ch->resetForReuse();
  }

  juce::ScopedLock sl(localChannelMutex);
  localChannels.push_back(ch);
  publishLocalChannels();
  return (int)localChannels.size() - 1;
}

void AntiphonAudioProcessor::removeLastLocalChannel() {
  juce::ScopedLock sl(localChannelMutex);
  if (localChannels.size() <= 1) return;
  localChannels.back()->isValid.store(false);
  // Kept alive rather than destroyed: the audio thread may be holding the raw
  // pointer for the rest of this block, and freeing an 11 MB ring underneath it
  // is exactly the crash this design exists to prevent. A later add takes it
  // back out of the pool.
  spareLocalChannels.push_back(localChannels.back());
  localChannels.pop_back();
  publishLocalChannels();
  // Removing the only soloed channel must release the solo bus, or everything
  // else stays silent with nothing on screen explaining why.
  bool anyLocalSolo = false;
  for (const auto &lc : localChannels)
    if (lc->monitorSolo.load()) { anyLocalSolo = true; break; }
  ninjamClient.setLocalSoloActive(anyLocalSolo);
}

// A plain updateHostDisplay() does not tell a host that the bus topology
// changed -- JUCE's ChangeDetails had no way to say it, so the host kept
// routing to the layout it cached when the plugin was loaded and a new bus
// never appeared. patches/juce-bus-layout-change-notification.patch adds the
// flag and maps it to VST3's kIoChanged; patches/clap-bus-layout-rescan.patch
// maps it to CLAP's audio-ports rescan.
static juce::AudioProcessorListener::ChangeDetails busLayoutChange() {
  return juce::AudioProcessorListener::ChangeDetails{}.withBusLayoutChanged(true);
}

int AntiphonAudioProcessor::addInputBus() {
  if (!addBus(true)) return -1;
  updateHostDisplay(busLayoutChange());
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
  updateHostDisplay(busLayoutChange());
}

int AntiphonAudioProcessor::addOutputBus() {
  if (!addBus(false)) return -1;
  updateHostDisplay(busLayoutChange());
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
  updateHostDisplay(busLayoutChange());
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
  // The spares are sized here too. A recycled channel must never resize its own
  // ring -- see LocalChannel::resetForReuse -- so this is the only place the
  // sample rate can reach it. Safe because the host does not call prepareToPlay
  // concurrently with processBlock.
  for (auto *list : {&localChannels, &spareLocalChannels}) {
    for (auto &lc : *list) {
      lc->ring.setSize(2, ringSize);
      lc->ring.clear();
      lc->fifo.setTotalSize(ringSize);
      lc->monitorRamp.prepare(sampleRate);
      lc->monitorRamp.jumpTo(1.0f);
    }
  }
  publishLocalChannels();
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

  const int numInBuses = getBusCount(true);
  const int numChannels = audioChannelCount.load(std::memory_order_acquire);

  // Null for a channel index the snapshot does not have, which ChannelMix
  // treats as absent rather than reading past the end.
  auto sourcePointer = [this](int ch) -> const float * {
    return ch >= 0 && ch < inputSnapshot.getNumChannels()
               ? inputSnapshot.getReadPointer(ch)
               : nullptr;
  };

  for (int ci = 0; ci < numChannels; ++ci) {
    auto &lc = *audioChannels[(std::size_t)ci];
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

    // The ring stores what was played, un-gated. Where transmit was on is
    // recorded separately and applied at the boundary, which is what lets the
    // retroactive gesture reach back into an interval that is still being
    // captured.
    auto &spans = lc.spans[(std::size_t)lc.writeSpanIndex.load()];
    spans.setStateAt(lc.fifo.getNumReady(), lc.xmitEnabled.load());

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
    const int numChannels = audioChannelCount.load(std::memory_order_acquire);

    // The global solo bus, so a soloed *remote* channel silences local monitors
    // too (njclient.cpp:1307 tests the combined mask). Two independent solo
    // buses meant solo never did the one thing solo is for.
    const bool anySolo = ninjamClient.isAnySoloActive();

    int numInBuses = getBusCount(true);
    for (int ci = 0; ci < numChannels; ++ci) {
      auto &lc = *audioChannels[(std::size_t)ci];
      int busIdx = juce::jlimit(0, numInBuses - 1, lc.inputBusIndex.load());
      auto *bus = getBus(true, busIdx);
      if (!bus) continue;
      int offset = bus->getChannelIndexInProcessBlockBuffer(0);
      int busCh = bus->getNumberOfChannels();

      // njclient.cpp:1307 -- solo wins outright over mute, so a channel that is
      // both is heard. We used to multiply the two, so mute won and solo could
      // not bring a muted channel back.
      const bool muted = lc.muted.load();
      const bool solo = lc.monitorSolo.load();
      const bool audible = (!anySolo && !muted) || solo;
      lc.monitorRamp.setTarget(audible ? 1.0f : 0.0f);

      const auto txGains =
          ChannelMix::panGains(lc.volume.load(), lc.pan.load());
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
      const ChannelMix::Frame monGains{
          txGains.left * lc.monitorRamp.current(),
          txGains.right * lc.monitorRamp.current()};
      const auto p =
          ChannelMix::peaks(srcL, srcR, mono, 0, ns, monGains);
      lc.peakL.store(p.left);
      lc.peakR.store(p.right);

      if (totalNumOutputChannels >= 2) {
        // Per sample, because the ramp moves within the block. addInto took a
        // constant gain, which is exactly why mute used to click.
        float *outL = buffer.getWritePointer(0);
        float *outR = buffer.getWritePointer(1);
        for (int i = 0; i < ns; ++i) {
          const float g = lc.monitorRamp.gainAt(i);
          const auto f = ChannelMix::sourceFrame(srcL, srcR, mono, i);
          outL[i] += f.left * txGains.left * g;
          outR[i] += f.right * txGains.right * g;
        }
      }

      // Advances for the whole block whether or not it was mixed, so the ramp
      // stays in step with the clock.
      lc.monitorRamp.advance(ns);
    }
  }

  // Helper: fire per-channel callAsync at interval boundary
  //
  // The callAsync below still allocates and posts from the audio thread, which
  // is a PRINCIPLES 7 violation in its own right -- but it fires once per
  // interval rather than per block, and removing it means the redesign tracked
  // as *Lock-free TX handoff* in ROADMAP.md. The lock is gone; the post is not.
  auto fireCaptureLambdas = [this](const RunGate &postGate) {
    // inJam and echoOn are read HERE and captured by value. Re-reading them
    // inside the lambda would let a connection landing between the boundary and
    // the message thread's turn transmit an interval that was captured offline.
    const bool postInJam = postGate.inJam;
    const bool postEchoOn = postGate.echoOn;

    const int numChannels = audioChannelCount.load(std::memory_order_acquire);
    for (int ci = 0; ci < numChannels; ++ci) {
      auto *lc = audioChannels[(std::size_t)ci];
      const int length = lc->fifo.getNumReady();

      // Hand this interval's spans to the copy-out and start recording the next
      // one, carrying the current transmit state across the boundary.
      const int handoff = lc->writeSpanIndex.load();
      const int next = 1 - handoff;
      lc->spans[(std::size_t)next].beginInterval(lc->xmitEnabled.load());
      lc->writeSpanIndex.store(next);

      // An interval goes out if transmit was on for any part of it. Only one
      // you were silent for throughout is dropped, which is what the reference
      // client does for a channel that is not broadcasting.
      if (!lc->spans[(std::size_t)handoff].anyActive(length)) {
        lc->fifo.reset();
        continue;
      }
      if ((postInJam || postEchoOn) && length > 0) {
        bool mono = lc->isMono.load();
        const int rampSamples = lc->monitorRamp.rampLengthSamples();
        // A raw pointer is as safe here as the shared_ptr used to be: the
        // channel outlives the processor's audio path, and the lambda already
        // captures `this`, so it cannot outlive the processor either.
        juce::MessageManager::callAsync(
            [this, lc, length, ci, mono, handoff, rampSamples, postInJam,
             postEchoOn]() {
          if (!lc->isValid.load()) return;
          juce::AudioBuffer<float> buf(2, length);
          int s1, n1, s2, n2;
          lc->fifo.prepareToRead(length, s1, n1, s2, n2);
          for (int ch = 0; ch < 2; ++ch) {
            if (n1 > 0) buf.copyFrom(ch, 0,  lc->ring, ch, s1, n1);
            if (n2 > 0) buf.copyFrom(ch, n1, lc->ring, ch, s2, n2);
          }
          lc->fifo.finishedRead(n1 + n2);
          // Silence the stretches transmit was off for, ramping each edge so
          // switching it cannot click in what the other players hear.
          lc->spans[(std::size_t)handoff].applyTo(
              buf.getArrayOfWritePointers(), 2, length, rampSamples);
          if (postInJam)
            ninjamClient.processCapturedAudio(buf, length, ci, mono);
          // v1 echoes one channel: summing several needs to know when the last
          // channel's post for a boundary has arrived, which is fragile.
          if (postEchoOn && ci == 0)
            ninjamClient.pushEchoInterval(buf, length);
        });
      } else {
        lc->fifo.reset();
      }
    }
  };

  // 5. Local interval metronome (sole authority for swap timing).
  // Per njclient's design, DOWNLOAD_INTERVAL_BEGIN is only used to queue
  // incoming audio, never to drive the local clock. Network jitter would
  // otherwise yank the interval clock mid-interval and discard seconds of
  // un-played audio.
  // Declared out here because the remote mix below sits outside the clock
  // block; assigned inside, once SyncState has been updated for this block.
  RunGate gate;

  double sampleRate = getSampleRate();
  if (sampleRate > 0.0) {
    // Drain any pending signal so the diagnostic counter stays at 0.
    ninjamClient.intervalBeginSignal.store(false);

    // 6. Metronome click + interval boundary detection.
    // IntervalClock owns the grid; it is sample-exact and every interval is
    // the same length, matching the reference client's integer arithmetic.
    intervalClock.setTempo(internalBpm.load(), internalBpi.load());
    // The click's bar accent depends on the interval length; see
    // MetronomeVoice::setBeatsPerInterval. Taken from the clock rather than
    // from internalBpi, so a pending change does not alter the accent of the
    // interval still playing.
    metronomeVoice.setBeatsPerInterval(intervalClock.getBpi());

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

    // Three separate questions, which used to be one. See RunGate.h: the
    // transport drives the grid, the connection decides what happens to the
    // audio. Recomputed every block from live state, so a connection landing or
    // dropping takes effect immediately without anything having to remember.
    gate = computeRunGate(ninjamClient.isConnected(), syncState.isRunning(),
                          transportIsPlaying(), practiceEnabled.load());

    // Offline, the downbeat is wherever the transport started.
    //
    // Connected, SyncState owns the phase: it resets the clock on the armed
    // transport-start edge and deliberately keeps running across a stop, so
    // that an accidental stop cannot re-phase a jam. Offline it reports
    // Disconnected and never fires at all, so nothing aligned the grid to the
    // transport and the metronome free-ran against the host's. There is no
    // room to desync from here, so starting the grid at zero is simply right.
    if (gate.gridRunning && !gridWasRunning && !si.connected) {
      intervalClock.reset();
      metronomeVoice.reset();
    }
    gridWasRunning = gate.gridRunning;

    if (gate.gridRunning) {
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
    // Published together with the phase they belong to, so the UI can never
    // divide this interval's progress by the next interval's length.
    publishedActiveBpm.store(intervalClock.getBpm());
    publishedActiveBpi.store(intervalClock.getBpi());

    // Capture, split at the interval boundary.
    //
    // The samples before the boundary belong to the interval that is ending
    // and must be in the buffer we transmit; the samples after it start the
    // next one. Capturing whole blocks and flushing at the boundary instead
    // rounds every transmitted interval up to a multiple of the block size --
    // measured against the reference client as roughly +1.3 ms of stretch at
    // each interval seam (work item #27).
    if (gate.inJam || gate.echoOn) {
      IntervalClock::splitAtIntervalStarts(clockEvents, ns, captureSegments);
      for (const auto &seg : captureSegments) {
        if (seg.count > 0)
          captureInputRange(seg.start, seg.count);
        if (seg.closesInterval) {
          publishedIntervalIndex.fetch_add(1);
          // The gate is captured by value at post time inside this call.
          fireCaptureLambdas(gate);
          if (gate.inJam) {
            ninjamClient.swapIntervalBuffers();
            ninjamClient.diagSwaps.fetch_add(1);
            ninjamClient.intervalBeginSignal.store(false);
          } else if (gate.echoOn) {
            // The echo's boundary swap, and the only place it belongs. It
            // consumes what the message thread stored after an earlier
            // boundary, which is why EchoSchedule charges two intervals for
            // the handoff.
            ninjamClient.swapEchoBuffers();
          }
        }
      }
    }

    // Echo slots are serviced on EVERY block outside a jam, not only while
    // practice is on. Switching practice off marks them draining, and only the
    // audio thread completes that transition -- skip the call and they stay
    // draining forever, so the history can never be released and the next
    // enable deadlocks against its own predecessor.
    //
    // Servicing is NOT swapping. This used to call swapEchoBuffers, which
    // meant the interval that had just started playing was retired one block
    // later: you heard the first block of each interval and nothing else.
    if (!gate.inJam)
      ninjamClient.serviceEchoSlots();
  }

  // 7. Remote audio mix. Gated on being in the jam rather than merely
  // connected, so a stopped transport stops everything together.
  if (gate.inJam)
    ninjamClient.getDecodedAudio(buffer);
  else
    ninjamClient.getEchoAudio(buffer);

}

void AntiphonAudioProcessor::applyDebugCaptureSettings() {
  // Opens or closes the dump files, so it takes a lock and touches the disk.
  // Called from the message thread only -- it used to run from processBlock on
  // every block, which put file I/O on the audio thread for the one block where
  // a toggle changed (PRINCIPLES section 7).
  ninjamClient.setSaveTx(saveTxEnabled.load());
  ninjamClient.setSaveRx(saveRxEnabled.load());
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
        // Clamped: a corrupt or hostile state must not be able to grow this
        // past what the published audio-thread view can hold.
        if (idx < 0 || idx >= kMaxLocalChannels) continue;
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
    publishLocalChannels();
}

bool AntiphonAudioProcessor::applyRetroactiveTransmit(
    int channelIndex, bool on, juce::int64 pressIntervalIndex) {
  // Too late: the interval the press belonged to has already been handed off
  // and transmitted, so rewriting its spans would change nothing at best and
  // the wrong interval at worst.
  if (pressIntervalIndex != publishedIntervalIndex.load())
    return false;

  juce::ScopedLock sl(localChannelMutex);
  if (channelIndex < 0 || channelIndex >= (int)localChannels.size())
    return false;

  auto &lc = *localChannels[(std::size_t)channelIndex];
  // O(1): the ordinary toggle already covered the press onwards, so making the
  // earlier part match leaves one uniform span with no transitions.
  lc.spans[(std::size_t)lc.writeSpanIndex.load()].makeWholeInterval(on);
  return true;
}

bool AntiphonAudioProcessor::setPracticeEnabled(bool on) {
  if (!on) {
    practiceEnabled.store(false);
    ninjamClient.setPracticeEnabled(false, 0, 0.0);
    return true;
  }

  // The history is interval-sized, so it needs the grid the clock is actually
  // running -- not the pending tempo, which may not have taken effect yet.
  const int intervalSamples = intervalClock.samplesPerInterval();
  const double sr = getSampleRate();
  if (!ninjamClient.setPracticeEnabled(true, intervalSamples, sr))
    return false;

  practiceEnabled.store(true);
  return true;
}

void AntiphonAudioProcessor::refreshLocalSoloState() {
  bool anyLocalSolo = false;
  {
    juce::ScopedLock sl(localChannelMutex);
    for (const auto &lc : localChannels)
      if (lc->monitorSolo.load()) { anyLocalSolo = true; break; }
  }
  ninjamClient.setLocalSoloActive(anyLocalSolo);
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
  // Practice is offline-only; the gate enforces that every block, but switching
  // it off here keeps the UI honest about what is going on.
  setPracticeEnabled(false);
  // The standalone has no host transport to start, so connecting starts ours.
  // Without this you would join a room and hear nothing until you found a
  // button that did not exist before today.
  if (isStandaloneApp())
    localTransportPlaying.store(true);
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
