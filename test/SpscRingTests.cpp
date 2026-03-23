#include <JuceHeader.h>

#include "SpscRing.h"

#include <thread>
#include <vector>

namespace {

class SpscRingTests : public juce::UnitTest {
public:
  SpscRingTests() : juce::UnitTest("SpscRing", "SpscRing") {}

  void runTest() override {
    beginTest("what goes in comes out, in order");
    {
      SpscRing<int, 8> ring;
      int values[5] = {1, 2, 3, 4, 5};
      for (auto &v : values)
        expect(ring.push(&v));
      for (auto &v : values)
        expectEquals(*ring.pop(), v);
      expect(ring.pop() == nullptr, "and then it is empty");
    }

    beginTest("an empty ring returns null rather than blocking");
    {
      SpscRing<int, 4> ring;
      expect(ring.isEmpty());
      expect(ring.pop() == nullptr);
    }

    beginTest("a full ring refuses rather than blocking");
    {
      // The audio path has to be able to keep going when the ring is full, so
      // push reports failure instead of waiting for room.
      SpscRing<int, 4> ring;
      int v = 7;
      for (int i = 0; i < 4; ++i)
        expect(ring.push(&v), "capacity 4 must accept 4");
      expect(!ring.push(&v), "and refuse the fifth");
      expectEquals(ring.sizeApprox(), 4);
    }

    beginTest("capacity really is the stated capacity");
    {
      // The spare slot that distinguishes full from empty is an implementation
      // detail and must not cost the caller an entry.
      SpscRing<int, 3> ring;
      int v = 0;
      expectEquals(ring.capacity(), 3);
      for (int i = 0; i < 3; ++i)
        expect(ring.push(&v));
      expect(!ring.push(&v));
    }

    beginTest("it wraps");
    {
      SpscRing<int, 4> ring;
      std::vector<int> vals(64);
      for (int i = 0; i < 64; ++i) vals[(size_t)i] = i;
      // Far more traffic than the ring holds, one in one out, so the indices
      // wrap many times.
      for (int i = 0; i < 64; ++i) {
        expect(ring.push(&vals[(size_t)i]), "push " + juce::String(i));
        expectEquals(*ring.pop(), i);
      }
      expect(ring.isEmpty());
    }

    beginTest("popping frees the slot for reuse");
    {
      SpscRing<int, 2> ring;
      int a = 1, b = 2, c = 3;
      expect(ring.push(&a));
      expect(ring.push(&b));
      expect(!ring.push(&c), "full");
      expectEquals(*ring.pop(), 1);
      expect(ring.push(&c), "a pop must make room");
      expectEquals(*ring.pop(), 2);
      expectEquals(*ring.pop(), 3);
    }

    beginTest("a producer and a consumer on separate threads lose nothing");
    {
      // The property that matters: under real concurrency every pointer that is
      // accepted comes out exactly once, in order. Run under TSan to also check
      // the memory ordering -- this test passing under a normal build says
      // nothing about that.
      constexpr int kCount = 20000;
      SpscRing<int, 64> ring;
      std::vector<int> source((size_t)kCount);
      for (int i = 0; i < kCount; ++i) source[(size_t)i] = i;

      std::atomic<bool> producerDone{false};
      std::vector<int> received;
      received.reserve((size_t)kCount);

      std::thread producer([&] {
        for (int i = 0; i < kCount;) {
          if (ring.push(&source[(size_t)i]))
            ++i;            // only advance when it was accepted
          else
            std::this_thread::yield();
        }
        producerDone.store(true);
      });

      while (!producerDone.load() || !ring.isEmpty()) {
        if (int *v = ring.pop())
          received.push_back(*v);
      }
      producer.join();

      expectEquals((int)received.size(), kCount, "nothing was dropped");
      bool ordered = true;
      for (int i = 0; i < (int)received.size(); ++i)
        if (received[(size_t)i] != i) { ordered = false; break; }
      expect(ordered, "and nothing was reordered or duplicated");
    }
  }
};

static SpscRingTests spscRingTests;

} // namespace
