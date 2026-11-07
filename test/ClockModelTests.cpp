#include <JuceHeader.h>

#include "ClockModel.h"

namespace {

using ClockModel::GridSource;
using ClockModel::Inputs;
using ClockModel::Machine;
using ClockModel::Mode;
using ClockModel::Outputs;
using ClockModel::State;

Inputs syncedInputs() {
  Inputs i;
  i.connected = true;
  i.mode = Mode::SyncedToDaw;
  i.dawPlaying = true;
  i.dawHasPosition = true;
  i.dawBpm = 110.0;
  i.roomBpm = 110;
  return i;
}

Outputs once(const Inputs &i) {
  Machine m;
  return m.update(i);
}

class ClockModelTests : public juce::UnitTest {
public:
  ClockModelTests() : juce::UnitTest("ClockModel", "ClockModel") {}

  void expectRow(const Outputs &o, State s, GridSource g, bool inJam,
                 const juce::String &what) {
    expect(o.state == s, what + ": state " + ClockModel::stateName(o.state));
    expect(o.grid == g, what + ": grid");
    expect(o.inJam == inJam, what + ": inJam");
  }

  void runTest() override {
    beginTest("synced: DAW playing at the room tempo with a position");
    expectRow(once(syncedInputs()), State::Synced, GridSource::Playhead, true,
              "synced");

    beginTest("a DAW a fraction off the room tempo is not synced");
    {
      auto i = syncedInputs();
      i.dawBpm = 110.3;
      expectRow(once(i), State::NotSyncedTempo, GridSource::OwnClock, true,
                "110.3 vs 110");
    }

    beginTest("a stopped DAW keeps the jam on our own clock");
    {
      auto i = syncedInputs();
      i.dawPlaying = false;
      expectRow(once(i), State::NotSyncedDawStopped, GridSource::OwnClock, true,
                "stopped");
    }

    beginTest("a DAW that reports no position is not synced");
    {
      auto i = syncedInputs();
      i.dawHasPosition = false;
      expectRow(once(i), State::NotSyncedNoPosition, GridSource::OwnClock,
                true, "no position");
    }

    beginTest("a DAW reporting a tempo of zero is treated as no position");
    {
      auto i = syncedInputs();
      i.dawBpm = 0.0;
      expectRow(once(i), State::NotSyncedNoPosition, GridSource::OwnClock,
                true, "zero tempo");
    }

    beginTest("Antiphon's clock, playing, ignores the DAW entirely");
    for (bool dawPlaying : {false, true})
      for (double bpm : {0.0, 95.0, 110.0}) {
        auto i = syncedInputs();
        i.mode = Mode::AntiphonClock;
        i.antiphonPlaying = true;
        i.dawPlaying = dawPlaying;
        i.dawBpm = bpm;
        expectRow(once(i), State::AntiphonClock, GridSource::OwnClock, true,
                  "antiphon clock");
      }

    beginTest("Antiphon's clock, stopped, is out of the jam even if the DAW "
              "would sync");
    {
      auto i = syncedInputs();
      i.mode = Mode::AntiphonClock;
      i.antiphonPlaying = false;
      expectRow(once(i), State::Stopped, GridSource::None, false, "stopped");
    }

    beginTest("disconnected in a host: the offline grid, never the jam");
    {
      Inputs i;
      i.dawPlaying = true;
      i.dawHasPosition = true;
      i.dawBpm = 128.5;
      expectRow(once(i), State::Disconnected, GridSource::HostOffline, false,
                "offline, playing");
      i.dawHasPosition = false;
      expectRow(once(i), State::Disconnected, GridSource::OwnClock, false,
                "offline, playing, no position: counted as today");
      i.dawPlaying = false;
      expectRow(once(i), State::Disconnected, GridSource::None, false,
                "offline, stopped");
    }

    beginTest("the standalone runs on its own transport");
    {
      Inputs i;
      i.standalone = true;
      i.antiphonPlaying = true;
      expectRow(once(i), State::Disconnected, GridSource::StandaloneOwn, false,
                "standalone offline");
      i.connected = true;
      expectRow(once(i), State::AntiphonClock, GridSource::StandaloneOwn, true,
                "standalone connected");
      i.antiphonPlaying = false;
      expectRow(once(i), State::Stopped, GridSource::None, false,
                "standalone stopped");
    }

    beginTest("tempo matches within 0.001 BPM and not beyond");
    {
      expect(ClockModel::tempoMatches(110.0009, 110));
      expect(ClockModel::tempoMatches(109.9991, 110));
      expect(!ClockModel::tempoMatches(110.002, 110));
      expect(!ClockModel::tempoMatches(0.0, 0), "zero is never a tempo");
    }

    beginTest("a room tempo change in grace keeps a synced player synced");
    {
      Machine m;
      auto i = syncedInputs();
      expect(m.update(i).state == State::Synced);
      i.roomBpm = 120;
      i.inTempoGrace = true;
      expect(m.update(i).state == State::Synced, "within grace");
      i.inTempoGrace = false;
      expect(m.update(i).state == State::NotSyncedTempo, "grace over");
    }

    beginTest("grace never makes an unsynced player synced");
    {
      Machine m;
      auto i = syncedInputs();
      i.dawBpm = 95.0;
      i.inTempoGrace = true;
      expect(m.update(i).state == State::NotSyncedTempo);
    }

    beginTest("entering Synced asks for a jump to the playhead, exactly once");
    {
      for (int from = 0; from < 3; ++from) {
        Machine m;
        auto i = syncedInputs();
        if (from == 0)
          i.dawPlaying = false;
        else if (from == 1)
          i.dawBpm = 95.0;
        else {
          i.mode = Mode::AntiphonClock;
          i.antiphonPlaying = true;
        }
        expect(!m.update(i).jumpToPlayhead, "not synced yet");
        const auto entering = m.update(syncedInputs());
        expect(entering.state == State::Synced);
        expect(entering.jumpToPlayhead, "the entering update jumps");
        expect(!m.update(syncedInputs()).jumpToPlayhead, "only once");
        auto leave = syncedInputs();
        leave.dawPlaying = false;
        expect(!m.update(leave).jumpToPlayhead, "leaving never jumps");
      }
    }

    beginTest("invariants over every input combination");
    {
      int combos = 0;
      for (int bits = 0; bits < 256; ++bits) {
        Inputs i;
        i.connected = bits & 1;
        i.standalone = bits & 2;
        i.mode = (bits & 4) ? Mode::AntiphonClock : Mode::SyncedToDaw;
        i.antiphonPlaying = bits & 8;
        i.dawPlaying = bits & 16;
        i.dawHasPosition = bits & 32;
        i.dawBpm = (bits & 64) ? 110.0 : 95.0;
        i.roomBpm = 110;
        i.inTempoGrace = bits & 128;
        const auto o = once(i);
        if (o.inJam)
          expect(i.connected, "inJam implies connected");
        if (o.grid == GridSource::None)
          expect(!o.inJam, "no grid, no jam");
        if (!i.connected)
          expect(o.state == State::Disconnected);
        ++combos;
      }
      expectEquals(combos, 256);
    }
  }
};

static ClockModelTests clockModelTests;

} // namespace
