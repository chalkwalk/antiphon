#include "IntervalClock.h"

#include <algorithm>
#include <cmath>

void IntervalClock::prepare(double sr) {
  sampleRate = sr > 0.0 ? sr : 0.0;
  recomputeGrid();
  reset();
}

void IntervalClock::setTempo(int newBpm, int newBpi) {
  if (newBpm <= 0 || newBpi <= 0)
    return;
  pendingBpm = newBpm;
  pendingBpi = newBpi;
  if (atIntervalStart) {
    bpm = pendingBpm;
    bpi = pendingBpi;
    recomputeGrid();
  }
}

void IntervalClock::reset() {
  posInInterval = 0;
  nextBeat = 0;
  atIntervalStart = true;
  if (bpm != pendingBpm || bpi != pendingBpi) {
    bpm = pendingBpm;
    bpi = pendingBpi;
    recomputeGrid();
  }
}

void IntervalClock::recomputeGrid() {
  beatOffsets.clear();
  intervalSamples = 0;
  if (sampleRate <= 0.0 || bpm <= 0 || bpi <= 0)
    return;

  // Deliberately identical arithmetic to the reference client
  // (justinfrankel/ninjam njclient.cpp:794-810): samples per interval is
  // truncated, not rounded, and the beat grid is a whole number of samples
  // obtained by integer division. Matching this keeps our interval boundaries
  // aligned with every other Ninjam client on the server.
  const double v = (double)bpi / ((double)bpm * (1.0 / 60.0)) * sampleRate;
  intervalSamples = (int)v;

  // Degenerate tempos (absurdly high bpm at a low sample rate) would otherwise
  // give a zero-length interval and spin forever in advance().
  if (intervalSamples < bpi)
    intervalSamples = bpi;

  // Beats are placed by rounding rather than by njclient's integer division
  // (:810), which accumulates most of a sample of error per beat. Only the
  // interval length has to match the reference exactly -- that is what other
  // clients see. Beat offsets drive the local click and the UI, so they may as
  // well be sample-accurate. They restart from the boundary every interval, so
  // nothing accumulates across intervals either way.
  beatOffsets.reserve((size_t)bpi);
  for (int i = 0; i < bpi; ++i)
    beatOffsets.push_back(
        (int)std::llround((double)intervalSamples * (double)i / (double)bpi));

  for (int i = 1; i < bpi; ++i)
    if (beatOffsets[(size_t)i] <= beatOffsets[(size_t)i - 1])
      beatOffsets[(size_t)i] = beatOffsets[(size_t)i - 1] + 1;
}

int IntervalClock::beatStartSample(int beatIndex) const {
  if (beatIndex < 0 || beatIndex >= (int)beatOffsets.size())
    return -1;
  return beatOffsets[(size_t)beatIndex];
}

double IntervalClock::phaseBeats() const {
  if (intervalSamples <= 0)
    return 0.0;
  return (double)posInInterval / (double)intervalSamples * (double)bpi;
}

int IntervalClock::currentBeat() const {
  if (beatOffsets.empty())
    return 0;
  return nextBeat > 0 ? nextBeat - 1 : bpi - 1;
}

void IntervalClock::splitAtIntervalStarts(const std::vector<Event> &events,
                                          int numSamples,
                                          std::vector<BlockSegment> &out) {
  out.clear();
  if (numSamples <= 0)
    return;

  int cursor = 0;
  for (const auto &e : events) {
    if (e.type != Event::Type::IntervalStart)
      continue;
    const int at = e.sampleOffset < 0
                       ? 0
                       : (e.sampleOffset > numSamples ? numSamples
                                                      : e.sampleOffset);
    if (at < cursor)
      continue; // events are ordered; defensive
    // A zero-length piece is still emitted: the interval may have been
    // completed by earlier blocks, and the boundary must still fire.
    out.push_back({cursor, at - cursor, true});
    cursor = at;
  }
  if (cursor < numSamples)
    out.push_back({cursor, numSamples - cursor, false});
}

void IntervalClock::advance(int numSamples, std::vector<Event> &out) {
  if (numSamples <= 0 || !isValid())
    return;

  int consumed = 0;
  while (consumed < numSamples) {
    if (atIntervalStart) {
      // Apply any tempo change queued during the previous interval.
      if (bpm != pendingBpm || bpi != pendingBpi) {
        bpm = pendingBpm;
        bpi = pendingBpi;
        recomputeGrid();
        if (!isValid())
          return;
      }
      out.push_back({Event::Type::IntervalStart, consumed, 0});
      atIntervalStart = false;
    }

    // Emit every beat whose start lies at or before the current position.
    while (nextBeat < bpi &&
           beatOffsets[(size_t)nextBeat] <= (int)posInInterval) {
      out.push_back({Event::Type::Beat, consumed, nextBeat});
      ++nextBeat;
    }

    // Advance to whichever comes first: the next beat, the end of the
    // interval, or the end of the block.
    const int64_t nextEdge =
        (nextBeat < bpi) ? (int64_t)beatOffsets[(size_t)nextBeat]
                         : (int64_t)intervalSamples;
    const int step =
        (int)std::min<int64_t>(nextEdge - posInInterval, numSamples - consumed);
    if (step <= 0)
      break; // defensive; recomputeGrid guarantees a strictly increasing grid

    posInInterval += step;
    consumed += step;

    if (posInInterval >= intervalSamples) {
      posInInterval = 0;
      nextBeat = 0;
      atIntervalStart = true;
    }
  }
}
