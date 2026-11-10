#include <JuceHeader.h>

#include "HandoffQueue.h"

#include <atomic>

namespace {

struct Rec {
  int seq = 0;
  int length = 0;
};

class HandoffQueueTests : public juce::UnitTest {
public:
  HandoffQueueTests() : juce::UnitTest("HandoffQueue", "HandoffQueue") {}

  void runTest() override {
    beginTest("records come out whole and in order");
    {
      HandoffQueue<Rec, 4> q;
      expect(q.push({.seq = 1, .length = 10}));
      expect(q.push({.seq = 2, .length = 20}));
      juce::Array<int> seen;
      const int n = q.drain([&](const Rec &r) {
        seen.add(r.seq);
        expectEquals(r.length, r.seq * 10);
      });
      expectEquals(n, 2);
      expectEquals(seen[0], 1);
      expectEquals(seen[1], 2);
      expectEquals(q.drain([](const Rec &) {}), 0, "drained means empty");
    }

    beginTest("it holds exactly its capacity, and counts what it drops");
    {
      HandoffQueue<Rec, 4> q;
      for (int i = 0; i < 4; ++i)
        expect(q.push({.seq = i, .length = 0}));
      expect(!q.push({.seq = 99, .length = 0}), "full");
      expectEquals(q.dropped(), 1);
      int last = -1;
      q.drain([&](const Rec &r) { last = r.seq; });
      expectEquals(last, 3, "the dropped record is the new one, not an old");
      expect(q.push({.seq = 5, .length = 0}), "room again once drained");
    }

    beginTest("wraps round many times without losing order");
    {
      HandoffQueue<Rec, 3> q;
      int next = 0;
      int expected = 0;
      bool ordered = true;
      for (int round = 0; round < 100; ++round) {
        for (int k = 0; k < 1 + round % 3; ++k)
          q.push({.seq = next++, .length = 0});
        q.drain([&](const Rec &r) { ordered &= r.seq == expected++; });
      }
      expect(ordered);
      expectEquals(expected, next);
    }

    // The shape it is used in: one thread pushing at boundaries, another
    // draining on a timer. Run under TSan, this is the test that says the
    // record is published with the index rather than after it.
    beginTest("a producer and a consumer on two threads lose nothing");
    {
      constexpr int kCount = 20000;
      HandoffQueue<Rec, 64> q;
      std::atomic<bool> finished{false};
      struct Producer : juce::Thread {
        HandoffQueue<Rec, 64> &q;
        std::atomic<bool> &finished;
        Producer(HandoffQueue<Rec, 64> &queue, std::atomic<bool> &f)
            : juce::Thread("handoff producer"), q(queue), finished(f) {}
        void run() override {
          for (int i = 0; i < kCount; ++i)
            while (!q.push({.seq = i, .length = i * 3}))
              juce::Thread::yield();
          finished.store(true);
        }
      } producer(q, finished);

      int expected = 0;
      bool intact = true;
      auto consume = [&](const Rec &r) {
        intact &= r.seq == expected && r.length == expected * 3;
        ++expected;
      };
      producer.startThread();
      while (!finished.load())
        q.drain(consume);
      producer.stopThread(2000);
      q.drain(consume);
      expect(intact, "a record arrived torn or out of order");
      expectEquals(expected, kCount);
    }
  }
};

static HandoffQueueTests handoffQueueTests;

} // namespace
