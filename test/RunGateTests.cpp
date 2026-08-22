#include <JuceHeader.h>

#include "RunGate.h"

namespace {

class RunGateTests : public juce::UnitTest {
public:
  RunGateTests() : juce::UnitTest("RunGate", "RunGate") {}

  void runTest() override {
    beginTest("the grid follows the transport, connected or not");
    {
      // The point of the split. The clock, metronome and phase bar used to stop
      // whenever there was no server, because SyncState reports Disconnected
      // with no connection and one condition gated everything.
      expect(computeRunGate(false, false, true).gridRunning,
             "disconnected with the transport running: the grid must run, so "
             "this works as a plain metronome");
      expect(computeRunGate(true, true, true).gridRunning);
      expect(!computeRunGate(true, true, false).gridRunning,
             "transport stopped: nothing should advance");
      expect(!computeRunGate(false, false, false).gridRunning);
    }

    beginTest("the jam needs a connection, a sync and a running transport");
    {
      expect(computeRunGate(true, true, true).inJam);

      expect(!computeRunGate(false, true, true).inJam, "no connection");
      expect(!computeRunGate(true, false, true).inJam, "not in step");
      // SyncState stays Running across a transport stop on purpose, so that an
      // accidental stop does not re-phase a jam. "In step" and "playing now"
      // are therefore different questions, and the jam needs both.
      expect(!computeRunGate(true, true, false).inJam,
             "in step but stopped: nothing should be captured or transmitted");
    }

    beginTest("the standalone's own transport behaves like the host's");
    {
      // The standalone had no transport at all, so its grid ran purely on being
      // connected. With one, the same rule applies in both builds: press play,
      // the grid runs. Connecting starts it (see onConnected), so joining a
      // room still works without finding a button.
      const bool connected = true, inStep = true;

      expect(computeRunGate(connected, inStep, true).inJam,
             "connected with the transport running: in the jam");
      expect(!computeRunGate(connected, inStep, false).inJam,
             "stopping the transport must stop transmitting");
      expect(!computeRunGate(connected, inStep, false).gridRunning,
             "and must stop the clock");

      // Offline with the transport running is the plain-metronome case.
      expect(computeRunGate(false, false, true).gridRunning);
      expect(!computeRunGate(false, false, true).inJam);
    }

    beginTest("the offline grid starts on a transport edge, not on a sync");
    {
      // Why processBlock needs its own rising-edge reset offline. SyncState is
      // the thing that aligns the downbeat to the transport, and it reports
      // Disconnected whenever there is no server -- so with no connection it
      // never fires, and nothing else was aligning the grid, so the metronome
      // free-ran against the host's.
      const auto stopped = computeRunGate(false, false, false);
      const auto started = computeRunGate(false, false, true);
      expect(!stopped.gridRunning);
      expect(started.gridRunning,
             "the edge processBlock resets the interval clock on");

      // Connected, the edge belongs to SyncState and must not be taken twice.
      expect(computeRunGate(true, true, true).inJam,
             "connected, the jam owns the phase");
    }

    beginTest("a jam that loses its connection stops transmitting at once");
    {
      const auto before = computeRunGate(true, true, true);
      const auto after = computeRunGate(false, true, true);
      expect(before.inJam);
      expect(!after.inJam);
      expect(after.gridRunning,
             "the grid keeps running, so the metronome does not stop dead");
    }
  }
};

static RunGateTests runGateTests;

} // namespace
