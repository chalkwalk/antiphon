#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <type_traits>

// A fixed queue of small records, pushed by the audio thread and drained by
// one other thread. It replaces the callAsync each interval boundary used to
// post per channel: that allocated a message, took the message queue's lock and
// on Linux wrote to a socket, from inside the audio callback (PRINCIPLES 7).
// Measured on the development machine, 2026-11-09: callAsync took a median
// 2.25 us and up to 43 us per call from a high-priority thread; a push here
// takes 0.11 us and takes no lock (ROADMAP, Lock-free TX handoff).
//
// Records are copied in and out by value, so a Record must be trivially
// copyable. One producer and one consumer, as juce::AbstractFifo requires.
template <typename Record, int Capacity> class HandoffQueue {
public:
  static_assert(std::is_trivially_copyable_v<Record>);
  static_assert(Capacity > 1);

  // Audio thread. False when full, and the record is dropped and counted:
  // blocking is never an option here, and the queue only fills if its consumer
  // has stalled for several intervals.
  bool push(const Record &r) {
    int s1 = 0;
    int n1 = 0;
    int s2 = 0;
    int n2 = 0;
    fifo.prepareToWrite(1, s1, n1, s2, n2);
    if (n1 + n2 < 1) {
      droppedCount.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    slots[(std::size_t)(n1 > 0 ? s1 : s2)] = r;
    fifo.finishedWrite(1);
    return true;
  }

  // Consumer thread. Calls fn for every record queued so far, oldest first,
  // and returns how many.
  template <typename Fn> int drain(Fn &&fn) {
    int taken = 0;
    for (;;) {
      int s1 = 0;
      int n1 = 0;
      int s2 = 0;
      int n2 = 0;
      fifo.prepareToRead(1, s1, n1, s2, n2);
      if (n1 + n2 < 1)
        return taken;
      const Record r = slots[(std::size_t)(n1 > 0 ? s1 : s2)];
      fifo.finishedRead(1);
      fn(r);
      ++taken;
    }
  }

  int dropped() const { return droppedCount.load(std::memory_order_relaxed); }

private:
  // AbstractFifo keeps one slot empty to tell full from empty.
  juce::AbstractFifo fifo{Capacity + 1};
  std::array<Record, (std::size_t)Capacity + 1> slots{};
  std::atomic<int> droppedCount{0};
};
