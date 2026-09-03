#include "../src/NinjamClient.h"
#include "../src/PracticeServer.h"
#include "FakeNinjamServer.h" // for waitUntil
#include <JuceHeader.h>

// PracticeServer is driven by real NinjamClients rather than by hand-built
// frames, because the thing being tested is that a room served from here is
// indistinguishable from a room on the network. A test that spoke the protocol
// itself could pass while the actual client saw nothing.

namespace {

struct Recording : public NinjamClientListener {
  std::atomic<bool> connected{false};
  std::atomic<int> userInfoChanges{0};
  juce::CriticalSection lock;
  juce::Array<juce::String> chats;
  int bpm = 0, bpi = 0;

  void onConnected() override { connected = true; }
  void onDisconnected(const juce::String &) override { connected = false; }
  void onServerConfig(int b, int i) override {
    bpm = b;
    bpi = i;
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
};

// One client plus its listener, torn down in the order NinjamClient needs.
struct Member {
  NinjamClient client;
  Recording listener;

  Member() { client.addListener(&listener); }
  ~Member() {
    client.removeListener(&listener);
    client.disconnectFromServer();
  }

  bool join(int port, const juce::String &name, double sr = 48000.0) {
    client.setSampleRate(sr);
    client.connectToServer("127.0.0.1", port, name, "");
    return waitUntil([this] { return client.isConnected(); }, 5000);
  }
};

} // namespace

class PracticeServerTests : public juce::UnitTest {
public:
  PracticeServerTests() : juce::UnitTest("PracticeServer", "networking") {}

  void runTest() override {
    runLifecycleTests();
    runRosterTests();
    runUsermaskTests();
    runChatTests();
    runConfigTests();
    runVotingTests();
  }

  void runLifecycleTests() {
    beginTest("binds a loopback port and reports it");
    {
      PracticeServer server;
      expect(server.start(120, 8));
      expect(server.port() > 0, "no port bound");
      expect(server.isListening());
      server.stop();
      expectEquals(server.port(), 0, "port should clear on stop");
    }

    beginTest("the room is reachable on 127.0.0.1 and nowhere else");
    {
      PracticeServer server;
      expect(server.start());

      // Reachable on loopback.
      Member a;
      expect(a.join(server.port(), "alice"), "could not join over loopback");

      // The safety property that replaces practice being offline: the listener
      // is bound to the loopback interface only, so no other address on this
      // machine can reach it. Anything routable would make a practice room
      // visible to the network.
      juce::StreamingSocket outside;
      const auto ips = juce::IPAddress::getAllAddresses();
      for (const auto &ip : ips) {
        if (ip.isNull() || ip.toString().startsWith("127."))
          continue;
        expect(!outside.connect(ip.toString(), server.port(), 400),
               "practice room answered on " + ip.toString());
      }
    }

    beginTest("a second player joins the same room");
    {
      PracticeServer server;
      expect(server.start());
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      auto names = server.connectedUsernames();
      expect(names.contains("alice"));
      expect(names.contains("bob"));
    }

    beginTest("a duplicate name is made unique rather than shadowing");
    {
      // Two players sharing a name would collide in NinjamClient's
      // (username, channelIndex) slot key and mix into each other.
      PracticeServer server;
      expect(server.start());
      Member a, b;
      expect(a.join(server.port(), "sam"));
      expect(b.join(server.port(), "sam"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      auto names = server.connectedUsernames();
      expectEquals(names.size(), 2);
      expect(names[0] != names[1], "duplicate names were not disambiguated");
    }
  }

  void runRosterTests() {
    beginTest("a joining player learns who is already in the room");
    {
      PracticeServer server;
      expect(server.start());

      Member a;
      expect(a.join(server.port(), "alice"));
      a.client.updateChannelInfo({"gtr"});

      // Bob arrives after alice has declared a channel, so he must be told
      // about it on the way in rather than only on the next change.
      Member b;
      expect(b.join(server.port(), "bob"));

      expect(waitUntil([&] {
               auto users = b.client.getRemoteUsers();
               auto it = users.find("alice");
               return it != users.end() && it->second.channels.count(0) > 0;
             }),
             "bob never saw alice's channel");

      auto users = b.client.getRemoteUsers();
      expectEquals(users["alice"].channels[0].channelName, juce::String("gtr"));
    }

    beginTest("a channel declared later reaches everyone already present");
    {
      PracticeServer server;
      expect(server.start());
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.updateChannelInfo({"gtr", "vox"});

      expect(waitUntil([&] {
               auto users = b.client.getRemoteUsers();
               auto it = users.find("alice");
               return it != users.end() && it->second.channels.size() == 2;
             }),
             "bob never saw alice's two channels");

      auto users = b.client.getRemoteUsers();
      expectEquals(users["alice"].channels[1].channelName, juce::String("vox"));
    }

    beginTest("a departing player's channels are retired");
    {
      PracticeServer server;
      expect(server.start());
      Member b;
      expect(b.join(server.port(), "bob"));

      {
        Member a;
        expect(a.join(server.port(), "alice"));
        a.client.updateChannelInfo({"gtr"});
        expect(waitUntil([&] {
                 return b.client.getRemoteUsers().count("alice") > 0;
               }),
               "bob never saw alice arrive");
      }

      expect(waitUntil(
                 [&] { return b.client.getRemoteUsers().count("alice") == 0; }),
             "alice's channels outlived her connection");
    }
  }

  void runUsermaskTests() {
    beginTest("audio only reaches a subscriber");
    {
      // The memory argument for the whole bot design: a client that has not
      // subscribed receives nothing, so a deaf bot never causes an interval
      // buffer to be allocated at the far end.
      PracticeServer server;
      expect(server.start(120, 8));

      Member sender, listenerA, deaf;
      expect(sender.join(server.port(), "sender"));
      expect(listenerA.join(server.port(), "listener"));
      expect(deaf.join(server.port(), "deaf"));
      sender.client.updateChannelInfo({"gtr"});

      expect(waitUntil([&] {
               return listenerA.client.getRemoteUsers().count("sender") > 0 &&
                      deaf.client.getRemoteUsers().count("sender") > 0;
             }),
             "the room never converged");

      // NinjamClient subscribes to everyone it learns about; turning recv off
      // is how a bot goes deaf, and it is the same public call a user makes
      // with the Recv button.
      deaf.client.setRemoteUserRecv("sender", 0, false);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

      juce::AudioBuffer<float> tone(2, 4096);
      fillTone(tone, 440.0f, 48000.0);
      sender.client.processCapturedAudio(tone, tone.getNumSamples(), 0, false);

      // Wait for the interval to be fully decoded before swapping. Swapping
      // repeatedly would discard the very interval being waited for, which is
      // what diagSamplesDroppedOnSwap counts.
      expect(waitUntil(
                 [&] {
                   return listenerA.client.diagLastIntervalSamples.load() > 0;
                 },
                 5000),
             "the subscriber never decoded an interval");
      expect(renderPeak(listenerA.client) > 0.0f,
             "the subscriber decoded an interval but heard nothing");

      // Give the unsubscribed client every chance to be wrong.
      juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
      expectEquals(deaf.client.diagLastIntervalSamples.load(), 0,
                   "an unsubscribed client received audio");
    }

    beginTest("a sender never receives its own audio back");
    {
      PracticeServer server;
      expect(server.start());
      Member solo;
      expect(solo.join(server.port(), "solo"));
      solo.client.updateChannelInfo({"gtr"});

      juce::AudioBuffer<float> tone(2, 4096);
      fillTone(tone, 440.0f, 48000.0);
      solo.client.processCapturedAudio(tone, tone.getNumSamples(), 0, false);

      juce::MessageManager::getInstance()->runDispatchLoopUntil(500);
      expectEquals(solo.client.diagLastIntervalSamples.load(), 0,
                   "the room echoed a player back to themselves");
    }
  }

  void runChatTests() {
    beginTest("chat reaches the room, attributed to the sender");
    {
      PracticeServer server;
      expect(server.start());
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("hello room");

      expect(waitUntil([&] {
               for (const auto &line : b.listener.snapshot())
                 if (line == "MSG|alice|hello room")
                   return true;
               return false;
             }),
             "bob never received alice's message");

      // The sender sees their own message too, which is how the reference
      // server behaves and what the chat pane expects.
      expect(waitUntil([&] {
               for (const auto &line : a.listener.snapshot())
                 if (line == "MSG|alice|hello room")
                   return true;
               return false;
             }),
             "alice never saw her own message");
    }

    beginTest("the server can speak into the room");
    {
      PracticeServer server;
      expect(server.start());
      Member a;
      expect(a.join(server.port(), "alice"));

      server.broadcastChat("Mirn[kit-bot]", "counting you in");
      expect(waitUntil([&] {
               for (const auto &line : a.listener.snapshot())
                 if (line == "MSG|Mirn[kit-bot]|counting you in")
                   return true;
               return false;
             }),
             "a server-originated line never arrived");
    }

    beginTest("a topic set before joining is delivered on arrival");
    {
      PracticeServer server;
      expect(server.start());
      server.setTopic("practice room");

      Member a;
      expect(a.join(server.port(), "alice"));
      expect(waitUntil([&] {
               for (const auto &line : a.listener.snapshot())
                 if (line.startsWith("TOPIC|") &&
                     line.endsWith("practice room"))
                   return true;
               return false;
             }),
             "the topic was not sent to a joining player");
    }
  }

  void runConfigTests() {
    beginTest("tempo and BPI reach a joining player");
    {
      PracticeServer server;
      expect(server.start(96, 12));
      Member a;
      expect(a.join(server.port(), "alice"));
      expect(waitUntil([&] { return a.listener.bpm == 96; }));
      expectEquals(a.listener.bpm, 96);
      expectEquals(a.listener.bpi, 12);
    }

    beginTest("a tempo change is broadcast to everyone");
    {
      PracticeServer server;
      expect(server.start(120, 8));
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      server.setConfig(140, 16);
      expect(waitUntil([&] {
               return a.listener.bpm == 140 && b.listener.bpm == 140;
             }),
             "the tempo change did not reach both players");
      expectEquals(a.listener.bpi, 16);
      expectEquals(b.listener.bpi, 16);
      expectEquals(server.bpm(), 140);
    }
  }

private:
  static void fillTone(juce::AudioBuffer<float> &buf, float freq,
                       double sampleRate) {
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
      auto *w = buf.getWritePointer(ch);
      for (int i = 0; i < buf.getNumSamples(); ++i)
        w[i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * freq *
                               (float)i / (float)sampleRate);
    }
  }

  // -------------------------------------------------------------------------
  // Voting
  // -------------------------------------------------------------------------
  //
  // The arithmetic has its own tests in chalkwalk-ninjam. What is tested here
  // is the part that only exists once a socket does: that a vote is consumed
  // rather than relayed, that the room is told, that carrying actually moves
  // the tempo, and that the threshold counts everybody rather than the voters.

  // The vote lines are server notices, so they arrive with an empty username.
  static bool sawVoteLine(const Member &m, const juce::String &contains) {
    for (const auto &line : m.listener.snapshot())
      if (line.startsWith("MSG||[voting system]") && line.contains(contains))
        return true;
    return false;
  }

  void runVotingTests() {
    beginTest("a vote is consumed, not relayed as chat");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("!vote bpm 130");

      // Something must arrive, or the check below proves nothing. Which line
      // it is depends on the threshold -- one of two carries at once at 50% --
      // so this asks only that the room heard the voting system.
      expect(waitUntil([&] { return sawVoteLine(b, "130"); }),
             "the room was never told about the vote");

      for (const auto &line : b.listener.snapshot())
        expect(!line.contains("!vote bpm 130"),
               "the command itself reached the room: " + line);
    }

    beginTest("the tally counts everyone, not just the voters");
    {
      // Two users at 50% need one vote; three need two. So the same single
      // vote carries in a room of two and does not in a room of three, which
      // is the whole of \"not voting is voting against\".
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b, c;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(c.join(server.port(), "carol"));
      expect(waitUntil([&] { return server.clientCount() == 3; }));

      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return sawVoteLine(b, "1/2 votes for 130 BPM"); }),
             "one of three at 50% should need one more");
      expect(server.bpm() == 100, "the tempo moved on a minority vote");
    }

    beginTest("a carried vote moves the tempo for everyone");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b, c;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(c.join(server.port(), "carol"));
      expect(waitUntil([&] { return server.clientCount() == 3; }));

      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return sawVoteLine(c, "leading candidate"); }));
      b.client.sendChatMessage("!vote bpm 130");

      expect(waitUntil([&] { return sawVoteLine(c, "setting BPM to 130"); }),
             "the room was never told the vote carried");
      expect(waitUntil([&] { return server.bpm() == 130; }),
             "the server kept the old tempo");
      expect(waitUntil([&] { return c.listener.bpm == 130; }),
             "a non-voter never got the new tempo");
      expect(server.bpi() == 8, "a BPM vote changed the BPI");
    }

    beginTest("carrying clears the poll");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      // One of two carries it at 50%, so this lands immediately.
      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return server.bpm() == 130; }));

      // If the spent vote had survived, this second one would carry at once
      // and take the tempo to 140 with a single voter behind it.
      b.client.sendChatMessage("!vote bpm 140");
      expect(waitUntil([&] { return server.bpm() == 140; }),
             "a fresh majority of one should still carry at 50%");
    }

    beginTest("a vote below the threshold does not carry");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(100, 120); // unanimity
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return sawVoteLine(b, "1/2 votes for 130 BPM"); }));
      expect(server.bpm() == 100);

      b.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return server.bpm() == 130; }),
             "unanimous and still refused");
    }

    beginTest("BPI votes are a separate poll");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("!vote bpi 16");
      expect(waitUntil([&] { return server.bpi() == 16; }));
      expect(server.bpm() == 100, "a BPI vote changed the BPM");
      expect(waitUntil([&] { return b.listener.bpi == 16; }));
    }

    beginTest("an out-of-range vote is answered as a malformed one");
    {
      // The server does not say what was wrong with it, and neither do we --
      // a client taught to expect a clearer answer here would be taught wrong
      // (docs/PROTOCOL.md).
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(50, 120);
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("!vote bpm 30"); // below MIN_BPM
      expect(waitUntil([&] { return sawVoteLine(a, "!vote requires"); }),
             "no complaint reached the voter");
      expect(server.bpm() == 100);

      // The complaint is private: the room does not need to see it.
      expect(!sawVoteLine(b, "!vote requires"),
             "the complaint was broadcast to the room");
    }

    beginTest("voting can be disabled, and says so");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      Member a; // no setVoting call: off is the default
      expect(a.join(server.port(), "alice"));
      expect(waitUntil([&] { return server.clientCount() == 1; }));

      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return sawVoteLine(a, "Voting not enabled"); }));
      expect(server.bpm() == 100);
    }

    beginTest("a stale vote stops counting");
    {
      PracticeServer server;
      expect(server.start(100, 8));
      server.setVoting(100, 1); // unanimity, and a one-second memory
      Member a, b;
      expect(a.join(server.port(), "alice"));
      expect(b.join(server.port(), "bob"));
      expect(waitUntil([&] { return server.clientCount() == 2; }));

      a.client.sendChatMessage("!vote bpm 130");
      expect(waitUntil([&] { return sawVoteLine(b, "1/2 votes for 130 BPM"); }));

      // Wall clock, because that is what the server keeps: the vote must fall
      // out of the window before bob's arrives, so bob's is 1/2 and not 2/2.
      juce::Thread::sleep(2100);
      b.client.sendChatMessage("!vote bpm 130");

      expect(waitUntil([&] { return sawVoteLine(b, "leading candidate"); }));
      expect(server.bpm() == 100,
             "an expired vote was still counted towards unanimity");
    }
  }

  // Vorbis is lossy and has codec delay, so the question is only ever "was
  // there energy", never "were these samples equal" (AGENTS.md).
  //
  // Swaps exactly once: each swap retires whatever the audio thread has not
  // consumed, so swapping in a loop throws away the interval being measured.
  static float renderPeak(NinjamClient &client, int numSamples = 32768) {
    client.swapIntervalBuffers();

    const int blockSize = 512;
    juce::AudioBuffer<float> block(2, blockSize);
    float peak = 0.0f;
    for (int pos = 0; pos < numSamples; pos += blockSize) {
      const int n = std::min(blockSize, numSamples - pos);
      block.clear();
      juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), 2, n);
      client.getDecodedAudio(view);
      peak = std::max(peak, view.getMagnitude(0, n));
    }
    return peak;
  }
};

static PracticeServerTests practiceServerTests;
