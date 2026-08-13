#include "PracticeBot.h"

namespace {
// One place, so the help line and the parser cannot drift apart.
const char *const kPartCommands[] = {"part", "leave", "exit", "stop"};
} // namespace

PracticeBot::PracticeBot(juce::String name, juce::StringArray channelNames)
    : botName(std::move(name)), channels(std::move(channelNames)) {
  if (channels.isEmpty())
    channels.add("bot");

  // Deaf by default. A generative bot follows the grid rather than the room,
  // and an unsubscribed client never causes the server to send it an interval,
  // so it never allocates one. That is what keeps a room of bots costing one
  // client's worth of interval buffers instead of one per bot.
  netClient.setDefaultRecvEnabled(false);
  netClient.addListener(this);
}

PracticeBot::~PracticeBot() {
  netClient.removeListener(this);
  netClient.disconnectFromServer();
}

void PracticeBot::setRender(Render r) {
  juce::ScopedLock sl(stateMutex);
  render = std::move(r);
}

void PracticeBot::setOwner(juce::String ownerUsername) {
  juce::ScopedLock sl(stateMutex);
  owner = std::move(ownerUsername);
}

void PracticeBot::setListensTo(juce::String username) {
  {
    juce::ScopedLock sl(stateMutex);
    listensTo = std::move(username);
  }
  // Subscribing to one player is still deaf to everyone else; the recv flags
  // are applied as channels appear, in onUserInfoChange.
}

bool PracticeBot::join(const juce::String &host, int port, double sampleRate) {
  rate = sampleRate;
  netClient.setSampleRate(sampleRate);
  netClient.updateChannelInfo(channels);
  netClient.connectToServer(host, port, botName, "");
  active = true;
  return true;
}

void PracticeBot::part() {
  // Idempotent, and terminal: see onDisconnected for why there is no rejoin.
  if (!active.exchange(false))
    return;
  netClient.disconnectFromServer();
}

void PracticeBot::playAs(BotBand::Voice voice, const MusicalKey::Key &key,
                         int bpm, int bpi, double sampleRate,
                         std::uint32_t seed) {
  {
    juce::ScopedLock sl(stateMutex);
    bandVoice = voice;
    settings = BotBand::defaults(key, bpm, bpi, sampleRate, seed);
  }
  playing = true;

  setRender([this](juce::AudioBuffer<float> &buffer, int numSamples,
                   int intervalIndex) {
    BotBand::Voice v;
    BotBand::Settings snapshot;
    {
      juce::ScopedLock sl(stateMutex);
      v = bandVoice;
      snapshot = settings;
    }

    // Most voices are one close-miked instrument standing in one place, so they
    // render mono and are copied across: the band plays in the middle and the
    // listener decides where it sits, with the pan control every remote channel
    // already has.
    //
    // The kit is the exception, because a kit is heard through two overhead
    // mics that are not in the same place. It fills both channels itself, and
    // the copy is skipped. This costs no bandwidth: the encoder has always run
    // two channels here, so the stereo was already being paid for and simply
    // carried the same samples twice.
    const bool stereo = BotBand::isStereo(v) && buffer.getNumChannels() > 1;
    BotBand::renderInterval(v, snapshot, intervalIndex,
                            buffer.getWritePointer(0),
                            stereo ? buffer.getWritePointer(1) : nullptr,
                            numSamples);
    if (!stereo && buffer.getNumChannels() > 1)
      buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
  });
}

void PracticeBot::shake() {
  juce::ScopedLock sl(stateMutex);
  // A hash of the old seed rather than an increment, so the next figure is
  // unrelated to the last rather than adjacent to it.
  std::uint32_t s = settings.seed;
  s ^= s >> 16;
  s *= 0x7feb352dU;
  s ^= s >> 15;
  settings.seed = s | 1u;
}

BotBand::Settings PracticeBot::currentSettings() const {
  juce::ScopedLock sl(stateMutex);
  return settings;
}

bool PracticeBot::isShakeCommand(const juce::String &text) {
  const auto t = text.trim().toLowerCase();
  return t == "shake" || t == "new" || t == "again";
}

bool PracticeBot::handleBandCommand(const juce::String &text) {
  if (!playing.load())
    return false;

  if (isShakeCommand(text)) {
    shake();
    return true;
  }

  // The key travels as a tagged chat line, never as prose -- MusicalKey refuses
  // to guess, and so does this.
  const auto key = MusicalKey::parseTagged(text);
  if (key.valid) {
    juce::ScopedLock sl(stateMutex);
    settings.key = key;
    // A new key means the old chords are in the wrong one. An announced
    // progression is not transposed, because nobody announcing chords means
    // "those chords, moved".
    settings.chart = Harmony::defaultChart(key);
    return true;
  }

  Harmony::Chart chart;
  if (Harmony::parseChart(text, chart)) {
    juce::ScopedLock sl(stateMutex);
    settings.chart = std::move(chart);
    return true;
  }

  return false;
}

juce::String PracticeBot::handlePrivateCommand(const juce::String &text) {
  if (!playing.load())
    return {};

  const auto t = text.trim().toLowerCase();

  // Only the soloist answers to these. A drummer asked to play the guitar
  // should say so rather than silently accepting a setting it will never read.
  const bool leadWords = t == "epiano" || t == "piano" || t == "rhodes" ||
                         t == "guitar" || t == "synth";
  if (leadWords) {
    if (bandVoice != BotBand::Voice::Lead)
      return botName + " plays the " +
             juce::String(BotBand::voiceName(bandVoice)).toLowerCase() +
             ". Ask the lead.";

    int wanted = (int)BotVoice::LeadInstrument::Synth;
    if (t == "epiano" || t == "piano" || t == "rhodes")
      wanted = (int)BotVoice::LeadInstrument::EPiano;
    else if (t == "guitar")
      wanted = (int)BotVoice::LeadInstrument::Guitar;

    juce::ScopedLock sl(stateMutex);
    settings.leadOverride = wanted;
    return botName + " on " +
           BotVoice::leadInstrumentName((BotVoice::LeadInstrument)wanted) + ".";
  }

  // What are you playing? The one question worth being able to ask, because
  // the seed picks the answer and there is otherwise no way to find out.
  if (t == "sound" || t == "what" || t == "kit") {
    BotBand::Settings copy;
    {
      juce::ScopedLock sl(stateMutex);
      copy = settings;
    }

    switch (bandVoice) {
    case BotBand::Voice::Lead:
      return botName + " is playing " +
             BotVoice::leadInstrumentName(BotBand::leadInstrument(copy)) + ".";
    case BotBand::Voice::Keys:
      return botName + " is playing a " +
             BotVoice::padCharacterName(BotBand::keysPatch(copy).character) +
             " patch.";
    case BotBand::Voice::Bass:
      return botName + " is playing " +
             BotVoice::bassTechniqueName(BotBand::bassTechnique(copy)) +
             " bass.";
    case BotBand::Voice::Drums:
      return botName + " is playing the kit.";
    }
  }

  return {};
}

bool PracticeBot::isPartCommand(const juce::String &text) {
  const auto t = text.trim().toLowerCase();
  for (const auto *cmd : kPartCommands)
    if (t == cmd)
      return true;
  return false;
}

juce::String PracticeBot::helpLine(const juce::String &name) {
  return name + " is a bot. Send it a private message saying 'part' and it "
                "will leave.";
}

void PracticeBot::onConnected() {
  // Nothing to do. The channel list was stored before connecting and
  // NinjamClient sends it itself the moment auth succeeds
  // (NinjamClient.cpp:347), so resending here was redundant -- and it was a
  // write from the message thread at the exact moment the network thread might
  // be tearing the socket down, which is how the fd race above was found.
}

void PracticeBot::onDisconnected(const juce::String &) {
  // Terminal, always. The server exited, the network went, an admin kicked it:
  // all the same, and all final.
  //
  // DO NOT ADD A RECONNECT. A bot that reconnects is a bot nobody can get rid
  // of, and these can be pointed at a real server. The absence of retry logic
  // here is the feature.
  active = false;
}

bool PracticeBot::checkOwnerStillHere() {
  // Leave when the player who brought the bot leaves. On a real server this is
  // the rule that matters most: walking away is enough to clean up after
  // yourself, with nothing to remember.
  //
  // Called from BOTH the user-info and the chat callbacks, because membership
  // is maintained from both and the departure order is not the obvious one. A
  // leaving player produces a USER_INFO_CHANGE marking their channels inactive
  // and then a PART; only the PART removes the name from roomMembers
  // (NinjamClient.cpp:652). Checking on user-info alone therefore looks while
  // the owner is still listed, finds them present, and never looks again --
  // which is exactly the bug this comment replaces.
  juce::String ownerName;
  {
    juce::ScopedLock sl(stateMutex);
    ownerName = owner;
  }
  if (ownerName.isEmpty())
    return true;

  bool ownerPresent = false;
  for (const auto &m : netClient.getRoomMembers())
    if (m.username == ownerName) {
      ownerPresent = true;
      break;
    }

  if (ownerPresent) {
    sawOwner = true;
    return true;
  }

  // Absent is only "left" once they have actually turned up: bots connect
  // before the player does.
  if (!sawOwner.load())
    return true;

  part();
  return false;
}

void PracticeBot::onUserInfoChange() {
  if (!checkOwnerStillHere())
    return;

  juce::String wanted;
  {
    juce::ScopedLock sl(stateMutex);
    wanted = listensTo;
  }
  if (wanted.isEmpty())
    return;

  // Subscribe to exactly one player. Channels arrive over time, so this runs on
  // every change rather than once.
  const auto users = netClient.getRemoteUsers();
  auto it = users.find(wanted);
  if (it == users.end())
    return;
  for (const auto &[idx, ch] : it->second.channels)
    if (!ch.recvEnabled)
      netClient.setRemoteUserRecv(wanted, idx, true);
}

void PracticeBot::onServerConfig(int bpm, int bpi) {
  juce::ScopedLock sl(stateMutex);
  if (bpm > 0)
    settings.bpm = bpm;
  if (bpi > 0)
    settings.bpi = bpi;
}

void PracticeBot::onChatMessage(const juce::String &type,
                                const juce::String &username,
                                const juce::String &text) {
  // A PART is a chat message, and it is what actually removes a name from the
  // room. See checkOwnerStillHere.
  if (!checkOwnerStillHere())
    return;

  if (username == botName)
    return;

  // Room chat: the key, the chords and "shake" are addressed to everyone, so
  // they are taken from ordinary messages. Nothing here replies -- a band that
  // answers every line in the room is the annoyance.
  if (type == "MSG") {
    handleBandCommand(text);
    return;
  }

  if (type != "PRIVMSG")
    return;

  // Anyone may evict a bot, not just whoever brought it. A bot in someone
  // else's jam should be removable by the people it is bothering; making them
  // find its owner first is the annoyance being avoided.
  if (isPartCommand(text)) {
    netClient.sendPrivateMessage(username, botName + " leaving. Bye.");
    part();
    return;
  }

  if (text.trim().toLowerCase() == "help") {
    netClient.sendPrivateMessage(username, helpLine(botName));
    return;
  }

  // Things only one player is asked, and which room chat does not take.
  const auto reply = handlePrivateCommand(text);
  if (reply.isNotEmpty()) {
    netClient.sendPrivateMessage(username, reply);
    return;
  }

  // Privately: the same instructions, but aimed at one player, so this one
  // changes and the rest of the band carries on.
  if (handleBandCommand(text))
    netClient.sendPrivateMessage(username, botName + " ok.");
}

void PracticeBot::renderInterval(int numSamples, int intervalIndex) {
  if (!active.load() || numSamples <= 0)
    return;

  Render r;
  {
    juce::ScopedLock sl(stateMutex);
    r = render;
  }
  if (!r)
    return; // A silent bot is a valid bot.

  if (renderBuffer.getNumSamples() < numSamples)
    renderBuffer.setSize(2, numSamples, false, true, true);
  renderBuffer.clear(0, numSamples);

  r(renderBuffer, numSamples, intervalIndex);

  if (!active.load())
    return;
  netClient.processCapturedAudio(renderBuffer, numSamples, 0, false);
}
