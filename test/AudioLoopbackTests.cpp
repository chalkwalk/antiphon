#include <JuceHeader.h>

#include "FakeNinjamServer.h"
#include "IntervalClock.h"
#include "NinjamClient.h"
#include "TestSignal.h"

#include <vector>

namespace {

// Drives audio all the way round: processCapturedAudio -> real socket ->
// FakeNinjamServer echo -> handleMessage -> swapIntervalBuffers ->
// getDecodedAudio, and hands back the recovered signal.
struct AudioSession {
  FakeNinjamServer server;
  NinjamClient client;
  int intervalSamples = 0;
  double sampleRate = 48000.0;

  ~AudioSession() {
    client.disconnectFromServer();
    server.stop();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }

  bool start(double sr, int bpm, int bpi,
             const juce::String &peerName = "peer") {
    sampleRate = sr;
    server.setEchoUsername(peerName);
    if (!server.start(bpm, bpi))
      return false;

    client.setSampleRate(sr);
    client.setServerBpm(bpm);
    client.setServerBpi(bpi);
    client.connectToServer("127.0.0.1", server.port(), "tester", "");
    if (!waitUntil([this] { return client.isConnected(); }, 5000))
      return false;

    IntervalClock clock;
    clock.prepare(sr);
    clock.setTempo(bpm, bpi);
    intervalSamples = clock.samplesPerInterval();

    server.sendUserInfo(peerName, 0, "gtr");
    return waitUntil(
        [this, peerName] { return client.getRemoteUsers().count(peerName) > 0; });
  }

  // Sends one interval of audio and waits for the echo to be fully decoded.
  bool sendInterval(const juce::AudioBuffer<float> &pcm, int channelIndex = 0,
                    bool mono = false) {
    const int before = server.completedUploads();
    juce::AudioBuffer<float> copy(pcm);
    client.processCapturedAudio(copy, copy.getNumSamples(), channelIndex, mono);
    if (!waitUntil([&] { return server.completedUploads() > before; }, 5000))
      return false;
    // The final 0x05 must have been handled before the interval is playable.
    return waitUntil(
        [&] { return client.diagLastIntervalSamples.load() > 0; }, 5000);
  }

  // Pops the queued interval and renders `numSamples` of it in blocks.
  juce::AudioBuffer<float> render(int numSamples, int blockSize = 512,
                                  int numChannels = 2) {
    client.swapIntervalBuffers();
    juce::AudioBuffer<float> out(numChannels, numSamples);
    out.clear();

    juce::AudioBuffer<float> block(numChannels, blockSize);
    for (int pos = 0; pos < numSamples; pos += blockSize) {
      const int n = std::min(blockSize, numSamples - pos);
      block.clear();
      juce::AudioBuffer<float> view(block.getArrayOfWritePointers(),
                                    numChannels, n);
      client.getDecodedAudio(view);
      for (int ch = 0; ch < numChannels; ++ch)
        out.copyFrom(ch, pos, block, ch, 0, n);
    }
    return out;
  }
};

juce::AudioBuffer<float> makeSineBuffer(int numFrames, double freq, double sr,
                                        float amp) {
  juce::AudioBuffer<float> b(2, numFrames);
  for (int ch = 0; ch < 2; ++ch) {
    auto *p = b.getWritePointer(ch);
    for (int i = 0; i < numFrames; ++i)
      p[i] = amp * (float)std::sin(2.0 * TestSignal::kPi * freq * i / sr);
  }
  return b;
}

// Analyses the steady middle of a recovered channel, skipping codec ramp.
struct Analysis {
  double rms = 0.0;
  double freq = 0.0;
  double peak = 0.0;
};

Analysis analyse(const juce::AudioBuffer<float> &b, int ch, double sr) {
  const int total = b.getNumSamples();
  const int skip = total / 10;
  const int n = total - 2 * skip;
  const float *p = b.getReadPointer(ch) + skip;
  return {TestSignal::rms(p, n), TestSignal::dominantFrequency(p, n, sr),
          TestSignal::peak(p, n)};
}

class AudioLoopbackTests : public juce::UnitTest {
public:
  AudioLoopbackTests() : juce::UnitTest("AudioLoopback", "AudioLoopback") {}

  void runTest() override {
    beginTest("round-trip at 48 kHz preserves pitch and length");
    checkRoundTrip(48000.0);

    beginTest("round-trip at 44.1 kHz preserves pitch and length");
    // The companion to the 48 kHz case. Before the encoder was given the real
    // sample rate it always declared 48000, so at 44.1 kHz the receiver
    // resampled by 48000/44100 and the tone came back 8.8% sharp and 8% short.
    checkRoundTrip(44100.0);

    beginTest("round-trip at 96 kHz preserves pitch and length");
    checkRoundTrip(96000.0);

    beginTest("mono channel round-trips to both output channels");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      expect(s.sendInterval(pcm, 0, /*mono*/ true));
      s.client.setRemoteUserVolume("peer", 0, 1.0f);

      auto out = s.render(s.intervalSamples);
      auto l = analyse(out, 0, 48000.0);
      auto r = analyse(out, 1, 48000.0);
      expect(std::fabs(l.freq - 440.0) / 440.0 < 0.02,
             "left measured " + juce::String(l.freq, 1) + " Hz");
      expect(r.rms > 0.0, "right channel silent for a mono source");
      expect(std::fabs(l.rms - r.rms) < 0.01,
             "mono source did not produce equal channels");
    }

    beginTest("volume, mute and solo behave independently");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);

      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      expect(s.sendInterval(pcm));
      const double loud = analyse(s.render(s.intervalSamples), 0, 48000.0).rms;
      expect(loud > 0.0, "no audio at full volume");

      s.client.setRemoteUserVolume("peer", 0, 0.5f);
      expect(s.sendInterval(pcm));
      const double half = analyse(s.render(s.intervalSamples), 0, 48000.0).rms;
      expect(std::fabs(half / loud - 0.5) < 0.05,
             "half volume gave ratio " + juce::String(half / loud, 3));

      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      s.client.setRemoteUserMute("peer", 0, true);
      expect(s.sendInterval(pcm));
      auto muted = s.render(s.intervalSamples);
      expectEquals(TestSignal::peak(muted.getReadPointer(0),
                                    muted.getNumSamples()),
                   0.0, "muted channel produced audio");

      s.client.setRemoteUserMute("peer", 0, false);
      expect(s.sendInterval(pcm));
      expect(analyse(s.render(s.intervalSamples), 0, 48000.0).rms > 0.0,
             "unmute did not restore audio");
    }

    beginTest("hard pan silences the opposite channel");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      s.client.setRemoteUserPan("peer", 0, -1.0f); // hard left
      expect(s.sendInterval(pcm));

      auto out = s.render(s.intervalSamples);
      expect(analyse(out, 0, 48000.0).rms > 0.0, "left channel silent");
      expectEquals(
          TestSignal::peak(out.getReadPointer(1), out.getNumSamples()), 0.0,
          "right channel not silenced by hard-left pan");
    }

    beginTest("output bus routing sends audio to the chosen stereo pair");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      s.client.setRemoteUserOutputBus("peer", 0, 1); // second stereo bus
      expect(s.sendInterval(pcm));

      auto out = s.render(s.intervalSamples, 512, /*numChannels*/ 4);
      expectEquals(TestSignal::peak(out.getReadPointer(0),
                                    out.getNumSamples()),
                   0.0, "bus 0 left should be silent");
      expectEquals(TestSignal::peak(out.getReadPointer(1),
                                    out.getNumSamples()),
                   0.0, "bus 0 right should be silent");
      expect(analyse(out, 2, 48000.0).rms > 0.0, "bus 1 left silent");
      expect(analyse(out, 3, 48000.0).rms > 0.0, "bus 1 right silent");
    }

    beginTest("out-of-range output bus clamps instead of writing out of bounds");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      s.client.setRemoteUserOutputBus("peer", 0, 99);
      expect(s.sendInterval(pcm));

      // Only one stereo bus exists in the destination buffer.
      auto out = s.render(s.intervalSamples, 512, 2);
      expect(analyse(out, 0, 48000.0).rms > 0.0,
             "clamped routing produced no audio");
    }

    beginTest("playback is delayed by exactly one interval");
    {
      // Before the swap, nothing queued has been made current, so the mixer
      // must produce silence -- this is the one-interval delay.
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      expect(s.sendInterval(pcm));

      juce::AudioBuffer<float> preSwap(2, 4096);
      preSwap.clear();
      s.client.getDecodedAudio(preSwap);
      expectEquals(TestSignal::peak(preSwap.getReadPointer(0), 4096), 0.0,
                   "audio played before the interval boundary swap");

      auto out = s.render(s.intervalSamples);
      expect(analyse(out, 0, 48000.0).rms > 0.0,
             "no audio after the swap");
    }

    beginTest("recovered interval has no interior dropouts");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      auto pcm = makeSineBuffer(s.intervalSamples, 440.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      expect(s.sendInterval(pcm));

      // Render across an awkward block size to stress the partial-fill path.
      auto out = s.render(s.intervalSamples, 61);
      const int skip = s.intervalSamples / 10;
      const int n = s.intervalSamples - 2 * skip;
      const int longestGap =
          TestSignal::longestZeroRun(out.getReadPointer(0) + skip, n);
      expect(longestGap < 64,
             "interior dropout of " + juce::String(longestGap) + " samples");
    }

    beginTest("two channels from one user stay separate");
    {
      AudioSession s;
      expect(s.start(48000.0, 120, 8));
      s.server.sendUserInfo("peer", 1, "vox");
      expect(waitUntil([&] {
        auto u = s.client.getRemoteUsers();
        return u.count("peer") && u["peer"].channels.size() == 2;
      }));

      auto low = makeSineBuffer(s.intervalSamples, 220.0, 48000.0, 0.5f);
      auto high = makeSineBuffer(s.intervalSamples, 880.0, 48000.0, 0.5f);
      s.client.setRemoteUserVolume("peer", 0, 1.0f);
      s.client.setRemoteUserVolume("peer", 1, 1.0f);
      s.client.setRemoteUserOutputBus("peer", 0, 0);
      s.client.setRemoteUserOutputBus("peer", 1, 1);

      expect(s.sendInterval(low, 0));
      expect(s.sendInterval(high, 1));

      auto out = s.render(s.intervalSamples, 512, 4);
      const double f0 = analyse(out, 0, 48000.0).freq;
      const double f1 = analyse(out, 2, 48000.0).freq;
      expect(std::fabs(f0 - 220.0) / 220.0 < 0.03,
             "channel 0 measured " + juce::String(f0, 1) + " Hz");
      expect(std::fabs(f1 - 880.0) / 880.0 < 0.03,
             "channel 1 measured " + juce::String(f1, 1) + " Hz");
    }
  }

  void checkRoundTrip(double sr) {
    AudioSession s;
    expect(s.start(sr, 120, 8), "session failed to start at " +
                                    juce::String(sr));
    s.client.setRemoteUserVolume("peer", 0, 1.0f);

    auto pcm = makeSineBuffer(s.intervalSamples, 440.0, sr, 0.5f);
    expect(s.sendInterval(pcm), "interval never completed at " +
                                    juce::String(sr));

    const int decoded = s.client.diagLastIntervalSamples.load();
    const double lengthRatio = (double)decoded / (double)s.intervalSamples;
    expect(lengthRatio > 0.98 && lengthRatio < 1.02,
           juce::String(sr) + " Hz: decoded " + juce::String(decoded) +
               " of " + juce::String(s.intervalSamples) + " samples (" +
               juce::String(lengthRatio * 100.0, 1) + "%)");

    auto out = s.render(s.intervalSamples);
    auto a = analyse(out, 0, sr);
    expect(std::fabs(a.freq - 440.0) / 440.0 < 0.02,
           juce::String(sr) + " Hz: recovered " + juce::String(a.freq, 1) +
               " Hz, expected 440");

    const double inRms = TestSignal::rms(pcm.getReadPointer(0),
                                         pcm.getNumSamples());
    const double deltaDb = TestSignal::toDb(a.rms) - TestSignal::toDb(inRms);
    expect(std::fabs(deltaDb) < 1.5,
           juce::String(sr) + " Hz: level moved by " +
               juce::String(deltaDb, 2) + " dB");
  }
};

static AudioLoopbackTests audioLoopbackTests;

} // namespace
