#pragma once

#include <algorithm>
#include <vector>

// Euclidean rhythms: distribute `pulses` onsets as evenly as possible over
// `length` steps.
//
// One integer buys a pattern that is already idiomatic rather than mechanical:
// E(3,8) is the tresillo, E(5,8) the cinquillo. That is why the bots use it --
// a drum part worth playing along to, from a seed, with no pattern data to
// ship or maintain.
//
// Lifted with only cosmetic changes from a sibling project of this one
// (chalkwalk/seq_play src/core/Euclidean.h), where it arrived at the same shape
// this codebase wants: header-only, JUCE-free, no allocation in the hot path.
//
// The Bresenham formulation rather than Bjorklund's recursive one: onset at
// step i iff (i * pulses) % length < pulses. Same patterns, and it gives an
// O(1) membership test as well as the vector form.

namespace Euclidean {

// The pattern as a vector, for callers that want to look at all of it.
// `offset` rotates: positive forward (right), negative backward.
inline std::vector<bool> pattern(int length, int pulses, int offset = 0) {
  if (length <= 0)
    return {};
  if (pulses < 0)
    pulses = 0;
  if (pulses > length)
    pulses = length;

  std::vector<bool> result(static_cast<std::size_t>(length), false);
  if (pulses == 0)
    return result;

  for (int i = 0; i < length; ++i)
    result[static_cast<std::size_t>(i)] = ((i * pulses) % length) < pulses;

  int rot = offset % length;
  if (rot < 0)
    rot += length;
  if (rot != 0) {
    // std::rotate shifts LEFT by k, so a right shift of `rot` is a left shift
    // of length - rot.
    const int leftShift = length - rot;
    std::rotate(result.begin(),
                result.begin() + static_cast<std::ptrdiff_t>(leftShift),
                result.end());
  }
  return result;
}

// Whether step `pos` is an onset, without building the pattern. Mirrors
// `pattern` exactly, including the rotation. No allocation.
inline bool hit(int pos, int length, int pulses, int offset = 0) noexcept {
  if (length <= 0 || pulses <= 0)
    return false;
  if (pulses >= length)
    return true;
  int rot = offset % length;
  if (rot < 0)
    rot += length;
  const int pmod = ((pos % length) + length) % length;
  const int q = (pmod - rot + length) % length;
  return (q * pulses) % length < pulses;
}

// Velocities for each step: 0 rest, kAccentedVelocity or kOnsetVelocity for an
// onset. The accented onsets are themselves distributed Euclidean-wise over the
// onsets, so accents fall in a pattern rather than on a fixed beat.
inline constexpr int kOnsetVelocity = 64;
inline constexpr int kAccentedVelocity = 100;

inline std::vector<int> accents(int length, int pulses, int offset,
                                int numAccents) {
  const auto p = pattern(length, pulses, offset);

  std::vector<int> onsetIdx;
  onsetIdx.reserve(static_cast<std::size_t>(std::max(0, pulses)));
  for (int i = 0; i < length; ++i)
    if (p[static_cast<std::size_t>(i)])
      onsetIdx.push_back(i);

  std::vector<int> result(static_cast<std::size_t>(std::max(0, length)), 0);
  if (onsetIdx.empty())
    return result;

  if (numAccents <= 0) {
    for (int idx : onsetIdx)
      result[static_cast<std::size_t>(idx)] = kOnsetVelocity;
    return result;
  }

  const int k = static_cast<int>(onsetIdx.size());
  if (numAccents > k)
    numAccents = k;
  const auto accentPat = pattern(k, numAccents, 0);

  for (int j = 0; j < k; ++j) {
    const int step = onsetIdx[static_cast<std::size_t>(j)];
    result[static_cast<std::size_t>(step)] =
        accentPat[static_cast<std::size_t>(j)] ? kAccentedVelocity
                                               : kOnsetVelocity;
  }
  return result;
}

} // namespace Euclidean
