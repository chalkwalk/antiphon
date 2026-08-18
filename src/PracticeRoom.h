#pragma once

#include "BandPlayState.h"
#include "PracticeBot.h"
#include "PracticeServer.h"
#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

// The practice room: a server on the loopback interface, a band of bots
// connected to it, and the port for you to join on.
//
// Practice used to be a mode -- an offline branch through the UI, with its own
// gating and its own strip. Now it is a destination. You connect to it, and
// because everything on the far side is real, the whole connected UI works
// without knowing this room is any different: phase bar, remote strips,
// routing, chat, sync, recording, stems.
//
// The conductor is one thread for the whole band rather than one per bot. Bots
// that share a clock stay tight with each other for free, which is what a band
// is; and one thread is one thing to reason about at teardown.
//
// PHASE: a bot renders interval N during interval N and it is heard in N+1,
// exactly like a player. A bot that *reacts* to you cannot be heard sooner than
// N+2 -- you play in N, it hears you in N+1, the soonest it can send is N+1.
// That is the true latency of the form rather than a limitation here, and it is
// why the echo bot's shallowest delay is two.
class PracticeRoom {
public:
  PracticeRoom();
  ~PracticeRoom();

  struct Config {
    int bpm = 120;
    int bpi = 8;
    double sampleRate = 48000.0;
    juce::String ownerName = "you";

    // What the band calls itself, used once in the arrival roster.
    //
    // Not an address -- `band`, `everyone` and `all` are the words people
    // actually type, and a name would only be a fourth synonym. It earns its
    // place in the one line the band gets to introduce itself with, because
    // "The Understudies: Mirn (kit), ..." reads as a band arriving where four
    // usernames read as four processes starting.
    juce::String bandName = "The Understudies";
    juce::String topic = "Practice room -- play, nobody is listening";

    // What the band plays in. Announcing `[key: D minor]` in chat changes it
    // afterwards; this is only where they start.
    MusicalKey::Key key = MusicalKey::parseName("C major");

    // Rerolled by "shake". Fixed by default so a practice room is the same
    // room twice, which matters more for learning a piece than novelty does.
    std::uint32_t seed = 20260811u;
  };

  // Brings up the server and the band. Returns false having cleaned up if the
  // room could not be started.
  bool start(const Config &config);
  void stop();

  bool isRunning() const { return running.load(); }

  // Loopback only, always. Nothing here ever hands out another address.
  static const char *host() { return "127.0.0.1"; }
  int port() const { return server.port(); }

  int botCount() const;
  juce::StringArray botNames() const;

  // What each bot is currently playing. For tests and for the UI to report the
  // key and chords the band has settled on.
  std::vector<BotBand::Settings> bandSettings() const;

  // What each bot is playing, or how far through stopping it is. The observable
  // for the play/stop states: from outside, the difference between wrapping up
  // and being silent is several seconds of audio, which no test can watch.
  std::vector<BandPlayState::State> bandPhases() const;

  PracticeServer &practiceServer() { return server; }

private:
  // Drives every bot's interval render in step.
  class Conductor : public juce::Thread {
  public:
    explicit Conductor(PracticeRoom &r) : juce::Thread("PracticeBand"), room(r) {}
    void run() override;

  private:
    PracticeRoom &room;
  };

  void renderOneInterval(int intervalIndex,
                         const std::function<bool()> &shouldStop);
  void reapPartedBots();

  PracticeServer server;
  std::vector<std::unique_ptr<PracticeBot>> bots;
  mutable juce::CriticalSection botsMutex;

  Conductor conductor{*this};
  Config cfg;
  std::atomic<bool> running{false};
  int intervalSamples = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeRoom)
};
