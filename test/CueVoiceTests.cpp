#include <JuceHeader.h>

#include "CueVoice.h"
#include "TestSignal.h"

#include <cmath>
#include <vector>

namespace {

std::vector<float> renderCue(double sampleRate, int extra = 4800) {
  CueVoice v;
  v.prepare(sampleRate);
  v.trigger();
  std::vector<float> buf((size_t)(sampleRate * 0.2) + (size_t)extra, 0.0f);
  // In blocks, as the audio thread renders it.
  for (size_t at = 0; at < buf.size(); at += 61)
    v.render(buf.data() + at, (int)std::min<size_t>(61, buf.size() - at), 1.0f);
  return buf;
}

class CueVoiceTests : public juce::UnitTest {
public:
  CueVoiceTests() : juce::UnitTest("CueVoice", "CueVoice") {}

  void runTest() override {
    for (double sr : {44100.0, 48000.0}) {
      const auto buf = renderCue(sr);
      const juce::String at = " at " + juce::String(sr, 0);

      beginTest("the cue sounds at 1568 Hz" + at);
      {
        // The first 40 ms, where the decay has not yet taken it under the
        // detector's gate.
        const double f =
            TestSignal::dominantFrequency(buf.data(), (int)(sr * 0.04), sr);
        expect(std::fabs(f - CueVoice::kFrequencyHz) / CueVoice::kFrequencyHz <
                   0.01,
               "measured " + juce::String(f, 1) + " Hz");
      }

      beginTest("the cue is under -60 dB within 150 ms" + at);
      {
        const double pk = TestSignal::peak(buf.data(), (int)buf.size());
        expect(pk > 0.01, "the cue is audible at all");
        int last = 0;
        for (int i = 0; i < (int)buf.size(); ++i)
          if (std::fabs(buf[(size_t)i]) > pk * 0.001)
            last = i;
        const double ms = 1000.0 * (double)last / sr;
        expect(ms <= 150.0,
               "still above -60 dB at " + juce::String(ms, 1) + " ms");
      }

      beginTest("the cue is silent once it has ended" + at);
      {
        const int from = (int)(sr * 0.16);
        expect(TestSignal::peak(buf.data() + from, (int)buf.size() - from) ==
               0.0);
      }
    }

    beginTest("render writes only the samples it is given");
    {
      CueVoice v;
      v.prepare(48000.0);
      v.trigger();
      std::vector<float> buf(130, 0.0f);
      v.render(buf.data(), 128, 1.0f);
      expect(buf[128] == 0.0f && buf[129] == 0.0f);
      expect(v.isActive());
    }

    beginTest("an idle or unprepared voice adds nothing");
    {
      CueVoice idle;
      idle.prepare(48000.0);
      std::vector<float> buf(256, 0.0f);
      idle.render(buf.data(), 256, 1.0f);
      expect(TestSignal::peak(buf.data(), 256) == 0.0);
      CueVoice cold;
      cold.trigger();
      cold.render(buf.data(), 256, 1.0f);
      expect(TestSignal::peak(buf.data(), 256) == 0.0);
      expect(!cold.isActive());
    }

    beginTest("gain scales the cue");
    {
      CueVoice a, b;
      a.prepare(48000.0);
      b.prepare(48000.0);
      a.trigger();
      b.trigger();
      std::vector<float> x(2400, 0.0f), y(2400, 0.0f);
      a.render(x.data(), 2400, 1.0f);
      b.render(y.data(), 2400, 0.5f);
      expectWithinAbsoluteError(TestSignal::rms(y.data(), 2400) /
                                    TestSignal::rms(x.data(), 2400),
                                0.5, 1e-6);
    }
  }
};

static CueVoiceTests cueVoiceTests;

} // namespace
