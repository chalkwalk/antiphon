#include "../src/PracticeBot.h"
#include <JuceHeader.h>

// PracticeBot, with no socket and no room.
//
// It could not be tested this way before: it owned a `NinjamClient`, so every
// question about it -- does it answer, does it leave, does it stop rendering --
// needed a server, a thread and several seconds of waiting. `PracticeRoomTests`
// does that and takes three minutes, which is why the roadmap has carried "no
// test file of its own" since the class was written.
//
// The interface is what changes that. A fake client is thirty lines, the bot
// cannot tell the difference, and the answers arrive synchronously.

namespace {

// Records what the bot said and lets a test say what the room did.
class FakeClient final : public BotClient::Client {
public:
  std::vector<std::string> said;
  std::vector<std::pair<std::string, std::string>> whispered;
  std::vector<BotClient::Member> room;
  int intervalsSent = 0;
  bool connected = false;

  void say(const std::string &who, const std::string &what) {
    for (auto *l : listeners)
      l->onChatMessage("MSG", who, what);
  }
  void joins(const std::string &who) {
    room.push_back({who, 1});
    for (auto *l : listeners)
      l->onRoomMembershipChange(who, true);
  }
  void leaves(const std::string &who) {
    room.erase(std::remove_if(room.begin(), room.end(),
                              [&](const auto &m) { return m.username == who; }),
               room.end());
    for (auto *l : listeners)
      l->onRoomMembershipChange(who, false);
  }

  void addListener(BotClient::Listener *l) override { listeners.push_back(l); }
  void removeListener(BotClient::Listener *l) override {
    listeners.erase(std::remove(listeners.begin(), listeners.end(), l),
                    listeners.end());
  }
  void setSampleRate(double) override {}
  void setChannels(const std::vector<std::string> &) override {}
  void setDefaultRecvEnabled(bool) override {}
  void connect(const std::string &, int, const std::string &name,
               const std::string &) override {
    connected = true;
    room.push_back({name, 1});
  }
  void disconnect() override { connected = false; }
  bool isConnected() const override { return connected; }
  std::vector<BotClient::Member> members() const override { return room; }
  std::vector<BotClient::Peer> peers() const override { return {}; }
  void setRecv(const std::string &, int, bool) override {}
  void sendChat(const std::string &text) override { said.push_back(text); }
  void sendPrivate(const std::string &to, const std::string &text) override {
    whispered.push_back({to, text});
  }
  void transmit(const float *, const float *, int) override { ++intervalsSent; }

private:
  std::vector<BotClient::Listener *> listeners;
};

struct Rig {
  FakeClient *fake;
  std::unique_ptr<PracticeBot> bot;

  explicit Rig(const juce::String &name = "Ravo[keys-bot]") {
    auto client = std::make_unique<FakeClient>();
    fake = client.get();
    bot = std::make_unique<PracticeBot>(name, juce::StringArray{"keys"},
                                        std::move(client));
    bot->setOwner("you");
    bot->join("127.0.0.1", 1234, 48000.0);
    bot->playAs(BotBand::Voice::Keys, MusicalKey::parseName("D minor"), 120, 8,
                48000.0, 20260811);
    fake->joins("you");
  }
};

class PracticeBotTests : public juce::UnitTest {
public:
  PracticeBotTests() : juce::UnitTest("PracticeBot", "bots") {}

  void runTest() override {
    beginTest("a bot answers what it is asked, with no room around it");
    {
      Rig rig;
      rig.fake->say("you", "Ravo: what key are we in");
      expect(!rig.fake->said.empty(), "the bot said nothing");
      expect(rig.fake->said.back().find("D minor") != std::string::npos,
             "did not name the key: " + rig.fake->said.back());
    }

    beginTest("an unaddressed line is not answered");
    {
      Rig rig;
      const auto before = rig.fake->said.size();
      rig.fake->say("you", "what key are we in");
      rig.fake->say("you", "the bass is a bit loud");
      expectEquals((int)rig.fake->said.size(), (int)before,
                   "answered a question nobody asked it");
    }

    beginTest("a silent bot puts nothing on the wire");
    {
      // The property that makes an empty room free, asserted directly rather
      // than inferred from a room's phase list.
      Rig rig;
      rig.bot->renderInterval(4800, 0);
      expectEquals(rig.fake->intervalsSent, 0, "a silent bot transmitted");

      rig.bot->startPlaying();
      rig.bot->renderInterval(4800, 1);
      expectEquals(rig.fake->intervalsSent, 1, "a playing bot did not transmit");
    }

    beginTest("an ending is two intervals, and then nothing");
    {
      Rig rig;
      rig.bot->startPlaying();
      rig.bot->stopPlaying();
      for (int i = 0; i < 5; ++i)
        rig.bot->renderInterval(4800, i);
      // Wrap-up and resolve go out; the three after them do not.
      expectEquals(rig.fake->intervalsSent, 2,
                   "the ending was not exactly two intervals");
    }

    beginTest("told to leave, it goes and stays gone");
    {
      Rig rig;
      rig.fake->say("you", "Ravo: leave");
      expect(!rig.bot->isActive(), "the bot did not leave");
      expect(!rig.fake->connected, "the bot left without disconnecting");

      const auto after = rig.fake->said.size();
      rig.fake->say("you", "Ravo: what key are we in");
      expectEquals((int)rig.fake->said.size(), (int)after,
                   "a parted bot went on answering");
    }

    beginTest("the key follows the room, and a chart travels with it");
    {
      Rig rig;
      // From C major so the move is a pure transposition and the expected
      // answer is obvious: vi IV I V, a whole tone up.
      rig.fake->say("you", "[key: C major]");
      rig.fake->say("you", "| Am | F | C | G |");
      rig.fake->say("you", "[key: D major]");

      const auto s = rig.bot->currentSettings();
      expectEquals(s.key.tonic, 2, "the key did not follow");
      const auto chords = Harmony::flatten(s.chart);
      expectEquals((int)chords.size(), 4, "the chart was replaced");
      if (chords.size() == 4)
        expectEquals(chords[0].root, 11, "the chart did not transpose");
    }
  }
};

static PracticeBotTests practiceBotTests;

} // namespace
