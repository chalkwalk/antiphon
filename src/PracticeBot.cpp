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
  // The channel list is resent on connect: updateChannelInfo before connecting
  // only stores it, and the room needs to be told.
  netClient.updateChannelInfo(channels);
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

void PracticeBot::onUserInfoChange() {
  juce::String ownerName, wanted;
  {
    juce::ScopedLock sl(stateMutex);
    ownerName = owner;
    wanted = listensTo;
  }

  const auto members = netClient.getRoomMembers();

  // Leave when the player who brought the bot leaves. On a real server this is
  // the rule that matters most: walking away is enough to clean up after
  // yourself, with nothing to remember.
  if (ownerName.isNotEmpty()) {
    bool ownerPresent = false;
    for (const auto &m : members)
      if (m.username == ownerName) {
        ownerPresent = true;
        break;
      }

    if (ownerPresent)
      sawOwner = true;
    else if (sawOwner.load()) {
      part();
      return;
    }
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

void PracticeBot::onChatMessage(const juce::String &type,
                                const juce::String &username,
                                const juce::String &text) {
  if (type != "PRIVMSG" || username == botName)
    return;

  // Anyone may evict a bot, not just whoever brought it. A bot in someone
  // else's jam should be removable by the people it is bothering; making them
  // find its owner first is the annoyance being avoided.
  if (isPartCommand(text)) {
    netClient.sendPrivateMessage(username, botName + " leaving. Bye.");
    part();
    return;
  }

  if (text.trim().toLowerCase() == "help")
    netClient.sendPrivateMessage(username, helpLine(botName));
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
