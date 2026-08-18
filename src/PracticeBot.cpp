#include "PracticeBot.h"

#include "BotNames.h"

namespace {
// How much longer a bot with nothing to do waits before speaking for the band.
// Comfortably past the whole of the acting bots' spread, so any bot that
// actually did something wins.
constexpr int kIdleSpeakerPenaltyMs = 700;

// One place, so the help line and the parser cannot drift apart.
// "part" is deliberately absent, and so are "stop" and bare "go" -- see
// BotAddress::isPartCommand for why each was withdrawn.
const char *const kPartCommands[] = {"leave", "exit", "go away", "go home"};
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
  stopTimer();
  bandReply.cancel();
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
  inBand = true;
  startPlaying();

  setRender([this](juce::AudioBuffer<float> &buffer, int numSamples,
                   int intervalIndex, BotBand::Phase phase) {
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
    BotBand::renderInterval(v, snapshot, intervalIndex, phase,
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

namespace {
// The two vocabularies meet here and nowhere else: `BandPlayState` says WHEN a
// bot is ending and `BotBand::Phase` says what that sounds like.
BotBand::Phase phaseFor(BandPlayState::State s) {
  switch (s) {
  case BandPlayState::State::Wrapping:
    return BotBand::Phase::Wrapping;
  case BandPlayState::State::Resolving:
    return BotBand::Phase::Resolving;
  case BandPlayState::State::Playing:
  case BandPlayState::State::Silent:
    break;
  }
  return BotBand::Phase::Groove;
}
} // namespace

BandPlayState::State PracticeBot::playPhase() const {
  juce::ScopedLock sl(stateMutex);
  return playState.current();
}

void PracticeBot::startPlaying() {
  juce::ScopedLock sl(stateMutex);
  playState.start();
}

void PracticeBot::stopPlaying() {
  juce::ScopedLock sl(stateMutex);
  playState.stop();
}

BotBand::Settings PracticeBot::currentSettings() const {
  juce::ScopedLock sl(stateMutex);
  return settings;
}

bool PracticeBot::isShakeCommand(const juce::String &text) {
  const auto t = text.trim().toLowerCase();
  return t == "shake" || t == "new" || t == "again";
}

bool PracticeBot::handleStructured(const juce::String &text,
                                   const juce::String &username) {
  // Band membership, not audibility. A silent bot is still in the room and
  // still follows the key and the chart -- that is most of what somebody does
  // BETWEEN tunes, and a bot that stopped listening while stopped would have
  // to be told everything again when it came back in.
  if (!inBand.load())
    return false;

  // The key travels as a tagged line or a leading `/key`, never as prose --
  // MusicalKey refuses to guess, and so does this.
  const auto key = MusicalKey::parseAnnouncement(text);
  if (key.valid) {
    juce::ScopedLock sl(stateMutex);
    // Preserve what was written, re-derive what was delegated (`DESIGN.md`
    // section 6.4). A chart the key itself implied has nothing to preserve; a
    // chart somebody typed is relative to the key it was typed in, and naming
    // a new key says what it moves to rather than withdrawing it.
    if (chartSource == BotAnswer::Source::Chat && settings.key.valid)
      settings.chart = Harmony::resolve(
          Harmony::toRelative(settings.chart, settings.key), key);
    else
      settings.chart = Harmony::defaultChart(key);
    settings.key = key;
    keySource = BotAnswer::Source::Chat;
    keySetBy = username;
    return true;
  }

  // Degrees are read against the key the room is in, which is why the key is
  // taken first: "| ii | V | I |" means nothing on its own, and the resolved
  // absolute chart is what everything downstream sees (`PRINCIPLES` 10).
  MusicalKey::Key against;
  {
    juce::ScopedLock sl(stateMutex);
    against = settings.key;
  }

  Harmony::Chart chart;
  if (Harmony::parseChart(text, chart) ||
      (against.valid && Harmony::parseDegreeChart(text, against, chart))) {
    juce::ScopedLock sl(stateMutex);
    settings.chart = std::move(chart);
    chartSource = BotAnswer::Source::Chat;
    return true;
  }

  return false;
}

BotChat::Context PracticeBot::currentContext() const {
  BotChat::Context ctx;
  ctx.room = currentRoom();

  juce::ScopedLock sl(stateMutex);
  ctx.music.key = settings.key;
  ctx.music.keySource = keySource;
  ctx.music.keySetBy = keySetBy;
  ctx.music.chart = settings.chart;
  ctx.music.chartSource = chartSource;
  ctx.music.bpm = settings.bpm;
  ctx.music.bpi = settings.bpi;

  ctx.self.name = botName;
  ctx.self.handle = juce::String(BotNames::handleOf(botName.toStdString()));
  ctx.self.voice = bandVoice;
  ctx.self.settings = settings;
  ctx.self.phase = playState.current();
  ctx.self.chatMuted = chatMuted.load();
  return ctx;
}

bool PracticeBot::isPartCommand(const juce::String &text) {
  const auto t = text.trim().toLowerCase();
  for (const auto *cmd : kPartCommands)
    if (t == cmd)
      return true;
  return false;
}

juce::String PracticeBot::helpLine(const juce::String &name) {
  return name + " is a bot. Send it a private message saying 'leave' and it "
                "will go.";
}

void PracticeBot::setBandmates(juce::StringArray names, juce::String name) {
  juce::ScopedLock sl(stateMutex);
  bandmates = std::move(names);
  bandName = std::move(name);
}

juce::StringArray PracticeBot::botsPresent() const {
  juce::StringArray out;
  out.add(botName);
  for (const auto &m : netClient.getRoomMembers())
    if (m.username != botName && BotNames::looksLikeBot(m.username.toStdString()))
      out.add(m.username);
  // Sorted so that every bot in the room computes the same list, and therefore
  // agrees about who speaks without anybody having to ask.
  out.sort(true);
  return out;
}

void PracticeBot::timerCallback() {
  stopTimer();
  if (!active.load() || arrivalDone.exchange(true))
    return;

  // The rule: announce unless somebody has already announced ME.
  //
  // Self-referential, and that is what makes it work where a tiebreak does not.
  // A bot cannot know whether it is "first" -- at connect time the membership
  // list has not arrived, so every bot sees an empty room -- but it can always
  // know whether it has been introduced, because being introduced is something
  // it observes rather than something it has to infer.
  //
  // Everything falls out of that one question. During startup the earliest
  // waker sees the whole band and names all of them, so the others find
  // themselves already announced and stay quiet: one roster. A bot that joins
  // an hour later has not been announced, so it speaks -- and it names the band
  // it can see, which now includes everybody, so THE ANNOUNCEMENT LANDS WHEN
  // THE BAND IS COMPLETE rather than being lost because the moment passed. A
  // bot whose bandmates all failed to connect announces itself alone, correctly.
  if (announcedMe.load())
    return;

  const auto bots = botsPresent();

  // The roster lists what is ACTUALLY HERE, not what we were told to expect: a
  // bot that failed to connect is not announced as present, and bots brought by
  // two different people still make one sensible list.
  juce::StringArray entries;
  bool allSiblings = true;
  {
    juce::ScopedLock sl(stateMutex);
    for (const auto &name : bots) {
      if (!bandmates.isEmpty() && !bandmates.contains(name))
        allSiblings = false;
      const auto open = name.indexOfChar('[');
      const juce::String handle =
          open > 0 ? name.substring(0, open) : name;
      const juce::String instrument =
          open > 0 ? name.substring(open + 1)
                         .upToFirstOccurrenceOf("-bot]", false, false)
                   : juce::String();
      entries.add(instrument.isEmpty() ? handle
                                       : handle + " (" + instrument + ")");
    }
  }

  juce::String roster;
  {
    juce::ScopedLock sl(stateMutex);
    if (allSiblings && bandName.isNotEmpty())
      roster = bandName + " -- ";
  }
  roster += entries.joinIntoString(", ") + ".";

  netClient.sendChatMessage(roster);

  // The interesting thing first, and the destructive one stated so plainly
  // that nobody types it idly. Leading with `part` would invite a curious
  // player to empty their own room with the first command they were shown.
  netClient.sendChatMessage(
      "say a name to talk to one of us. say \"leave\" and we all go home.");
}

void PracticeBot::BandReply::timerCallback() {
  stopTimer();
  // Somebody got there first, so the room already has its answer. Saying it
  // again is the chorus this exists to prevent.
  if (heardOne || text.isEmpty())
    return;
  if (bot.chatMuted.load())
    return;
  bot.netClient.sendChatMessage(text);
}

int PracticeBot::speakDelayMs(const juce::String &botName) {
  // Long enough that the winner's line has crossed the server and come back to
  // everyone else -- loopback is immediate, a real server is tens of
  // milliseconds -- and short enough to read as an answer rather than a pause.
  std::uint32_t h = 2166136261u;
  for (auto c : botName)
    h = (h ^ (std::uint32_t)(juce::juce_wchar)c) * 16777619u;
  return 220 + (int)(h % 380u);
}

int PracticeBot::arrivalDelayMs() const {
  // Derived from the name rather than drawn randomly, so a room is reproducible
  // and a test can rely on it. Different names give different offsets, which is
  // all the spread has to do.
  std::uint32_t h = 2166136261u;
  for (auto c : botName)
    h = (h ^ (std::uint32_t)(juce::juce_wchar)c) * 16777619u;
  return 4000 + (int)(h % 2000u);
}

// The owner as the ROOM sees them. An anonymous NINJAM login arrives as
// `anonymous:nick`, so comparing against the bare nickname never matched and
// the eviction rules -- the ones that stop a bot outliving the player who
// brought it -- silently never fired for the commonest way anybody connects.
bool PracticeBot::isOwnerName(const juce::String &username,
                              const juce::String &ownerName) {
  if (ownerName.isEmpty())
    return false;
  return username == ownerName ||
         username.endsWithIgnoreCase(":" + ownerName);
}

void PracticeBot::onConnected() {
  // The arrival window: four seconds plus up to two more.
  //
  // The wait lets the join notices finish scrolling before the one line anybody
  // is meant to read. The SPREAD is what keeps two bots from announcing at
  // once -- whoever wakes first names the others, and they find themselves
  // already introduced. See timerCallback.
  //
  // Derived from the name rather than drawn randomly, so a room is reproducible
  // and a test can rely on it. Different names give different offsets, which is
  // all the spread has to do.
  startTimer(arrivalDelayMs());

  // Beyond that, nothing to do. The channel list was stored before connecting and
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

void PracticeBot::onRoomMembershipChange(const juce::String &username,
                                         bool joined) {
  // The authoritative way to know whether the owner is here, and the only one
  // that is not a race.
  //
  // `checkOwnerStillHere` below scans the room member list, which is maintained
  // on the network thread while this callback arrives on the message thread. A
  // player who joins and leaves inside one message-thread gap is, by the time
  // the scan runs, someone who was never in the list at all -- so the bot never
  // records having seen its owner, and therefore never leaves. On a real server
  // that is a bot outliving a player whose connection blipped at the wrong
  // moment, which is precisely the failure the eviction rules exist to prevent.
  //
  // An event does not go stale. A JOIN naming the owner means they arrived; a
  // PART naming them means they left, and it means they were here to leave,
  // which is why this path does not consult `sawOwner` at all.
  juce::String ownerName;
  {
    juce::ScopedLock sl(stateMutex);
    ownerName = owner;
  }
  // Introduce the band to the first person who turns up.
  //
  // The roster fires a few seconds after the BOTS connect, which in a room
  // started by a host process is several seconds before any human is there --
  // so the one line the band gets to introduce itself with was reliably said to
  // an empty room. Re-arming for the first human keeps the same rule ("announce
  // unless somebody announced me") and simply runs it when somebody can read it.
  //
  // Only for the first: with anybody else already present the band has been
  // seen, and a roster per arrival is the chattiness this design exists to
  // avoid.
  if (joined && !BotNames::looksLikeBot(username.toStdString())) {
    int otherHumans = 0;
    for (const auto &m : netClient.getRoomMembers())
      if (m.username != username && m.username != botName &&
          !BotNames::looksLikeBot(m.username.toStdString()))
        ++otherHumans;
    if (otherHumans == 0) {
      arrivalDone = false;
      announcedMe = false;
      startTimer(arrivalDelayMs());
    }
  }

  if (!isOwnerName(username, ownerName))
    return;

  if (joined) {
    sawOwner = true;
    return;
  }

  // First person, like everything else a bot says about itself: the chat line
  // already carries the name.
  netClient.sendChatMessage("leaving -- " + ownerName + " has gone.");
  part();
}

bool PracticeBot::checkOwnerStillHere() {
  // Leave when the player who brought the bot leaves. On a real server this is
  // the rule that matters most: walking away is enough to clean up after
  // yourself, with nothing to remember.
  //
  // This is the SECONDARY path, and it covers one case the membership events
  // above cannot: an owner who was already in the room before the bot arrived
  // never produces a JOIN the bot can hear, so their presence has to be
  // discovered by looking.
  //
  // Called from BOTH the user-info and the chat callbacks, because membership
  // is maintained from both and the departure order is not the obvious one. A
  // leaving player produces a USER_INFO_CHANGE marking their channels inactive
  // and then a PART; only the PART removes the name from roomMembers.
  // Checking on user-info alone therefore looks while the owner is still
  // listed, finds them present, and never looks again.
  juce::String ownerName;
  {
    juce::ScopedLock sl(stateMutex);
    ownerName = owner;
  }
  if (ownerName.isEmpty())
    return true;

  bool ownerPresent = false;
  for (const auto &m : netClient.getRoomMembers())
    if (isOwnerName(m.username, ownerName)) {
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

BotAddress::Room PracticeBot::currentRoom() const {
  BotAddress::Room room;

  auto add = [&room](const juce::String &name, const juce::String &channel) {
    BotAddress::Participant p;
    p.username = name.toStdString();
    p.handle = BotNames::handleOf(p.username);
    p.channel = channel.toLowerCase().toStdString();
    p.isBot = BotNames::looksLikeBot(p.username);
    if (p.isBot) {
      // The instrument is in the username between the bracket and the marker,
      // which is also what a player reads off the mixer.
      const auto open = name.indexOfChar('[');
      if (open > 0)
        p.instrument =
            name.substring(open + 1)
                .upToFirstOccurrenceOf("-bot]", false, false)
                .toLowerCase()
                .toStdString();
    }
    room.participants.push_back(p);
  };

  // Ourselves first, so the scan can find us even in an empty room.
  add(botName, channels.isEmpty() ? juce::String() : channels[0]);

  const auto users = netClient.getRemoteUsers();
  for (const auto &m : netClient.getRoomMembers()) {
    if (m.username == botName)
      continue;
    juce::String channel;
    const auto it = users.find(m.username);
    if (it != users.end() && !it->second.channels.empty())
      channel = it->second.channels.begin()->second.channelName;
    add(m.username, channel);
  }

  room.resolveHandles();
  return room;
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
  if (type != "MSG" && type != "PRIVMSG")
    return;

  // Have I just been introduced?
  //
  // The exact question, rather than the proxy an earlier version used ("did any
  // bot speak?"). A bot that lost a race is not the same as a bot that was
  // covered by somebody's roster, and only the second should stay silent.
  //
  // Any message from a bot naming me counts, which is safe because bots do not
  // speak unless spoken to: during the first few seconds of a room there is
  // nothing else a bot could be saying.
  if (BotNames::looksLikeBot(username.toStdString()) &&
      text.containsIgnoreCase(
          juce::String(BotNames::handleOf(botName.toStdString()))))
    announcedMe = true;

  // Another bot has spoken, so a band-wide line we were about to give has
  // already been given. This is the whole of the arbitration.
  if (BotNames::looksLikeBot(username.toStdString()))
    bandReply.somebodySpoke();

  const bool isPrivate = (type == "PRIVMSG");

  // The structured instructions are shouted, and take no address at all.
  //
  // A key tag and a chord chart are unambiguous by their SYNTAX -- nobody
  // types `[key: Dm]` or `| Am | F |` by accident -- and they are things the
  // whole band must agree about, so they are acted on wherever they appear and
  // answered by nobody. That is the one place an unaddressed room message
  // changes what a bot plays, and it is safe for the same reason `part` is:
  // the form is not something a person writes in passing.
  if (!isPrivate && handleStructured(text, username))
    return;

  BotAddress::Incoming in;
  in.sender = username.toStdString();
  in.text = text.toStdString();
  in.isPrivate = isPrivate;
  in.at = juce::Time::getMillisecondCounterHiRes() / 1000.0;

  // Everything from here is decided by `BotChat`, which is pure: who was
  // addressed, what they asked, and the words that answer it. This method used
  // to decide all three by matching exact strings, so the only way to see what
  // a bot would say was to start a room and say it -- which is why the
  // recognisers ended up measured to three decimal places while nothing
  // checked the replies at all.
  //
  // What is left here is the part that genuinely needs a bot: the socket, the
  // lock, and the state that outlives the message.
  const auto answer = BotChat::respond(currentContext(), in, attention);

  if (answer.speak) {
    // Answer where you were asked. A public question answered privately looks
    // like no answer at all, and the public path is how anybody else in the
    // room discovers that the bots can be spoken to.
    if (answer.privately)
      netClient.sendPrivateMessage(username, answer.text);
    else if (answer.forBand)
      // Acting is collective and speaking is arbitrated: the action below
      // happens in every addressed bot, and only the LINE about it is rationed.
      //
      // A bot that ACTED speaks ahead of one that had nothing to do. With the
      // band half stopped, "band stop" makes the playing ones wrap up and
      // leaves the silent ones with "already stopped" -- and whoever won a
      // flat race would answer for everybody. That is not merely noisy, it is
      // wrong: the room would be told nothing was happening while three bots
      // ended the tune. If nobody acted, the deferred line is the right answer
      // and it still gets said.
      bandReply.schedule(answer.text,
                         answer.act != BotChat::Act::None
                             ? speakDelayMs()
                             : speakDelayMs() + kIdleSpeakerPenaltyMs);
    else
      netClient.sendChatMessage(answer.text);
  }

  switch (answer.act) {
  case BotChat::Act::Part:
    part();
    return;
  case BotChat::Act::Reshuffle:
    shake();
    return;
  case BotChat::Act::SetLeadInstrument: {
    juce::ScopedLock sl(stateMutex);
    settings.leadOverride = answer.value;
    return;
  }
  case BotChat::Act::StartPlaying:
    startPlaying();
    return;
  case BotChat::Act::StopPlaying:
    stopPlaying();
    return;
  case BotChat::Act::SetChatMuted:
    chatMuted.store(answer.value != 0);
    return;
  case BotChat::Act::None:
    return;
  }
}

void PracticeBot::renderInterval(int numSamples, int intervalIndex) {
  if (!active.load() || numSamples <= 0)
    return;

  Render r;
  BandPlayState::State phase;
  {
    juce::ScopedLock sl(stateMutex);
    r = render;
    // Sampled ONCE, and the state advanced ONCE, for this interval. Reading it
    // again part-way through would tear an interval across two states, and
    // delivery is all-or-nothing -- half an ending is not something the
    // protocol can carry.
    phase = playState.current();
    playState.advance();
  }
  if (!r)
    return; // A silent bot is a valid bot.

  // Nothing on the wire at all, rather than an interval of zeroes: an
  // unsubscribed silent client costs the server nothing and the room hears no
  // difference.
  if (phase == BandPlayState::State::Silent)
    return;

  if (renderBuffer.getNumSamples() < numSamples)
    renderBuffer.setSize(2, numSamples, false, true, true);
  renderBuffer.clear(0, numSamples);

  // The phase sampled at the top of this interval, so what is rendered and what
  // the state machine thinks are the same thing by construction.
  r(renderBuffer, numSamples, intervalIndex, phaseFor(phase));

  if (!active.load())
    return;
  netClient.processCapturedAudio(renderBuffer, numSamples, 0, false);
}
