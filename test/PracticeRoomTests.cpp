#include <BotNames.h>
#include "../src/NinjamBotClient.h"
#include "../src/NinjamClient.h"
#include <PracticeBot.h>
#include "../src/PracticeRoom.h"
#include "FakeNinjamServer.h" // for waitUntil
#include "TimingBounds.h"
#include <limits>
#include <thread>
#include <JuceHeader.h>

// Two things are under test here, and the second matters more than it looks.
//
// That the room works: you connect to 127.0.0.1 like any server and the bots
// arrive as ordinary remote players.
//
// And that the bots are easy to get rid of. They can be pointed at a real
// server, so the failure mode to design against is a bot nobody can evict,
// playing to a room that never asked for it. Every exit route gets a test.

namespace {

struct Joiner : public NinjamClientListener {
  NinjamClient client;
  std::atomic<int> userInfoChanges{0};
  juce::CriticalSection lock;
  juce::Array<juce::String> chats;

  Joiner() { client.addListener(this); }
  ~Joiner() override {
    client.removeListener(this);
    client.disconnectFromServer();
  }

  void onUserInfoChange() override { userInfoChanges.fetch_add(1); }
  void onChatMessage(const juce::String &type, const juce::String &username,
                     const juce::String &text) override {
    juce::ScopedLock sl(lock);
    chats.add(type + "|" + username + "|" + text);
  }

  juce::Array<juce::String> snapshot() const {
    juce::ScopedLock sl(lock);
    return chats;
  }

  bool join(const PracticeRoom &room, const juce::String &name) {
    client.setSampleRate(48000.0);
    client.connectToServer(PracticeRoom::host(), room.port(), name, "");
    return waitUntil([this] { return client.isConnected(); }, 5000);
  }
};

// The bot playing a given instrument, whatever it happens to be called this
// session. Names come from the seed now, so a test that wants "the keys bot"
// has to ask rather than assume.
juce::String botPlaying(const PracticeRoom &room,
                        const juce::String &instrument) {
  for (const auto &n : room.botNames())
    if (n.contains("[" + instrument + "-bot]"))
      return n;
  return {};
}

MusicalKey::Key keyOf(const std::string &name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

// The band introduces itself a few seconds after the first human arrives, so a
// test that starts talking straight away races the roster and counts it as a
// reply. Wait for it to land instead of filtering it out afterwards -- the
// roster is a real thing the room says, and a test that ignored it could not
// tell it apart from a bot answering twice.
// The band arrives silent now, so anything about playing has to start it.
bool startBand(Joiner &you, const PracticeRoom &room) {
  you.client.sendChatMessage("band play");
  return waitUntil(
      [&] {
        const auto phases = room.bandPhases();
        if (phases.empty())
          return false;
        for (auto p : phases)
          if (p != BandPlayState::State::Playing)
            return false;
        return true;
      },
      6000);
}

bool waitForRoster(const Joiner &you) {
  return waitUntil(
      [&] {
        for (const auto &line : you.snapshot())
          if (juce::String(line).contains("say a name to talk to one of us"))
            return true;
        return false;
      },
      12000);
}

// Players, not everybody present. A room always has a conductor now, and it is
// deliberately not one of the band -- no voice, no pump slice, no share of the
// mix -- so a test asking "did the band arrive" must not count it. It has no
// channels at all, which is what tells it apart from a player.
int playersSeen(const NinjamClient &client) {
  int n = 0;
  for (const auto &[name, user] : client.getRemoteUsers())
    if (!user.channels.empty())
      ++n;
  return n;
}

PracticeRoom::Config testConfig(const juce::String &owner = "you") {
  PracticeRoom::Config c;
  c.bpm = 120;
  c.bpi = 8;
  c.sampleRate = 48000.0;
  c.ownerName = owner;
  // Minutes of grace are right for a person whose connection dropped and wrong
  // for a test: what is under test is that the countdown runs and what stops
  // it, never how long three minutes is.
  c.ownerGraceMs = 1200;
  c.initialGraceMs = 60000;
  // Off unless a test asks for it, so that membership and chat are the test's
  // own. The DEFAULT is on -- asserted below rather than relied on here, since
  // a shared fixture quietly carrying it would be the last place anybody would
  // look to find out.
  c.withTutor = false;
  return c;
}

} // namespace

class PracticeRoomTests : public juce::UnitTest {
public:
  PracticeRoomTests() : juce::UnitTest("PracticeRoom", "networking") {}

  void runTest() override {
    runStartupTests();
    runUiResponsivenessTests();
    runBotVisibilityTests();
    runPartCommandTests();
    runBandFollowingTests();
    runOwnerDepartureTests();
    runConnectionLossTests();
  }

  // What the UI does, thirty times a second, on the message thread.
  //
  // The band renders a whole interval in one burst on the pump thread --
  // four synths and four Vorbis encodes, over a second of work -- and it used
  // to hold `botsMutex` for all of it. `botCount()` took the same lock, so the
  // editor's timer blocked behind the render and the meters and phase bar
  // stalled for as long as it took. Audio was unaffected, which is what made
  // it read as a graphics problem: the audio thread never touches this lock.
  //
  // The bound is deliberately loose. This asserts that asking how many bots
  // there are does not wait for a Vorbis encode, not that it takes any
  // particular number of microseconds -- a tight bound here would fail on a
  // loaded CI box for reasons that have nothing to do with the bug.
  void runUiResponsivenessTests() {
    beginTest("asking the room a question does not block behind the render");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      // A SILENT bot transmits nothing and so encodes nothing, which is most
      // of the cost. Without this the render burst never happens and the test
      // passes against the bug.
      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));
      expect(startBand(you, room), "the band never started playing");

      std::atomic<bool> polling{true};
      std::atomic<double> worstMs{0.0};
      std::atomic<int> polls{0};

      std::thread ui([&] {
        while (polling.load()) {
          const auto before = juce::Time::getMillisecondCounterHiRes();
          (void)room.botCount();
          const auto took = juce::Time::getMillisecondCounterHiRes() - before;

          double previous = worstMs.load();
          while (took > previous &&
                 !worstMs.compare_exchange_weak(previous, took)) {
          }
          polls.fetch_add(1);
          juce::Thread::sleep(1);
        }
      });

      // Long enough to cover several interval boundaries at 120/8, which is
      // where the render burst happens.
      juce::Thread::sleep(6000);
      polling = false;
      ui.join();

      expect(polls.load() > 100, "the poller barely ran");
      logMessage("worst botCount() latency: " +
                 juce::String(worstMs.load(), 1) + " ms");
      if (timingbounds::kDistorted)
        logMessage("sanitiser build -- timing bound not asserted");
      else
        expect(worstMs.load() < 100.0,
               "botCount() blocked for " + juce::String(worstMs.load(), 1) +
                   " ms -- the render is holding the lock the UI reads");
    }

    beginTest("the band size is clamped by the room, not by the caller");
    {
      // Pure, so it is driven directly. The cap is the room's business: a chat
      // path or a tutor recruiting a player must not be able to exceed it by
      // not knowing about it, which is why nothing else gets to do this
      // arithmetic.
      expectEquals(PracticeRoom::maxBandSize(true), 8);
      expectEquals(PracticeRoom::maxBandSize(false), 4,
                   "somebody else's server got the loopback allowance");

      // Four voices exist, so four is the ceiling everywhere today even where
      // the cap is eight. When section 16's roles land, this is the assertion
      // that changes and the cap is what starts binding.
      expectEquals(PracticeRoom::voiceableBandSize(8, true), 4);
      expectEquals(PracticeRoom::voiceableBandSize(99, true), 4);
      expectEquals(PracticeRoom::voiceableBandSize(8, false), 4,
                   "the off-loopback cap was not applied");

      expectEquals(PracticeRoom::voiceableBandSize(3, true), 3);
      expectEquals(PracticeRoom::voiceableBandSize(1, true), 1);

      // A room with no band in it is a server, and there is a class for that.
      expectEquals(PracticeRoom::voiceableBandSize(0, true), 1);
      expectEquals(PracticeRoom::voiceableBandSize(-5, true), 1);
    }

    beginTest("a trio is a real room, and it is the right three");
    {
      // Players are added in arranging order, so what a trio drops is the
      // lead rather than whichever voice the enumeration happened to end on.
      auto cfg = testConfig("you");
      cfg.bandSize = 3;

      PracticeRoom room;
      expect(room.start(cfg));
      expectEquals(room.botCount(), 3, "asking for a trio did not give three");

      const auto names = room.botNames();
      expectEquals(names.size(), 3);
      expect(names.joinIntoString(" ").contains("kit"),
             "the trio has no drummer");
      expect(names.joinIntoString(" ").contains("bass"),
             "the trio has no bass");
      expect(!names.joinIntoString(" ").contains("lead"),
             "the trio kept the lead and dropped something else");

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] { return playersSeen(you.client) == 3; }),
             "three bots were made but not all three arrived");
    }

    beginTest("a bot's channel says its role and its instrument");
    {
      // End to end through a real socket, which is the half the bot's own
      // tests cannot reach: that `role: instrument` survives
      // CLIENT_SET_CHANNEL_INFO and arrives at another client as the name of
      // that bot's channel. Everything downstream -- the mixer strip, and
      // addressing a bot by what it plays -- reads it from there.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] { return playersSeen(you.client) == 4; }),
             "the band did not all arrive");

      expect(waitUntil([&] {
               for (const auto &[name, user] : you.client.getRemoteUsers())
                 for (const auto &[index, channel] : user.channels)
                   if (channel.channelName.startsWith("chords: "))
                     return true;
               return false;
             }),
             "no channel arrived named for its role and instrument");

      // Every player, not just the one that happened to be checked first.
      for (const auto &[name, user] : you.client.getRemoteUsers())
        for (const auto &[index, channel] : user.channels)
          expect(channel.channelName.contains(": "),
                 "a bot's channel is not `role: instrument`: " +
                     channel.channelName);
    }

    beginTest("a bot answers to the role its channel advertises");
    {
      // The half the bots' own tests cannot reach: that the role reaches
      // another client over CLIENT_SET_CHANNEL_INFO and comes back as an
      // address that works. `chords` is not in anybody's username and not in
      // the static vocabulary, so the only way this can pass is by the channel
      // name having made the round trip.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));

      const auto before = you.snapshot().size();
      you.client.sendChatMessage("chords, what are you playing?");

      expect(waitUntil([&] { return you.snapshot().size() > before; }),
             "nobody answered to `chords`");
    }

    beginTest("a player can be brought in, up to the cap and no further");
    {
      // Growth in session, and the cap enforced where bots are made rather
      // than where they are asked for -- so no chat path can exceed it by not
      // knowing about it (section 16.9).
      auto cfg = testConfig("you");
      cfg.bandSize = 2;

      PracticeRoom room;
      expect(room.start(cfg));
      expectEquals(room.botCount(), 2);

      Joiner you;
      expect(you.join(room, "you"));

      expect(room.addPlayer(), "a trio would not fit in a room of eight");
      expectEquals(room.botCount(), 3);

      // In arranging order: the third player is the keys, not whichever voice
      // came next in an enumeration.
      expect(room.botNames().joinIntoString(" ").contains("keys"),
             "the player brought in was not the next one a band would add");

      expect(room.addPlayer());
      expectEquals(room.botCount(), 4);

      // And then it stops, because four is all this band can voice. False is
      // the rule speaking, not a failure.
      expect(!room.addPlayer(),
             "a fifth player was created with nothing for it to play");
      expectEquals(room.botCount(), 4);

      // The newcomers are real clients in the room, not just objects.
      expect(waitUntil([&] { return playersSeen(you.client) == 4; }),
             "the players brought in never arrived");
    }

    beginTest("a room brings a tutor unless told not to");
    {
      // The default, pinned. A practice room is where somebody meets the
      // interval model for the first time, and a tutorial nobody switches on
      // teaches nobody.
      const PracticeRoom::Config fresh;
      expect(fresh.withTutor, "the practice room stopped teaching by default");
    }

    beginTest("a room can bring a tutor, and it is not one of the band");
    {
      // End to end through a real socket, which is the half TutorBotTests
      // cannot reach: that the tutor joins as an ordinary client, is visible in
      // the room, and takes no part in the band.
      auto cfg = testConfig("you");
      cfg.withTutor = true;

      PracticeRoom room;
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));

      expect(waitUntil(
                 [&] {
                   for (const auto &m : you.client.getRoomMembers())
                     if (m.username.contains("Tutor"))
                       return true;
                   return false;
                 },
                 6000),
             "the tutor never appeared in the room");

      // It is not in the band: no pump slice, no share of the mix, and
      // botCount is what the UI reports.
      expectEquals(room.botCount(), 4,
                   "the tutor was counted as one of the players");
      for (const auto &n : room.botNames())
        expect(!n.contains("Tutor"), "the tutor turned up in the band list");
    }

    beginTest("a room always has a conductor, tutor or not");
    {
      // The conductor is mandatory: an optional one would mean keeping both
      // coordination mechanisms alive and testing the fallback nobody
      // exercises, which is how the roster race held on Linux until macOS
      // found it. So --no-tutor still leaves a leader in the room.
      auto cfg = testConfig("you");
      cfg.withTutor = false;

      PracticeRoom room;
      expect(room.start(cfg));
      expect(room.hasConductor(), "a room without a tutor still has a leader");

      Joiner you;
      expect(you.join(room, "you"));

      expect(waitUntil(
                 [&] {
                   for (const auto &m : you.client.getRoomMembers())
                     if (m.username.contains("Conductor"))
                       return true;
                   return false;
                 },
                 6000),
             "the conductor never appeared in the room");

      expectEquals(room.botCount(), 4,
                   "the conductor was counted as one of the players");
      for (const auto &n : room.botNames())
        expect(!n.contains("Conductor"),
               "the conductor turned up in the band list");
    }

    beginTest("the first start of a session does not wait for a measurement");
    {
      // A practice room is typically started once and played once, so the FIRST
      // start is the one that matters -- and it is the one with no render
      // history to judge by, because a silent band renders nothing.
      //
      // An earlier version refused to start mid-interval without a measured
      // cost, which put an interval of silence between asking and hearing in
      // exactly the case the mechanism exists for. The cold start leans on the
      // interval instead.
      //
      // At 120/8 an interval is 4 s. Asking within the first half of one
      // should be honoured inside it, so the band is playing well before two
      // intervals have passed.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));

      for (auto p : room.bandPhases())
        expect(p == BandPlayState::State::Silent,
               "the band did not arrive silent, so this proves nothing");

      const auto askedAtMs = juce::Time::getMillisecondCounterHiRes();
      you.client.sendChatMessage("band play");

      // The LATCH, not the state machine. `bandPhases` flips the moment the
      // command lands, so it would report success before a note was rendered
      // and this test would pass with the mechanism removed entirely.
      expect(waitUntil(
                 [&] {
                   const auto latched = room.bandLatchedPhases();
                   if (latched.empty())
                     return false;
                   for (auto p : latched)
                     if (p != BandPlayState::State::Playing)
                       return false;
                   return true;
                 },
                 12000),
             "the band never latched a playing interval");

      const double tookMs =
          juce::Time::getMillisecondCounterHiRes() - askedAtMs;
      logMessage("cold start latched after " + juce::String(tookMs, 0) + " ms");

      // One interval at 120/8 is 4000 ms. Deferring to the head of the next
      // interval would take up to that; taking the start inside the current
      // one is bounded by what is left of it, and the room only accepts when
      // at least half remains. Two seconds is the honest ceiling, and the
      // bound is checked only where a stopwatch means anything.
      if (timingbounds::kDistorted)
        logMessage("sanitiser build -- timing bound not asserted");
      else
        expect(tookMs < 2200.0,
               "the cold start waited " + juce::String(tookMs, 0) +
                   " ms, so it deferred to the next interval instead of "
                   "using what was left of this one");
    }

    beginTest("the band transitions as a band, not one bot at a time");
    {
      // The regression that spreading the renders introduced. Each bot used to
      // sample its own play state inside its own render, and once those renders
      // were seconds apart so were the decisions: asked to stop mid-interval,
      // the bots that had already rendered kept going an interval longer, and
      // the last interval of the ending had only the kit and the bass in it.
      //
      // What is asserted is not WHEN the band changes state but that its
      // members are never in different ones. Sampled far faster than an
      // interval, so a divergence lasting even a fraction of one is caught.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));
      expect(startBand(you, room), "the band never started playing");

      std::atomic<bool> watching{true};
      std::atomic<bool> diverged{false};
      std::atomic<int> samples{0};

      // The LATCH, not the state machine.
      //
      // `bandPhases` reports each bot's own state, and a command reaches four
      // independent bots through four separate callAsync dispatches -- so for
      // the width of that dispatch they genuinely disagree, and watching it
      // makes this test flaky for a reason that is not the bug. The latch is
      // taken for the whole band in one pass under one lock, and it is what
      // decides what gets rendered, so it is both the stable observable and
      // the one that matters.
      std::thread watcher([&] {
        while (watching.load()) {
          const auto phases = room.bandLatchedPhases();
          if (phases.size() > 1) {
            for (std::size_t i = 1; i < phases.size(); ++i)
              if (phases[i] != phases[0])
                diverged = true;
            samples.fetch_add(1);
          }
          juce::Thread::sleep(5);
        }
      });

      // Pumped, not slept: chat reaches the bots through callAsync, so a test
      // that sleeps the main thread blocks the very delivery it is waiting for.
      // Deliberately mid-interval, which is the case that broke -- a stop
      // landing on the boundary would have looked fine all along.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
      you.client.sendChatMessage("band stop");

      // Long enough to cover the wrap-up and the resolve and land in silence,
      // worst case. A stop is taken at the HEAD of the next interval now, so
      // one landing just after a head waits nearly a whole interval before it
      // is even latched: at 120/8 that is 4 s of waiting plus two 4 s ending
      // intervals, and the band is silent some time inside the fourth.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(14000);
      watching = false;
      watcher.join();

      expect(samples.load() > 100, "the watcher barely ran");
      expect(!diverged.load(),
             "the band's bots were in different play states -- the decision is "
             "being taken per render again, not once for the band");

      const auto finalPhases = room.bandLatchedPhases();
      for (auto p : finalPhases)
        expect(p == BandPlayState::State::Silent,
               "a bot was still going after the ending");
    }

    beginTest("the band's compute is spread across the interval, not stacked");
    {
      // The same measurement pointed at a accessor that DOES take the lock, so
      // what it reports is the longest time the room spends inside one render.
      //
      // The band used to render all four bots back to back, which put a
      // second of contiguous compute on every interval boundary -- 1073 ms at
      // 120/8, and the reason a fan spins up. One bot per pump slice
      // makes the longest piece one bot's synth-and-encode instead.
      //
      // EVERY NUMBER IN THIS COMMENT IS FROM AN UNOPTIMISED BUILD, which is
      // what `cmake -B build` produced until the tree started defaulting to
      // RelWithDebInfo. Optimised, one render is about 162 ms rather than
      // 435. The bound below is deliberately NOT retightened to suit: it has
      // to hold on a Debug build too, and the claim being made is that four
      // renders are no longer one, not that a render is any particular speed.
      // Quote the build with the number, or the number means nothing
      // (`PRINCIPLES` section 5).
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));
      expect(startBand(you, room), "the band never started playing");

      std::atomic<bool> polling{true};
      std::atomic<double> worstMs{0.0};

      std::thread prober([&] {
        while (polling.load()) {
          const auto before = juce::Time::getMillisecondCounterHiRes();
          (void)room.botNames();
          const auto took = juce::Time::getMillisecondCounterHiRes() - before;

          double previous = worstMs.load();
          while (took > previous &&
                 !worstMs.compare_exchange_weak(previous, took)) {
          }
          juce::Thread::sleep(1);
        }
      });

      juce::Thread::sleep(6000);
      polling = false;
      prober.join();

      logMessage("longest single render: " + juce::String(worstMs.load(), 1) +
                 " ms (all four back to back measured 1073 ms, both on a "
                 "build with no optimiser in it)");
      if (timingbounds::kDistorted)
        logMessage("sanitiser build -- timing bound not asserted");
      else
        expect(worstMs.load() < 700.0,
               "the longest render was " + juce::String(worstMs.load(), 1) +
                   " ms -- the bots are still rendering back to back");
    }
  }

  void runStartupTests() {
    beginTest("a room starts, binds loopback, and brings a band");
    {
      PracticeRoom room;
      expect(room.start(testConfig()));
      expect(room.isRunning());
      expect(room.port() > 0);
      expectEquals(juce::String(PracticeRoom::host()),
                   juce::String("127.0.0.1"));
      expect(room.botCount() > 0, "the room brought no bots");
    }

    beginTest("a nonsensical tempo is refused rather than guessed at");
    {
      PracticeRoom room;
      auto bad = testConfig();
      bad.bpm = 0;
      expect(!room.start(bad));
      expect(!room.isRunning());
      expectEquals(room.port(), 0, "a refused start left a socket open");
    }

    beginTest("starting twice is safe and leaves one room");
    {
      PracticeRoom room;
      expect(room.start(testConfig()));
      const int first = room.port();
      expect(room.start(testConfig()));
      expect(room.isRunning());
      expect(room.port() != first || first == 0,
             "the second start reused a stale port");
      room.stop();
      expect(!room.isRunning());
    }

    beginTest("stop is idempotent");
    {
      PracticeRoom room;
      expect(room.start(testConfig()));
      room.stop();
      room.stop();
      expect(!room.isRunning());
    }
  }

  void runBotVisibilityTests() {
    beginTest("bots arrive as ordinary remote players");
    {
      // The whole point: nothing in the client knows this room is special.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));

      const auto expected = room.botNames();
      expect(expected.size() > 0);

      expect(waitUntil(
                 [&] {
                   auto users = you.client.getRemoteUsers();
                   for (const auto &n : expected)
                     if (users.count(n) == 0)
                       return false;
                   return true;
                 },
                 5000),
             "the band never appeared in the mixer");

      auto users = you.client.getRemoteUsers();
      expect(users[expected[0]].channels.size() > 0,
             "a bot arrived with no channels");
    }

    beginTest("bot names say they are bots, and can be sent a message");
    {
      // A human reading the mixer deserves to know which strips are not people,
      // and every client sends a private message by splitting on the first
      // space -- so a name with one in it cannot be reached at all. Both
      // properties are checked here because the second is invisible until
      // somebody tries to talk to a bot and nothing happens.
      PracticeRoom room;
      expect(room.start(testConfig()));

      juce::StringArray handles;
      for (const auto &n : room.botNames()) {
        expect(n.endsWith("-bot]"), "bot name does not identify itself: " + n);
        expect(!n.containsChar(' '),
               "a name with a space cannot be sent a private message: " + n);

        // The handle is what a player types to address it, and two bots
        // sharing one would make both unaddressable.
        const auto handle = juce::String(BotNames::handleOf(n.toStdString()));
        expect(juce::String(handle).isNotEmpty(), "no handle in " + n);
        expect(!handles.contains(handle),
               "two bots answer to the same handle: " + handle);
        handles.add(handle);
      }
      expectEquals(handles.size(), 4);
    }
  }

  void runPartCommandTests() {
    beginTest("the part commands are recognised, and nothing else is");
    {
      expect(PracticeBot::isPartCommand("leave"));
      expect(PracticeBot::isPartCommand("exit"));
      expect(PracticeBot::isPartCommand("go away"));
      expect(PracticeBot::isPartCommand("go home"));
      expect(PracticeBot::isPartCommand("  LEAVE  "), "not trimmed or folded");

      // Withdrawn: "part" is the most ordinary word in a jam, and using it for
      // a destructive command put it one word from "what's your part".
      expect(!PracticeBot::isPartCommand("part"));
      expect(!PracticeBot::isPartCommand("whats your part"));

      // Withdrawn for the same reason, and it was the worse of the two: to a
      // musician "stop" is the least destructive thing you can say, and it
      // sent the whole band home. It means stop PLAYING now
      // (docs/BOT-CHAT.md section 15). Bare "go" goes with it -- on its own it
      // is as likely to mean start.
      expect(!PracticeBot::isPartCommand("stop"));
      expect(!PracticeBot::isPartCommand("go"));

      expect(!PracticeBot::isPartCommand("particularly"));
      expect(!PracticeBot::isPartCommand("please leave"));
      expect(!PracticeBot::isPartCommand(""));
    }

    beginTest("the help line says how to remove the bot");
    {
      const auto help = PracticeBot::helpLine("Mirn[kit-bot]");
      expect(juce::String(help).contains("Mirn[kit-bot]"));
      expect(juce::String(help).contains("leave"),
             "help does not name the command");
    }

    beginTest(
        "a private message parts a bot, from someone who does not own it");
    {
      // Anyone in the room may evict a bot. Needing to find its owner first is
      // exactly the annoyance being avoided.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner owner, stranger;
      expect(owner.join(room, "you"));
      expect(stranger.join(room, "someone-else"));

      const auto botName = room.botNames()[0];
      expect(waitUntil(
                 [&] {
                   return stranger.client.getRemoteUsers().count(botName) > 0;
                 },
                 5000),
             "the bot never appeared");

      stranger.client.sendPrivateMessage(botName, "leave");

      expect(waitUntil(
                 [&] {
                   return stranger.client.getRemoteUsers().count(botName) == 0;
                 },
                 5000),
             "the bot ignored a part request from a non-owner");
    }

    beginTest("a bot answers help privately");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));
      Joiner you;
      expect(you.join(room, "you"));

      const auto botName = room.botNames()[0];
      expect(waitUntil(
          [&] { return you.client.getRemoteUsers().count(botName) > 0; },
          5000));

      you.client.sendPrivateMessage(botName, "help");
      expect(waitUntil(
                 [&] {
                   for (const auto &line : you.snapshot())
                     if (juce::String(line).startsWith("PRIVMSG|" + botName) &&
                         juce::String(line).contains("leave"))
                       return true;
                   return false;
                 },
                 5000),
             "the bot did not explain how to remove it");
    }
  }

  void runOwnerDepartureTests() {
    beginTest("an empty room takes the band with it, after the grace");
    {
      // The rule that matters most on a real server: walking away is enough to
      // clean up after yourself, with nothing to remember.
      //
      // Not INSTANTLY, which it used to be. A part was terminal, there is no
      // reconnect by design and the room reaped the object, so a thirty-second
      // blip did not lose the band for thirty seconds -- it destroyed it, and
      // the room ran on with nothing in it (docs/BOT-CHAT.md section 15).
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      {
        Joiner you;
        expect(you.join(room, "you"));
        expect(waitUntil(
                   [&] {
                     return you.client.getRemoteUsers().count(
                                room.botNames()[0]) > 0;
                   },
                   5000),
               "the bot never appeared");
        // `you` disconnects here, leaving nobody at all.
      }

      // Still there immediately afterwards: the grace is the whole point.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
      expect(room.botCount() > 0, "the band went the instant the room emptied");

      expect(waitUntil([&] { return room.botCount() == 0; }, 8000),
             "the band outlived the empty room");
    }

    beginTest("a blip does not lose the band");
    {
      // The case the grace exists for. Leave and come back inside it and the
      // band is still there -- silent, because there was nobody to play to,
      // and waiting to be asked.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.ownerGraceMs = 4000;
      expect(room.start(cfg));

      {
        Joiner you;
        expect(you.join(room, "you"));
        expect(waitUntil(
                   [&] {
                     return you.client.getRemoteUsers().count(
                                room.botNames()[0]) > 0;
                   },
                   5000),
               "the bot never appeared");
      }

      juce::MessageManager::getInstance()->runDispatchLoopUntil(800);
      expect(room.botCount() > 0, "the band did not survive the blip");

      Joiner back;
      expect(back.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return back.client.getRemoteUsers().count(
                              room.botNames()[0]) > 0;
                 },
                 5000),
             "the band was gone when the player came back");

      // The room says the band is still there and how to start it. Not a
      // separate "welcome back" line: the CONDUCTOR's arrival re-arms for the
      // first human in a room, which on a reconnect is you -- so a line of our
      // own would say what it is about to say anyway.
      expect(waitUntil(
                 [&] {
                   for (const auto &line : back.snapshot())
                     if (juce::String(line).contains("bot]") &&
                         juce::String(line).containsIgnoreCase("play"))
                       return true;
                   return false;
                 },
                 12000),
             "nothing told the returning player the band was still there");

      // ...and the countdown really was cancelled, rather than merely
      // outrun: past the original expiry, they are still here.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(5000);
      expect(room.botCount() > 0,
             "the band left anyway after the player came back");
    }

    beginTest("a room that still has people in it keeps its band");
    {
      // The owner is who summoned the band, not who it plays for. Stopping
      // four voices because one person's router hiccuped disrupts everybody
      // who did not drop -- and nothing leaks, because anyone present can send
      // them home.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner watcher;
      expect(watcher.join(room, "watcher"));
      const auto botName = room.botNames()[0];

      {
        Joiner you;
        expect(you.join(room, "you"));
        expect(waitUntil(
                   [&] {
                     return watcher.client.getRemoteUsers().count(botName) > 0;
                   },
                   5000),
               "the bot never appeared");
      }

      // Well past the grace, and still playing for the room.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);
      expect(watcher.client.getRemoteUsers().count(botName) > 0,
             "the band left a room that still had people in it");

      // And whoever is left can still get rid of them, which is what makes
      // staying safe rather than a bot nobody can remove.
      watcher.client.sendChatMessage("leave");
      expect(waitUntil([&] { return room.botCount() == 0; }, 8000),
             "the band could not be dismissed by whoever was left");
    }

    beginTest("an owner who comes and goes unseen still takes the bots");
    {
      // The regression test for a race that made the suite intermittently
      // flaky and, on a real server, would have made a bot immortal.
      //
      // `roomMembers` is maintained on the NETWORK thread the instant a JOIN or
      // PART arrives; listener callbacks reach the bot on the MESSAGE thread
      // afterwards. So a bot that answers "is my owner here?" by scanning that
      // set is asking about a list which may already have moved on -- and an
      // owner who joins and leaves inside one message-thread gap was, as far as
      // the scan can tell, never there at all. The bot never records having
      // seen them, so it never leaves.
      //
      // Reproducing that needs the gap to be real rather than hoped for, which
      // is why this test does its joining and leaving WITHOUT pumping the
      // message loop: `juce::Thread::sleep` on the message thread lets the
      // network thread run and dispatches nothing. Both events are therefore
      // certain to be processed before any callback fires. An earlier version
      // of this test used the ordinary helper, which pumps, and consequently
      // passed with the bug still in place.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      const auto botName = room.botNames()[0];
      expect(waitUntil([&] { return room.botCount() > 0; }, 5000),
             "the bot never appeared");

      {
        NinjamClient you;
        you.setSampleRate(48000.0);
        you.connectToServer(PracticeRoom::host(), room.port(), "you", "");
        juce::Thread::sleep(700); // on the wire, off the message loop
        you.disconnectFromServer();
        juce::Thread::sleep(300);
      }

      juce::ignoreUnused(botName);
      expect(waitUntil([&] { return room.botCount() == 0; }, 8000),
             "a bot outlived an owner it never saw arrive");
    }

    beginTest("a bot does not leave before its owner has ever arrived");
    {
      // Bots connect before the player does, so "owner absent" must not mean
      // "owner has left" until the owner has actually been seen.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      juce::MessageManager::getInstance()->runDispatchLoopUntil(700);
      expect(room.botCount() > 0,
             "the band left before the player ever turned up");
    }
  }

  void runBandFollowingTests() {
    beginTest("the room brings a full band, each voice on its own channel");
    {
      // A rhythm section and a lead, so any one part can be muted or sent home
      // and played by a person instead.
      PracticeRoom room;
      expect(room.start(testConfig()));
      expectEquals(room.botCount(), BotBand::kNumVoices);

      // Which NAME goes to which instrument comes from the room seed, so the
      // assertion is about the instruments being covered rather than about any
      // particular player turning up.
      const auto names = room.botNames();
      for (const char *instrument : {"kit", "bass", "keys", "lead"}) {
        int found = 0;
        for (const auto &n : names)
          if (n.contains(juce::String("[") + instrument + "-bot]"))
            ++found;
        expectEquals(found, 1,
                     juce::String("no single bot plays ") + instrument + ": " +
                         names.joinIntoString(", "));
      }
    }

    beginTest("shake changes the figures");
    {
      PracticeBot bot("Mirn[kit-bot]", {"kit"},
                      std::make_unique<NinjamBotClient>());
      bot.playAs(BotBand::Voice::Drums, MusicalKey::parseName("C major"), 120,
                 8, 48000.0, 7);
      const auto before = bot.currentSettings().seed;
      bot.shake();
      const auto after = bot.currentSettings().seed;
      expect(before != after, "shake did not change the seed");
      expect(std::abs((long long)before - (long long)after) > 1000,
             "shake produced an adjacent seed");
    }

    beginTest("shake is TYPED, and it has to be addressed");
    {
      // The gap this closes: `shake` is documented as a thing you type, and
      // the test above calls the method instead. A feature described as
      // something you say wants a test that says it -- otherwise the chat path
      // can break without anything noticing, which is how this was found.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you));

      const auto seedsNow = [&] {
        std::vector<std::uint32_t> out;
        for (const auto &s : room.bandSettings())
          out.push_back(s.seed);
        return out;
      };
      const auto before = seedsNow();
      expect(!before.empty(), "no band to shake");

      // Unaddressed: nothing happens, because nobody is addressed by default.
      // These are ordinary clients that can join anybody's server, and a band
      // that reacts to every line typed between two people is the thing the
      // chat design refuses.
      you.client.sendChatMessage("shake");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
      expect(seedsNow() == before, "an unaddressed `shake` rerolled the band");

      // Addressed: the whole band rerolls, which is what the manual means.
      you.client.sendChatMessage("band, shake");
      expect(waitUntil([&] { return seedsNow() != before; }),
             "`band, shake` did not reach the band over chat");

      // All of them, not whichever one happened to be listening.
      const auto after = seedsNow();
      for (std::size_t i = 0; i < before.size() && i < after.size(); ++i)
        expect(before[i] != after[i], "one shake left a bot on its old figure");
    }

    beginTest("the shake words are recognised, and nothing else is");
    {
      expect(PracticeBot::isShakeCommand("shake"));
      expect(PracticeBot::isShakeCommand("new"));
      expect(PracticeBot::isShakeCommand(" AGAIN "));
      expect(!PracticeBot::isShakeCommand("shaken"));
      expect(!PracticeBot::isShakeCommand("news"));
      expect(!PracticeBot::isShakeCommand(""));
    }

    beginTest("asking in chat brings a player in");
    {
      // The whole point of the item this closes: growth was reachable from
      // code and not from a room.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bandSize = 3;
      expect(room.start(cfg));
      expectEquals(room.botCount(), 3);

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you), "the band never introduced itself");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band, add a player");
      expect(waitUntil([&] { return room.botCount() == 4; }, 8000),
             "asking for a player did not bring one in");

      // And it said so, naming them. A band that grows silently reads as a
      // process starting.
      expect(waitUntil(
                 [&] {
                   const auto lines = you.snapshot();
                   for (int i = before; i < lines.size(); ++i)
                     if (lines[i].containsIgnoreCase("bringing"))
                       return true;
                   return false;
                 },
                 6000),
             "the band grew and nobody said who arrived");
    }

    beginTest("a full room says no, and says why");
    {
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bandSize = PracticeRoom::maxBandSize(true);
      expect(room.start(cfg));
      const int full = room.botCount();

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you), "the band never introduced itself");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band, add a player");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);

      expectEquals(room.botCount(), full, "a full room grew anyway");

      bool refused = false;
      const auto lines = you.snapshot();
      for (int i = before; i < lines.size(); ++i)
        if (lines[i].containsIgnoreCase("as many"))
          refused = true;
      expect(refused,
             "the room refused silently, which reads as a broken command");
    }

    beginTest("the band introduces itself once, and only once");
    {
      // The one line every player is guaranteed to read, and the only answer to
      // "how would anybody know they can talk to these things". Four separate
      // "X here" lines would read as four processes starting; one roster reads
      // as a band arriving.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return you.client.getRemoteUsers().count(
                              botPlaying(room, "keys")) > 0;
                 },
                 5000),
             "the band never arrived");

      // Five seconds of deliberate delay, plus room to be late.
      expect(waitUntil(
                 [&] {
                   for (const auto &line : you.snapshot())
                     if (juce::String(line).contains("The Understudies"))
                       return true;
                   return false;
                 },
                 9000),
             "no roster was ever posted");

      juce::StringArray roster, instructions, introductions;
      for (const auto &line : you.snapshot()) {
        // `[bot]` rather than `-bot]`: the roster comes from the CONDUCTOR now,
        // and a conductor is not a bandmate, so it carries the role marker
        // rather than an instrument one.
        if (!juce::String(line).startsWith("MSG|") ||
            !juce::String(line).contains("bot]"))
          continue;
        if (juce::String(line).contains("The Understudies"))
          roster.add(line);
        else if (juce::String(line).contains("say a name"))
          instructions.add(line);
        else if (juce::String(line).contains("joining the others"))
          introductions.add(line);
      }

      expectEquals(roster.size(), 1,
                   "the roster was posted " + juce::String(roster.size()) +
                       " times: " + roster.joinIntoString(" / "));
      expectEquals(instructions.size(), 1,
                   "instructions posted more than once");
      expect(introductions.isEmpty(),
             "a bot introduced itself as well as being on the roster: " +
                 introductions.joinIntoString(" / "));

      // Every player is named, with what they play, so the room is legible.
      for (const auto &n : room.botNames()) {
        const auto handle = juce::String(BotNames::handleOf(n.toStdString()));
        expect(roster[0].containsIgnoreCase(handle),
               handle + " is missing from the roster: " + roster[0]);
      }

      // And it leads with the interesting thing. A first-time player who types
      // the first command they are shown should not empty their own room.
      const int nameAt = instructions[0].indexOf("say a name");
      const int partAt = instructions[0].indexOf("leave");
      expect(nameAt >= 0 && partAt > nameAt,
             "the eviction command is offered before the interesting one: " +
                 instructions[0]);
    }

    beginTest("nobody answers a question that was not aimed at anybody");
    {
      // The failure this whole addressing layer exists to prevent, tested end
      // to end rather than in the corpus: four bots answering one question.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return you.client.getRemoteUsers().count(
                              botPlaying(room, "keys")) > 0;
                 },
                 5000),
             "the band never arrived");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("what are you playing");
      you.client.sendChatMessage("what key are we in");
      you.client.sendChatMessage("the bass is a bit loud");

      // Give them every chance to misbehave.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);

      juce::StringArray fromBots;
      for (const auto &line : you.snapshot())
        if (juce::String(line).startsWith("MSG|") &&
            juce::String(line).contains("-bot]"))
          fromBots.add(line);
      expect(fromBots.isEmpty(), "unaddressed chat was answered: " +
                                     fromBots.joinIntoString(" / "));
      expect(you.snapshot().size() >= before);
    }

    beginTest("addressing a bot by name gets exactly that bot");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      // Its name alone, which is the opener: it should say what it is playing.
      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      you.client.sendChatMessage(handle);

      expect(waitUntil(
                 [&] {
                   for (const auto &line : you.snapshot())
                     if (juce::String(line).startsWith("MSG|" + keys + "|"))
                       return true;
                   return false;
                 },
                 4000),
             "the bot did not answer to its own name");

      // And nobody else did.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(800);
      juce::StringArray others;
      for (const auto &line : you.snapshot())
        if (juce::String(line).startsWith("MSG|") &&
            juce::String(line).contains("-bot]") &&
            !juce::String(line).startsWith("MSG|" + keys + "|"))
          others.add(line);
      expect(others.isEmpty(),
             "another bot answered too: " + others.joinIntoString(" / "));
    }

    beginTest("an addressed question is answered with the answer");
    {
      // The counterpart to "nobody answers a question that was not aimed at
      // anybody". That test can pass with the whole answering path dead, and
      // for a while it was the only one over a real socket: silence proves
      // restraint and nothing else.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.key = MusicalKey::parseName("D minor");
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      you.client.sendChatMessage(handle + ": what key are we in");

      expect(waitUntil(
                 [&] {
                   for (const auto &line : you.snapshot())
                     if (juce::String(line).startsWith("MSG|" + keys + "|") &&
                         juce::String(line).containsIgnoreCase("D minor"))
                       return true;
                   return false;
                 },
                 4000),
             "the bot did not say what key the room was in");
    }

    beginTest("the band arrives silent, and the roster says how to start it");
    {
      // Bots connect before the player does, so a band that played on connect
      // played to an empty room -- encoding and sending a full interval every
      // few seconds to nobody for as long as it took you to arrive. Arriving
      // silent also disposes of the wait-forever cost entirely
      // (docs/BOT-CHAT.md section 15).
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return you.client.getRemoteUsers().count(
                              botPlaying(room, "keys")) > 0;
                 },
                 5000),
             "the band never arrived");
      expect(waitForRoster(you), "the band never introduced itself");

      for (auto p : room.bandPhases())
        expect(p == BandPlayState::State::Silent,
               "a bot started playing without being asked");

      // A room where nothing happens looks broken, so the one line anybody
      // reads has to carry the way in.
      bool taught = false;
      for (const auto &line : you.snapshot())
        if (juce::String(line).contains("bot]") &&
            juce::String(line).containsIgnoreCase("play"))
          taught = true;
      expect(taught, "nothing told the room how to start the band");

      you.client.sendChatMessage("band play");
      expect(waitUntil(
                 [&] {
                   for (auto p : room.bandPhases())
                     if (p != BandPlayState::State::Playing)
                       return false;
                   return !room.bandPhases().empty();
                 },
                 5000),
             "the band would not start");
    }

    beginTest("one bot speaks for the band, and all four still act");
    {
      // Reported from a real room: "band stop" got four identical replies.
      // Acting is collective -- every bot ends the tune -- and only the LINE
      // about it is rationed (docs/BOT-CHAT.md section 5).
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return you.client.getRemoteUsers().count(
                              botPlaying(room, "keys")) > 0;
                 },
                 5000),
             "the band never arrived");

      auto botLinesSince = [&](int from) {
        juce::StringArray out;
        const auto all = you.snapshot();
        for (int i = from; i < all.size(); ++i)
          if (all[i].startsWith("MSG|") && all[i].contains("-bot]"))
            out.add(all[i]);
        return out;
      };

      expect(waitForRoster(you), "the band never introduced itself");
      expect(startBand(you, room), "the band would not start");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band stop");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);

      const auto replies = botLinesSince(before);
      expectEquals(replies.size(), 1,
                   "the band answered as a chorus: " +
                       replies.joinIntoString(" / "));
      if (replies.size() == 1)
        expect(replies[0].containsIgnoreCase("we"),
               "the one reply does not speak for the band: " + replies[0]);

      // ...and every bot acted, not just the one that spoke.
      expect(waitUntil(
                 [&] {
                   for (auto p : room.bandPhases())
                     if (p == BandPlayState::State::Playing)
                       return false;
                   return !room.bandPhases().empty();
                 },
                 8000),
             "only the bot that spoke actually stopped");
    }

    beginTest("with the band half stopped, the one that acts speaks");
    {
      // The mixed case, which the "same answer" rule alone gets wrong. Some
      // bots wrap up and some say "already stopped" -- different sentences,
      // but still one thing happening to one band. Whoever won a flat race
      // would answer for everybody, and a silent bot winning would tell the
      // room nothing was happening while the rest ended the tune.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bpm = 240;
      cfg.bpi = 4; // one second per interval, so an ending takes about two
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      expect(waitForRoster(you), "the band never introduced itself");
      expect(startBand(you, room), "the band would not start");

      // Any bot will do, and that is new. This used to stop whichever bot
      // would WIN a flat race, because the delay was ranked by name and
      // picking another would have let the winner answer regardless. The
      // rank is gone with the arrival roster it arbitrated, and command
      // confirmations now wait a FLAT delay -- so nothing about the choice
      // can hide a broken idle penalty, which is the only thing still
      // separating these bots.
      //
      // Sorted rather than first-out-of-the-container, so a failure names
      // the same bot every time.
      auto names = room.botNames();
      names.sort(true);
      const juce::String first = names[0];
      expect(first.isNotEmpty());

      const auto handle = juce::String(BotNames::handleOf(first.toStdString()));
      you.client.sendChatMessage(handle + ": stop");
      expect(waitUntil(
                 [&] {
                   int silent = 0, playing = 0;
                   for (auto p : room.bandPhases()) {
                     if (p == BandPlayState::State::Silent)
                       ++silent;
                     if (p == BandPlayState::State::Playing)
                       ++playing;
                   }
                   return silent >= 1 && playing >= 1;
                 },
                 10000),
             "never reached a half-stopped band");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band stop");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);

      juce::StringArray replies;
      const auto all = you.snapshot();
      for (int i = before; i < all.size(); ++i)
        if (all[i].startsWith("MSG|") && all[i].contains("-bot]"))
          replies.add(all[i]);

      expectEquals(replies.size(), 1,
                   "a half-stopped band answered as a chorus: " +
                       replies.joinIntoString(" / "));
      if (replies.size() == 1)
        expect(replies[0].containsIgnoreCase("wrapping"),
               "a bot with nothing to do answered for the band: " + replies[0]);
    }

    beginTest("each bot answers for itself when the answers differ");
    {
      // The case the arbitration must NOT swallow. "band what are you playing"
      // is four different facts and deserves four replies; collapsing it to
      // one would lose three of them.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
                 [&] {
                   return you.client.getRemoteUsers().count(
                              botPlaying(room, "keys")) > 0;
                 },
                 5000),
             "the band never arrived");

      expect(waitForRoster(you), "the band never introduced itself");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band what are you playing");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);

      juce::StringArray replies;
      const auto all = you.snapshot();
      for (int i = before; i < all.size(); ++i)
        if (all[i].startsWith("MSG|") && all[i].contains("-bot]"))
          replies.add(all[i]);

      expect(replies.size() >= 3,
             "the band gave one answer to a question with four: " +
                 replies.joinIntoString(" / "));
    }

    beginTest("the phase the renderer is given is the phase the bot is in");
    {
      // The seam between the state machine and the sound, which nothing else
      // reaches: `BandPlayState` decides WHEN a bot is ending and
      // `BotBand::Phase` decides what that sounds like, and a bot that tracked
      // its states perfectly while always rendering the groove would pass
      // every other test in this file.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      PracticeBot bot("Probe[kit-bot]", {"kit"},
                      std::make_unique<NinjamBotClient>());
      expect(bot.join(PracticeRoom::host(), room.port(), 48000.0));
      bot.playAs(BotBand::Voice::Drums, keyOf("C major"), 120, 8, 48000.0, 7u);
      bot.startPlaying(); // it joins silent, like every bot now does

      // Replaces the band's own render, which is the point: we care about the
      // phase it is handed, not the audio it would have made from it.
      std::vector<BotBand::Phase> seen;
      bot.setRender([&seen](float *, float *, int, int, BotBand::Phase phase) {
        seen.push_back(phase);
      });

      bot.renderInterval(4800, 0);
      bot.stopPlaying();
      bot.renderInterval(4800, 1);
      bot.renderInterval(4800, 2);
      bot.renderInterval(4800, 3);

      // Three calls, not four: a silent bot does not render at all, let alone
      // transmit an interval of zeroes.
      expectEquals((int)seen.size(), 3, "a silent bot still rendered");
      if (seen.size() == 3) {
        expect(seen[0] == BotBand::Phase::Groove,
               "the tune was not the groove");
        expect(seen[1] == BotBand::Phase::Wrapping, "no wrap-up interval");
        expect(seen[2] == BotBand::Phase::Resolving, "no resolving interval");
      }

      bot.part();
    }

    beginTest("stopping ends the tune over two intervals, and does not leave");
    {
      // The whole point of the four states, end to end over a real socket. A
      // short interval so the ending is observable in about a second rather
      // than twelve (docs/BOT-CHAT.md section 15).
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bpm = 240;
      cfg.bpi = 4; // one second per interval
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      expect(startBand(you, room), "the band would not start");

      auto everyoneIs = [&](BandPlayState::State want) {
        const auto phases = room.bandPhases();
        if (phases.empty())
          return false;
        for (auto p : phases)
          if (p != want)
            return false;
        return true;
      };
      expect(everyoneIs(BandPlayState::State::Playing),
             "the band did not start out playing");

      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      you.client.sendChatMessage(handle + ": stop");

      // Poll fast enough to see the ending happen rather than only its result:
      // the states between playing and silence ARE the ending, and a bot that
      // jumped straight to silence would have none.
      bool sawEnding = false, sawSilent = false;
      for (int i = 0; i < 400 && !sawSilent; ++i) {
        const auto phases = room.bandPhases();
        for (auto p : phases) {
          if (p == BandPlayState::State::Wrapping ||
              p == BandPlayState::State::Resolving)
            sawEnding = true;
        }
        for (auto p : phases)
          if (p == BandPlayState::State::Silent)
            sawSilent = true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
      }

      expect(sawEnding, "the bot went silent without playing an ending");
      expect(sawSilent, "the bot never stopped");

      // Stopping is NOT leaving: it is still in the room, still a remote
      // player, and can be asked to come back.
      expect(room.botCount() > 0, "stopping sent the band home");
      expect(you.client.getRemoteUsers().count(keys) > 0,
             "the bot left the room instead of stopping");

      you.client.sendChatMessage(handle + ": play");
      expect(waitUntil(
                 [&] {
                   for (auto p : room.bandPhases())
                     if (p == BandPlayState::State::Playing)
                       return true;
                   return false;
                 },
                 5000),
             "the bot could not be brought back in");
    }

    beginTest(
        "a bot told to be quiet stops answering, and can be brought back");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      auto linesFrom = [&](const juce::String &who) {
        int n = 0;
        for (const auto &line : you.snapshot())
          if (juce::String(line).startsWith("MSG|" + who + "|"))
            ++n;
        return n;
      };

      you.client.sendChatMessage(handle + ": be quiet");
      expect(waitUntil([&] { return linesFrom(keys) > 0; }, 4000),
             "going quiet was not acknowledged");
      const int afterHush = linesFrom(keys);

      // Directly addressed, and understood -- and still nothing, which is the
      // whole of what was asked for.
      you.client.sendChatMessage(handle + ": what key are we in");
      you.client.sendChatMessage(handle + ": whats your part");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
      expectEquals(linesFrom(keys), afterHush, "a quiet bot kept answering");

      // And the way back, which is the only thing the acknowledgement said.
      you.client.sendChatMessage(handle + ": talk");
      expect(waitUntil([&] { return linesFrom(keys) > afterHush; }, 4000),
             "the bot could not be brought back");

      // Only that bot went quiet: hushing one voice is not hushing the band.
      const auto kit = botPlaying(room, "kit");
      const auto kitHandle =
          juce::String(BotNames::handleOf(kit.toStdString()));
      you.client.sendChatMessage(kitHandle + ": what key are we in");
      expect(waitUntil([&] { return linesFrom(kit) > 0; }, 4000),
             "hushing one bot silenced another");
    }

    beginTest("bots do not answer each other");
    {
      // The invariant that makes a feedback loop impossible rather than
      // unlikely. A bot's own roster line names every other bot, so if this
      // were wrong the room would fill in its opening second.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(
          waitUntil([&] { return you.client.getRemoteUsers().count(keys) > 0; },
                    5000),
          "the band never arrived");

      // Speak as a bot, naming another bot as plainly as possible.
      const auto kit = botPlaying(room, "kit");
      room.practiceServer().broadcastChat(
          kit, juce::String(BotNames::handleOf(keys.toStdString())) +
                   " what are you playing");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);

      juce::StringArray replies;
      for (const auto &line : you.snapshot())
        if (juce::String(line).startsWith("MSG|") &&
            juce::String(line).contains("-bot]") &&
            !juce::String(line).contains("what are you playing"))
          replies.add(line);
      expect(replies.isEmpty(),
             "a bot answered a bot: " + replies.joinIntoString(" / "));
    }

    beginTest("a bot follows a key announced in room chat");
    {
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.key = MusicalKey::parseName("C major");
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
          [&] {
            return you.client.getRemoteUsers().count(botPlaying(room, "keys")) >
                   0;
          },
          5000));

      you.client.sendChatMessage("[key: D minor]");

      // Observable through the room rather than by reaching into a bot: the
      // chords the band is playing are what changed.
      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings())
                     if (s.key.tonic == 2 && Harmony::isMinorish(s.key.mode))
                       return true;
                   return false;
                 },
                 5000),
             "the band ignored the announced key");

      for (const auto &s : room.bandSettings())
        expectEquals(Harmony::flatten(s.chart)[0].root, 2,
                     "the chords did not follow");
    }

    beginTest("a bot follows chords announced in room chat");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
          [&] {
            return you.client.getRemoteUsers().count(botPlaying(room, "keys")) >
                   0;
          },
          5000));

      you.client.sendChatMessage("| Am | F | C | G |");

      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings()) {
                     const auto chords = Harmony::flatten(s.chart);
                     if (chords.size() == 4 && chords[0].root == 9)
                       return true;
                   }
                   return false;
                 },
                 5000),
             "the band ignored the announced chords");
    }

    beginTest(
        "a key change moves a chart the room wrote rather than binning it");
    {
      // The bug DESIGN.md section 6.4 exists to fix: announcing a key called
      // `defaultChart` and threw away a progression somebody had typed. A
      // player who writes a chart and then names the key has not withdrawn the
      // chart -- they have said what it is relative to.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.key = MusicalKey::parseName("C major");
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
          [&] {
            return you.client.getRemoteUsers().count(botPlaying(room, "keys")) >
                   0;
          },
          5000));

      you.client.sendChatMessage("| Am | F | C | G |");
      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings()) {
                     const auto chords = Harmony::flatten(s.chart);
                     if (chords.size() == 4 && chords[0].root == 9)
                       return true;
                   }
                   return false;
                 },
                 5000),
             "the band ignored the announced chords");

      // A tonic move with the mode unchanged is pure transposition: vi IV I V
      // in C is vi IV I V in D, two semitones up.
      you.client.sendChatMessage("[key: D major]");
      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings())
                     if (s.key.tonic == 2 && !Harmony::isMinorish(s.key.mode))
                       return true;
                   return false;
                 },
                 5000),
             "the band ignored the announced key");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(500);

      for (const auto &s : room.bandSettings()) {
        const auto chords = Harmony::flatten(s.chart);
        expectEquals((int)chords.size(), 4, "the chart was replaced");
        const int wanted[] = {11, 7, 2, 9};
        for (int i = 0; i < juce::jmin(4, (int)chords.size()); ++i)
          expectEquals(chords[(size_t)i].root, wanted[i],
                       "the chart did not travel with the key");
      }
    }

    beginTest("a chart written in degrees reaches the band");
    {
      // `parseDegreeChart` existed and nothing in the room called it, so
      // "| ii | V | I |" was not a chart at all where the band could hear it.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.key = MusicalKey::parseName("C major");
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
          [&] {
            return you.client.getRemoteUsers().count(botPlaying(room, "keys")) >
                   0;
          },
          5000));

      you.client.sendChatMessage("| ii | V | I |");
      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings()) {
                     const auto chords = Harmony::flatten(s.chart);
                     if (chords.size() == 3 && chords[0].root == 2 &&
                         chords[1].root == 7 && chords[2].root == 0)
                       return true;
                   }
                   return false;
                 },
                 5000),
             "degrees did not reach the band");
    }

    beginTest("prose in chat does not become a progression");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil(
          [&] {
            return you.client.getRemoteUsers().count(botPlaying(room, "keys")) >
                   0;
          },
          5000));

      const auto before = room.bandSettings();
      you.client.sendChatMessage("I AM TIRED OF THIS");
      you.client.sendChatMessage("anyone here?");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(600);

      const auto after = room.bandSettings();
      expectEquals((int)after.size(), (int)before.size());
      for (size_t i = 0; i < after.size(); ++i)
        expect(Harmony::flatten(after[i].chart) ==
                   Harmony::flatten(before[i].chart),
               "chat prose changed the harmony");
    }

    beginTest("the band follows a tempo change");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] { return room.botCount() == BotBand::kNumVoices; }));

      room.practiceServer().setConfig(96, 12);

      expect(waitUntil(
                 [&] {
                   for (const auto &s : room.bandSettings())
                     if (s.bpm != 96 || s.bpi != 12)
                       return false;
                   return !room.bandSettings().empty();
                 },
                 5000),
             "the band did not follow the tempo");
    }
  }

  void runConnectionLossTests() {
    beginTest("a bot stops when the server goes, and does not come back");
    {
      // Server exits, network drops, an admin kicks it: all the same path, and
      // all terminal. A bot that reconnects is a bot nobody can get rid of.
      PracticeServer server;
      expect(server.start(120, 8));

      PracticeBot bot("Mirn[kit-bot]", {"kit"},
                      std::make_unique<NinjamBotClient>());
      expect(bot.join(PracticeRoom::host(), server.port(), 48000.0));
      expect(waitUntil([&] { return bot.client().isConnected(); }, 5000));
      expect(bot.isActive());

      server.stop();

      expect(waitUntil([&] { return !bot.isActive(); }, 5000),
             "the bot stayed active after the server went");

      // Give any reconnect logic every chance to exist and be caught.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1000);
      expect(!bot.isActive(), "the bot came back");
      expect(!bot.client().isConnected(), "the bot reconnected");
    }

    beginTest("part is idempotent and terminal");
    {
      PracticeServer server;
      expect(server.start(120, 8));

      PracticeBot bot("Mirn[kit-bot]", {"kit"},
                      std::make_unique<NinjamBotClient>());
      expect(bot.join(PracticeRoom::host(), server.port(), 48000.0));
      expect(waitUntil([&] { return bot.client().isConnected(); }, 5000));

      bot.part();
      bot.part();
      expect(!bot.isActive());

      // Rendering after parting must do nothing rather than crash or transmit.
      bot.renderInterval(1024, 0);
      expect(!bot.isActive());
    }

    beginTest("stopping a room removes the band from it");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));
      Joiner you;
      expect(you.join(room, "you"));

      const auto botName = room.botNames()[0];
      expect(waitUntil(
          [&] { return you.client.getRemoteUsers().count(botName) > 0; },
          5000));

      room.stop();
      expectEquals(room.botCount(), 0);
    }
  }
};

static PracticeRoomTests practiceRoomTests;
