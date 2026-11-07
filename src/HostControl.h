#pragma once

#include "ClockModel.h"

// What Antiphon may ask of the DAW itself, where the DAW lets it.
//
// No plugin format gives a plugin a way to set the host's tempo or start its
// transport. Reaper does, through its own plugin API (ReaperBridge.cpp), and a
// blind NINJAM player is very likely arriving from Reaper and ReaNINJAM. So in
// Reaper the room's tempo is followed for you; in every other DAW nothing here
// exists and the player is told what to set instead. This is NON-GOALS fence
// #7, narrowed to the one host that offers it.
//
// The interface is pure so the policy is testable; the only implementation is
// compiled into the plugin alone. Message thread only: none of these calls may
// be made from processBlock.

struct HostControl {
  virtual ~HostControl() = default;
  // True only inside Reaper with its API found.
  virtual bool available() const = 0;
  virtual bool setTempo(double bpm) = 0;
  virtual bool play() = 0;
  virtual bool isPlaying() const = 0;
  // Positions in quarter notes, as the playhead reports them.
  virtual bool setLoopBeats(double startPpq, double endPpq) = 0;
};

// When the follow timer should act. A join counts only once this connection's
// room config has arrived: the connection and the config are separate
// messages, and between them the stored tempo is the previous room's -- or the
// default -- which would set Reaper to a wrong tempo, with an undo point, and
// announce it.
struct FollowTrigger {
  enum class Event { None, Joined, TempoApplied };

  Event update(bool connected, bool roomConfigSeen, int tempoChanges) {
    const bool ready = connected && roomConfigSeen;
    const bool joined = ready && !wasReady;
    const bool changed = ready && tempoChanges != seenChanges;
    wasReady = ready;
    seenChanges = tempoChanges;
    if (joined)
      return Event::Joined;
    return changed ? Event::TempoApplied : Event::None;
  }

private:
  bool wasReady = false;
  int seenChanges = 0;
};

// When to set the DAW's tempo. Returns true when it did, which is what the
// announcement says ("Reaper set to 110").
struct TempoFollow {
  bool enabled = true; // persisted as "followRoomTempo"

  bool onJoin(HostControl &host, int roomBpm, double dawBpm) const {
    return follow(host, roomBpm, dawBpm);
  }

  // The grid has just applied a new room tempo -- not when the vote was
  // announced, but when it took effect at the interval boundary.
  bool onGridTempoApplied(HostControl &host, int roomBpm,
                          double dawBpm) const {
    return follow(host, roomBpm, dawBpm);
  }

private:
  bool follow(HostControl &host, int roomBpm, double dawBpm) const {
    if (!enabled || !host.available() || roomBpm <= 0)
      return false;
    if (ClockModel::tempoMatches(dawBpm, roomBpm))
      return false;
    return host.setTempo((double)roomBpm);
  }
};
