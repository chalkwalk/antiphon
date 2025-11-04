#include <JuceHeader.h>

#include "MetronomeVoice.h"
#include "TestSignal.h"

#include <vector>

namespace {

// Renders one complete click into a buffer.
std::vector<float> renderClick(double sampleRate, int beatIndex,
                               float gain = 1.0f) {
  MetronomeVoice v;
  v.prepare(sampleRate);
  v.trigger(beatIndex);
  std::vector<float> buf((size_t)v.clickLengthSamples() + 64, 0.0f);
  v.render(buf.data(), (int)buf.size(), gain);
  return buf;
}

class MetronomeVoiceTests : public juce::UnitTest {
public:
  MetronomeVoiceTests() : juce::UnitTest("MetronomeVoice", "MetronomeVoice") {}

  void expectPitch(const std::vector<float> &buf, double sampleRate,
                   double expectedHz, const juce::String &what) {
    // Measure over the first 60% of the click, where the envelope is still
    // high enough for the hysteresis gate to track cleanly.
    const int n = (int)((double)buf.size() * 0.6);
    const double f =
        TestSignal::dominantFrequency(buf.data(), n, sampleRate);
    expect(std::fabs(f - expectedHz) / expectedHz < 0.03,
           what + ": measured " + juce::String(f, 1) + " Hz, expected " +
               juce::String(expectedHz, 1));
  }

  void runTest() override {
    beginTest("downbeat click sounds at 880 Hz");
    // The formula this replaced swept 2*pi*freq/bpm radians over a click of
    // 3/bpm seconds, realising freq/3 Hz -- a downbeat came out near 293 Hz.
    expectPitch(renderClick(48000.0, 0), 48000.0, 880.0, "downbeat");

    beginTest("bar and beat clicks sound at 660 and 440 Hz");
    expectPitch(renderClick(48000.0, 4), 48000.0, 660.0, "bar start");
    expectPitch(renderClick(48000.0, 1), 48000.0, 440.0, "plain beat");
    expectPitch(renderClick(48000.0, 8), 48000.0, 660.0, "bar start");
    expectPitch(renderClick(48000.0, 7), 48000.0, 440.0, "plain beat");

    beginTest("pitch is independent of sample rate");
    for (double sr : {44100.0, 48000.0, 88200.0, 96000.0}) {
      expectPitch(renderClick(sr, 0), sr, 880.0,
                  "downbeat at " + juce::String(sr));
      expectPitch(renderClick(sr, 1), sr, 440.0,
                  "beat at " + juce::String(sr));
    }

    beginTest("nominal frequency is reported correctly");
    {
      MetronomeVoice v;
      v.prepare(48000.0);
      v.trigger(0);
      expect(v.currentFrequency() == 880.0);
      v.trigger(4);
      expect(v.currentFrequency() == 660.0);
      v.trigger(3);
      expect(v.currentFrequency() == 440.0);
    }

    beginTest("downbeat is louder than a bar start, which is louder than a beat");
    {
      const double p0 = TestSignal::peak(renderClick(48000.0, 0).data(),
                                         (int)renderClick(48000.0, 0).size());
      auto bar = renderClick(48000.0, 4);
      auto beat = renderClick(48000.0, 1);
      const double p4 = TestSignal::peak(bar.data(), (int)bar.size());
      const double p1 = TestSignal::peak(beat.data(), (int)beat.size());
      expect(p0 > p4, "downbeat not louder than bar start");
      expect(p4 > p1, "bar start not louder than plain beat");
    }

    beginTest("click terminates within its stated length");
    {
      MetronomeVoice v;
      v.prepare(48000.0);
      v.trigger(0);
      const int len = v.clickLengthSamples();
      expect(len > 0);

      std::vector<float> buf((size_t)len + 256, 0.0f);
      v.render(buf.data(), (int)buf.size(), 1.0f);
      expect(!v.isActive(), "voice still active after a full render");
      for (size_t i = (size_t)len; i < buf.size(); ++i)
        expect(buf[i] == 0.0f, "audio past the end of the click");
    }

    beginTest("click never outlasts a beat at a fast tempo");
    {
      // 200 bpm is 0.3 s per beat; the click must be comfortably shorter.
      MetronomeVoice v;
      v.prepare(48000.0);
      v.trigger(0);
      const double clickSeconds = (double)v.clickLengthSamples() / 48000.0;
      expect(clickSeconds < 60.0 / 200.0,
             "click is longer than a beat at 200 bpm");
    }

    beginTest("render across block boundaries is continuous");
    {
      MetronomeVoice a;
      a.prepare(48000.0);
      a.trigger(0);
      std::vector<float> whole((size_t)a.clickLengthSamples() + 64, 0.0f);
      a.render(whole.data(), (int)whole.size(), 1.0f);

      MetronomeVoice b;
      b.prepare(48000.0);
      b.trigger(0);
      std::vector<float> split(whole.size(), 0.0f);
      const int block = 37; // deliberately not a divisor of anything
      for (size_t pos = 0; pos < split.size(); pos += (size_t)block) {
        const int n = (int)std::min((size_t)block, split.size() - pos);
        b.render(split.data() + pos, n, 1.0f);
      }

      double maxDiff = 0.0;
      for (size_t i = 0; i < whole.size(); ++i)
        maxDiff = std::max(maxDiff, (double)std::fabs(whole[i] - split[i]));
      expect(maxDiff < 1e-6,
             "block-split render differs by " + juce::String(maxDiff));
    }

    beginTest("gain scales the output linearly");
    {
      auto full = renderClick(48000.0, 0, 1.0f);
      auto half = renderClick(48000.0, 0, 0.5f);
      auto zero = renderClick(48000.0, 0, 0.0f);

      const double pFull = TestSignal::peak(full.data(), (int)full.size());
      const double pHalf = TestSignal::peak(half.data(), (int)half.size());
      expect(std::fabs(pHalf / pFull - 0.5) < 0.01,
             "half gain gave ratio " + juce::String(pHalf / pFull, 4));
      expectEquals(TestSignal::peak(zero.data(), (int)zero.size()), 0.0);
    }

    beginTest("unprepared or untriggered voice renders silence");
    {
      MetronomeVoice v;
      v.trigger(0); // never prepared
      std::vector<float> buf(256, 0.0f);
      v.render(buf.data(), (int)buf.size(), 1.0f);
      expectEquals(TestSignal::peak(buf.data(), (int)buf.size()), 0.0);

      MetronomeVoice w;
      w.prepare(48000.0);
      std::vector<float> buf2(256, 0.0f);
      w.render(buf2.data(), (int)buf2.size(), 1.0f);
      expectEquals(TestSignal::peak(buf2.data(), (int)buf2.size()), 0.0);
      expect(!w.isActive());
    }
  }
};

static MetronomeVoiceTests metronomeVoiceTests;

} // namespace
