#include <JuceHeader.h>

#include "HostControl.h"

#include <vector>

namespace {

struct FakeHost : HostControl {
  bool present = true;
  bool tempoSucceeds = true;
  bool playing = false;
  std::vector<double> tempos;
  int plays = 0;

  bool available() const override { return present; }
  bool setTempo(double bpm) override {
    tempos.push_back(bpm);
    return tempoSucceeds;
  }
  bool play() override {
    ++plays;
    playing = true;
    return true;
  }
  bool isPlaying() const override { return playing; }
  bool setLoopBeats(double, double) override { return true; }
};

class HostControlTests : public juce::UnitTest {
public:
  HostControlTests() : juce::UnitTest("HostControl", "HostControl") {}

  void runTest() override {
    beginTest("joining at the room's tempo touches nothing");
    {
      FakeHost h;
      TempoFollow f;
      expect(!f.onJoin(h, 110, 110.0));
      expect(h.tempos.empty());
    }

    beginTest("joining at another tempo sets the DAW to the room's, once");
    {
      FakeHost h;
      TempoFollow f;
      expect(f.onJoin(h, 110, 95.0));
      expectEquals((int)h.tempos.size(), 1);
      expectEquals(h.tempos[0], 110.0);
    }

    beginTest("a fraction off is still set: synced means exactly");
    {
      FakeHost h;
      TempoFollow f;
      expect(f.onJoin(h, 110, 110.3));
      expectEquals((int)h.tempos.size(), 1);
    }

    beginTest("turned off, it never touches the DAW");
    {
      FakeHost h;
      TempoFollow f;
      f.enabled = false;
      expect(!f.onJoin(h, 110, 95.0));
      expect(!f.onGridTempoApplied(h, 120, 110.0));
      expect(h.tempos.empty());
    }

    beginTest("outside Reaper, nothing is called");
    {
      FakeHost h;
      h.present = false;
      TempoFollow f;
      expect(!f.onJoin(h, 110, 95.0));
      expect(!f.onGridTempoApplied(h, 120, 110.0));
      expect(h.tempos.empty());
    }

    beginTest("a room tempo change is followed when it takes effect");
    {
      FakeHost h;
      TempoFollow f;
      expect(f.onGridTempoApplied(h, 120, 110.0));
      expectEquals((int)h.tempos.size(), 1);
      expectEquals(h.tempos[0], 120.0);
    }

    beginTest("a failed call reports failure, so the wording falls back");
    {
      FakeHost h;
      h.tempoSucceeds = false;
      TempoFollow f;
      expect(!f.onJoin(h, 110, 95.0));
    }

    beginTest("a zero room tempo is never sent");
    {
      FakeHost h;
      TempoFollow f;
      expect(!f.onJoin(h, 0, 95.0));
      expect(h.tempos.empty());
    }
  }
};

static HostControlTests hostControlTests;

} // namespace
