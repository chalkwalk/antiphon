#pragma once

#include <cmath>

// Synced, or not -- and never something in between.
//
// "Synced" means the DAW is playing at the room's tempo, exactly, and reports a
// playhead position: the interval position is then derived from the playhead,
// so a tempo-synced synth or a loop in the DAW lands on the room's bars, and a
// pause that resumes somewhere else lands correctly rather than drifting.
//
// Anything short of that is "not synced", which is a legitimate, announced
// state: the room keeps playing on our own clock at the room's tempo and your
// live audio keeps going out. Silence is reserved for the one state that asks
// for it -- Antiphon's clock with Play off.
//
// This replaces SyncState and RunGate. Those had an armed Sync button and a
// transport-start edge, and between them five in-between states in which the
// player heard nothing and was told nothing useful
// (docs/superpowers/specs/2026-11-06-sync-and-tempo-design.md).
//
//   Clock          DAW                 Tempo     State                Grid      Jam
//   Synced to DAW  playing, position   matches   Synced               playhead  yes
//   Synced to DAW  playing             differs   NotSyncedTempo       own       yes
//   Synced to DAW  stopped             any       NotSyncedDawStopped  own       yes
//   Synced to DAW  no position         any       NotSyncedNoPosition  own       yes
//   Antiphon's     Play on             any       AntiphonClock        own       yes
//   Antiphon's     Play off            any       Stopped              none      no
//
// Disconnected, nothing changes from before: in a host the offline grid
// follows the DAW, and the standalone has its own transport.
//
// Pure and JUCE-free, because PluginProcessor cannot be compiled into the test
// target and this is the decision every block makes.

namespace ClockModel {

// A room tempo is an integer and every DAW lets you type one exactly. 110.3
// against 110 is a quarter of a percent per interval, which glitches every
// boundary, so "close" is not synced.
inline constexpr double kTempoTolerance = 0.001;

// How long a new state must hold before it is announced, so a tempo ramp
// passing through the room tempo does not chatter. Used by the editor.
inline constexpr int kAnnounceHoldMs = 500;

enum class Mode { SyncedToDaw = 0, AntiphonClock = 1 };

enum class State {
  Disconnected = 0,
  Synced,
  NotSyncedTempo,
  NotSyncedDawStopped,
  NotSyncedNoPosition,
  AntiphonClock,
  Stopped
};

enum class GridSource {
  None,          // nothing advances
  Playhead,      // derived from the DAW's position (PlayheadGrid + HostGrid)
  OwnClock,      // IntervalClock, counted, at the room's tempo
  HostOffline,   // not connected: HostGrid from the DAW, as before
  StandaloneOwn  // the standalone's own transport
};

struct Inputs {
  bool connected = false;
  bool standalone = false;
  Mode mode = Mode::SyncedToDaw;
  bool antiphonPlaying = false;
  bool dawPlaying = false;
  bool dawHasPosition = false;
  double dawBpm = 0.0;
  int roomBpm = 0;
  // Just after a room tempo change that Antiphon is setting in the DAW itself:
  // the DAW lags the grid by a message-thread hop, and that is not a desync.
  bool inTempoGrace = false;
};

struct Outputs {
  State state = State::Disconnected;
  GridSource grid = GridSource::None;
  bool inJam = false;
  // True on exactly the update that enters Synced from any other state.
  bool jumpToPlayhead = false;
};

inline bool tempoMatches(double dawBpm, int roomBpm) {
  if (roomBpm <= 0 || dawBpm <= 0.0)
    return false;
  return std::fabs(dawBpm - (double)roomBpm) <= kTempoTolerance;
}

class Machine {
public:
  Outputs update(const Inputs &in) {
    Outputs o;
    o.state = decide(in);
    o.grid = gridFor(o.state, in);
    o.inJam = in.connected && o.grid != GridSource::None &&
              o.grid != GridSource::HostOffline;
    o.jumpToPlayhead = o.state == State::Synced && state != State::Synced;
    state = o.state;
    return o;
  }

  State get() const { return state; }
  void reset() { state = State::Disconnected; }

private:
  State decide(const Inputs &in) const {
    if (!in.connected)
      return State::Disconnected;
    if (in.standalone || in.mode == Mode::AntiphonClock)
      return in.antiphonPlaying ? State::AntiphonClock : State::Stopped;
    if (!in.dawHasPosition || in.dawBpm <= 0.0)
      return State::NotSyncedNoPosition;
    if (!in.dawPlaying)
      return State::NotSyncedDawStopped;
    if (tempoMatches(in.dawBpm, in.roomBpm))
      return State::Synced;
    if (in.inTempoGrace && state == State::Synced)
      return State::Synced;
    return State::NotSyncedTempo;
  }

  static GridSource gridFor(State s, const Inputs &in) {
    if (in.standalone)
      return in.antiphonPlaying ? GridSource::StandaloneOwn : GridSource::None;
    switch (s) {
    case State::Disconnected:
      if (!in.dawPlaying)
        return GridSource::None;
      return in.dawHasPosition ? GridSource::HostOffline : GridSource::OwnClock;
    case State::Synced:
      return GridSource::Playhead;
    case State::Stopped:
      return GridSource::None;
    default:
      return GridSource::OwnClock;
    }
  }

  State state = State::Disconnected;
};

// For logs and tests. Never user-facing copy: that is SyncAnnouncements.
inline const char *stateName(State s) {
  switch (s) {
  case State::Disconnected:
    return "Disconnected";
  case State::Synced:
    return "Synced";
  case State::NotSyncedTempo:
    return "NotSyncedTempo";
  case State::NotSyncedDawStopped:
    return "NotSyncedDawStopped";
  case State::NotSyncedNoPosition:
    return "NotSyncedNoPosition";
  case State::AntiphonClock:
    return "AntiphonClock";
  case State::Stopped:
    return "Stopped";
  }
  return "";
}

} // namespace ClockModel
