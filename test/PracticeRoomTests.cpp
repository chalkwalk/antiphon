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

    beginTest("bot names say they are bots");
    {
      // A human reading the mixer deserves to know which strips are not people.
      PracticeRoom room;
      expect(room.start(testConfig()));
      for (const auto &n : room.botNames())
        expect(n.contains("[bot]"), "bot name does not identify itself: " + n);
    }
  }

  void runPartCommandTests() {
    beginTest("the part commands are recognised, and nothing else is");
    {
      expect(PracticeBot::isPartCommand("part"));
      expect(PracticeBot::isPartCommand("leave"));
      expect(PracticeBot::isPartCommand("exit"));
      expect(PracticeBot::isPartCommand("stop"));
      expect(PracticeBot::isPartCommand("  PART  "), "not trimmed or folded");

      expect(!PracticeBot::isPartCommand("particularly"));
      expect(!PracticeBot::isPartCommand("please leave"));
      expect(!PracticeBot::isPartCommand(""));
    }

    beginTest("the help line says how to remove the bot");
    {
      const auto help = PracticeBot::helpLine("Kit [bot]");
      expect(help.contains("Kit [bot]"));
      expect(help.contains("part"), "help does not name the command");
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

      stranger.client.sendPrivateMessage(botName, "part");

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
          if (line.startsWith("PRIVMSG|" + botName) && line.contains("part"))
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
    beginTest("the room brings three voices, each on its own channel");
    {
      PracticeRoom room;
      expect(room.start(testConfig()));
      expectEquals(room.botCount(), 3);

      const auto names = room.botNames();
      expect(names.contains("Kit [bot]"));
      expect(names.contains("Bass [bot]"));
      expect(names.contains("Keys [bot]"));
    }

    beginTest("shake changes the figures");
    {
      PracticeBot bot("Kit [bot]", {"Kit"});
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

    beginTest("a bot follows a key announced in room chat");
    {
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.key = MusicalKey::parseName("C major");
      expect(room.start(cfg));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count("Keys [bot]") > 0;
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
        expectEquals(s.progression[0].root, 2, "the chords did not follow");
    }

    beginTest("a bot follows chords announced in room chat");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count("Keys [bot]") > 0;
      }, 5000));

      you.client.sendChatMessage("| Am | F | C | G |");

      expect(waitUntil([&] {
        for (const auto &s : room.bandSettings())
          if (s.progression.size() == 4 && s.progression[0].root == 9)
            return true;
        return false;
      }, 5000), "the band ignored the announced chords");
    }

    beginTest("prose in chat does not become a progression");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] {
        return you.client.getRemoteUsers().count("Keys [bot]") > 0;
      }, 5000));

      const auto before = room.bandSettings();
      you.client.sendChatMessage("I AM TIRED OF THIS");
      you.client.sendChatMessage("anyone here?");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(600);

      const auto after = room.bandSettings();
      expectEquals((int)after.size(), (int)before.size());
      for (size_t i = 0; i < after.size(); ++i)
        expect(after[i].progression == before[i].progression,
               "chat prose changed the harmony");
    }

    beginTest("the band follows a tempo change");
    {
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitUntil([&] { return room.botCount() == 3; }));

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

      PracticeBot bot("Kit [bot]", {"Kit"});
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

      PracticeBot bot("Kit [bot]", {"Kit"});
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
