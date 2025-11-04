#include <JuceHeader.h>

#include "IntervalClock.h"

#include <cmath>
#include <vector>

namespace {

// Verbatim reproduction of the float phase accumulator this class replaced
// (PluginProcessor.cpp interval loop, stripped of metronome and flash state).
// Used only to show that the new clock lands on the same boundaries and beats
// over the first interval. It is expected to diverge over many intervals: the
// old accumulator tracked the exact fractional interval length while the new
// one, like the reference client, uses a fixed truncated integer length.
struct LegacyPhaseReference {
  double phaseBeats = 0.0;
  int lastTimestampedBeat = -1;

  struct Hit {
    int sample;
    int beat;
    bool isBoundary;
  };

  std::vector<Hit> run(int bpm, int bpi, double sampleRate, int numSamples) {
    std::vector<Hit> hits;
    const double beatsPerSample = (bpm / 60.0) / sampleRate;
    for (int n = 0; n < numSamples; ++n) {
      phaseBeats += beatsPerSample;
      if (phaseBeats >= bpi)
        phaseBeats -= bpi;

      const double fractionalBeat = phaseBeats - std::floor(phaseBeats);

      if (phaseBeats < beatsPerSample)
        hits.push_back({n, 0, true});

      if (fractionalBeat < 0.05) {
        const int currentBeat = (int)std::floor(phaseBeats);
        if (currentBeat != lastTimestampedBeat) {
          lastTimestampedBeat = currentBeat;
          hits.push_back({n, currentBeat, false});
        }
      }
    }
    return hits;
  }
};

// Runs the clock over `total` samples in fixed-size blocks, returning events
// with absolute sample positions.
struct Run {
  std::vector<int> intervalStarts;
  std::vector<std::pair<int, int>> beats; // (absolute sample, beat index)
};

Run runClock(IntervalClock &clock, int totalSamples, int blockSize) {
  Run r;
  std::vector<IntervalClock::Event> events;
  int pos = 0;
  while (pos < totalSamples) {
    const int n = std::min(blockSize, totalSamples - pos);
    events.clear();
    clock.advance(n, events);
    for (const auto &e : events) {
      if (e.type == IntervalClock::Event::Type::IntervalStart)
        r.intervalStarts.push_back(pos + e.sampleOffset);
      else
        r.beats.emplace_back(pos + e.sampleOffset, e.beatIndex);
    }
    pos += n;
  }
  return r;
}

class IntervalClockTests : public juce::UnitTest {
public:
  IntervalClockTests() : juce::UnitTest("IntervalClock", "IntervalClock") {}

  void runTest() {
    const std::vector<int> bpms{40, 90, 120, 137, 200};
    const std::vector<int> bpis{4, 8, 16, 24};
    const std::vector<double> rates{44100.0, 48000.0, 88200.0, 96000.0};
    const std::vector<int> blocks{1, 32, 64, 441, 512, 1024};

    beginTest("interval length matches the reference client formula");
    for (int bpm : bpms)
      for (int bpi : bpis)
        for (double sr : rates) {
          IntervalClock c;
          c.prepare(sr);
          c.setTempo(bpm, bpi);
          const int expected =
              (int)((double)bpi / ((double)bpm * (1.0 / 60.0)) * sr);
          expectEquals(c.samplesPerInterval(), expected,
                       juce::String(bpm) + "bpm/" + juce::String(bpi) + "bpi/" +
                           juce::String(sr));
        }

    beginTest("every interval is exactly the same length");
    // The defect this class was written to remove: the float accumulator made
    // the boundary walk by a sample from interval to interval, which changed
    // the length of every transmitted interval.
    for (int bpm : bpms)
      for (int bpi : bpis)
        for (double sr : rates) {
          IntervalClock c;
          c.prepare(sr);
          c.setTempo(bpm, bpi);
          const int len = c.samplesPerInterval();
          auto r = runClock(c, len * 200 + 1, 512);
          expect(r.intervalStarts.size() >= 200, "too few intervals");
          bool uniform = true;
          for (size_t i = 1; i < r.intervalStarts.size(); ++i)
            if (r.intervalStarts[i] - r.intervalStarts[i - 1] != len)
              uniform = false;
          expect(uniform, "interval length drifted at " + juce::String(bpm) +
                              "bpm/" + juce::String(bpi) + "bpi/" +
                              juce::String(sr));
        }

    beginTest("event positions are independent of block size");
    for (int bpm : bpms)
      for (int bpi : bpis)
        for (double sr : rates) {
          Run reference;
          bool first = true;
          for (int block : blocks) {
            IntervalClock c;
            c.prepare(sr);
            c.setTempo(bpm, bpi);
            const int total = c.samplesPerInterval() * 3 + 7;
            auto r = runClock(c, total, block);
            if (first) {
              reference = r;
              first = false;
            } else {
              expect(r.intervalStarts == reference.intervalStarts,
                     "boundaries moved at block " + juce::String(block));
              expect(r.beats == reference.beats,
                     "beats moved at block " + juce::String(block));
            }
          }
          // One single giant block must agree too.
          IntervalClock c;
          c.prepare(sr);
          c.setTempo(bpm, bpi);
          const int total = c.samplesPerInterval() * 3 + 7;
          auto giant = runClock(c, total, total);
          expect(giant.intervalStarts == reference.intervalStarts,
                 "boundaries moved in a single block");
          expect(giant.beats == reference.beats,
                 "beats moved in a single block");
        }

    beginTest("exactly bpi beats and one boundary per interval");
    for (int bpm : bpms)
      for (int bpi : bpis)
        for (double sr : rates) {
          IntervalClock c;
          c.prepare(sr);
          c.setTempo(bpm, bpi);
          auto r = runClock(c, c.samplesPerInterval() * 5, 256);
          expectEquals((int)r.intervalStarts.size(), 5);
          expectEquals((int)r.beats.size(), 5 * bpi);
          for (size_t i = 0; i < r.beats.size(); ++i)
            expectEquals(r.beats[i].second, (int)(i % (size_t)bpi));
        }

    beginTest("beat 0 coincides with the interval boundary");
    {
      IntervalClock c;
      c.prepare(48000.0);
      c.setTempo(120, 8);
      auto r = runClock(c, c.samplesPerInterval() * 3, 128);
      for (size_t i = 0; i < r.intervalStarts.size(); ++i) {
        const auto &beat0 = r.beats[i * 8];
        expectEquals(beat0.second, 0);
        expectEquals(beat0.first, r.intervalStarts[i]);
      }
    }

    beginTest("no drift over an hour of audio");
    {
      IntervalClock c;
      c.prepare(48000.0);
      c.setTempo(120, 8);
      const int len = c.samplesPerInterval();
      const int total = 48000 * 3600;
      auto r = runClock(c, total, 1024);
      expectEquals((int)r.intervalStarts.size(), (total + len - 1) / len);
      expectEquals(r.intervalStarts.back(),
                   ((int)r.intervalStarts.size() - 1) * len);
    }

    beginTest("agrees with the legacy float clock over the first interval");
    for (int bpm : bpms)
      for (int bpi : bpis)
        for (double sr : rates) {
          IntervalClock c;
          c.prepare(sr);
          c.setTempo(bpm, bpi);
          const int len = c.samplesPerInterval();

          LegacyPhaseReference legacy;
          auto legacyHits = legacy.run(bpm, bpi, sr, len);
          auto r = runClock(c, len, 64);

          // The legacy clock emitted beat 0 of the first interval a sample or
          // two in, where the new clock emits it at sample 0; compare the
          // remaining beats, which is what the metronome and the UI key off.
          for (const auto &h : legacyHits) {
            if (h.isBoundary || h.beat == 0)
              continue;
            bool matched = false;
            for (const auto &b : r.beats)
              if (b.second == h.beat && std::abs(b.first - h.sample) <= 2)
                matched = true;
            expect(matched, "legacy beat " + juce::String(h.beat) + " at " +
                                juce::String(h.sample) + " unmatched (" +
                                juce::String(bpm) + "bpm/" +
                                juce::String(bpi) + "bpi/" +
                                juce::String(sr) + ")");
          }
        }

    beginTest("reset returns to the top of an interval");
    {
      IntervalClock c;
      c.prepare(48000.0);
      c.setTempo(120, 8);
      std::vector<IntervalClock::Event> ev;
      c.advance(10000, ev);
      expect(c.samplePosInInterval() > 0);

      c.reset();
      expectEquals((int)c.samplePosInInterval(), 0);
      expect(c.phaseBeats() == 0.0);

      ev.clear();
      c.advance(64, ev);
      expect(!ev.empty());
      expect(ev[0].type == IntervalClock::Event::Type::IntervalStart);
      expectEquals(ev[0].sampleOffset, 0);
    }

    beginTest("tempo change takes effect at the next boundary");
    {
      IntervalClock c;
      c.prepare(48000.0);
      c.setTempo(120, 8);
      const int oldLen = c.samplesPerInterval();

      std::vector<IntervalClock::Event> ev;
      c.advance(oldLen / 2, ev); // mid-interval
      c.setTempo(90, 12);
      expectEquals(c.samplesPerInterval(), oldLen,
                   "current interval must keep its original length");

      ev.clear();
      c.advance(oldLen, ev);
      expectEquals(c.getBpm(), 90);
      expectEquals(c.getBpi(), 12);

      // No event may ever carry a beat index outside the new range.
      IntervalClock d;
      d.prepare(48000.0);
      d.setTempo(120, 24);
      ev.clear();
      d.advance(d.samplesPerInterval() / 2, ev);
      d.setTempo(120, 4); // large drop in bpi
      ev.clear();
      d.advance(d.samplesPerInterval() * 4, ev);

      // The in-flight interval correctly finishes at the old tempo, so only
      // check events from the first boundary after the change onwards.
      bool afterSwitch = false;
      int checked = 0;
      for (const auto &e : ev) {
        if (e.type == IntervalClock::Event::Type::IntervalStart)
          afterSwitch = true;
        if (!afterSwitch)
          continue;
        ++checked;
        expect(e.beatIndex >= 0 && e.beatIndex < d.getBpi(),
               "beat index " + juce::String(e.beatIndex) + " out of range");
      }
      expect(checked > 0, "no events after the tempo switch");
    }

    beginTest("degenerate inputs produce no events and do not hang");
    {
      std::vector<IntervalClock::Event> ev;

      IntervalClock a;
      a.prepare(0.0);
      a.setTempo(120, 8);
      a.advance(4096, ev);
      expect(ev.empty());
      expect(!a.isValid());

      IntervalClock b;
      b.prepare(48000.0);
      b.setTempo(0, 8); // ignored
      b.setTempo(120, 0);
      ev.clear();
      b.advance(4096, ev);
      // The rejected tempos leave the defaults in place, which are valid.
      expectEquals(b.getBpm(), 120);

      IntervalClock e;
      e.prepare(48000.0);
      e.setTempo(120, 8);
      ev.clear();
      e.advance(0, ev);
      e.advance(-5, ev);
      expect(ev.empty());

      // Absurd tempo must still terminate.
      IntervalClock f;
      f.prepare(8000.0);
      f.setTempo(60000, 32);
      ev.clear();
      f.advance(100000, ev);
      expect(f.samplesPerInterval() > 0);
    }
  }
};

static IntervalClockTests intervalClockTests;

} // namespace
