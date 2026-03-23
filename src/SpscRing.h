#pragma once

#include <array>
#include <atomic>
#include <cstddef>

// A single-producer, single-consumer ring of pointers.
//
// The primitive the RX path needs to stop taking a lock on the audio thread.
// Two rings per stream carry ownership in a circle without either side ever
// waiting for the other:
//
//   ready:   network thread -> audio thread   ("here is a decoded interval")
//   retired: audio thread   -> network thread ("done with this one, free it")
//
// The retire direction is the load-bearing half. The audio thread must never
// drop the last reference to a DecodedInterval, because that frees a
// multi-megabyte buffer inside the callback. Handing the pointer back and
// letting the owning thread release it keeps deallocation off the audio thread
// entirely.
//
// Deliberately holds raw pointers, not shared_ptr: copying a shared_ptr touches
// an atomic refcount, and destroying one can free. Ownership lives in the
// producer's own container for the whole time a pointer is in flight.
//
// Exactly one thread may push and exactly one may pop. With more of either the
// index arithmetic is wrong, and nothing here will tell you.

template <typename T, int Capacity>
class SpscRing {
public:
  static_assert(Capacity > 1, "a one-slot ring is always either full or empty");

  // Producer side. False when full, which the caller must handle -- dropping
  // is usually right on an audio path, and is always better than blocking.
  bool push(T *value) {
    const int w = writeIndex.load(std::memory_order_relaxed);
    const int next = advance(w);
    // acquire pairs with the consumer's release, so a slot freed by pop() is
    // visible here before it is reused.
    if (next == readIndex.load(std::memory_order_acquire))
      return false; // full

    items[(std::size_t)w] = value;
    // release publishes the slot's contents along with the new index.
    writeIndex.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side. Null when empty.
  T *pop() {
    const int r = readIndex.load(std::memory_order_relaxed);
    if (r == writeIndex.load(std::memory_order_acquire))
      return nullptr; // empty

    T *value = items[(std::size_t)r];
    items[(std::size_t)r] = nullptr;
    readIndex.store(advance(r), std::memory_order_release);
    return value;
  }

  // Consumer side, and only meaningful there: the producer may fill it the
  // instant after this returns.
  bool isEmpty() const {
    return readIndex.load(std::memory_order_acquire) ==
           writeIndex.load(std::memory_order_acquire);
  }

  // Approximate, for diagnostics only.
  int sizeApprox() const {
    const int w = writeIndex.load(std::memory_order_acquire);
    const int r = readIndex.load(std::memory_order_acquire);
    return w >= r ? w - r : Capacity + 1 - r + w;
  }

  static constexpr int capacity() { return Capacity; }

private:
  // One slot is always left empty so full and empty are distinguishable
  // without a separate count, which would need its own synchronisation.
  static constexpr int Slots = Capacity + 1;
  static int advance(int i) { return i + 1 == Slots ? 0 : i + 1; }

  std::array<T *, (std::size_t)Slots> items{};
  std::atomic<int> writeIndex{0};
  std::atomic<int> readIndex{0};
};
