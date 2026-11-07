#include <JuceHeader.h>

#include "HostGrid.h"
#include "PlayheadGrid.h"

namespace {

using PlayheadGrid::Move;

class PlayheadGridTests : public juce::UnitTest {
public:
  PlayheadGridTests() : juce::UnitTest("PlayheadGrid", "PlayheadGrid") {}

  void runTest() override {
    beginTest("a block that starts where the last ended is continuous");
    {
      expect(PlayheadGrid::classify(4.0, 4.0, false, 0, 0) == Move::Continuous);
      expect(PlayheadGrid::classify(4.0, 4.0 + 5e-7, false, 0, 0) ==
             Move::Continuous);
      expect(PlayheadGrid::classify(4.0, 4.0 + 1e-5, false, 0, 0) ==
             Move::Jump);
    }

    beginTest("the loop end back to the loop start is a wrap, not a jump");
    {
      expect(PlayheadGrid::classify(8.0, 0.0, true, 0.0, 8.0) ==
             Move::LoopWrap);
      // Mid-block wrap: the previous block ran past the loop end, and this
      // one starts the same distance past the loop start.
      expect(PlayheadGrid::classify(8.1, 0.1, true, 0.0, 8.0) ==
             Move::LoopWrap);
    }

    beginTest("any other backward move is a jump, looping or not");
    {
      expect(PlayheadGrid::classify(6.0, 3.0, true, 0.0, 8.0) == Move::Jump);
      expect(PlayheadGrid::classify(6.0, 3.0, false, 0.0, 8.0) == Move::Jump);
      expect(PlayheadGrid::classify(6.0, 40.0, false, 0.0, 0.0) ==
             Move::Jump);
    }

    beginTest("an 8-beat loop counts through a 16-beat interval");
    {
      PlayheadGrid::Position p;
      p.reset();
      expect(p.observe(0.0, 0.0, true, 0.0, 8.0) == Move::Continuous);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(6.0), 16), 6.0, 1e-9);
      expect(p.observe(0.0, 8.0, true, 0.0, 8.0) == Move::LoopWrap);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(0.0), 16), 8.0, 1e-9);
      p.observe(0.0, 8.0, true, 0.0, 8.0);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(0.0), 16), 0.0, 1e-9);
      p.observe(0.0, 8.0, true, 0.0, 8.0);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(3.0), 16), 11.0, 1e-9);
    }

    beginTest("a jump after loop wraps lands on the playhead again");
    {
      PlayheadGrid::Position p;
      p.reset();
      p.observe(0.0, 8.0, true, 0.0, 8.0); // one wrap
      expect(p.observe(37.0, 4.0, false, 0.0, 8.0) == Move::Jump);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(37.0), 16), 5.0, 1e-9);
    }

    beginTest("a BPI change re-anchors at the boundary it took effect on");
    {
      PlayheadGrid::Position p;
      p.reset();
      p.onBpiChangeAt(32.0);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(32.0), 12), 0.0, 1e-9);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(44.0), 12), 0.0, 1e-9);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(35.0), 12), 3.0, 1e-9);
      // A later jump still measures from that anchor.
      p.observe(50.0, 40.0, false, 0, 0);
      expectWithinAbsoluteError(
          HostGrid::phaseBeatsAt(p.musicalPpq(50.0), 12), 6.0, 1e-9);
    }

    beginTest("a forward jump pads the hole and keeps the interval open");
    {
      const auto f = PlayheadGrid::fillForJump(1000, 5000, 10000);
      expectEquals(f.padBeforeClose, 4000);
      expect(!f.closeInterval);
      expectEquals(f.padAfter, 0);
    }

    beginTest("a backward jump pads to full length, closes, and pads the new "
              "interval up to the playhead");
    {
      const auto f = PlayheadGrid::fillForJump(7000, 2000, 10000);
      expectEquals(f.padBeforeClose, 3000);
      expect(f.closeInterval);
      expectEquals(f.padAfter, 2000);
    }

    beginTest("landing exactly on a boundary closes once, never twice");
    {
      const auto full = PlayheadGrid::fillForJump(10000, 0, 10000);
      expectEquals(full.padBeforeClose, 0);
      expect(full.closeInterval);
      expectEquals(full.padAfter, 0);

      const auto empty = PlayheadGrid::fillForJump(0, 0, 10000);
      expectEquals(empty.padBeforeClose, 0);
      expect(!empty.closeInterval, "nothing captured, nothing to close");
      expectEquals(empty.padAfter, 0);
    }

    beginTest("no jump, no fill");
    {
      const auto f = PlayheadGrid::fillForJump(4000, 4000, 10000);
      expectEquals(f.padBeforeClose, 0);
      expect(!f.closeInterval);
      expectEquals(f.padAfter, 0);
    }
  }
};

static PlayheadGridTests playheadGridTests;

} // namespace
