#include "PracticeRoom.h"

#include "NinjamBotClient.h"

#include "jambot/BotNames.h"

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

    const BotBand::Voice voices[] = {
        BotBand::Voice::Drums, BotBand::Voice::Bass, BotBand::Voice::Keys,
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
        server.stop();
        return false;
      }
  }

  running = true;
  conductor.start((double)intervalSamples / cfg.sampleRate,
                  [this](int intervalIndex) {
                    reapPartedBots();
                    renderOneInterval(intervalIndex,
                                      [this] { return !running.load(); });
                  });
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
}

void PracticeRoom::renderOneInterval(int intervalIndex,
                                     const std::function<bool()> &shouldStop) {
  juce::ScopedLock sl(botsMutex);
  for (auto &b : bots) {
    // Between bots, not just between intervals. One interval of four bots is
    // four Vorbis encodes of several seconds of audio each; without a check in
    // here, stop() waits for all of them however long that takes, and the
    // budget it allows is not the one that matters.
    if (shouldStop && shouldStop())
      return;
    b->renderInterval(intervalSamples, intervalIndex);
  }
}

