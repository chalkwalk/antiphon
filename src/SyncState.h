#pragma once

// Getting in step with the DAW.
//
// The reference client integrates with REAPER's timeline and loop points. We
// cannot do that from a generic plugin, and it would be the wrong model anyway:
// in a session/clip view the timeline advances but is musically meaningless,
// and jogging the playhead during a live jam is not a real use case.
//
// So we lock to the DAW's TRANSPORT START instead. That covers both the common
// setups -- clip launching, and a loop sized to the interval -- without needing
// to know anything about the timeline.
//
// The user arms it explicitly with a Sync button rather than it happening on
// every play. Re-phasing mid-jam cuts the interval being transmitted short,
// which other players hear as a glitch, so it should only ever happen when
// asked for.
//
// Pure and free of JUCE so the transitions can be tested directly.

class SyncState {
public:
  enum class State {
    Disconnected,   // not on a server
    TempoMismatch,  // connected, but the DAW is at a different BPM
    ReadyToSync,    // tempo agrees; waiting for the user to press Sync
    WaitingForPlay, // armed; waiting for the DAW transport to start
    Running         // in step, transmitting and playing
  };

  struct Inputs {
    bool connected = false;
    bool tempoMatches = false;
    bool transportPlaying = false;
    // Hosts without a transport (the standalone) skip straight to Running:
    // there is nothing to wait for and nothing to lock to.
    bool hasTransport = true;
    bool syncRequested = false;
  };

  // Returns true when the interval clock should be reset to zero, i.e. this
  // call is the transport-start edge we were armed for.
  bool update(const Inputs &in) {
    const bool startEdge = in.transportPlaying && !wasPlaying;
    wasPlaying = in.transportPlaying;

    if (!in.connected) {
      state = State::Disconnected;
      return false;
    }

    if (!in.hasTransport) {
      // Standalone: run as soon as we are connected.
      const bool entering = state != State::Running;
      state = State::Running;
      return entering;
    }

    if (!in.tempoMatches) {
      // A tempo change invalidates any sync: the grid is a different length.
      state = State::TempoMismatch;
      return false;
    }

    // Written as sequential steps rather than a switch so that several
    // transitions can happen in one call. A Sync press that arrives in the
    // same block as the tempo starting to match would otherwise be dropped,
    // and the button would feel dead.
    if (state == State::Disconnected || state == State::TempoMismatch)
      state = State::ReadyToSync;

    // Deliberately re-armable from Running, but only on request: an accidental
    // transport stop/start must never re-phase a jam in progress, because that
    // truncates the interval being transmitted and other players hear it.
    if (in.syncRequested &&
        (state == State::ReadyToSync || state == State::Running))
      state = State::WaitingForPlay;

    if (state == State::WaitingForPlay && startEdge) {
      state = State::Running;
      return true;
    }
    return false;
  }

  State get() const { return state; }
  bool isRunning() const { return state == State::Running; }

  void reset() {
    state = State::Disconnected;
    wasPlaying = false;
  }

  static const char *describe(State s) {
    switch (s) {
    case State::Disconnected:
      return "Not connected";
    case State::TempoMismatch:
      return "DAW tempo does not match the server -- change it to continue";
    case State::ReadyToSync:
      return "Tempo matches. Press Sync, then start the DAW transport.";
    case State::WaitingForPlay:
      return "Press play in the DAW to join the jam";
    case State::Running:
      return "In sync";
    }
    return "";
  }

private:
  State state = State::Disconnected;
  bool wasPlaying = false;
};
