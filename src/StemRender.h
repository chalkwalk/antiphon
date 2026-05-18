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
inline void placeClip(const float *interleaved, int numFrames, int numChannels,
                      double srcRate, float *left, float *right, int frames,
                      double outRate) {
  if (interleaved == nullptr || left == nullptr || right == nullptr)
    return;
  if (numFrames <= 0 || numChannels <= 0 || frames <= 0 || outRate <= 0.0)
    return;

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
    return;
  }

  // The same interpolator the live decode path uses for a remote player at a
  // different rate (NinjamClient.cpp).
  const double ratio = srcRate / outRate;
  const int available = (int)((double)numFrames / ratio);
  const int n = juce::jmin(frames, available);
  if (n <= 0)
    return;

  juce::LagrangeInterpolator interpL, interpR;
  interpL.process(ratio, srcL.data(), left, n);
  interpR.process(ratio, srcR.data(), right, n);
}

} // namespace StemRender
