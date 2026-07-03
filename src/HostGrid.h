#pragma once

#include "IntervalClock.h"

#include <cmath>
#include <vector>

// The interval grid, derived from the host's timeline instead of counted out
// from a sample cursor.
//
// `IntervalClock` free-runs: it is handed a block size and steps its own
// position forward, on an integer grid whose interval length is *truncated* to
// match njclient.cpp:806. In a jam that is exactly right and must not change --
// truncating identically is what keeps our boundaries aligned with every other
// client in the room (PRINCIPLES 9).
//
// Offline it is exactly wrong. Nothing is listening for parity, and two things
// pull the free-running grid away from the DAW's:
//
//   - Ninjam's tempo is an integer, the host's is not. A project at 128.5 bpm
//     against a grid at 128 slips a beat every couple of minutes.
//   - The truncation loses up to a sample per interval, and unlike in a jam
//     there is nothing else truncating the same way to stay level with.
//
// Neither is a large error and both accumulate without bound, which is the
// worst shape a timing error can have: right when you check it and wrong by
// the end of the take. Recording a practice session is the case that cares
// most, and it is the one that suffers most.
//
// So offline the host's PPQ position owns the grid. This is stateless by
// construction -- every answer is a pure function of the position the host
// reports for this block -- and that is the point. There is no accumulator to
// drift, tempo automation is followed for free, and a loop or a playhead jump
// lands the grid where the host put it rather than somewhere it counted its
// way to.
//
// Beats are quarter notes: Ninjam's beat is the host's, which is what makes
// `ppq` directly usable as a beat count.

namespace HostGrid {

// The tolerance for calling a floating-point PPQ position "exactly on a beat".
// A host computing ppq as a running sum lands a hair either side of the
// integer, and without this a beat fires twice or not at all at the seam.
// A microbeat is far below any audible placement error.
inline constexpr double kBeatEpsilon = 1.0e-9;

// Position within the interval, in beats: 0 .. bpi. Drives the phase bar.
inline double phaseBeatsAt(double ppq, int bpi) {
  if (bpi < 1)
    return 0.0;
  const double wrapped = std::fmod(ppq, (double)bpi);
  return wrapped < 0.0 ? wrapped + (double)bpi : wrapped;
}

// Which interval the host's timeline is in. Negative positions (a playhead
// before zero) floor downwards, so the grid stays continuous across the
// origin rather than mirroring around it.
inline long long intervalIndexAt(double ppq, int bpi) {
  if (bpi < 1)
    return 0;
  return (long long)std::floor(ppq / (double)bpi);
}

// How far the block advances the beat counter.
inline double beatsPerSample(double bpm, double sampleRate) {
  if (bpm <= 0.0 || sampleRate <= 0.0)
    return 0.0;
  return bpm / (60.0 * sampleRate);
}

// Emits the block's events, in the same order and with the same meaning as
// IntervalClock::advance -- IntervalStart before Beat 0 at a boundary -- so
// everything downstream, including splitAtIntervalStarts and the metronome,
// works unchanged.
//
// A beat landing exactly on the first sample of the block belongs to this
// block, not the previous one; the half-open interval [ppqStart, ppqEnd) is
// what stops it being emitted twice at the seam.
inline void advance(double ppqStart, double beatsPerSampleValue, int bpi,
                    int numSamples, std::vector<IntervalClock::Event> &out) {
  if (numSamples <= 0 || bpi < 1 || beatsPerSampleValue <= 0.0)
    return;

  const double ppqEnd = ppqStart + (double)numSamples * beatsPerSampleValue;
  long long beat = (long long)std::ceil(ppqStart - kBeatEpsilon);

  // A host that reports a wild position must not spin: the loop is bounded by
  // the beats a block can possibly contain.
  for (; (double)beat < ppqEnd - kBeatEpsilon; ++beat) {
    int offset =
        (int)((((double)beat - ppqStart) / beatsPerSampleValue) + 0.5);
    if (offset < 0)
      offset = 0;
    if (offset > numSamples - 1)
      offset = numSamples - 1;

    const int beatInInterval = (int)(((beat % bpi) + bpi) % bpi);
    if (beatInInterval == 0)
      out.push_back({IntervalClock::Event::Type::IntervalStart, offset, 0});
    out.push_back({IntervalClock::Event::Type::Beat, offset, beatInInterval});
  }
}

// How long an interval is at this tempo, for sizing buffers. Rounded, not
// truncated: nothing downstream of it is talking to another client, and the
// exact boundary always comes from the host position rather than from here.
inline int intervalSamples(double bpm, int bpi, double sampleRate) {
  if (bpm <= 0.0 || bpi < 1 || sampleRate <= 0.0)
    return 0;
  return (int)std::llround((double)bpi * 60.0 / bpm * sampleRate);
}

} // namespace HostGrid
