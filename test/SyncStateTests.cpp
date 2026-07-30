#include <JuceHeader.h>

#include "SyncState.h"

namespace {

using S = SyncState::State;

SyncState::Inputs in(bool connected, bool tempo, bool playing,
                     bool sync = false, bool hasTransport = true) {
  SyncState::Inputs i;
  i.connected = connected;
  i.tempoMatches = tempo;
  i.transportPlaying = playing;
  i.syncRequested = sync;
  i.hasTransport = hasTransport;
  return i;
}

class SyncStateTests : public juce::UnitTest {
public:
  SyncStateTests() : juce::UnitTest("SyncState", "SyncState") {}

  void runTest() override {
    beginTest("the happy path");
    {
      SyncState s;
      expect(s.get() == S::Disconnected);

      expect(!s.update(in(true, false, false)));
      expect(s.get() == S::TempoMismatch, "wrong tempo must block");

      expect(!s.update(in(true, true, false)));
      expect(s.get() == S::ReadyToSync, "matching tempo offers Sync");

      expect(!s.update(in(true, true, false, /*sync*/ true)));
      expect(s.get() == S::WaitingForPlay);

      // The transport-start edge is what locks the clock.
      expect(s.update(in(true, true, true)), "should reset the clock here");
      expect(s.get() == S::Running);
      expect(s.isRunning());
    }

    beginTest("only the rising edge of the transport counts");
    {
      SyncState s;
      s.update(in(true, true, true));       // already playing when armed
      s.update(in(true, true, true, true)); // press Sync mid-playback
      expect(s.get() == S::WaitingForPlay,
             "arming while the transport runs must wait for a fresh start");

      expect(!s.update(in(true, true, true)),
             "a continuing transport is not a start edge");
      expect(s.get() == S::WaitingForPlay);

      expect(!s.update(in(true, true, false))); // stop
      expect(s.update(in(true, true, true)), "now it should fire");
      expect(s.get() == S::Running);
    }

    beginTest("a running jam is not re-phased by a stray stop/start");
    {
      SyncState s;
      s.update(in(true, true, false));
      s.update(in(true, true, false, true));
      expect(s.update(in(true, true, true)));
      expect(s.get() == S::Running);

      // The whole reason Sync is explicit: re-phasing mid-jam truncates the
      // interval being transmitted, which other players hear as a glitch.
      expect(!s.update(in(true, true, false)), "transport stopped");
      expect(!s.update(in(true, true, true)),
             "restarting the transport must NOT silently re-sync");
      expect(s.get() == S::Running);

      // Asking for it explicitly does re-arm.
      s.update(in(true, true, true, true));
      expect(s.get() == S::WaitingForPlay);
    }

    beginTest("a tempo change invalidates the sync");
    {
      SyncState s;
      s.update(in(true, true, false));
      s.update(in(true, true, false, true));
      s.update(in(true, true, true));
      expect(s.get() == S::Running);

      s.update(in(true, false, true));
      expect(s.get() == S::TempoMismatch,
             "the interval grid changed length; the old lock is meaningless");

      // Recovering requires arming again, not just fixing the tempo.
      s.update(in(true, true, true));
      expect(s.get() == S::ReadyToSync);
    }

    beginTest("disconnecting resets everything");
    {
      SyncState s;
      s.update(in(true, true, false, true));
      s.update(in(true, true, true));
      expect(s.get() == S::Running);
      s.update(in(false, true, true));
      expect(s.get() == S::Disconnected);
      expect(!s.isRunning());
    }

    beginTest("a host with no transport runs immediately");
    {
      // The standalone has nothing to wait for and nothing to lock to.
      SyncState s;
      expect(s.update(in(true, false, false, false, /*hasTransport*/ false)),
             "entering Running should reset the clock once");
      expect(s.get() == S::Running);
      expect(!s.update(in(true, false, false, false, false)),
             "and not keep resetting it every block");
      expect(s.isRunning());
    }

    beginTest("every state has a description");
    for (auto st : {S::Disconnected, S::TempoMismatch, S::ReadyToSync,
                    S::WaitingForPlay, S::Running})
      expect(juce::String(SyncState::describe(st)).isNotEmpty());
  }
};

static SyncStateTests syncStateTests;

} // namespace
