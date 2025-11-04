#pragma once

#include <cstdint>
#include <vector>

// Sample-exact beat and interval clock.
//
// Pure and deterministic: given (sampleRate, bpm, bpi) and a sequence of
// advance() calls, the emitted event stream is fully determined and does not
// depend on how the samples are divided into blocks. No JUCE, no allocation
// inside advance().
//
// The grid is integer: samplesPerInterval() is computed once per tempo change,
// so every interval is exactly the same length. The previous implementation
// accumulated a double phase and wrapped it by subtraction, which left a
// residual uniformly distributed in [0, beatsPerSample) and made the interval
// boundary walk by a sample from interval to interval. That jitter propagated
// straight into the length of each transmitted interval.

class IntervalClock {
public:
  struct Event {
    enum class Type { IntervalStart, Beat };
    Type type;
    int sampleOffset; // index within the block passed to advance()
    int beatIndex;    // 0 .. bpi-1; IntervalStart always carries 0
  };

  void prepare(double sampleRate);

  // Takes effect at the start of the next interval boundary; the current
  // interval always plays out at its original length. Ignored if either value
  // is not positive.
  void setTempo(int bpm, int bpi);

  // Returns to the top of an interval. The next advance() emits IntervalStart
  // (and Beat 0) at sample offset 0.
  void reset();

  // Appends events in ascending sampleOffset order. At an interval boundary
  // both IntervalStart and Beat{0} are emitted, IntervalStart first.
  void advance(int numSamples, std::vector<Event> &out);

  int samplesPerInterval() const { return intervalSamples; }
  int64_t samplePosInInterval() const { return posInInterval; }

  // Exact start sample of the given beat within the interval, or -1 if out of
  // range.
  int beatStartSample(int beatIndex) const;

  // Position within the interval expressed in beats, 0 .. bpi. Drives the UI
  // phase bar.
  double phaseBeats() const;

  int currentBeat() const;
  int getBpm() const { return bpm; }
  int getBpi() const { return bpi; }
  bool isValid() const { return intervalSamples > 0; }

private:
  void recomputeGrid();

  double sampleRate = 0.0;
  int bpm = 120;
  int bpi = 16;
  int pendingBpm = 120;
  int pendingBpi = 16;

  int intervalSamples = 0;
  int64_t posInInterval = 0;
  int nextBeat = 0;
  bool atIntervalStart = true;
  std::vector<int> beatOffsets; // size bpi; beatOffsets[i] = start of beat i
};
