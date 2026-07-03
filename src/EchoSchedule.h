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

// What the handoff costs, and therefore what the shallowest tap is.
//
// An interval's audio is complete at the boundary that ends it, and the audio
// thread stores it into the ring as it goes -- so at that boundary the entry
// is already there to be handed over, and the swap at the same boundary can
// start playing it immediately. One interval is therefore the shortest
// possible echo, and it is the useful one: what you just played comes back at
// the top of the next interval, which is what a looper does and what the room
// would have done with you at a delay of one.
//
// This was two while the store went through the message thread: an entry
// posted at a boundary could not be seen by the swap that had already
// happened, so the earliest consumer was the boundary after. Moving the store
// onto the audio thread is what bought the interval back, and it is the only
// reason the picker can start at 1.
static constexpr int kHandoffIntervals = 1;
static constexpr int kMinDelayIntervals = kHandoffIntervals;

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

// Which interval a tap is heard during, given the interval it was played in.
// The definition the UI promises: "4 intervals" means you hear it four
// intervals after you played it, exactly as another player in the room would
// have heard it one interval later.
inline long long heardDuring(long long playedDuring, int delay) {
  return playedDuring + delay;
}

// Which stored slot a tap should play during interval `now`, or -1 while the
// pipeline is still filling -- for the first `delay` intervals there is simply
// nothing that old to play, and silence is the honest answer.
inline int readSlotFor(long long now, int delay, int depth) {
  if (depth < 1 || delay < 1 || now - delay < 0)
    return -1;
  return writeSlotFor(now - delay, depth);
}

// Which stored slot to hand a tap when interval `pushInterval` closes, or -1
// when there is nothing honest to hand it.
//
// The entry handed here is consumed by the swap at this same boundary, which
// begins interval `pushInterval + kHandoffIntervals`. That is the interval the
// tap will be heard during, so the entry it needs is the one `delay` earlier
// than that. Deriving it from the delay the user asked for -- rather than
// counting back from the close -- is the whole difference between a tap that
// means what its label says and one that is deeper than it claims.
//
// Three ways to have nothing to hand over, all of them silence: a delay
// shallower than the handoff can deliver, a session too young to have an
// interval that old, and a source interval that has not been played yet.
inline int readSlotForPush(long long pushInterval, int delay, int depth) {
  if (depth < 1 || delay < kMinDelayIntervals)
    return -1;
  const long long source = pushInterval + kHandoffIntervals - delay;
  if (source < 0 || source > pushInterval)
    return -1;
  return writeSlotFor(source, depth);
}

// Whether the write at the push for `pushInterval` would land on an entry a
// tap could still be reading. False is the only acceptable answer; it is a
// test, not a runtime check, because the depth is chosen to make it impossible.
inline bool wouldOverwriteLiveEntry(long long pushInterval, int delay,
                                    int depth) {
  // When interval `pushInterval` closes, the audio thread starts filling the
  // entry for the next one. Live at that moment: the entry just handed over,
  // which is about to play, and the one handed at the previous boundary, which
  // may still be sounding its fade tail.
  const int writeSlot = writeSlotFor(pushInterval + 1, depth);
  for (int back = 0; back <= 1; ++back) {
    const long long playing = pushInterval + kHandoffIntervals - delay - back;
    if (playing < 0)
      continue;
    if (writeSlotFor(playing, depth) == writeSlot)
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
