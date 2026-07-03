#include <JuceHeader.h>

#include "HostGrid.h"
#include "IntervalClock.h"

#include <vector>

namespace {

using Event = IntervalClock::Event;

// Runs a whole stretch of timeline through the grid the way processBlock does:
// a PPQ position per block, advanced by the host's own tempo.
struct Run {
  std::vector<Event> events;
  std::vector<long long> intervalStartSamples;
  std::vector<long long> beatSamples;
};

Run runBlocks(double bpm, int bpi, double sampleRate, int blockSize,
              int numBlocks, double startPpq = 0.0) {
  Run r;
  const double bps = HostGrid::beatsPerSample(bpm, sampleRate);
  double ppq = startPpq;
  for (int b = 0; b < numBlocks; ++b) {
    std::vector<Event> block;
    HostGrid::advance(ppq, bps, bpi, blockSize, block);
    for (const auto &e : block) {
      const long long abs = (long long)b * blockSize + e.sampleOffset;
      if (e.type == Event::Type::IntervalStart)
        r.intervalStartSamples.push_back(abs);
      else
        r.beatSamples.push_back(abs);
      r.events.push_back(e);
    }
    ppq += (double)blockSize * bps;
  }
  return r;
}

class HostGridTests : public juce::UnitTest {
public:
  HostGridTests() : juce::UnitTest("HostGrid", "HostGrid") {}

  void runTest() override {
    beginTest("the grid does not drift away from the host over a long take");
    {
      // The defect this module exists for. A free-running integer grid loses a
      // little at every interval and never gets it back, so a practice session
      // starts in time and ends out of it -- and a recording of one is useless
      // in exactly the way that is hardest to notice while playing.
      //
      // Half an hour at a fractional tempo, which is where the old grid was
      // worst: Ninjam's tempo is an integer and the host's is not.
      const double bpm = 128.5;
      const int bpi = 16, blockSize = 512;
      const double sr = 48000.0;
      const int blocks = (int)(30 * 60 * sr / blockSize);

      const auto r = runBlocks(bpm, bpi, sr, blockSize, blocks);
      expect(!r.intervalStartSamples.empty(), "something must have fired");

      // Where the host says the last interval began, to the sample.
      const long long n = (long long)r.intervalStartSamples.size() - 1;
      const double beatSamples = 60.0 / bpm * sr;
      const double expected = (double)n * (double)bpi * beatSamples;
      const double err =
          std::abs((double)r.intervalStartSamples.back() - expected);

      expect(err <= 1.0,
             "after " + juce::String((int)n) + " intervals the boundary is " +
                 juce::String(err, 2) + " samples from where the host puts it");
    }

    beginTest("a free-running integer grid really would have drifted");
    {
      // The measurement that makes the test above mean something, and it is
      // worth keeping the two error terms apart because they are wildly
      // different sizes.
      const double hostBpm = 128.5;
      const int bpi = 16;
      const double sr = 48000.0;
      const double halfAnHour = 30 * 60 * sr;

      // Second term: the interval length is truncated to whole samples
      // (njclient.cpp:806) and then repeated. Real, unbounded, and small.
      const double exact = (double)bpi / (hostBpm / 60.0) * sr;
      const int truncated = (int)exact;
      const double truncationErr =
          (exact - (double)truncated) * (halfAnHour / exact);
      expect(truncationErr > 1.0,
             "truncation should be measurable, got " +
                 juce::String(truncationErr, 1) + " samples");

      // First term, and the one that is actually audible: Ninjam's tempo is an
      // integer and the host's is not, so the free-running grid runs at the
      // rounded tempo and slips by the difference. This is seconds, not
      // samples -- which is why a practice session starts in time and ends
      // somewhere else entirely.
      const double gridBpm = std::round(hostBpm);
      const double roundingErr =
          halfAnHour * std::abs(gridBpm - hostBpm) / hostBpm;
      expect(roundingErr > sr,
             "over half an hour the integer tempo should cost more than a "
             "second, measured " +
                 juce::String(roundingErr / sr, 2) + " s");
      expect(roundingErr > truncationErr * 100.0,
             "and it should dominate truncation by a wide margin");
    }

    beginTest("every beat fires exactly once, in order");
    {
      // Blocks are the enemy here: a beat landing on a block seam is the case
      // where a stateless grid double-fires or drops one, and it is the case a
      // whole-number tempo hits constantly.
      for (double bpm : {120.0, 128.5, 90.0, 137.0}) {
        for (int blockSize : {64, 128, 512, 1024}) {
          const int bpi = 8;
          const double sr = 48000.0;
          const auto r = runBlocks(bpm, bpi, sr, blockSize, 4000);

          const double bps = HostGrid::beatsPerSample(bpm, sr);
          const double totalBeats = 4000.0 * blockSize * bps;
          const int wanted = (int)std::ceil(totalBeats - 1e-9);

          expectEquals((int)r.beatSamples.size(), wanted,
                       "bpm " + juce::String(bpm) + " block " +
                           juce::String(blockSize));

          for (std::size_t i = 1; i < r.beatSamples.size(); ++i)
            expect(r.beatSamples[i] > r.beatSamples[i - 1],
                   "beats must be strictly increasing");
        }
      }
    }

    beginTest("an interval start always carries beat 0 straight after it");
    {
      // The ordering IntervalClock promises, which the metronome and the
      // capture split both rely on.
      const auto r = runBlocks(120.0, 4, 48000.0, 512, 500);
      for (std::size_t i = 0; i < r.events.size(); ++i) {
        if (r.events[i].type != Event::Type::IntervalStart)
          continue;
        expect(i + 1 < r.events.size(), "an interval start cannot be last");
        expect(r.events[i + 1].type == Event::Type::Beat &&
                   r.events[i + 1].beatIndex == 0,
               "beat 0 must follow the interval start");
        expectEquals(r.events[i + 1].sampleOffset, r.events[i].sampleOffset);
      }
    }

    beginTest("the downbeat is the host's bar, not wherever you pressed play");
    {
      // What makes a practice recording line up in the DAW. Starting the
      // transport at bar 5 must not put our interval 1 there.
      const int bpi = 8;
      expectEquals((int)HostGrid::intervalIndexAt(0.0, bpi), 0);
      expectEquals((int)HostGrid::intervalIndexAt(8.0, bpi), 1);
      expectEquals((int)HostGrid::intervalIndexAt(15.99, bpi), 1);
      expectEquals((int)HostGrid::intervalIndexAt(16.0, bpi), 2);

      expectWithinAbsoluteError(HostGrid::phaseBeatsAt(0.0, bpi), 0.0, 1e-9);
      expectWithinAbsoluteError(HostGrid::phaseBeatsAt(11.5, bpi), 3.5, 1e-9);
      expectWithinAbsoluteError(HostGrid::phaseBeatsAt(16.0, bpi), 0.0, 1e-9);

      // Starting mid-timeline lands on the host's grid, not on a fresh count.
      const auto r = runBlocks(120.0, bpi, 48000.0, 512, 200, 6.0);
      expect(!r.intervalStartSamples.empty());
      // Two beats after ppq 6 comes ppq 8, the next interval start.
      const double bps = HostGrid::beatsPerSample(120.0, 48000.0);
      const long long expectedAt = (long long)std::llround(2.0 / bps);
      expect(std::abs(r.intervalStartSamples.front() - expectedAt) <= 1,
             "the first boundary must be where the host's grid says");
    }

    beginTest("a playhead that jumps backwards does not confuse the grid");
    {
      // Looping is the normal case in a DAW, and a counted grid has to be told
      // about it. A derived one simply asks where the host is.
      std::vector<Event> out;
      const double bps = HostGrid::beatsPerSample(120.0, 48000.0);
      HostGrid::advance(100.0, bps, 8, 512, out);
      const auto after = out.size();
      out.clear();
      HostGrid::advance(4.0, bps, 8, 512, out); // loop back
      expect(out.size() <= after + 4,
             "a jump is just another position, not a burst of catch-up beats");

      // And a negative position stays on grid rather than mirroring at zero.
      expectEquals((int)HostGrid::intervalIndexAt(-1.0, 8), -1);
      expectWithinAbsoluteError(HostGrid::phaseBeatsAt(-1.0, 8), 7.0, 1e-9);
    }

    beginTest("nonsense inputs emit nothing rather than spinning");
    {
      std::vector<Event> out;
      HostGrid::advance(0.0, 0.0, 8, 512, out);
      expect(out.empty(), "a stopped tempo has no beats");
      HostGrid::advance(0.0, 0.001, 0, 512, out);
      expect(out.empty(), "a zero bpi is not a grid");
      HostGrid::advance(0.0, 0.001, 8, 0, out);
      expect(out.empty(), "an empty block has no room for one");

      expectEquals(HostGrid::intervalSamples(0.0, 8, 48000.0), 0);
      expectEquals(HostGrid::intervalSamples(120.0, 8, 0.0), 0);
      expectEquals(HostGrid::intervalSamples(120.0, 8, 48000.0), 192000);
    }

    beginTest("every event lands inside the block it was emitted for");
    {
      // A sample offset outside the buffer is a crash, and the rounding at the
      // top of the loop is the only thing keeping it in range.
      for (int blockSize : {16, 64, 512}) {
        for (double bpm : {120.0, 240.0, 999.0}) {
          std::vector<Event> out;
          const double bps = HostGrid::beatsPerSample(bpm, 48000.0);
          for (int b = 0; b < 200; ++b) {
            out.clear();
            HostGrid::advance((double)b * blockSize * bps, bps, 4, blockSize,
                              out);
            for (const auto &e : out)
              expect(e.sampleOffset >= 0 && e.sampleOffset < blockSize,
                     "offset " + juce::String(e.sampleOffset) + " outside a " +
                         juce::String(blockSize) + " sample block");
          }
        }
      }
    }
  }
};

static HostGridTests hostGridTests;

} // namespace
