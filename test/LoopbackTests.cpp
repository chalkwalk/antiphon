#include <JuceHeader.h>

#include "FakeNinjamServer.h"
#include "NinjamClient.h"
#include "NinjamProtocol.h"
#include "TestSignal.h"

#include <vector>

namespace {

// Records listener callbacks so tests can assert on what the plugin layer
// would have seen.
struct RecordingListener : public NinjamClientListener {
  std::atomic<int> connectedCount{0};
  std::atomic<int> disconnectedCount{0};
  std::atomic<int> userInfoCount{0};
  std::atomic<int> lastBpm{0};
  std::atomic<int> lastBpi{0};
  juce::StringArray chatTypes, chatUsers, chatTexts;

  void onConnected() override { connectedCount.fetch_add(1); }
  void onDisconnected(const juce::String &) override {
    disconnectedCount.fetch_add(1);
  }
  void onServerConfig(int bpm, int bpi) override {
    lastBpm.store(bpm);
    lastBpi.store(bpi);
  }
  void onUserInfoChange() override { userInfoCount.fetch_add(1); }
  void onChatMessage(const juce::String &type, const juce::String &user,
                     const juce::String &text) override {
    chatTypes.add(type);
    chatUsers.add(user);
    chatTexts.add(text);
  }
};

// Owns a server and a client wired together, and tears them down in the order
// that avoids the pre-existing destructor race in NinjamClient (which closes
// the socket while run() may still be inside read()).
struct Session {
  FakeNinjamServer server;
  NinjamClient client;
  RecordingListener listener;

  Session() { client.addListener(&listener); }

  ~Session() {
    client.removeListener(&listener);
    client.disconnectFromServer();
    server.stop();
    // Drain anything callAsync left queued.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
  }

  bool connect(double sampleRate = 48000.0, int bpm = 120, int bpi = 8,
               const juce::String &user = "tester",
               const juce::String &pass = "") {
    if (!server.start(bpm, bpi))
      return false;
    client.setSampleRate(sampleRate);
    client.setServerBpm(bpm);
    client.setServerBpi(bpi);
    client.connectToServer("127.0.0.1", server.port(), user, pass);
    return waitUntil([this] { return client.isConnected(); }, 5000);
  }
};

class LoopbackProtocolTests : public juce::UnitTest {
public:
  LoopbackProtocolTests()
      : juce::UnitTest("LoopbackProtocol", "LoopbackProtocol") {}

  void runTest() override {
    beginTest("handshake completes and the on-wire hash is correct");
    {
      Session s;
      expect(s.connect(), "client never reached connected");

      // The client sets connectionState synchronously on the network thread,
      // but onConnected arrives via callAsync.
      expect(waitUntil([&] { return s.listener.connectedCount.load() > 0; }),
             "onConnected never fired");

      auto authPayload = s.server.lastPayloadOfType(0x80);
      expect(authPayload.size() >= 20, "no CLIENT_AUTH_USER received");

      juce::uint8 expected[20];
      NinjamProtocol::computeAuthHash("tester", "", s.server.challengeBytes(),
                                      expected);
      expect(memcmp(authPayload.data(), expected, 20) == 0,
             "auth hash on the wire does not match");

      // The username follows the hash, NUL-terminated.
      const auto *b = static_cast<const juce::uint8 *>(authPayload.data());
      expect(memcmp(b + 20, "tester\0", 7) == 0, "username malformed");

      // CLIENT_SET_CHANNEL_INFO is sent immediately after the grant.
      expect(waitUntil([&] { return s.server.countReceived(0x82) > 0; }),
             "no CLIENT_SET_CHANNEL_INFO after grant");
    }

    beginTest("password is carried into the auth hash");
    {
      Session s;
      expect(s.connect(48000.0, 120, 8, "alice", "secret"));
      auto payload = s.server.lastPayloadOfType(0x80);
      expect(payload.size() >= 20);
      juce::uint8 expected[20];
      NinjamProtocol::computeAuthHash("alice", "secret",
                                      s.server.challengeBytes(), expected);
      expect(memcmp(payload.data(), expected, 20) == 0);
    }

    beginTest("auth denial disconnects cleanly");
    {
      Session s;
      s.server.setGrantAccess(false);
      expect(s.server.start(120, 8));
      s.client.setSampleRate(48000.0);
      s.client.connectToServer("127.0.0.1", s.server.port(), "tester", "");

      expect(waitUntil([&] { return s.listener.disconnectedCount.load() > 0; }),
             "onDisconnected never fired after denial");
      expect(!s.client.isConnected(), "client still reports connected");
    }

    beginTest("server config reaches the listener with exact values");
    {
      Session s;
      expect(s.connect(48000.0, 120, 8));
      expect(waitUntil([&] { return s.listener.lastBpm.load() == 120; }));
      expectEquals(s.listener.lastBpi.load(), 8);

      s.server.sendServerConfig(90, 12);
      expect(waitUntil([&] { return s.listener.lastBpm.load() == 90; }),
             "bpm change not delivered");
      expectEquals(s.listener.lastBpi.load(), 12);
    }

    beginTest("user info creates a remote user and triggers a usermask");
    {
      Session s;
      expect(s.connect());
      s.server.clearReceived();
      s.server.sendUserInfo("peer", 0, "gtr");

      expect(waitUntil([&] { return s.client.getRemoteUsers().size() == 1; }),
             "remote user never appeared");
      auto users = s.client.getRemoteUsers();
      expect(users.count("peer") == 1);
      expectEquals(users["peer"].channels.at(0).channelName,
                   juce::String("gtr"));

      expect(waitUntil([&] { return s.server.countReceived(0x81) > 0; }),
             "no CLIENT_SET_USERMASK after user info");

      auto mask = s.server.lastPayloadOfType(0x81);
      expectEquals((int)mask.size(), 5 + 4); // "peer\0" + 4-byte mask
      const auto *b = static_cast<const juce::uint8 *>(mask.data());
      expect(memcmp(b, "peer\0", 5) == 0);
      expectEquals((int)b[5], 1, "channel 0 bit should be set");
    }

    beginTest("new remote channels default to the reference client's gain");
    {
      // The reference plays remote channels at 0.25 (-12 dB) by default:
      // RemoteUser_Channel::volume is 0.25 and RemoteUser::volume is 1.0, and
      // they are multiplied at mix time (njclient.cpp:2948, :1967). Matching it
      // keeps our balance between "me" and "everyone else" the same as every
      // other client in the session. Verified against the real reference client
      // by the interop tests, which measure 0.253.
      Session s;
      expect(s.connect());
      s.server.sendUserInfo("peer", 0, "gtr");
      expect(waitUntil([&] { return s.client.getRemoteUsers().size() == 1; }));

      auto users = s.client.getRemoteUsers();
      const float vol = users["peer"].channels.at(0).volume;
      expectEquals(vol, NinjamClient::kDefaultRemoteChannelVolume);
      expectEquals(vol, 0.25f,
                   "remote channel default gain must match the reference");
      expectEquals(users["peer"].channels.at(0).pan, 0.0f);

      // Stated in dB as well, because that is how the intent is expressed:
      // remote players sit 12 dB below your own signal.
      const double db = TestSignal::toDb(vol);
      expect(std::fabs(db - (-12.04)) < 0.1, "default remote gain is " +
                                                 juce::String(db, 2) +
                                                 " dB, expected -12.04 dB");
    }

    beginTest("recv toggle clears the channel bit in the usermask");
    {
      Session s;
      expect(s.connect());
      s.server.sendUserInfo("peer", 0, "gtr");
      s.server.sendUserInfo("peer", 2, "vox");
      expect(waitUntil([&] {
        auto u = s.client.getRemoteUsers();
        return u.count("peer") && u["peer"].channels.size() == 2;
      }));

      s.server.clearReceived();
      s.client.setRemoteUserRecv("peer", 2, false);
      expect(waitUntil([&] { return s.server.countReceived(0x81) > 0; }));

      auto mask = s.server.lastPayloadOfType(0x81);
      const auto *b = static_cast<const juce::uint8 *>(mask.data());
      // Channel 0 still on, channel 2 now off -> 0b0001.
      expectEquals((int)b[5], 1);

      s.server.clearReceived();
      s.client.setRemoteUserRecv("peer", 2, true);
      expect(waitUntil([&] { return s.server.countReceived(0x81) > 0; }));
      auto mask2 = s.server.lastPayloadOfType(0x81);
      const auto *b2 = static_cast<const juce::uint8 *>(mask2.data());
      expectEquals((int)b2[5], 5, "channels 0 and 2 -> 0b0101");
    }

    beginTest("user info with active=0 removes the user");
    {
      Session s;
      expect(s.connect());
      s.server.sendUserInfo("peer", 0, "gtr");
      expect(waitUntil([&] { return s.client.getRemoteUsers().size() == 1; }));

      s.server.sendUserInfo("peer", 0, "gtr", false);
      expect(waitUntil([&] { return s.client.getRemoteUsers().empty(); }),
             "user was not removed");
    }

    beginTest("chat messages round-trip in both directions");
    {
      Session s;
      expect(s.connect());

      s.server.sendChat("MSG", "alice", "hello there");
      s.server.sendChat("JOIN", "bob");
      s.server.sendChat("PART", "carol");
      s.server.sendChat("TOPIC", "admin", "jam night");
      s.server.sendChat("PRIVMSG", "dave", "psst");

      expect(waitUntil([&] { return s.listener.chatTypes.size() >= 5; }),
             "not all chat messages arrived");

      auto log = s.client.getChatLog();
      expect(log.size() >= 5);
      expectEquals(log[0].type, juce::String("MSG"));
      expectEquals(log[0].username, juce::String("alice"));
      expectEquals(log[0].text, juce::String("hello there"));
      expectEquals(log[1].text, juce::String("bob joined"));
      expectEquals(log[2].text, juce::String("carol left"));
      expectEquals(log[3].text, juce::String("Topic: jam night"));
      expectEquals(log[4].type, juce::String("PRIVMSG"));
      expectEquals(log[4].text, juce::String("psst"));

      // Outbound.
      s.server.clearReceived();
      s.client.sendChatMessage("hi from the client");
      expect(waitUntil([&] { return s.server.countReceived(0xC0) > 0; }));

      NinjamProtocol::Chat parsed;
      expect(
          NinjamProtocol::parseChat(s.server.lastPayloadOfType(0xC0), parsed));
      expectEquals(juce::String(parsed.type), juce::String("MSG"));
      expectEquals(juce::String(parsed.p1), juce::String("hi from the client"));

      s.server.clearReceived();
      s.client.sendPrivateMessage("bob", "secret");
      expect(waitUntil([&] { return s.server.countReceived(0xC0) > 0; }));
      expect(
          NinjamProtocol::parseChat(s.server.lastPayloadOfType(0xC0), parsed));
      expectEquals(juce::String(parsed.type), juce::String("PRIVMSG"));
      expectEquals(juce::String(parsed.p1), juce::String("bob"));
      expectEquals(juce::String(parsed.p2), juce::String("secret"));
    }

    beginTest("chat log is capped at 100 entries");
    {
      Session s;
      expect(s.connect());
      for (int i = 0; i < 130; ++i)
        s.server.sendChat("MSG", "alice", "m" + juce::String(i));

      expect(waitUntil([&] { return s.client.getChatLog().size() >= 100; }));
      // Give any stragglers a moment, then assert the cap holds.
      waitUntil([] { return false; }, 200);
      auto log = s.client.getChatLog();
      expect(log.size() <= 100, "chat log grew to " + juce::String(log.size()));
      expectEquals(log[log.size() - 1].text, juce::String("m129"));
    }

    beginTest("keep-alive is sent roughly every three seconds");
    {
      Session s;
      expect(s.connect());
      s.server.clearReceived();
      const auto start = juce::Time::getMillisecondCounter();
      expect(waitUntil([&] { return s.server.countReceived(0xFD) >= 2; }, 9000),
             "fewer than two keep-alives in nine seconds");
      const auto elapsed = juce::Time::getMillisecondCounter() - start;
      expect(elapsed >= 2500, "two keep-alives arrived in only " +
                                  juce::String((int)elapsed) + " ms");

      for (const auto &m : s.server.messagesOfType(0xFD))
        expectEquals((int)m.payload.size(), 0,
                     "keep-alive must have an empty payload");
    }

    beginTest("malformed messages are dropped without killing the connection");
    {
      Session s;
      expect(s.connect());

      // A USER_INFO_CHANGE whose username has no terminator, and a chat
      // message that stops mid-field. Both used to read past the payload.
      juce::MemoryBlock bad;
      const juce::uint8 head[6] = {1, 0, 0, 0, 0, 0};
      bad.append(head, 6);
      bad.append("alice", 5); // no NUL
      s.server.sendRaw(0x03, bad.getData(), (int)bad.getSize());

      juce::MemoryBlock badChat;
      badChat.append("MSG\0", 4);
      badChat.append("bob", 3); // no NUL
      s.server.sendRaw(0xC0, badChat.getData(), (int)badChat.getSize());

      // Truncated interval messages.
      const juce::uint8 shortGuid[8] = {1, 2, 3, 4, 5, 6, 7, 8};
      s.server.sendRaw(0x04, shortGuid, 8);
      s.server.sendRaw(0x05, shortGuid, 8);
      s.server.sendRaw(0x02, shortGuid, 1);

      // A well-formed message afterwards must still be processed, which proves
      // the connection survived and the stream stayed in sync.
      s.server.sendChat("MSG", "zoe", "still alive");
      expect(waitUntil([&] {
               auto log = s.client.getChatLog();
               return log.size() > 0 &&
                      log[log.size() - 1].text == "still alive";
             }),
             "connection did not survive malformed input");
      expect(s.client.isConnected());
    }

    beginTest("destroying a client with queued callbacks is safe");
    {
      // Listener callbacks are delivered with callAsync, so a message can
      // still be in the queue when the client is destroyed -- a plugin closed
      // shortly after a disconnect. The queued lambda then runs against a
      // dangling `this`. Under ASan this test reports stack-use-after-return
      // without the aliveFlag guard in NinjamClient.
      FakeNinjamServer server;
      expect(server.start(120, 8));

      {
        NinjamClient shortLived;
        RecordingListener listener;
        shortLived.addListener(&listener);
        shortLived.setSampleRate(48000.0);
        shortLived.connectToServer("127.0.0.1", server.port(), "tester", "");
        expect(waitUntil([&] { return shortLived.isConnected(); }, 5000));

        server.sendChat("MSG", "alice", "queued");
        server.sendUserInfo("peer", 0, "gtr");
        shortLived.disconnectFromServer();

        // Deliberately do NOT pump the message loop here: leave the callbacks
        // queued, then destroy the client and its listener.
        shortLived.removeListener(&listener);
      }

      server.stop();

      // Draining now must not touch the destroyed client.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
      expect(true, "survived draining callbacks queued by a destroyed client");
    }

    beginTest("disconnect and reconnect clears stale remote state");
    {
      Session s;
      expect(s.connect());
      s.server.sendUserInfo("peer", 0, "gtr");
      expect(waitUntil([&] { return s.client.getRemoteUsers().size() == 1; }));

      s.client.disconnectFromServer();
      expect(waitUntil([&] { return !s.client.isConnected(); }));
      s.server.stop();

      FakeNinjamServer server2;
      expect(server2.start(120, 8));
      s.client.setSampleRate(48000.0);
      s.client.connectToServer("127.0.0.1", server2.port(), "tester", "");
      expect(waitUntil([&] { return s.client.isConnected(); }),
             "reconnect failed");

      expect(s.client.getRemoteUsers().empty(),
             "stale remote users survived the reconnect: " +
                 juce::String((int)s.client.getRemoteUsers().size()) +
                 " remain");

      s.client.disconnectFromServer();
      server2.stop();
    }
  }
};

static LoopbackProtocolTests loopbackProtocolTests;

} // namespace
