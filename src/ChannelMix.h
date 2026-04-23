#pragma once

#include <cmath>

// How a local channel's input becomes the pair of samples that get monitored,
// metered and transmitted.
//
// Split out of PluginProcessor because that file cannot be compiled into the
// test target (it needs the JucePlugin_* defines), and because the same three
// rules were previously written out three times -- in the capture path, the
// monitor mix and the peak meters -- and had drifted apart. "Mono" summed in
// none of them: it selected the left channel and silently discarded the right
// half of a stereo source, while the meters ignored the flag entirely and went
// on showing an independent stereo pair.

namespace ChannelMix {

struct Frame {
  float left = 0.0f;
  float right = 0.0f;
};

// Volume and pan, applied to both the monitor mix and the transmitted audio.
// Mute and solo are deliberately absent: they are monitor-only and must never
// change what other players hear.
inline Frame panGains(float volume, float pan) {
  return {volume * (pan <= 0.0f ? 1.0f : 1.0f - pan),
          volume * (pan >= 0.0f ? 1.0f : 1.0f + pan)};
}

// One frame of the channel's source, before gain.
//
// `srcR` is null when the assigned bus is itself mono, in which case the single
// channel feeds both sides. When `mono` is set on a stereo bus the two sides are
// summed and halved -- averaging rather than adding keeps a correlated stereo
// source at its original level instead of doubling it.
inline Frame sourceFrame(const float *srcL, const float *srcR, bool mono,
                         int index) {
  if (srcL == nullptr) return {};
  const float l = srcL[index];
  if (srcR == nullptr) return {l, l};
  if (mono) {
    const float summed = 0.5f * (l + srcR[index]);
    return {summed, summed};
  }
  return {l, srcR[index]};
}

// Peak of each side over `count` frames, measured on the post-mono signal so a
// mono channel reports the level it actually transmits. `gains` scales the
// result, so the meter shows what is heard.
inline Frame peaks(const float *srcL, const float *srcR, bool mono, int start,
                   int count, Frame gains) {
  Frame p;
  for (int i = 0; i < count; ++i) {
    const Frame f = sourceFrame(srcL, srcR, mono, start + i);
    p.left = std::max(p.left, std::abs(f.left));
    p.right = std::max(p.right, std::abs(f.right));
  }
  p.left *= gains.left;
  p.right *= gains.right;
  return p;
}

// Writes `count` gained frames into two destination pointers. Used for the
// transmit ring buffer, which is written in up to two segments.
// Writes the channel's contribution to the transmit ring.
//
// `transmitting` gates the *audio*, not the capture. When it is false this
// writes silence rather than writing nothing, and that distinction is the whole
// point: an interval must keep its exact length, so the only way to represent
// "I was not transmitting for this stretch" is samples of zero.
//
// Transmit used to be read once per interval, at the boundary, while capture
// ran unconditionally. The flag therefore decided all-or-nothing for a whole
// interval: switch it off, play something private, switch it back on before the
// boundary, and every bit of it went out. Gating here makes the control mean
// what it says at the moment you use it.
//
// Deliberately independent of mute and solo, which are monitor-only: what you
// hear and what you send are separate questions in both directions.
inline void write(float *dstL, float *dstR, const float *srcL,
                  const float *srcR, bool mono, int srcStart, int count,
                  Frame gains, bool transmitting) {
  if (!transmitting) {
    for (int i = 0; i < count; ++i) {
      if (dstL != nullptr) dstL[i] = 0.0f;
      if (dstR != nullptr) dstR[i] = 0.0f;
    }
    return;
  }

  for (int i = 0; i < count; ++i) {
    const Frame f = sourceFrame(srcL, srcR, mono, srcStart + i);
    if (dstL != nullptr) dstL[i] = f.left * gains.left;
    if (dstR != nullptr) dstR[i] = f.right * gains.right;
  }
}

// Adds `count` gained frames into two destination pointers, for the monitor mix
// where several channels sum into the same output bus.
inline void addInto(float *dstL, float *dstR, const float *srcL,
                    const float *srcR, bool mono, int count, Frame gains) {
  for (int i = 0; i < count; ++i) {
    const Frame f = sourceFrame(srcL, srcR, mono, i);
    if (dstL != nullptr) dstL[i] += f.left * gains.left;
    if (dstR != nullptr) dstR[i] += f.right * gains.right;
  }
}

} // namespace ChannelMix
