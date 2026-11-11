#include <JuceHeader.h>

#include "SyncAnnouncements.h"

namespace {

using ClockModel::Mode;
using ClockModel::State;
namespace SA = SyncAnnouncements;

SA::Context room(double daw = 110.0) {
  SA::Context c;
  c.place = "ninbot.com";
  c.players = 5;
  c.roomBpm = 110;
  c.bpi = 16;
  c.dawBpm = daw;
  c.dawPlaying = true;
  c.beat = 5;
  return c;
}

SA::Context reaper(double daw = 110.0) {
  auto c = room(daw);
  c.reaper = true;
  c.reaperFollows = true;
  return c;
}

bool ascii(const juce::String &s) {
  for (auto *p = s.toRawUTF8(); *p != 0; ++p)
    if ((unsigned char)*p > 127)
      return false;
  return true;
}

class SyncAnnouncementsTests : public juce::UnitTest {
public:
  SyncAnnouncementsTests()
      : juce::UnitTest("SyncAnnouncements", "SyncAnnouncements") {}

  void says(const juce::String &got, const juce::String &want) {
    expect(got == want, "\n  got:  " + got + "\n  want: " + want);
    expect(ascii(got), "non-ASCII in: " + got);
  }

  void runTest() override {
    beginTest("joining, synced");
    {
      auto c = reaper();
      c.reaperSetTempo = true;
      says(SA::onJoin(State::Synced, c),
           "Connected to ninbot.com. Room 110 BPM, 16 beats. Reaper set to "
           "110. Synced.");
      says(SA::onJoin(State::Synced, room()),
           "Connected to ninbot.com. Room 110 BPM, 16 beats. Synced.");
    }

    beginTest("joining at the wrong tempo says so, and that you can hear");
    says(SA::onJoin(State::NotSyncedTempo, room(95.0)),
         "Connected to ninbot.com. Room 110 BPM, your DAW is at 95: not "
         "synced, but you can hear the room. Set your DAW to 110 to sync.");

    beginTest("joining with the DAW stopped");
    {
      auto c = reaper();
      c.dawPlaying = false;
      c.reaperSetTempo = true;
      says(SA::onJoin(State::NotSyncedDawStopped, c),
           "Connected to ninbot.com. Room 110 BPM, 16 beats. Reaper set to "
           "110. Hearing the room on Antiphon's clock; press play to sync.");
      auto o = room(95.0);
      o.dawPlaying = false;
      says(SA::onJoin(State::NotSyncedDawStopped, o),
           "Connected to ninbot.com. Room 110 BPM, 16 beats. Your DAW is at "
           "95 BPM and stopped: not synced, hearing the room on Antiphon's "
           "clock. Set your DAW to 110 BPM and press play to sync.");
    }

    beginTest("a room tempo change");
    {
      auto c = reaper(110.0);
      c.roomBpm = 120;
      c.prevRoomBpm = 110;
      c.prevBpi = 16;
      c.reaperSetTempo = true;
      says(SA::onRoomTempoChange(State::Synced, c),
           "Room tempo changed to 120 BPM. Reaper set to match.");
      auto o = room(110.0);
      o.roomBpm = 120;
      o.prevRoomBpm = 110;
      o.prevBpi = 16;
      says(SA::onRoomTempoChange(State::NotSyncedTempo, o),
           "Room tempo changed to 120 BPM. Your DAW is at 110: not synced. "
           "Set it to 120 to sync.");
      o.dawPlaying = false;
      says(SA::onRoomTempoChange(State::NotSyncedDawStopped, o),
           "Room tempo changed to 120 BPM. Your DAW is at 110: not synced. "
           "Set it to 120 to sync.");
    }

    beginTest("a room BPI change names the beats");
    {
      auto o = room();
      o.prevRoomBpm = 110;
      o.prevBpi = 16;
      o.bpi = 12;
      says(SA::onRoomTempoChange(State::Synced, o),
           "Room interval changed to 12 beats.");
    }

    beginTest("transitions");
    {
      says(SA::onTransition(State::NotSyncedTempo, State::Synced, room()),
           "Synced to your DAW.");
      auto c = room();
      c.beat = 9;
      says(SA::onTransition(State::NotSyncedDawStopped, State::Synced, c),
           "Synced to your DAW, beat 9 of 16.");
      auto r = reaper();
      r.beat = 9;
      says(SA::onTransition(State::NotSyncedDawStopped, State::Synced, r),
           "Synced to Reaper, beat 9 of 16.");
      says(SA::onTransition(State::Synced, State::NotSyncedDawStopped, room()),
           "DAW stopped. The room is still playing on Antiphon's clock.");
      says(SA::onTransition(State::NotSyncedTempo, State::NotSyncedDawStopped,
                            room(95.0)),
           "DAW stopped at 95 BPM, the room at 110. The room is still playing "
           "on Antiphon's clock. Set it to 110 and press play to sync.");
      says(SA::onTransition(State::Synced, State::AntiphonClock, room()),
           "Antiphon's clock. Not following the DAW.");
      says(SA::onTransition(State::Stopped, State::AntiphonClock, room()),
           "Playing on Antiphon's clock.");
      says(SA::onTransition(State::AntiphonClock, State::Stopped, room()),
           "Stopped. You are out of the jam.");
      says(SA::onTransition(State::Synced, State::Synced, room()), "");
      says(SA::onTransition(State::Disconnected, State::Synced, room()), "");
      says(SA::onTransition(State::Synced, State::Disconnected, room()), "");
    }

    beginTest("sync now, from each state");
    {
      auto p = SA::syncNow(Mode::AntiphonClock, room());
      expect(p.selectSynced && !p.setTempo && !p.pressPlay);
      says(p.say, ""); // the transition says where you landed

      p = SA::syncNow(Mode::SyncedToDaw, reaper(95.0));
      expect(p.setTempo && !p.pressPlay);
      says(p.say, "");

      p = SA::syncNow(Mode::SyncedToDaw, room(95.0));
      expect(!p.setTempo && !p.pressPlay);
      says(p.say,
           "Set your DAW to 110 BPM. Antiphon syncs as soon as it matches.");

      auto stopped = reaper();
      stopped.dawPlaying = false;
      p = SA::syncNow(Mode::SyncedToDaw, stopped);
      expect(p.pressPlay && !p.setTempo);

      auto stoppedOther = room();
      stoppedOther.dawPlaying = false;
      p = SA::syncNow(Mode::SyncedToDaw, stoppedOther);
      expect(!p.pressPlay);
      says(p.say, "Press play in your DAW.");

      p = SA::syncNow(Mode::SyncedToDaw, room());
      expect(!p.selectSynced && !p.setTempo && !p.pressPlay);
      says(p.say, "In sync, beat 5 of 16.");
    }

    beginTest("Reaper with following turned off is spoken as any other DAW");
    {
      auto c = reaper(95.0);
      c.reaperFollows = false;
      auto p = SA::syncNow(Mode::SyncedToDaw, c);
      expect(!p.setTempo);
      says(p.say,
           "Set your DAW to 110 BPM. Antiphon syncs as soon as it matches.");
      says(SA::onJoin(State::NotSyncedTempo, c),
           "Connected to ninbot.com. Room 110 BPM, your DAW is at 95: not "
           "synced, but you can hear the room. Set your DAW to 110 to sync.");
    }

    beginTest("status on demand");
    {
      says(SA::status(State::Synced, reaper()),
           "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. Synced "
           "to Reaper, beat 5 of 16.");
      says(SA::status(State::NotSyncedTempo, room(95.0)),
           "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. Not "
           "synced: your DAW is at 95 BPM. Hearing the room on Antiphon's "
           "clock.");
      says(SA::status(State::NotSyncedDawStopped, room()),
           "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. DAW "
           "stopped. The room is still playing on Antiphon's clock. Press play "
           "to sync.");
      says(SA::status(State::NotSyncedDawStopped, room(95.0)),
           "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. Not "
           "synced: your DAW is at 95 BPM and stopped. The room is still "
           "playing on Antiphon's clock. Set your DAW to 110 BPM and press "
           "play to sync.");
      says(SA::status(State::AntiphonClock, room()),
           "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. On "
           "Antiphon's clock, playing.");
      auto one = room();
      one.players = 1;
      says(SA::status(State::AntiphonClock, one),
           "Connected to ninbot.com, 1 player. Room 110 BPM, 16 beats. On "
           "Antiphon's clock, playing.");
      says(SA::status(State::Disconnected, room()), "Not connected.");
    }

    beginTest("a fractional DAW tempo is spoken with one decimal place");
    says(SA::onJoin(State::NotSyncedTempo, room(110.3)),
         "Connected to ninbot.com. Room 110 BPM, your DAW is at 110.3: not "
         "synced, but you can hear the room. Set your DAW to 110 to sync.");

    beginTest("every state has header text, and none is non-ASCII");
    for (int s = 1; s <= (int)State::Stopped; ++s) {
      const auto t = SA::headerText((State)s, room(95.0));
      expect(t.isNotEmpty(),
             juce::String("no header for ") + ClockModel::stateName((State)s));
      expect(ascii(t));
    }
  }
};

static SyncAnnouncementsTests syncAnnouncementsTests;

} // namespace
