#pragma once

// The arithmetic behind the practice echo: which stored interval each tap
// should be playing, and when a stored interval is safe to overwrite.
//
// Echo plays your own audio back N intervals late, as a virtual player, so you
// can practise the Ninjam form alone -- a duet with your past self. Several
// taps at different delays make a canon, and they share ONE history ring: the
// memory is set by the deepest delay, not by the sum of them.
//
// Sharing the ring is what makes this subtle. The same stored interval is read
// by the 4-interval tap now, the 6-interval tap two intervals later, and the
// 8-interval tap two after that. So an entry cannot be reused until the
// deepest tap has finished with it, and "finished" includes the crossfade tail
// that swapIntervalBuffers keeps alive into the following interval. Hence the
// slack below.
//
// Pure and JUCE-free: this is index arithmetic whose failure mode is playing
// the wrong interval, or worse, writing over one still being read. That has to
// be testable without a socket, a device or a UI.

namespace EchoSchedule {

// How many stored intervals a set of taps needs.
//
// One spare beyond the deepest delay would be enough to stop the writer
// catching the reader in the same interval; the second covers the fade tail
// that the previous interval keeps sounding into the current one.
inline int historyDepth(int deepestDelayIntervals) {
  if (deepestDelayIntervals < 1)
    deepestDelayIntervals = 1;
  return deepestDelayIntervals + 2;
}

// Where interval `n` is stored.
inline int writeSlotFor(long long intervalIndex, int depth) {
  if (depth < 1)
    return 0;
  return (int)(((intervalIndex % depth) + depth) % depth);
}

// Which stored slot a tap should play during interval `now`, or -1 while the
// pipeline is still filling -- for the first `delay` intervals there is simply
// nothing that old to play, and silence is the honest answer.
inline int readSlotFor(long long now, int delay, int depth) {
  if (depth < 1 || delay < 1 || now - delay < 0)
    return -1;
  return writeSlotFor(now - delay, depth);
}

// Whether writing interval `now` would land on an entry a tap could still be
// reading. False is the only acceptable answer; it is a test, not a runtime
// check, because the depth is chosen to make it impossible.
inline bool wouldOverwriteLiveEntry(long long now, int delay, int depth) {
  const int writeSlot = writeSlotFor(now, depth);
  // The tap is reading the entry for `now - delay`, and the one before it may
  // still be sounding its fade tail.
  for (int back = 0; back <= 1; ++back) {
    const long long reading = now - delay - back;
    if (reading < 0)
      continue;
    if (writeSlotFor(reading, depth) == writeSlot)
      return true;
  }
  return false;
}

// The deepest delay that fits a memory budget.
//
// One interval of stereo float is not small -- eight seconds at 48 kHz is about
// 3 MB, and a slow tempo with a high BPI is four times that. The ring is sized
// by the deepest delay whether or not that tap is audible, because a muted tap
// has to be able to unmute instantly rather than waiting for the history to
// refill.
inline int maxDelayForBudget(long long budgetBytes, int intervalSamples,
                             int numChannels) {
  const long long perInterval =
      (long long)intervalSamples * numChannels * (long long)sizeof(float);
  if (perInterval <= 0 || budgetBytes <= 0)
    return 0;
  // Depth is delay + 2, so solve for the delay the budget allows.
  const long long entries = budgetBytes / perInterval;
  const long long delay = entries - 2;
  return delay < 0 ? 0 : (int)delay;
}

} // namespace EchoSchedule
