#include <JuceHeader.h>

#include "RunGate.h"

namespace {

class RunGateTests : public juce::UnitTest {
public:
  RunGateTests() : juce::UnitTest("RunGate", "RunGate") {}

  void runTest() override {
    beginTest("transmitting and practising can never both be live");
    {
      // The safety property, over every input there is. Practice plays your own
      // audio back to you and must never reach the room; making it offline-only
      // is what guarantees that, and this is the assertion that says so.
      // PluginProcessor cannot be compiled into this target, so if the property
      // is not tested here it is not tested at all.
      for (int bits = 0; bits < 16; ++bits) {
        const bool connected = (bits & 1) != 0;
        const bool syncRunning = (bits & 2) != 0;
        const bool transport = (bits & 4) != 0;
        const bool practice = (bits & 8) != 0;

        const auto g =
            computeRunGate(connected, syncRunning, transport, practice);
        expect(runGateIsSafe(g),
               "unsafe for connected=" + juce::String((int)connected) +
                   " sync=" + juce::String((int)syncRunning) +
                   " transport=" + juce::String((int)transport) +
                   " practice=" + juce::String((int)practice));
      }
    }

    beginTest("the grid follows the transport, connected or not");
    {
      // The point of the split. The clock, metronome and phase bar used to stop
      // whenever there was no server, because SyncState reports Disconnected
      // with no connection and one condition gated everything.
      expect(computeRunGate(false, false, true, false).gridRunning,
             "disconnected with the transport running: the grid must run, so "
             "this works as a plain metronome");
      expect(computeRunGate(true, true, true, false).gridRunning);
      expect(!computeRunGate(true, true, false, false).gridRunning,
             "transport stopped: nothing should advance");
      expect(!computeRunGate(false, false, false, true).gridRunning);
    }

    beginTest("the jam needs a connection, a sync and a running transport");
    {
      expect(computeRunGate(true, true, true, false).inJam);

      expect(!computeRunGate(false, true, true, false).inJam, "no connection");
      expect(!computeRunGate(true, false, true, false).inJam, "not in step");
      // SyncState stays Running across a transport stop on purpose, so that an
      // accidental stop does not re-phase a jam. "In step" and "playing now"
      // are therefore different questions, and the jam needs both.
      expect(!computeRunGate(true, true, false, false).inJam,
             "in step but stopped: nothing should be captured or transmitted");
    }

    beginTest("practice is offline only");
    {
      expect(computeRunGate(false, false, true, true).echoOn);
      expect(!computeRunGate(true, true, true, true).echoOn,
             "connected: practice must not run alongside a jam");
      expect(!computeRunGate(true, false, true, true).echoOn,
             "connected but not in step is still connected");
      expect(!computeRunGate(false, false, false, true).echoOn,
             "no transport, no grid, no echo");
      expect(!computeRunGate(false, false, true, false).echoOn, "practice off");
    }

    beginTest("connecting while practising stops the practice on its own");
    {
      // The gate is recomputed every block from live state, so the connection
      // landing is enough. Nothing has to remember to switch practice off,
      // which is the kind of thing that gets forgotten on one of the paths.
      const auto before = computeRunGate(false, false, true, true);
      const auto after = computeRunGate(true, false, true, true);
      expect(before.echoOn);
      expect(!after.echoOn);
    }

    beginTest("the standalone's own transport behaves like the host's");
    {
      // The standalone had no transport at all, so its grid ran purely on being
      // connected. With one, the same rule applies in both builds: press play,
      // the grid runs. Connecting starts it (see onConnected), so joining a
      // room still works without finding a button.
      const bool connected = true, inStep = true;

      expect(computeRunGate(connected, inStep, true, false).inJam,
             "connected with the transport running: in the jam");
      expect(!computeRunGate(connected, inStep, false, false).inJam,
             "stopping the transport must stop transmitting");
      expect(!computeRunGate(connected, inStep, false, false).gridRunning,
             "and must stop the clock");

      // Offline with the transport running is the practice case, and also a
      // plain metronome when practice is off.
      expect(computeRunGate(false, false, true, false).gridRunning);
      expect(!computeRunGate(false, false, true, false).inJam);
    }

    beginTest("the offline grid starts on a transport edge, not on a sync");
    {
      // Why processBlock needs its own rising-edge reset offline. SyncState is
      // the thing that aligns the downbeat to the transport, and it reports
      // Disconnected whenever there is no server -- so with no connection it
      // never fires, and nothing else was aligning the grid. The metronome
      // free-ran against the host's, which is precisely what practice mode is
      // measured against.
      const auto stopped = computeRunGate(false, false, false, true);
      const auto started = computeRunGate(false, false, true, true);
      expect(!stopped.gridRunning);
      expect(started.gridRunning,
             "the edge processBlock resets the interval clock on");

      // Connected, the edge belongs to SyncState and must not be taken twice.
      expect(computeRunGate(true, true, true, false).inJam,
             "connected, the jam owns the phase");
    }

    beginTest("a jam that loses its connection stops transmitting at once");
    {
      const auto before = computeRunGate(true, true, true, false);
      const auto after = computeRunGate(false, true, true, false);
      expect(before.inJam);
      expect(!after.inJam);
      expect(after.gridRunning,
             "the grid keeps running, so the metronome does not stop dead");
    }
  }
};

static RunGateTests runGateTests;

} // namespace
