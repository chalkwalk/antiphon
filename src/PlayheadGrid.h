#pragma once

#include <cmath>

// The interval position, read from the DAW's playhead while synced.
//
// HostGrid answers "where in the interval is this PPQ position"; this answers
// "which PPQ position should it be asked about". Three things make those
// differ:
//
//   - A loop. A DAW loop shorter than the interval wraps the playhead before
//     the interval ends. Musically the loop is continuous -- the bars keep
//     coming -- so a wrap from the loop end to the loop start is counted as
//     time passing, not as a move. Without that, a 2-bar loop under a 4-bar
//     interval would never finish an interval at all.
//   - A BPI change. The room's new interval length takes effect at a boundary
//     of the old grid; measuring from bar 1 with the new modulus would jump
//     there. Re-anchoring at that boundary makes the change seamless.
//   - Everything else that moves the playhead -- a pause that resumes from the
//     start marker, a click on the ruler -- is a jump, and the position is
//     re-derived from the playhead. That is the point of following it: there is
//     nothing counted, so nothing can be left out of step.
//
// fillForJump is the other half of a jump: what to do with the interval being
// captured, so every upload stays full length and everything after the jump
// reaches the room at the right point in the bar.
//
// Pure and JUCE-free.

namespace PlayheadGrid {

// Positions agree to this many beats when nothing moved. A block's end is
// computed as start + n * beatsPerSample, so a host reporting the next start
// independently differs by rounding only.
inline constexpr double kContinuityBeats = 1.0e-6;
// How far a loop wrap may land from where the loop arithmetic says it should.
inline constexpr double kWrapToleranceBeats = 1.0e-3;

enum class Move { Continuous, LoopWrap, Jump };

inline Move classify(double prevEndPpq, double ppqStart, bool looping,
                     double loopStartPpq, double loopEndPpq) {
  if (std::fabs(ppqStart - prevEndPpq) <= kContinuityBeats)
    return Move::Continuous;
  const double loopLength = loopEndPpq - loopStartPpq;
  if (looping && loopLength > 0.0 && ppqStart < prevEndPpq &&
      std::fabs((prevEndPpq - loopLength) - ppqStart) <= kWrapToleranceBeats)
    return Move::LoopWrap;
  return Move::Jump;
}

class Position {
public:
  void reset() {
    anchor = 0.0;
    loopOffset = 0.0;
  }

  // The room's new BPI took effect at this playhead position.
  void onBpiChangeAt(double ppqBoundary) {
    anchor = ppqBoundary - loopOffset;
  }

  Move observe(double ppqStart, double prevEndPpq, bool looping,
               double loopStartPpq, double loopEndPpq) {
    const Move m =
        classify(prevEndPpq, ppqStart, looping, loopStartPpq, loopEndPpq);
    if (m == Move::LoopWrap)
      loopOffset += loopEndPpq - loopStartPpq;
    else if (m == Move::Jump)
      loopOffset = 0.0;
    return m;
  }

  // What HostGrid is asked about in place of the raw playhead.
  double musicalPpq(double ppq) const { return ppq - anchor + loopOffset; }

private:
  double anchor = 0.0;
  double loopOffset = 0.0;
};

struct JumpFill {
  int padBeforeClose = 0;
  bool closeInterval = false;
  int padAfter = 0;
};

// captured: samples already captured in this interval. target: where in the
// interval, in samples, the playhead now says we are. len: interval length.
inline JumpFill fillForJump(int captured, int target, int len) {
  JumpFill f;
  if (len <= 0)
    return f;
  if (target < 0)
    target = 0;
  if (target >= len)
    target = len - 1;
  if (captured > len)
    captured = len;

  if (target >= captured) {
    // Forward (or no move): silence over the hole, the interval stays open.
    f.padBeforeClose = target - captured;
    return f;
  }
  // Backward: this interval ends now. Pad it to full length so no upload is
  // ever short, then start the next one with silence up to the playhead.
  f.padBeforeClose = len - captured;
  f.closeInterval = true;
  f.padAfter = target;
  return f;
}

} // namespace PlayheadGrid
