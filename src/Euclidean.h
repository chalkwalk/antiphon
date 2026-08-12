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

// How long the pattern takes to come round: steps / gcd(steps, pulses).
//
// This is the property that decides whether a figure moves or locks, and it is
// worth naming because it is easy to choose a pulse count by density alone and
// get a very different rhythm than intended. E(8,32) has period 4 -- eight
// repetitions of `x...` inside the bar, a metronome. E(9,32) has period 32 and
// takes the whole bar to return.
//
// NEITHER is better in general. A kick usually wants a short period: repetition
// is what makes it a pulse you can rely on. A bass line usually wants a long
// one, because a bass that repeats every four steps is not playing against the
// kick, it is doubling it. Choose deliberately.
inline int patternPeriod(int steps, int pulses) {
  if (steps <= 0)
    return 0;
  if (pulses <= 0 || pulses >= steps)
    return 1;

  int a = steps, b = pulses;
  while (b != 0) {
    const int t = b;
    b = a % b;
    a = t;
  }
  return steps / a;
}

// The pulse count nearest `wanted` whose pattern spans all `steps` -- that is,
// coprime with them.
//
// For a caller that has decided it wants movement rather than lock. Ties go to
// the higher count when `preferAbove`, which is how a seed varies density
// without ever landing back on a repeating figure.
//
// Always terminates for steps > 1: `steps - 1` and 1 are both coprime with
// `steps`, so the outward search cannot run out of candidates.
inline int nearestCoprimePulses(int steps, int wanted, bool preferAbove) {
  if (steps <= 1)
    return steps;
  if (wanted < 1)
    wanted = 1;
  if (wanted > steps - 1)
    wanted = steps - 1;

  for (int distance = 0; distance < steps; ++distance)
    for (int pass = 0; pass < 2; ++pass) {
      const bool high = preferAbove ? (pass == 0) : (pass == 1);
      const int candidate = high ? wanted + distance : wanted - distance;
      if (candidate < 1 || candidate > steps - 1)
        continue;
      if (patternPeriod(steps, candidate) == steps)
        return candidate;
      if (distance == 0)
        break; // both passes are the same candidate
    }
  return wanted;
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
