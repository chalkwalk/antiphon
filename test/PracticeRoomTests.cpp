#include "../src/BotNames.h"
#include "../src/PracticeBot.h"
#include "../src/PracticeRoom.h"
#include "FakeNinjamServer.h" // for waitUntil
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
juce::String botPlaying(const PracticeRoom &room, const juce::String &instrument) {
  for (const auto &n : room.botNames())
    if (n.contains("[" + instrument + "-bot]"))
      return n;
  return {};
}

MusicalKey::Key keyOf(const juce::String &name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

PracticeRoom::Config testConfig(const juce::String &owner = "you") {
  PracticeRoom::Config c;
  c.bpm = 120;
  c.bpi = 8;
  c.sampleRate = 48000.0;
  c.ownerName = owner;
  return c;
}

} // namespace

class PracticeRoomTests : public juce::UnitTest {
public:
  PracticeRoomTests() : juce::UnitTest("PracticeRoom", "networking") {}

  void runTest() override {
    runStartupTests();
    runBotVisibilityTests();
    runPartCommandTests();
    runBandFollowingTests();
    runOwnerDepartureTests();
    runConnectionLossTests();
  }

  void runStartupTests() {
    beginTest("a room starts, binds loopback, and brings a band");
    {
      PracticeRoom room;
      expect(room.start(testConfig()));
      expect(room.isRunning());
      expect(room.port() > 0);
      expectEquals(juce::String(PracticeRoom::host()), juce::String("127.0.0.1"));
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

      expect(waitUntil([&] {
        auto users = you.client.getRemoteUsers();
        for (const auto &n : expected)
          if (users.count(n) == 0)
            return false;
        return true;
      }, 5000), "the band never appeared in the mixer");

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
        expect(n.endsWith("-bot]"),
               "bot name does not identify itself: " + n);
        expect(!n.containsChar(' '),
               "a name with a space cannot be sent a private message: " + n);

        // The handle is what a player types to address it, and two bots
        // sharing one would make both unaddressable.
        const auto handle = juce::String(BotNames::handleOf(n.toStdString()));
        expect(handle.isNotEmpty(), "no handle in " + n);
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
      expect(help.contains("Mirn[kit-bot]"));
      expect(help.contains("leave"), "help does not name the command");
    }

    beginTest("a private message parts a bot, from someone who does not own it");
    {
      // Anyone in the room may evict a bot. Needing to find its owner first is
      // exactly the annoyance being avoided.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner owner, stranger;
      expect(owner.join(room, "you"));
      expect(stranger.join(room, "someone-else"));

      const auto botName = room.botNames()[0];
      expect(waitUntil([&] {
        return stranger.client.getRemoteUsers().count(botName) > 0;
      }, 5000), "the bot never appeared");

      stranger.client.sendPrivateMessage(botName, "leave");

      expect(waitUntil([&] {
        return stranger.client.getRemoteUsers().count(botName) == 0;
      }, 5000), "the bot ignored a part request from a non-owner");
    }

    beginTest("a bot answers help privately");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));
      Joiner you;
      expect(you.join(room, "you"));

      const auto botName = room.botNames()[0];
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botName) > 0;
      }, 5000));

      you.client.sendPrivateMessage(botName, "help");
      expect(waitUntil([&] {
        for (const auto &line : you.snapshot())
          if (line.startsWith("PRIVMSG|" + botName) && line.contains("leave"))
            return true;
        return false;
      }, 5000), "the bot did not explain how to remove it");
    }
  }

  void runOwnerDepartureTests() {
    beginTest("bots leave when the player who brought them leaves");
    {
      // The rule that matters most on a real server: walking away is enough to
      // clean up after yourself, with nothing to remember.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner watcher;
      expect(watcher.join(room, "watcher"));

      const auto botName = room.botNames()[0];

      {
        Joiner you;
        expect(you.join(room, "you"));
        expect(waitUntil([&] {
          return watcher.client.getRemoteUsers().count(botName) > 0;
        }, 5000), "the bot never appeared");
        // `you` disconnects here.
      }

      expect(waitUntil([&] {
        return watcher.client.getRemoteUsers().count(botName) == 0;
      }, 8000), "the bot outlived the player who brought it");
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

      Joiner watcher;
      expect(watcher.join(room, "watcher"));
      const auto botName = room.botNames()[0];
      expect(waitUntil([&] {
        return watcher.client.getRemoteUsers().count(botName) > 0;
      }, 5000), "the bot never appeared");

      {
        NinjamClient you;
        you.setSampleRate(48000.0);
        you.connectToServer(PracticeRoom::host(), room.port(), "you", "");
        juce::Thread::sleep(700);  // on the wire, off the message loop
        you.disconnectFromServer();
        juce::Thread::sleep(300);
      }

      expect(waitUntil([&] {
        return watcher.client.getRemoteUsers().count(botName) == 0;
      }, 8000), "a bot outlived an owner it never saw arrive");
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
        expectEquals(found, 1, juce::String("no single bot plays ") +
                                   instrument + ": " +
                                   names.joinIntoString(", "));
      }
    }

    beginTest("shake changes the figures");
    {
      PracticeBot bot("Mirn[kit-bot]", {"kit"});
      bot.playAs(BotBand::Voice::Drums, MusicalKey::parseName("C major"), 120,
                 8, 48000.0, 7);
      const auto before = bot.currentSettings().seed;
      bot.shake();
      const auto after = bot.currentSettings().seed;
      expect(before != after, "shake did not change the seed");
      expect(std::abs((long long)before - (long long)after) > 1000,
             "shake produced an adjacent seed");
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000), "the band never arrived");

      // Five seconds of deliberate delay, plus room to be late.
      expect(waitUntil([&] {
        for (const auto &line : you.snapshot())
          if (line.contains("The Understudies"))
            return true;
        return false;
      }, 9000), "no roster was ever posted");

      juce::StringArray roster, instructions, introductions;
      for (const auto &line : you.snapshot()) {
        if (!line.startsWith("MSG|") || !line.contains("-bot]"))
          continue;
        if (line.contains("The Understudies"))
          roster.add(line);
        else if (line.contains("say a name"))
          instructions.add(line);
        else if (line.contains("joining the others"))
          introductions.add(line);
      }

      expectEquals(roster.size(), 1,
                   "the roster was posted " + juce::String(roster.size()) +
                       " times: " + roster.joinIntoString(" / "));
      expectEquals(instructions.size(), 1, "instructions posted more than once");
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

    beginTest("a bot that was never announced announces the band itself");
    {
      // The case a tiebreak cannot handle, and the reason the rule is "announce
      // unless somebody announced ME" rather than "announce if you are first".
      //
      // A bot joining after the roster has gone out was not in it, so it says
      // so -- and it names the band it can SEE, which by then is everybody. The
      // announcement lands when the band is complete rather than being lost
      // because the moment passed.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        for (const auto &line : you.snapshot())
          if (line.contains("The Understudies"))
            return true;
        return false;
      }, 9000), "no first roster");

      const int before = you.snapshot().size();

      // A latecomer, arriving well after the roster it was not part of.
      PracticeBot late("Vurn[horn-bot]", {"horn"});
      late.playAs(BotBand::Voice::Lead, MusicalKey::parseName("C major"), 120, 8,
                  48000.0, 77u);
      expect(late.join(PracticeRoom::host(), room.port(), 48000.0));

      juce::String second;
      expect(waitUntil([&] {
        const auto lines = you.snapshot();
        for (int i = before; i < lines.size(); ++i)
          if (lines[i].startsWith("MSG|Vurn[horn-bot]|")) {
            second = lines[i];
            return true;
          }
        return false;
      }, 9000), "the latecomer never introduced itself");

      // And it named the WHOLE room, not just itself.
      expect(second.containsIgnoreCase("vurn"), "it left itself out: " + second);
      int named = 0;
      for (const auto &n : room.botNames())
        if (second.containsIgnoreCase(
                juce::String(BotNames::handleOf(n.toStdString()))))
          ++named;
      expect(named >= 3, "the latecomer announced only itself: " + second);

      // Nobody who was already announced said anything again.
      juce::StringArray extra;
      const auto lines = you.snapshot();
      for (int i = before; i < lines.size(); ++i)
        if (lines[i].startsWith("MSG|") && lines[i].contains("-bot]") &&
            !lines[i].startsWith("MSG|Vurn[horn-bot]|"))
          extra.add(lines[i]);
      expect(extra.isEmpty(),
             "an already-announced bot spoke again: " + extra.joinIntoString(" / "));

      late.part();
    }

    beginTest("nobody answers a question that was not aimed at anybody");
    {
      // The failure this whole addressing layer exists to prevent, tested end
      // to end rather than in the corpus: four bots answering one question.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000), "the band never arrived");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("what are you playing");
      you.client.sendChatMessage("what key are we in");
      you.client.sendChatMessage("the bass is a bit loud");

      // Give them every chance to misbehave.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);

      juce::StringArray fromBots;
      for (const auto &line : you.snapshot())
        if (line.startsWith("MSG|") && line.contains("-bot]"))
          fromBots.add(line);
      expect(fromBots.isEmpty(),
             "unaddressed chat was answered: " + fromBots.joinIntoString(" / "));
      expect(you.snapshot().size() >= before);
    }

    beginTest("addressing a bot by name gets exactly that bot");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(keys) > 0;
      }, 5000), "the band never arrived");

      // Its name alone, which is the opener: it should say what it is playing.
      const auto handle =
          juce::String(BotNames::handleOf(keys.toStdString()));
      you.client.sendChatMessage(handle);

      expect(waitUntil([&] {
        for (const auto &line : you.snapshot())
          if (line.startsWith("MSG|" + keys + "|"))
            return true;
        return false;
      }, 4000), "the bot did not answer to its own name");

      // And nobody else did.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(800);
      juce::StringArray others;
      for (const auto &line : you.snapshot())
        if (line.startsWith("MSG|") && line.contains("-bot]") &&
            !line.startsWith("MSG|" + keys + "|"))
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(keys) > 0;
      }, 5000), "the band never arrived");

      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      you.client.sendChatMessage(handle + ": what key are we in");

      expect(waitUntil([&] {
        for (const auto &line : you.snapshot())
          if (line.startsWith("MSG|" + keys + "|") &&
              line.containsIgnoreCase("D minor"))
            return true;
        return false;
      }, 4000), "the bot did not say what key the room was in");
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

      PracticeBot bot("Probe[kit-bot]", {"kit"});
      expect(bot.join(PracticeRoom::host(), room.port(), 48000.0));
      bot.playAs(BotBand::Voice::Drums, keyOf("C major"), 120, 8, 48000.0, 7u);

      // Replaces the band's own render, which is the point: we care about the
      // phase it is handed, not the audio it would have made from it.
      std::vector<BotBand::Phase> seen;
      bot.setRender([&seen](juce::AudioBuffer<float> &, int, int,
                            BotBand::Phase phase) { seen.push_back(phase); });

      bot.renderInterval(4800, 0);
      bot.stopPlaying();
      bot.renderInterval(4800, 1);
      bot.renderInterval(4800, 2);
      bot.renderInterval(4800, 3);

      // Three calls, not four: a silent bot does not render at all, let alone
      // transmit an interval of zeroes.
      expectEquals((int)seen.size(), 3, "a silent bot still rendered");
      if (seen.size() == 3) {
        expect(seen[0] == BotBand::Phase::Groove, "the tune was not the groove");
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(keys) > 0;
      }, 5000), "the band never arrived");

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
      expect(waitUntil([&] {
        for (auto p : room.bandPhases())
          if (p == BandPlayState::State::Playing)
            return true;
        return false;
      }, 5000), "the bot could not be brought back in");
    }

    beginTest("a bot told to be quiet stops answering, and can be brought back");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      const auto keys = botPlaying(room, "keys");
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(keys) > 0;
      }, 5000), "the band never arrived");

      const auto handle = juce::String(BotNames::handleOf(keys.toStdString()));
      auto linesFrom = [&](const juce::String &who) {
        int n = 0;
        for (const auto &line : you.snapshot())
          if (line.startsWith("MSG|" + who + "|"))
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
      const auto kitHandle = juce::String(BotNames::handleOf(kit.toStdString()));
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(keys) > 0;
      }, 5000), "the band never arrived");

      // Speak as a bot, naming another bot as plainly as possible.
      const auto kit = botPlaying(room, "kit");
      room.practiceServer().broadcastChat(
          kit, juce::String(BotNames::handleOf(keys.toStdString())) +
                   " what are you playing");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);

      juce::StringArray replies;
      for (const auto &line : you.snapshot())
        if (line.startsWith("MSG|") && line.contains("-bot]") &&
            !line.contains("what are you playing"))
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000));

      you.client.sendChatMessage("[key: D minor]");

      // Observable through the room rather than by reaching into a bot: the
      // chords the band is playing are what changed.
      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings())
          if (s.key.tonic == 2 && Harmony::isMinorish(s.key.mode))
            return true;
        return false;
      }, 5000), "the band ignored the announced key");

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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000));

      you.client.sendChatMessage("| Am | F | C | G |");

      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings()) {
          const auto chords = Harmony::flatten(s.chart);
          if (chords.size() == 4 && chords[0].root == 9)
            return true;
        }
        return false;
      }, 5000), "the band ignored the announced chords");
    }

    beginTest("a key change moves a chart the room wrote rather than binning it");
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000));

      you.client.sendChatMessage("| Am | F | C | G |");
      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings()) {
          const auto chords = Harmony::flatten(s.chart);
          if (chords.size() == 4 && chords[0].root == 9)
            return true;
        }
        return false;
      }, 5000), "the band ignored the announced chords");

      // A tonic move with the mode unchanged is pure transposition: vi IV I V
      // in C is vi IV I V in D, two semitones up.
      you.client.sendChatMessage("[key: D major]");
      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings())
          if (s.key.tonic == 2 && !Harmony::isMinorish(s.key.mode))
            return true;
        return false;
      }, 5000), "the band ignored the announced key");
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000));

      you.client.sendChatMessage("| ii | V | I |");
      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings()) {
          const auto chords = Harmony::flatten(s.chart);
          if (chords.size() == 3 && chords[0].root == 2 &&
              chords[1].root == 7 && chords[2].root == 0)
            return true;
        }
        return false;
      }, 5000), "degrees did not reach the band");
    }

    beginTest("prose in chat does not become a progression");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botPlaying(room, "keys")) > 0;
      }, 5000));

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

      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings())
          if (s.bpm != 96 || s.bpi != 12)
            return false;
        return !room.bandSettings().empty();
      }, 5000), "the band did not follow the tempo");
    }
  }

  void runConnectionLossTests() {
    beginTest("a bot stops when the server goes, and does not come back");
    {
      // Server exits, network drops, an admin kicks it: all the same path, and
      // all terminal. A bot that reconnects is a bot nobody can get rid of.
      PracticeServer server;
      expect(server.start(120, 8));

      PracticeBot bot("Mirn[kit-bot]", {"kit"});
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

      PracticeBot bot("Mirn[kit-bot]", {"kit"});
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
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count(botName) > 0;
      }, 5000));

      room.stop();
      expectEquals(room.botCount(), 0);
    }
  }
};

static PracticeRoomTests practiceRoomTests;
