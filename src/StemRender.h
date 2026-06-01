#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <vector>

// Laying one decoded clip into one interval of a stem.
//
// Small, but it is the part of the stem converter that can silently ruin the
// output. Two rules, both of which produce a plausible-looking file when got
// wrong:
//
//  - Every interval occupies exactly its nominal length, however long the clip
//    decoded to. Vorbis has codec delay, so a clip is rarely the exact nominal
//    length; letting stems inherit those differences makes each one drift
//    against the others by a little more every interval, and the result plays
//    fine on its own and is unusable in a DAW.
//  - Interleaved source, planar stereo destination, resampled when the clip
//    declares a different rate from the output -- players in one room are not
//    all at 48 kHz.
//
// Lives here rather than in the tool's main so it can be unit-tested: an
// executable's own .cpp cannot be compiled into the test target.

namespace StemRender {

// Writes into `left`/`right`, which the caller has already cleared to `frames`
// of silence. A clip shorter than the interval therefore leaves the remainder
// silent, and a longer one is truncated.
//
// `numChannels` is the clip's own; a mono clip feeds both output sides.
// Returns how many frames were actually written, which the caller needs in
// order to know whether the interval was filled or padded.
inline int placeClip(const float *interleaved, int numFrames, int numChannels,
                     double srcRate, float *left, float *right, int frames,
                     double outRate) {
  if (interleaved == nullptr || left == nullptr || right == nullptr)
    return 0;
  if (numFrames <= 0 || numChannels <= 0 || frames <= 0 || outRate <= 0.0)
    return 0;

  std::vector<float> srcL((std::size_t)numFrames);
  std::vector<float> srcR((std::size_t)numFrames);
  for (int i = 0; i < numFrames; ++i) {
    srcL[(std::size_t)i] = interleaved[(std::size_t)(i * numChannels)];
    srcR[(std::size_t)i] =
        numChannels > 1 ? interleaved[(std::size_t)(i * numChannels + 1)]
                        : srcL[(std::size_t)i];
  }

  const bool needsResample = srcRate > 0.0 && srcRate != outRate;
  if (!needsResample) {
    const int n = juce::jmin(numFrames, frames);
    std::copy(srcL.begin(), srcL.begin() + n, left);
    std::copy(srcR.begin(), srcR.begin() + n, right);
    return n;
  }

  // The same interpolator the live decode path uses for a remote player at a
  // different rate (NinjamClient.cpp).
  const double ratio = srcRate / outRate;
  const int available = (int)((double)numFrames / ratio);
  const int n = juce::jmin(frames, available);
  if (n <= 0)
    return 0;

  juce::LagrangeInterpolator interpL, interpR;
  interpL.process(ratio, srcL.data(), left, n);
  interpR.process(ratio, srcR.data(), right, n);
  return n;
}

// Fades the first and/or last `fadeSamples` of an interval.
//
// Where consecutive intervals both carry audio, the waveform continues across
// the join naturally and must be left alone -- fading every edge would put an
// audible amplitude wobble at the interval rate through the whole stem.
//
// A fade is needed only where audio meets silence: a player who sat an interval
// out, or a clip that came up short and was padded. Those joins are a step in
// the waveform, which is a click. Playback has the same problem and solves it
// differently, by crossfading the previous interval's un-played tail over the
// new one; here nothing is dropped, so there is nothing to crossfade with and a
// fade to zero is the whole fix.
inline void fadeEdges(float *left, float *right, int frames, int fadeSamples,
                      bool fadeIn, bool fadeOut) {
  if (left == nullptr || right == nullptr || frames <= 0 || fadeSamples <= 0)
    return;
  const int n = juce::jmin(fadeSamples, frames / 2);
  if (n <= 0)
    return;

  for (int i = 0; i < n; ++i) {
    const float t = (float)(i + 1) / (float)n;
    // Smoothstep, matching GainRamp: continuous in value and slope at both
    // ends, which is what actually removes the click.
    const float g = t * t * (3.0f - 2.0f * t);
    if (fadeIn) {
      left[i] *= g;
      right[i] *= g;
    }
    if (fadeOut) {
      left[frames - 1 - i] *= g;
      right[frames - 1 - i] *= g;
    }
  }
}

} // namespace StemRender
