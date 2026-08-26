#pragma once

#include <BandPlayState.h>
#include <Conductor.h>
#include <PracticeBot.h>
#include <TutorBot.h>
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

    // The fifth bot: no instrument, no channel, six lines and gone
    // (`libs/jambot/docs/BOT-CHAT.md` section 7).
    //
    // ON, because a practice room is exactly where somebody meeting the
    // interval model for the first time arrives, and a tutorial nobody
    // switches on teaches nobody.
    //
    // It was off while the bot was being built, on the grounds that flipping
    // it changes the membership of every room this repository's tests start.
    // What made it safe to flip is not that the tests were updated: it is that
    // the tutor is finite by construction and now has a test saying so -- ten
    // lines is its whole vocabulary, however long a session runs, and then it
    // parts of its own accord. A room that has heard it is a room with four
    // bots in it again.
    //
    // Set it false for a room whose owner has done this before.
    bool withTutor = true;
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

  // What each bot LATCHED for the interval it is rendering, which is a
  // different question from `bandPhases` and the one that says whether the
  // band is actually playing yet.
  //
  // `bandPhases` reports the state machine, and that flips the instant a
  // command lands -- so it says a band asked to play is playing before a note
  // has been rendered. The latch is what a render will actually produce, so it
  // is what tells a test whether a start was taken inside this interval or
  // deferred to the head of the next.
  std::vector<BandPlayState::State> bandLatchedPhases() const;

  PracticeServer &practiceServer() { return server; }

private:
  // Drives every bot's interval render in step. The loop itself is
  // `jambot::Conductor`, which is JUCE-free because a band on a command line
  // needs exactly the same counting.

  // The conductor's callback. Ticks run several times per bot per interval:
  // tick 0 is the band's decision point and the rest are render slots.
  void onTick(int intervalIndex, int tick);

  // Every bot latches the phase it will render this interval, together. The
  // band's transitions have to be simultaneous even though its renders are
  // not; see `PracticeBot::beginInterval`.
  void latchBand();
  void refreshBandLatch();
  bool bandWantsStart() const;

  // Whether this bot's latched phase will actually produce audio. Only these
  // renders are worth timing; see `renderScheduledBots`.
  bool botIsAudible(int index) const;

  // Whether what is left of the interval will fit the whole band's renders.
  bool enoughTimeToStart(int tick) const;

  // Lays the band's renders out across the ticks from `tick` to the end of the
  // interval.
  void scheduleRendersFrom(int tick);
  void renderScheduledBots(int intervalIndex, int tick,
                           const std::function<bool()> &shouldStop);

  // One bot's interval, identified by its index in `bots`.
  void renderOneBot(int intervalIndex, int slice,
                    const std::function<bool()> &shouldStop);

  static constexpr int kTicksPerBot = 4;
  void reapPartedBots();

  // Republishes `publishedBotCount`. Call while holding `botsMutex`, from
  // every path that changes `bots`.
  void publishBotCount();

  PracticeServer server;
  std::vector<std::unique_ptr<PracticeBot>> bots;

  // Outside `bots` on purpose. It has no voice, so it takes no conductor slice
  // and no share of the mix, and every loop over the band would otherwise have
  // to remember to skip it.
  std::unique_ptr<TutorBot> tutor;

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

  int ticksPerInterval = 0;

  // Which tick each bot renders on, this interval. Rewritten whenever the
  // schedule changes, which is at every head and at a start taken mid-interval.
  std::vector<int> renderTick;

  // A running average of what one bot's render costs on this machine, which is
  // what decides whether a late start can still be fitted into the interval.
  std::atomic<double> avgBotRenderMs{0.0};

  jambot::Conductor conductor;
  Config cfg;
  std::atomic<bool> running{false};
  int intervalSamples = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PracticeRoom)
};
