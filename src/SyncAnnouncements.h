#pragma once

#include "ClockModel.h"

#include <JuceHeader.h>

#include <cmath>

// What is said about sync, and when -- every sentence, in one place.
//
// The rule (ClockModel.h) is that you are synced or not, and always told which
// and why. A screen reader user hears these; a sighted one reads headerText.
// One sentence per event, composed whole: the join announcement used to be
// three, spoken in the same instant, and Announcer's 400 ms gap dropped the
// two that mattered (the tempo warning among them). Wording follows the spec's
// "What is said" table, docs/superpowers/specs/2026-11-06-sync-and-tempo-design.md,
// and the tests hold it to that table word for word.
//
// Pure: no state, no JUCE beyond juce::String. ASCII only.

namespace SyncAnnouncements {

struct Context {
  juce::String place; // "ninbot.com", or "the practice room"
  int players = 0;    // other people in the room
  int roomBpm = 0;
  int bpi = 0;
  int prevRoomBpm = 0; // before a room change, for onRoomTempoChange
  int prevBpi = 0;
  double dawBpm = 0.0;
  bool dawPlaying = false;
  bool dawHasPosition = true;
  int beat = 0;                // 1-based beat in the interval
  bool reaper = false;         // the host is Reaper and its API was found
  bool reaperFollows = false;  // ... and it follows the room's tempo
  bool reaperSetTempo = false; // Antiphon just set Reaper's tempo
};

inline juce::String bpmText(double bpm) {
  const double whole = std::round(bpm);
  if (std::fabs(bpm - whole) < ClockModel::kTempoTolerance)
    return juce::String((int)whole);
  return juce::String(bpm, 1);
}

inline juce::String dawName(const Context &c) {
  return c.reaper ? "Reaper" : "your DAW";
}

inline juce::String roomLine(const Context &c) {
  return "Room " + juce::String(c.roomBpm) + " BPM, " + juce::String(c.bpi) +
         " beats.";
}

inline juce::String beatText(const Context &c) {
  return "beat " + juce::String(c.beat) + " of " + juce::String(c.bpi);
}

inline juce::String onJoin(ClockModel::State s, const Context &c) {
  using ClockModel::State;
  const juce::String head = "Connected to " + c.place + ". ";
  const juce::String set =
      c.reaperSetTempo ? "Reaper set to " + juce::String(c.roomBpm) + ". "
                       : juce::String();
  const juce::String room = juce::String(c.roomBpm);
  switch (s) {
  case State::Synced:
    return head + roomLine(c) + " " + set + "Synced.";
  case State::NotSyncedTempo:
    return head + "Room " + room + " BPM, your DAW is at " + bpmText(c.dawBpm) +
           ": not synced, but you can hear the room. Set your DAW to " + room +
           " to sync.";
  case State::NotSyncedDawStopped:
    if (ClockModel::tempoMatches(c.dawBpm, c.roomBpm) || c.reaperSetTempo)
      return head + roomLine(c) + " " + set +
             "Hearing the room on Antiphon's clock; press play to sync.";
    return head + roomLine(c) +
           " Hearing the room on Antiphon's clock. Set your DAW to " + room +
           " BPM and press play to sync.";
  case State::NotSyncedNoPosition:
    return head + roomLine(c) +
           " Your DAW reports no playhead position: not synced, but you can "
           "hear the room.";
  default:
    return head + roomLine(c);
  }
}

inline juce::String onRoomTempoChange(ClockModel::State s, const Context &c) {
  const bool bpmChanged = c.prevRoomBpm != c.roomBpm;
  const bool bpiChanged = c.prevBpi != c.bpi;
  juce::String out;
  if (bpmChanged && bpiChanged)
    out = "Room tempo changed to " + juce::String(c.roomBpm) + " BPM, " +
          juce::String(c.bpi) + " beats.";
  else if (bpmChanged)
    out = "Room tempo changed to " + juce::String(c.roomBpm) + " BPM.";
  else if (bpiChanged)
    return "Room interval changed to " + juce::String(c.bpi) + " beats.";
  else
    return {};

  if (c.reaperSetTempo)
    return out + " Reaper set to match.";
  if (s == ClockModel::State::NotSyncedTempo)
    return out + " Your DAW is at " + bpmText(c.dawBpm) +
           ": not synced. Set it to " + juce::String(c.roomBpm) + " to sync.";
  return out;
}

inline juce::String onTransition(ClockModel::State from, ClockModel::State to,
                                 const Context &c) {
  using ClockModel::State;
  if (from == to || from == State::Disconnected || to == State::Disconnected)
    return {};
  switch (to) {
  case State::Synced:
    if (from == State::NotSyncedTempo)
      return "Synced to " + dawName(c) + ".";
    return "Synced to " + dawName(c) + ", " + beatText(c) + ".";
  case State::NotSyncedDawStopped:
    return "DAW stopped. The room is still playing on Antiphon's clock.";
  case State::NotSyncedTempo:
    return "Not synced: your DAW is at " + bpmText(c.dawBpm) +
           " BPM, the room at " + juce::String(c.roomBpm) + ". Set it to " +
           juce::String(c.roomBpm) + " to sync.";
  case State::NotSyncedNoPosition:
    return "Not synced: your DAW reports no playhead position. Hearing the "
           "room on Antiphon's clock.";
  case State::AntiphonClock:
    if (from == State::Stopped)
      return "Playing on Antiphon's clock.";
    return "Antiphon's clock. Not following the DAW.";
  case State::Stopped:
    return "Stopped. You are out of the jam.";
  default:
    return {};
  }
}

inline juce::String status(ClockModel::State s, const Context &c) {
  using ClockModel::State;
  if (s == State::Disconnected)
    return "Not connected.";
  juce::String out =
      "Connected to " + c.place + ", " + juce::String(c.players) +
      (c.players == 1 ? " player. " : " players. ") + roomLine(c) + " ";
  switch (s) {
  case State::Synced:
    return out + "Synced to " + dawName(c) + ", " + beatText(c) + ".";
  case State::NotSyncedTempo:
    return out + "Not synced: your DAW is at " + bpmText(c.dawBpm) +
           " BPM. Hearing the room on Antiphon's clock.";
  case State::NotSyncedDawStopped:
    return out + "DAW stopped. The room is still playing on Antiphon's "
                 "clock. Press play to sync.";
  case State::NotSyncedNoPosition:
    return out + "Not synced: your DAW reports no playhead position. Hearing "
                 "the room on Antiphon's clock.";
  case State::AntiphonClock:
    return out + "On Antiphon's clock, playing.";
  case State::Stopped:
    return out + "On Antiphon's clock, stopped. You are out of the jam.";
  default:
    return out.trimEnd();
  }
}

// What Ctrl+Alt+S does: whatever comes next, from anywhere. When an action will
// change the state, `say` is empty -- the transition it causes says where you
// landed, and two sentences in one instant is how the join warning was lost.
struct SyncNowPlan {
  bool selectSynced = false;
  bool setTempo = false;
  bool pressPlay = false;
  juce::String say;
};

inline SyncNowPlan syncNow(ClockModel::Mode mode, const Context &c) {
  SyncNowPlan p;
  p.selectSynced = mode != ClockModel::Mode::SyncedToDaw;
  if (!c.dawHasPosition) {
    p.say = "Your DAW reports no playhead position, so Antiphon cannot sync "
            "to it.";
    return p;
  }
  const bool tempoOk = ClockModel::tempoMatches(c.dawBpm, c.roomBpm);
  const bool canDrive = c.reaper && c.reaperFollows;
  if (!tempoOk) {
    if (canDrive)
      p.setTempo = true;
    else {
      p.say = "Set your DAW to " + juce::String(c.roomBpm) +
              " BPM. Antiphon syncs as soon as it matches.";
      return p;
    }
  }
  if (!c.dawPlaying) {
    if (c.reaper)
      p.pressPlay = true;
    else if (!p.setTempo)
      p.say = "Press play in your DAW.";
    return p;
  }
  if (!p.selectSynced && !p.setTempo)
    p.say = "In sync, " + beatText(c) + ".";
  return p;
}

// The header's status row, for those who read it rather than hear it.
inline juce::String headerText(ClockModel::State s, const Context &c) {
  using ClockModel::State;
  switch (s) {
  case State::Synced:
    return "Synced to " + juce::String(c.reaper ? "Reaper" : "DAW");
  case State::NotSyncedTempo:
    return "Not synced - set the DAW to " + juce::String(c.roomBpm) +
           " BPM (it is at " + bpmText(c.dawBpm) + ")";
  case State::NotSyncedDawStopped:
    return "DAW stopped - room playing on Antiphon's clock. Press play to sync";
  case State::NotSyncedNoPosition:
    return "Not synced - the DAW reports no position";
  case State::AntiphonClock:
    return "Antiphon's clock - playing";
  case State::Stopped:
    return "Antiphon's clock - stopped, out of the jam";
  default:
    return {};
  }
}

} // namespace SyncAnnouncements
