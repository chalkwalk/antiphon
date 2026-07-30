#pragma once

#include "GainRamp.h"

// Where transmit was on within one interval, recorded as the points at which it
// changed rather than as a flag per sample.
//
// The transmit ring stores the audio you played, unconditionally; this records
// which parts of it you agreed to send. Keeping the two separate is what makes
// the retroactive gesture possible at all -- gating at capture time destroys
// the audio, so there is nothing left to retroactively enable.
//
// Transition points rather than a bitmask, because transmit can only change
// once per capture call: the flag is sampled per block, so the finest possible
// resolution is one block and a human produces a handful of changes per
// interval at most. For a 30-second interval at 48 kHz the alternatives cost
// 11.5 MB (a second audio buffer), 1.44 MB (a byte per sample) or 180 KB (a bit
// per sample); this costs about 2 KB. It is also faster at the boundary, where
// whole spans can be copied or skipped and the ramp applied only at the few
// edges, and it makes the retroactive edit O(1) instead of a bulk memset.
//
// JUCE-free and allocation-free so it is safe on the audio thread and testable
// headlessly.

class TransmitSpans {
public:
  // Far beyond anything a person can do inside one interval. Costs 2 KB.
  static constexpr int kMaxTransitions = 512;

  struct Span {
    int start = 0;
    int count = 0;
    bool on = false;
  };

  // Starts a fresh interval carrying `on` in from wherever transmit was left.
  void beginInterval(bool on) {
    baseState = on;
    count = 0;
    overflow = false;
  }

  bool stateAtEnd() const { return (count % 2 == 0) ? baseState : !baseState; }

  // Records that transmit became `on` at `samplePos` samples into the interval.
  // Returns false only when the list is full, in which case the change is
  // dropped and the previous state continues -- the audio is captured either
  // way, so nothing is lost but the change does not register until the next
  // interval.
  bool setStateAt(int samplePos, bool on) {
    if (samplePos < 0)
      samplePos = 0;
    if (on == stateAtEnd())
      return true; // nothing changed

    // A change at the very start is the interval's state, not a transition.
    if (samplePos == 0 && count == 0) {
      baseState = on;
      return true;
    }

    // Two changes at the same sample cancel: keeping both would leave an empty
    // span and break the "positions strictly increase" invariant.
    if (count > 0 && positions[count - 1] == samplePos) {
      --count;
      return true;
    }

    if (count >= kMaxTransitions) {
      overflow = true;
      return false;
    }

    positions[count++] = samplePos;
    return true;
  }

  // The retroactive gesture: the whole interval becomes `on`, as though it had
  // been from the start.
  //
  // This is all it takes, because the ordinary toggle has already set the state
  // from the press onwards -- so making the earlier part match leaves the entire
  // interval in one state with no transitions at all.
  void makeWholeInterval(bool on) {
    baseState = on;
    count = 0;
  }

  bool overflowed() const { return overflow; }
  int transitionCount() const { return count; }

  // True when any part of [0, length) has transmit on. This is what decides
  // whether the interval is sent at all: an interval you were silent for
  // throughout sends nothing.
  bool anyActive(int length) const {
    for (int i = 0, n = spanCount(length); i < n; ++i) {
      const Span s = span(i, length);
      if (s.on && s.count > 0)
        return true;
    }
    return false;
  }

  // Spans covering [0, length) in order, skipping any that would be empty.
  int spanCount(int length) const {
    if (length <= 0)
      return 0;
    int n = 1;
    int previous = 0;
    for (int i = 0; i < count; ++i) {
      const int p = positions[i];
      if (p > previous && p < length) {
        ++n;
        previous = p;
      }
    }
    return n;
  }

  Span span(int index, int length) const {
    Span out;
    if (length <= 0)
      return out;

    // Rebuild the effective boundary list, dropping transitions that would
    // produce an empty span (at or past the end, or repeated).
    int starts[kMaxTransitions + 1];
    bool states[kMaxTransitions + 1];
    int n = 0;
    starts[n] = 0;
    states[n] = baseState;
    ++n;

    bool state = baseState;
    int previous = 0;
    for (int i = 0; i < count; ++i) {
      const int p = positions[i];

      // A change at or past the end happened after this interval, so it does
      // not belong to it at all. Absorbing it into the previous span -- which
      // is what happens if this is lumped in with the coincident case -- would
      // flip the whole interval's state.
      if (p >= length)
        break;

      state = !state;
      if (p > previous) {
        starts[n] = p;
        states[n] = state;
        ++n;
        previous = p;
      } else {
        // Coincident with the span already open, so the span would be empty.
        // The change still applies, to that span.
        states[n - 1] = state;
      }
    }

    if (index < 0 || index >= n)
      return out;

    out.start = starts[index];
    out.count = (index + 1 < n ? starts[index + 1] : length) - starts[index];
    out.on = states[index];
    return out;
  }

  // Applies the spans in place across `numChannels` planar buffers of `length`
  // samples: off spans become silence, and every edge is ramped so switching
  // transmit cannot click in what the other players hear.
  //
  // The ramp needs the audio on both sides of an edge, which is exactly why the
  // ring stores it un-gated: a fade-out has to keep sounding into the off span.
  void applyTo(float *const *channels, int numChannels, int length,
               int rampSamples) const {
    if (length <= 0 || numChannels <= 0)
      return;

    GainRamp ramp;
    ramp.prepareSamples(rampSamples);
    ramp.jumpTo(baseState ? 1.0f : 0.0f);

    const int n = spanCount(length);
    for (int i = 0; i < n; ++i) {
      const Span s = span(i, length);
      ramp.setTarget(s.on ? 1.0f : 0.0f);
      for (int k = 0; k < s.count; ++k) {
        const float g = ramp.next();
        for (int ch = 0; ch < numChannels; ++ch)
          channels[ch][s.start + k] *= g;
      }
    }
  }

private:
  bool baseState = false;
  bool overflow = false;
  int count = 0;
  int positions[kMaxTransitions] = {};
};
