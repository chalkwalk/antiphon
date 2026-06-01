#include <JuceHeader.h>

#include "StemRender.h"

#include <vector>

namespace {

// A clip of `frames` frames, interleaved, where every sample of channel 0 is
// `l` and channel 1 is `r`. Constant values make truncation and padding
// obvious at a glance.
std::vector<float> makeClip(int frames, int channels, float l, float r) {
  std::vector<float> out((std::size_t)(frames * channels));
  for (int i = 0; i < frames; ++i) {
    out[(std::size_t)(i * channels)] = l;
    if (channels > 1)
      out[(std::size_t)(i * channels + 1)] = r;
  }
  return out;
}

class StemRenderTests : public juce::UnitTest {
public:
  StemRenderTests() : juce::UnitTest("StemRender", "StemRender") {}

  void runTest() override {
    beginTest("a clip shorter than its interval leaves the rest silent");
    {
      // The alignment rule. If a short clip shortened the interval instead,
      // every stem would slip against the others a little more each time.
      const int frames = 100;
      auto clip = makeClip(60, 2, 0.5f, -0.5f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), 60, 2, 48000.0, l.data(), r.data(),
                            frames, 48000.0);

      expectWithinAbsoluteError(l[0], 0.5f, 1.0e-6f);
      expectWithinAbsoluteError(l[59], 0.5f, 1.0e-6f);
      expectEquals(l[60], 0.0f, "the tail must be silence");
      expectEquals(l[99], 0.0f);
      expectWithinAbsoluteError(r[59], -0.5f, 1.0e-6f);
      expectEquals(r[60], 0.0f);
    }

    beginTest("a clip longer than its interval is truncated");
    {
      const int frames = 50;
      auto clip = makeClip(200, 2, 0.5f, -0.5f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), 200, 2, 48000.0, l.data(), r.data(),
                            frames, 48000.0);

      for (int i = 0; i < frames; ++i)
        expectWithinAbsoluteError(l[(std::size_t)i], 0.5f, 1.0e-6f);
      // Nothing was written past the interval: the vectors are exactly `frames`
      // long, so an overrun would corrupt the heap and ASan would say so.
      expectEquals((int)l.size(), frames);
    }

    beginTest("a mono clip feeds both sides");
    {
      const int frames = 32;
      auto clip = makeClip(frames, 1, 0.25f, 0.0f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), frames, 1, 48000.0, l.data(), r.data(),
                            frames, 48000.0);

      for (int i = 0; i < frames; ++i) {
        expectWithinAbsoluteError(l[(std::size_t)i], 0.25f, 1.0e-6f);
        expectWithinAbsoluteError(r[(std::size_t)i], 0.25f, 1.0e-6f,
                                  "a mono clip must not leave one side silent");
      }
    }

    beginTest("channels stay separate for a stereo clip");
    {
      const int frames = 16;
      auto clip = makeClip(frames, 2, 1.0f, -1.0f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), frames, 2, 48000.0, l.data(), r.data(),
                            frames, 48000.0);

      expectWithinAbsoluteError(l[0], 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(r[0], -1.0f, 1.0e-6f);
    }

    beginTest("a clip at another rate is resampled to fill the interval");
    {
      // Players in one room are not all at 48 kHz, and a stem written at the
      // clip's own rate would run at the wrong speed against the others.
      // 44100 -> 48000 stretches, so 44100 source frames become about 48000.
      const int frames = 48000;
      auto clip = makeClip(44100, 1, 0.5f, 0.0f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), 44100, 1, 44100.0, l.data(), r.data(),
                            frames, 48000.0);

      // A constant input stays constant through the interpolator, away from the
      // very start where its filter is still filling.
      expectWithinAbsoluteError(l[1000], 0.5f, 0.01f);
      expectWithinAbsoluteError(l[47000], 0.5f, 0.01f,
                                "resampling must reach the end of the interval");
    }

    beginTest("downsampling does not run past the end of the interval");
    {
      // 96000 -> 48000 halves, so 96000 source frames yield 48000 output. The
      // guard that matters is the other direction: a long source must not write
      // beyond `frames`.
      const int frames = 1000;
      auto clip = makeClip(96000, 1, 0.5f, 0.0f);
      std::vector<float> l((std::size_t)frames, 0.0f), r((std::size_t)frames, 0.0f);

      StemRender::placeClip(clip.data(), 96000, 1, 96000.0, l.data(), r.data(),
                            frames, 48000.0);

      expectEquals((int)l.size(), frames);
      expectWithinAbsoluteError(l[500], 0.5f, 0.01f);
    }

    beginTest("placeClip reports how much it actually wrote");
    {
      // The caller needs this to know whether the interval was filled or
      // padded, which is what decides whether its tail needs a fade.
      auto clip = makeClip(60, 2, 0.5f, -0.5f);
      std::vector<float> l(100, 0.0f), r(100, 0.0f);
      expectEquals(StemRender::placeClip(clip.data(), 60, 2, 48000.0, l.data(),
                                         r.data(), 100, 48000.0),
                   60, "a short clip fills only what it has");
      expectEquals(StemRender::placeClip(clip.data(), 60, 2, 48000.0, l.data(),
                                         r.data(), 40, 48000.0),
                   40, "a long clip fills the interval and no more");
    }

    beginTest("a faded edge removes the step where audio meets silence");
    {
      // The click this exists to prevent. A player who sat an interval out
      // leaves a step from full amplitude straight to zero.
      const int frames = 1000, fade = 240;
      std::vector<float> l((std::size_t)frames, 1.0f), r((std::size_t)frames, 1.0f);

      StemRender::fadeEdges(l.data(), r.data(), frames, fade, true, true);

      expectWithinAbsoluteError(l[0], 0.0f, 0.02f, "must start from silence");
      expectWithinAbsoluteError(l[(std::size_t)frames - 1], 0.0f, 0.02f,
                                "must end in silence");
      expectWithinAbsoluteError(l[(std::size_t)frames / 2], 1.0f, 1.0e-6f,
                                "the middle must be untouched");

      double worst = 0.0;
      for (int i = 1; i < frames; ++i)
        worst = std::max(worst, (double)std::abs(l[(std::size_t)i] -
                                                 l[(std::size_t)(i - 1)]));
      expect(worst < 0.05, "largest step was " + juce::String(worst, 4) +
                               " -- that is still a click");
    }

    beginTest("fading only one end leaves the other alone");
    {
      const int frames = 1000, fade = 240;
      std::vector<float> l((std::size_t)frames, 1.0f), r((std::size_t)frames, 1.0f);
      StemRender::fadeEdges(l.data(), r.data(), frames, fade, false, true);
      expectWithinAbsoluteError(l[0], 1.0f, 1.0e-6f,
                                "the start joins audio and must not be faded");
      expectWithinAbsoluteError(l[(std::size_t)frames - 1], 0.0f, 0.02f);
    }

    beginTest("a fade longer than the interval cannot overrun or overlap");
    {
      const int frames = 10;
      std::vector<float> l((std::size_t)frames, 1.0f), r((std::size_t)frames, 1.0f);
      StemRender::fadeEdges(l.data(), r.data(), frames, 10000, true, true);
      expectEquals((int)l.size(), frames);
      for (int i = 0; i < frames; ++i)
        expect(l[(std::size_t)i] >= 0.0f && l[(std::size_t)i] <= 1.0f,
               "gain left its range");
    }

    beginTest("degenerate input is ignored rather than trusted");
    {
      std::vector<float> l(8, 0.0f), r(8, 0.0f);
      auto clip = makeClip(8, 2, 1.0f, 1.0f);

      StemRender::fadeEdges(nullptr, r.data(), 8, 4, true, true);
      StemRender::fadeEdges(l.data(), r.data(), 0, 4, true, true);
      StemRender::fadeEdges(l.data(), r.data(), 8, 0, true, true);
      StemRender::placeClip(nullptr, 8, 2, 48000.0, l.data(), r.data(), 8, 48000.0);
      StemRender::placeClip(clip.data(), 0, 2, 48000.0, l.data(), r.data(), 8, 48000.0);
      StemRender::placeClip(clip.data(), 8, 0, 48000.0, l.data(), r.data(), 8, 48000.0);
      StemRender::placeClip(clip.data(), 8, 2, 48000.0, l.data(), r.data(), 0, 48000.0);
      StemRender::placeClip(clip.data(), 8, 2, 48000.0, l.data(), r.data(), 8, 0.0);

      for (int i = 0; i < 8; ++i)
        expectEquals(l[(std::size_t)i], 0.0f, "nothing should have been written");
    }
  }
};

static StemRenderTests stemRenderTests;

} // namespace
