#pragma once

#include <BandPlayState.h>
#include <Conductor.h>
#include <PracticeBot.h>
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
    // 100 bpm and sixteen to the interval, not 120 and eight.
    //
    // Eight beats at 120 is four seconds, which is two bars -- so the band
    // turns the form over twice as often as it sounds like it should, and the
    // whole thing reads as double time or as 2/2. Sixteen at 100 is 9.6
    // seconds and four bars of 4/4, which is a phrase rather than a fragment.
    //
    // It also gives the keys room. The pad's attack is capped at a quarter of
    // the chord it is playing, so a chord held four beats at 100 allows the
    // whole of the Strings swell where two beats at 120 clipped it.
    int bpm = 100;
    int bpi = 16;
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

    // How long the band waits for the owner before leaving for good.
    //
    // A departure used to be fatal: a PART parted the bot at once, there is no
    // reconnect by design, and the room reaped it -- so a thirty-second blip
    // destroyed the band and left the room running empty. Three minutes covers
    // a router reboot or a client restart; past that it was either deliberate
    // or something bigger than a blip.
    int ownerGraceMs = 3 * 60 * 1000;

    // Twice as long before the owner has EVER arrived. Starting a room and
    // then going to find your instrument is ordinary, and a band that has
    // never seen anybody is costing nothing while it waits -- it arrives
    // silent. This exists so a forgotten room does not sit on a real server
    // for ever, not to hurry anybody.
    int initialGraceMs = 6 * 60 * 1000;

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
  // Drives every bot's interval render in step. The loop itself is
  // `jambot::Conductor`, which is JUCE-free because a band on a command line
  // needs exactly the same counting.

  void renderOneInterval(int intervalIndex,
                         const std::function<bool()> &shouldStop);
  void reapPartedBots();

  // Republishes `publishedBotCount`. Call while holding `botsMutex`, from
  // every path that changes `bots`.
  void publishBotCount();

  PracticeServer server;
  std::vector<std::unique_ptr<PracticeBot>> bots;

  // Held for a WHOLE interval render -- four synths and four Vorbis encodes.
  // Nothing on the message thread may take it: the editor's timer runs at 30
  // Hz and would block behind the burst, which is what stalled the meters and
  // the phase bar for over a second an interval while the audio played on.
  //
  // Anything the UI needs gets published out here instead, the way
  // `botCount()` does.
  mutable juce::CriticalSection botsMutex;

  // `bots.size()`, readable without the lock.
  std::atomic<int> publishedBotCount{0};

  jambot::Conductor conductor;
  Config cfg;
  std::atomic<bool> running{false};
  int intervalSamples = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeRoom)
};
