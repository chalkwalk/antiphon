#pragma once

// What is running right now: the clock, the jam, or practice.
//
// These three used to be one condition. `syncState.isRunning()` gated both
// whether the interval clock advanced and whether we were in the jam, and
// SyncState reports Disconnected whenever there is no server -- so offline the
// clock, the metronome, the phase bar and capture all stopped together. That is
// why practice mode needs the split: it wants the grid without the jam.
//
// The model is one sentence. **A transport drives the grid; the connection
// decides what happens to the audio.** In the plugin that transport is the
// host's, in the standalone it is our own.
//
// This is safe to do because Ninjam's absolute interval phase is free: every
// client plays each received interval starting at its own downbeat, so phase
// offsets between clients cancel out per listener. That is exactly why
// PRINCIPLES 9 makes the local metronome the sole authority and drains
// intervalBeginSignal without acting on it. A locally started grid cannot
// desync us from the room.
//
// Note that SyncState::Running deliberately survives a transport stop -- an
// accidental stop must never re-phase a jam, because that truncates the
// interval being transmitted and everyone hears it. So "in step" and "playing
// right now" are separate questions, and `inJam` is the conjunction.
//
// Pure and JUCE-free so every combination can be tested, which matters because
// the invariant below is a safety property and PluginProcessor cannot be
// compiled into the test target.

struct RunGate {
  // The interval clock advances, the metronome sounds, the phase bar moves.
  // True whenever the transport runs, connected or not -- which is what makes
  // Antiphon usable as a plain BPI-aware metronome on its own.
  bool gridRunning = false;

  // Local audio is captured and transmitted, and remote players are mixed in.
  bool inJam = false;

  // Your own audio is played back to you, delayed, as practice.
  bool echoOn = false;
};

// `syncRunning` is SyncState::isRunning(): in step, not necessarily playing.
inline RunGate computeRunGate(bool connected, bool syncRunning,
                              bool transportPlaying, bool practiceEnabled) {
  RunGate g;
  g.gridRunning = transportPlaying;
  g.inJam = connected && syncRunning && transportPlaying;
  // Practice is offline-only, which is what makes "never transmitted" true by
  // construction rather than by remembering to check.
  g.echoOn = practiceEnabled && !connected && transportPlaying;
  return g;
}

// The safety property, stated so it can be asserted rather than assumed:
// transmitting and practising can never both be live, because one requires a
// connection and the other requires none.
inline bool runGateIsSafe(const RunGate &g) { return !(g.inJam && g.echoOn); }
