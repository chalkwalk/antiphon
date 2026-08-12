#include "PracticeRoom.h"

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

    const BotBand::Voice voices[] = {BotBand::Voice::Drums,
                                     BotBand::Voice::Bass,
                                     BotBand::Voice::Keys};
    std::uint32_t seed = cfg.seed;
    for (auto voice : voices) {
      const juce::String instrument = BotBand::voiceName(voice);

      // The marker is for the human reading the mixer: a strip that is not a
      // person should say so. It identifies nothing -- the echo bot is told who
      // to listen to rather than working it out from a name.
      auto bot = std::make_unique<PracticeBot>(instrument + " [bot]",
                                               juce::StringArray{instrument});
      bot->setOwner(cfg.ownerName);
      bot->playAs(voice, cfg.key, cfg.bpm, cfg.bpi, cfg.sampleRate, seed);
      bots.push_back(std::move(bot));

      // A different seed per player as well as the salt inside BotBand, so two
      // voices cannot land on the same figure by coincidence.
      seed = seed * 1664525u + 1013904223u;
    }

    for (auto &b : bots)
      if (!b->join(host(), server.port(), cfg.sampleRate)) {
        bots.clear();
        server.stop();
        return false;
      }
  }

  running = true;
  conductor.startThread();
  return true;
}

void PracticeRoom::stop() {
  running = false;
  conductor.stopThread(2000);

  {
    juce::ScopedLock sl(botsMutex);
    for (auto &b : bots)
      b->part();
    bots.clear();
  }

  server.stop();
  intervalSamples = 0;
}

int PracticeRoom::botCount() const {
  juce::ScopedLock sl(botsMutex);
  return (int)bots.size();
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

void PracticeRoom::reapPartedBots() {
  // A bot that has parted -- because its owner left, because someone asked it
  // to, or because the connection went -- is not coming back. Drop it rather
  // than calling into it every interval forever.
  juce::ScopedLock sl(botsMutex);
  for (int i = (int)bots.size() - 1; i >= 0; --i)
    if (!bots[(size_t)i]->isActive())
      bots.erase(bots.begin() + i);
}

void PracticeRoom::renderOneInterval(int intervalIndex) {
  juce::ScopedLock sl(botsMutex);
  for (auto &b : bots)
    b->renderInterval(intervalSamples, intervalIndex);
}

void PracticeRoom::Conductor::run() {
  // Free-running, and deliberately not synchronised to the joining player's
  // grid. Ninjam's absolute interval phase is free -- every client plays each
  // received interval starting at its own downbeat, so phase offsets between
  // clients cancel out per listener (PRINCIPLES 9). Chasing the player's phase
  // here would add a dependency for no audible difference.
  const double intervalMs =
      1000.0 * (double)room.intervalSamples / room.cfg.sampleRate;

  double nextDue = juce::Time::getMillisecondCounterHiRes();
  int intervalIndex = 0;

  while (!threadShouldExit() && room.running.load()) {
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now < nextDue) {
      // Wake early enough to be punctual without spinning.
      const int sleepMs = (int)std::min(50.0, nextDue - now);
      wait(juce::jmax(1, sleepMs));
      continue;
    }

    room.reapPartedBots();
    room.renderOneInterval(intervalIndex++);

    nextDue += intervalMs;

    // If the whole band overran -- a debugger breakpoint, a stalled machine --
    // skip forward rather than sprinting to catch up, which would burst several
    // intervals onto the wire at once.
    const double after = juce::Time::getMillisecondCounterHiRes();
    if (nextDue < after)
      nextDue = after + intervalMs;
  }
}
