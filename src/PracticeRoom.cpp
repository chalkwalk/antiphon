#include "PracticeRoom.h"

#include "NinjamBotClient.h"

#include <BotNames.h>

#include <algorithm>

#include "IntervalClock.h"

PracticeRoom::PracticeRoom() = default;

PracticeRoom::~PracticeRoom() { stop(); }

bool PracticeRoom::start(const Config &config) {
  stop();
  cfg = config;

  if (cfg.bpm <= 0 || cfg.bpi <= 0 || cfg.sampleRate <= 0.0)
    return false;

  if (!server.start(cfg.bpm, cfg.bpi))
    return false;
  server.setTopic(cfg.topic);

  // The same truncating arithmetic every other client on a server uses
  // (justinfrankel/ninjam njclient.cpp:806). A bot that rounded differently
  // would drift against the room by a sample per interval.
  IntervalClock clock;
  clock.prepare(cfg.sampleRate);
  clock.setTempo(cfg.bpm, cfg.bpi);
  intervalSamples = clock.samplesPerInterval();
  if (intervalSamples <= 0) {
    server.stop();
    return false;
  }

  {
    juce::ScopedLock sl(botsMutex);
    bots.clear();
    publishBotCount();

    const BotBand::Voice voices[] = {BotBand::Voice::Drums,
                                     BotBand::Voice::Bass, BotBand::Voice::Keys,
                                     BotBand::Voice::Lead};

    // Names before players, because a name has to be checked against the room.
    //
    // The owner is already in it -- or about to be -- so their name is what a
    // bot's handle must not collide with. See BotNames.h for why the handle
    // matters enough to pick around: it is what lets "what are the changes
    // delvo" work, and an ambiguous one costs the bot natural address for the
    // whole session.
    std::vector<std::string> taken;
    if (cfg.ownerName.isNotEmpty())
      taken.push_back(cfg.ownerName.toStdString());
    const auto chosen = BotNames::bandFor(4, cfg.seed, taken);

    std::uint32_t seed = cfg.seed;
    int index = 0;
    for (auto voice : voices) {
      const juce::String instrument =
          juce::String(BotBand::voiceName(voice)).toLowerCase();

      // One token, no spaces, so `/msg` can reach it in every client. The
      // marker is for the human reading the mixer -- a strip that is not a
      // person should say so -- and for other bots deciding whether to answer.
      // It identifies nothing and is spoofable, which is fine, because it
      // decides only who talks.
      const juce::String botUsername = BotNames::usernameFor(
          chosen[(size_t)index++], instrument.toStdString());

      // Antiphon's client, behind the interface the bots see. This is the one
      // place the plugin's transport meets the band.
      auto bot = std::make_unique<PracticeBot>(
          botUsername.toStdString(),
          std::vector<std::string>{instrument.toStdString()},
          std::make_unique<NinjamBotClient>());
      bot->setOwner(cfg.ownerName.toStdString());
      bot->setGrace(cfg.ownerGraceMs, cfg.initialGraceMs);
      bot->playAs(voice, cfg.key, cfg.bpm, cfg.bpi, cfg.sampleRate, seed);
      bots.push_back(std::move(bot));
      publishBotCount();

      // A different seed per player as well as the salt inside BotBand, so two
      // voices cannot land on the same figure by coincidence.
      seed = seed * 1664525u + 1013904223u;
    }

    // Who arrived together, so the roster can say whether these are a band or
    // merely a list. Told before joining, because the announcement happens five
    // seconds after connect and nobody should be racing it.
    std::vector<std::string> names;
    for (const auto &b : bots)
      names.push_back(b->name());
    for (auto &b : bots)
      b->setBandmates(names, cfg.bandName.toStdString());

    for (auto &b : bots)
      if (!b->join(std::string(host()), server.port(), cfg.sampleRate)) {
        bots.clear();
        publishBotCount();
        server.stop();
        return false;
      }
  }

  // After the band, so the tutor's greeting lands after the roster rather than
  // racing it, and so a failure to bring the band does not leave a tutor in an
  // empty room.
  if (cfg.withTutor) {
    tutor = std::make_unique<TutorBot>(BotNames::tutorName(),
                                       std::make_unique<NinjamBotClient>());
    tutor->setOwner(cfg.ownerName.toStdString());
    if (!tutor->join(std::string(host()), server.port(), cfg.sampleRate))
      tutor.reset(); // A room without a tutor is a room; not worth failing for.
  }

  running = true;

  // Ticks, not one call per bot. The band's compute is spread through the
  // interval rather than landing on the boundary in one burst (ROADMAP.md,
  // *The interval boundary is a compute spike*), but the schedule has to be
  // finer than one tick per bot so a start arriving mid-interval can still be
  // fitted into what is left of it.
  //
  // Four ticks per bot: fine enough to place a late start sensibly, coarse
  // enough that the wakeups cost nothing.
  ticksPerInterval = std::max(1, (int)bots.size()) * kTicksPerBot;
  renderTick.assign(bots.size(), 0);

  conductor.start(
      (double)intervalSamples / cfg.sampleRate, ticksPerInterval,
      [this](int intervalIndex, int tick) { onTick(intervalIndex, tick); });
  return true;
}

void PracticeRoom::stop() {
  running = false;

  // Joins, and waits as long as it takes rather than to a deadline.
  //
  // The deadline it replaces was there because a conductor still running is
  // one whose bots are about to be destroyed underneath it, and rendering a
  // whole interval for four bots means four Vorbis encodes -- not fast under a
  // sanitiser. Waiting is the safe half of that trade: `renderOneInterval`
  // checks between bots, so the longest this can block is one bot's encode.
  conductor.stop();

  if (tutor) {
    tutor->part();
    tutor.reset();
  }

  {
    juce::ScopedLock sl(botsMutex);
    for (auto &b : bots)
      b->part();
    bots.clear();
    publishBotCount();
  }

  server.stop();
  intervalSamples = 0;
}

int PracticeRoom::botCount() const {
  // Deliberately lock-free. See the note on `botsMutex`: this is called from
  // the editor's timer, and the lock is held across a whole interval render.
  return publishedBotCount.load(std::memory_order_relaxed);
}

void PracticeRoom::publishBotCount() {
  publishedBotCount.store((int)bots.size(), std::memory_order_relaxed);
}

juce::StringArray PracticeRoom::botNames() const {
  juce::ScopedLock sl(botsMutex);
  juce::StringArray names;
  for (const auto &b : bots)
    names.add(b->name());
  return names;
}

std::vector<BotBand::Settings> PracticeRoom::bandSettings() const {
  juce::ScopedLock sl(botsMutex);
  std::vector<BotBand::Settings> out;
  out.reserve(bots.size());
  for (const auto &b : bots)
    out.push_back(b->currentSettings());
  return out;
}

std::vector<BandPlayState::State> PracticeRoom::bandPhases() const {
  juce::ScopedLock sl(botsMutex);
  std::vector<BandPlayState::State> out;
  out.reserve(bots.size());
  for (const auto &b : bots)
    out.push_back(b->playPhase());
  return out;
}

void PracticeRoom::reapPartedBots() {
  // A bot that has parted -- because its owner left, because someone asked it
  // to, or because the connection went -- is not coming back. Drop it rather
  // than calling into it every interval forever.
  juce::ScopedLock sl(botsMutex);
  for (int i = (int)bots.size() - 1; i >= 0; --i)
    if (!bots[(size_t)i]->isActive())
      bots.erase(bots.begin() + i);
  publishBotCount();
}

void PracticeRoom::onTick(int intervalIndex, int tick) {
  const auto shouldStop = [this] { return !running.load(); };

  if (tick == 0) {
    // Reaping once per interval, not once per tick: it takes the lock and
    // nothing about it is urgent.
    reapPartedBots();

    // THE DECISION POINT, and the only one for a stop. Every bot latches its
    // phase here, together, so the band wraps up and resolves as a band even
    // though the four renders that follow are seconds apart. A stop asked for
    // mid-interval therefore takes effect at the head of the next one -- the
    // whole band a beat late rather than half of it a whole interval late.
    latchBand();
    scheduleRendersFrom(0);
  } else if (bandWantsStart() && enoughTimeToStart(tick)) {
    // A start is the one transition worth catching mid-interval: waiting for
    // the next head would put an interval of silence between asking the band
    // to play and hearing it, and unlike an ending there is nothing musical
    // happening in that gap. Re-latch (without advancing -- that already
    // happened at tick 0) and fit everyone into what is left.
    refreshBandLatch();
    scheduleRendersFrom(tick);
  }

  renderScheduledBots(intervalIndex, tick, shouldStop);
}

std::vector<BandPlayState::State> PracticeRoom::bandLatchedPhases() const {
  juce::ScopedLock sl(botsMutex);
  std::vector<BandPlayState::State> out;
  out.reserve(bots.size());
  for (const auto &b : bots)
    out.push_back(b->latchedPhase());
  return out;
}

bool PracticeRoom::botIsAudible(int index) const {
  juce::ScopedLock sl(botsMutex);
  if (index < 0 || index >= (int)bots.size())
    return false;
  return bots[(std::size_t)index]->latchedPhase() !=
         BandPlayState::State::Silent;
}

void PracticeRoom::latchBand() {
  juce::ScopedLock sl(botsMutex);
  for (auto &b : bots)
    b->beginInterval();
}

void PracticeRoom::refreshBandLatch() {
  juce::ScopedLock sl(botsMutex);
  for (auto &b : bots)
    b->refreshLatch();
}

bool PracticeRoom::bandWantsStart() const {
  juce::ScopedLock sl(botsMutex);
  for (const auto &b : bots)
    if (b->startPending())
      return true;
  return false;
}

bool PracticeRoom::enoughTimeToStart(int tick) const {
  const double intervalMs = 1000.0 * (double)intervalSamples /
                            (cfg.sampleRate > 0.0 ? cfg.sampleRate : 48000.0);
  const double remainingMs =
      intervalMs * (double)(ticksPerInterval - tick) / (double)ticksPerInterval;

  // Measured, not assumed: what one bot costs depends on the machine, the
  // tempo and the interval length, and a constant here would be wrong on three
  // axes at once. `avgBotRenderMs` averages the AUDIBLE renders only.
  //
  // But a practice room is typically started once and played once, so the
  // first start is the one that matters and there is nothing to average yet.
  // Refusing on that basis would put an interval of silence between asking and
  // hearing in exactly the case this exists for.
  //
  // So a cold start leans on the interval instead: half of it. The band has to
  // render a whole interval within every interval or it cannot keep up at all,
  // so a machine that cannot manage the band inside half an interval is
  // already failing its ordinary job -- and where this was measured the whole
  // band costs about a quarter of one. The bound scales with tempo and bpi for
  // free, because both are already in `intervalMs`.
  const double perBot = avgBotRenderMs.load();
  if (perBot <= 0.0)
    return remainingMs > 0.5 * intervalMs;

  const int count = publishedBotCount.load(std::memory_order_relaxed);

  // Twice what the work should take. The band has to finish inside the
  // interval it is playing for, and being wrong here means a torn start --
  // worse than the interval of silence it is trying to avoid.
  return remainingMs > 2.0 * perBot * (double)count;
}

void PracticeRoom::scheduleRendersFrom(int tick) {
  juce::ScopedLock sl(botsMutex);
  renderTick.assign(bots.size(), 0);
  if (bots.empty())
    return;

  // Spread across whatever is left, so a start late in the interval is tighter
  // than one at its head rather than being refused outright.
  const int remaining = std::max(1, ticksPerInterval - tick);
  for (std::size_t i = 0; i < bots.size(); ++i)
    renderTick[i] = tick + (int)((remaining * i) / bots.size());
}

void PracticeRoom::renderScheduledBots(
    int intervalIndex, int tick, const std::function<bool()> &shouldStop) {
  for (std::size_t i = 0; i < renderTick.size(); ++i) {
    if (renderTick[i] != tick)
      continue;
    if (shouldStop && shouldStop())
      return;

    // Whether this render will do any WORK, decided before it runs. A silent
    // bot returns immediately -- no synthesis, no encode, nothing on the wire
    // -- so timing it would measure the early return.
    //
    // That is not a missing measurement but a poisonous one. The band sits
    // silent between tunes, so an average fed by silent renders converges
    // toward zero, and a cost of zero fits any remaining time at all: a start
    // arriving with a twentieth of an interval left would be judged to have
    // room for four full renders. That is the torn start this exists to
    // prevent, reached by measuring the wrong thing.
    const bool audible = botIsAudible((int)i);

    const double startMs = juce::Time::getMillisecondCounterHiRes();
    renderOneBot(intervalIndex, (int)i, shouldStop);
    const double tookMs = juce::Time::getMillisecondCounterHiRes() - startMs;

    if (!audible)
      continue;

    // A running average rather than the last value, so one descheduled render
    // does not convince the room it has no time to start.
    const double previous = avgBotRenderMs.load();
    avgBotRenderMs.store(previous <= 0.0 ? tookMs
                                         : 0.75 * previous + 0.25 * tookMs);
  }
}

void PracticeRoom::renderOneBot(int intervalIndex, int slice,
                                const std::function<bool()> &shouldStop) {
  juce::ScopedLock sl(botsMutex);
  if (slice < 0 || slice >= (int)bots.size())
    return; // A bot parted and its slice outlived it.

  // Checked here as well as by the conductor, because this is the call that
  // takes a while: one bot is a synth pass and a Vorbis encode of several
  // seconds of audio, and stop() waits for whichever one is in flight.
  if (shouldStop && shouldStop())
    return;
  bots[(std::size_t)slice]->renderInterval(intervalSamples, intervalIndex);
}
