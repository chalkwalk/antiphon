#include "PracticeBot.h"

#include <algorithm>
#include <chrono>
#include <chalkwalk/music/Text.h>

#include "jambot/BotNames.h"

namespace cwtext = chalkwalk::music::text;

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

PracticeBot::PracticeBot(std::string name, std::vector<std::string> channelNames,
                         BotClient::ClientPtr client)
    : botName(std::move(name)), channels(std::move(channelNames)),
      netClient(std::move(client)) {
  if (channels.empty())
    channels.push_back("bot");

  // Deaf by default. A generative bot follows the grid rather than the room,
  // and an unsubscribed client never causes the server to send it an interval,
  // so it never allocates one. That is what keeps a room of bots costing one
  // client's worth of interval buffers instead of one per bot.
  netClient->setDefaultRecvEnabled(false);
  netClient->addListener(this);

  arrivalTimer = netClient->createTimer([this] { onArrivalDue(); });
  bandReplyTimer = netClient->createTimer([this] { onBandReplyDue(); });
  graceTimer = netClient->createTimer([this] { onGraceExpired(); });
}

PracticeBot::~PracticeBot() {
  netClient->removeListener(this);
  netClient->disconnect();
}

void PracticeBot::setRender(Render r) {
  std::lock_guard<std::mutex> sl(stateMutex);
  render = std::move(r);
}

void PracticeBot::setOwner(std::string ownerUsername) {
  std::lock_guard<std::mutex> sl(stateMutex);
  owner = std::move(ownerUsername);
}

void PracticeBot::setGrace(int afterDepartureMs, int beforeFirstArrivalMs) {
  graceMs = afterDepartureMs;
  initialGraceMs = beforeFirstArrivalMs;
}

int PracticeBot::humansPresent() const {
  int n = 0;
  for (const auto &m : netClient->members())
    if (m.username != botName && !BotNames::looksLikeBot(m.username))
      ++n;
  return n;
}

void PracticeBot::ownerAbsent(bool everArrived) {
  if (!active.load())
    return;

  // Others are still here, so keep playing and start no clock at all. The band
  // plays for the ROOM; the owner is only who summoned it, and stopping four
  // voices because one person's router hiccuped disrupts everybody who did not
  // drop. Nothing leaks: anyone present can send them home
  // (docs/BOT-CHAT.md section 15).
  if (everArrived && humansPresent() > 0) {
    graceTimer->stop();
    return;
  }

  // Nobody is listening, so playing on is waste -- and an ending is FOR
  // somebody, so this cuts rather than wrapping up.
  if (everArrived) {
    std::lock_guard<std::mutex> sl(stateMutex);
    playState.silence();
  }

  // `arm` used to refuse to restart a running countdown, which is what makes
  // this safe to call on every user-info change.
  if (!graceTimer->isRunning())
    graceTimer->start(everArrived ? graceMs : initialGraceMs);
}

void PracticeBot::ownerBack() {
  graceTimer->stop();

  // Deliberately says nothing of its own.
  //
  // The arrival roster already re-arms for the first human in a room, which on
  // a reconnect is the returning player -- and it says the band is here and
  // how to start it, which is the whole of what a welcome back would say. Where
  // the roster does NOT re-arm, other people were present, so the band never
  // stopped and there is nothing to announce. Both cases are covered without a
  // line, which is better than a line: four bots saying "welcome back" is the
  // chorus this design exists to prevent, and the cheapest way not to have it
  // is not to have the line.
}

void PracticeBot::setListensTo(std::string username) {
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    listensTo = std::move(username);
  }
  // Subscribing to one player is still deaf to everyone else; the recv flags
  // are applied as channels appear, in onUserInfoChange.
}

bool PracticeBot::join(const std::string &host, int port, double sampleRate) {
  rate = sampleRate;
  netClient->setSampleRate(sampleRate);
  std::vector<std::string> names;
  for (const auto &c : channels)
    names.push_back(c);
  netClient->setChannels(names);
  netClient->connect(host, port, botName, "");
  active = true;
  return true;
}

void PracticeBot::part() {
  // Idempotent, and terminal: see onDisconnected for why there is no rejoin.
  if (!active.exchange(false))
    return;
  arrivalTimer->stop();
  bandReplyTimer->stop();
  graceTimer->stop();
  netClient->disconnect();
}

void PracticeBot::playAs(BotBand::Voice voice, const MusicalKey::Key &key,
                         int bpm, int bpi, double sampleRate,
                         std::uint32_t seed) {
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    bandVoice = voice;
    settings = BotBand::defaults(key, bpm, bpi, sampleRate, seed);
  }
  // In the band, and SILENT. The bots connect before the player does, so a
  // band that played on connect played to an empty room -- and arrival then
  // becomes the first turn of the same stop/start loop you use between tunes
  // rather than a special case (docs/BOT-CHAT.md section 15).
  inBand = true;

  setRender([this](float *left, float *right, int numSamples,
                   int intervalIndex, BotBand::Phase phase) {
    BotBand::Voice v;
    BotBand::Settings snapshot;
    {
      std::lock_guard<std::mutex> sl(stateMutex);
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
    const bool stereo = BotBand::isStereo(v) && right != nullptr;
    BotBand::renderInterval(v, snapshot, intervalIndex, phase, left,
                            stereo ? right : nullptr, numSamples);
    if (!stereo && right != nullptr)
      std::copy(left, left + numSamples, right);
  });
}

void PracticeBot::shake() {
  std::lock_guard<std::mutex> sl(stateMutex);
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
  std::lock_guard<std::mutex> sl(stateMutex);
  return playState.current();
}

void PracticeBot::startPlaying() {
  std::lock_guard<std::mutex> sl(stateMutex);
  playState.start();
}

void PracticeBot::stopPlaying() {
  std::lock_guard<std::mutex> sl(stateMutex);
  playState.stop();
}

BotBand::Settings PracticeBot::currentSettings() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return settings;
}

bool PracticeBot::isShakeCommand(const std::string &text) {
  const auto t = chalkwalk::music::text::lower(chalkwalk::music::text::trim(text));
  return t == "shake" || t == "new" || t == "again";
}

bool PracticeBot::handleStructured(const std::string &text,
                                   const std::string &username) {
  // Band membership, not audibility. A silent bot is still in the room and
  // still follows the key and the chart -- that is most of what somebody does
  // BETWEEN tunes, and a bot that stopped listening while stopped would have
  // to be told everything again when it came back.
  if (!inBand.load())
    return false;

  std::lock_guard<std::mutex> sl(stateMutex);

  // The decision itself lives in RoomHarmony, because the editor has to make
  // exactly the same one and the two used to disagree (`PRINCIPLES` 8).
  RoomHarmony::State st;
  st.key = settings.key;
  st.chart = settings.chart;
  st.chartFromChat = chartSource == BotAnswer::Source::Chat;

  switch (RoomHarmony::apply(text, st)) {
  case RoomHarmony::Change::Key:
    settings.key = st.key;
    settings.chart = st.chart;
    keySource = BotAnswer::Source::Chat;
    keySetBy = username;
    return true;
  case RoomHarmony::Change::Chart:
    settings.chart = st.chart;
    chartSource = BotAnswer::Source::Chat;
    return true;
  case RoomHarmony::Change::None:
    break;
  }
  return false;
}

BotChat::Context PracticeBot::currentContext() const {
  BotChat::Context ctx;
  ctx.room = currentRoom();

  std::lock_guard<std::mutex> sl(stateMutex);
  ctx.music.key = settings.key;
  ctx.music.keySource = keySource;
  ctx.music.keySetBy = keySetBy;
  ctx.music.chart = settings.chart;
  ctx.music.chartSource = chartSource;
  ctx.music.bpm = settings.bpm;
  ctx.music.bpi = settings.bpi;
  ctx.music.articulation = settings.articulation;

  ctx.self.name = botName;
  ctx.self.handle = std::string(BotNames::handleOf(botName));
  ctx.self.voice = bandVoice;
  ctx.self.settings = settings;
  ctx.self.phase = playState.current();
  ctx.self.chatMuted = chatMuted.load();
  return ctx;
}

bool PracticeBot::isPartCommand(const std::string &text) {
  const auto t = chalkwalk::music::text::lower(chalkwalk::music::text::trim(text));
  for (const auto *cmd : kPartCommands)
    if (t == cmd)
      return true;
  return false;
}

std::string PracticeBot::helpLine(const std::string &name) {
  return name + " is a bot. Send it a private message saying 'leave' and it "
                "will go.";
}

void PracticeBot::setBandmates(std::vector<std::string> names, std::string name) {
  std::lock_guard<std::mutex> sl(stateMutex);
  bandmates = std::move(names);
  bandName = std::move(name);
}

std::vector<std::string> PracticeBot::botsPresent() const {
  std::vector<std::string> out;
  out.push_back(botName);
  for (const auto &m : netClient->members())
    if (m.username != botName && BotNames::looksLikeBot(m.username))
      out.push_back(m.username);
  // Sorted so that every bot in the room computes the same list, and therefore
  // agrees about who speaks without anybody having to ask. Case-insensitive,
  // as the sort it replaces was.
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return chalkwalk::music::text::lower(a) < chalkwalk::music::text::lower(b);
  });
  return out;
}

void PracticeBot::onArrivalDue() {
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
  std::vector<std::string> entries;
  bool allSiblings = true;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    for (const auto &name : bots) {
      if (!bandmates.empty() &&
          std::find(bandmates.begin(), bandmates.end(), name) == bandmates.end())
        allSiblings = false;

      const auto open = name.find('[');
      const std::string handle =
          open == std::string::npos ? name : name.substr(0, open);
      std::string instrument;
      if (open != std::string::npos) {
        const auto rest = name.substr(open + 1);
        const auto end = rest.find("-bot]");
        instrument = end == std::string::npos ? rest : rest.substr(0, end);
      }
      entries.push_back(instrument.empty() ? handle
                                           : handle + " (" + instrument + ")");
    }
  }

  std::string roster;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    if (allSiblings && !bandName.empty())
      roster = bandName + " -- ";
  }
  roster += chalkwalk::music::text::join(entries, ", ") + ".";

  netClient->sendChat(std::string(roster));

  // The interesting thing first, and the destructive one stated so plainly
  // that nobody types it idly. Leading with `part` would invite a curious
  // player to empty their own room with the first command they were shown.
  // The way IN first, because the band is silent and a room where nothing
  // happens looks broken; then how to talk to one of us; and the destructive
  // one last and stated plainly enough that nobody types it idly.
  netClient->sendChat(
      "say \"band play\" to start us and \"band stop\" to end the tune. say a "
      "name to talk to one of us. say \"leave\" and we all go home.");
}

void PracticeBot::onBandReplyDue() {
  // Somebody got there first, so the room already has its answer. Saying it
  // again is the chorus this exists to prevent.
  if (heardAnotherBot || pendingBandReply.empty())
    return;
  if (chatMuted.load())
    return;
  netClient->sendChat(pendingBandReply);
}

void PracticeBot::onGraceExpired() { part(); }

int PracticeBot::speakDelayMs(const std::string &botName) {
  // Long enough that the winner's line has crossed the server and come back to
  // everyone else -- loopback is immediate, a real server is tens of
  // milliseconds -- and short enough to read as an answer rather than a pause.
  std::uint32_t h = 2166136261u;
  for (auto c : botName)
    h = (h ^ (std::uint32_t)(char)c) * 16777619u;
  return 220 + (int)(h % 380u);
}

int PracticeBot::arrivalDelayMs() const {
  // Derived from the name rather than drawn randomly, so a room is reproducible
  // and a test can rely on it. Different names give different offsets, which is
  // all the spread has to do.
  std::uint32_t h = 2166136261u;
  for (auto c : botName)
    h = (h ^ (std::uint32_t)(char)c) * 16777619u;
  return 4000 + (int)(h % 2000u);
}

// The owner as the ROOM sees them. An anonymous NINJAM login arrives as
// `anonymous:nick`, so comparing against the bare nickname never matched and
// the eviction rules -- the ones that stop a bot outliving the player who
// brought it -- silently never fired for the commonest way anybody connects.
bool PracticeBot::isOwnerName(const std::string &username,
                              const std::string &ownerName) {
  if (ownerName.empty())
    return false;
  // An anonymous NINJAM login arrives as `anonymous:nick`.
  const auto lower = chalkwalk::music::text::lower(username);
  const auto suffix = chalkwalk::music::text::lower(":" + ownerName);
  return username == ownerName ||
         (lower.size() >= suffix.size() &&
          lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0);
}

void PracticeBot::onConnected() {
  // The long clock starts now: nobody has ever arrived, and this is what stops
  // a room being started and forgotten. Cancelled the moment the owner shows.
  if (!graceTimer->isRunning())
    graceTimer->start(initialGraceMs);

  // The arrival window: four seconds plus up to two more.
  //
  // The wait lets the join notices finish scrolling before the one line anybody
  // is meant to read. The SPREAD is what keeps two bots from announcing at
  // once -- whoever wakes first names the others, and they find themselves
  // already introduced. See onArrivalDue.
  //
  // Derived from the name rather than drawn randomly, so a room is reproducible
  // and a test can rely on it. Different names give different offsets, which is
  // all the spread has to do.
  arrivalTimer->start(arrivalDelayMs());

  // Beyond that, nothing to do. The channel list was stored before connecting and
  // NinjamClient sends it itself the moment auth succeeds
  // (NinjamClient.cpp:347), so resending here was redundant -- and it was a
  // write from the message thread at the exact moment the network thread might
  // be tearing the socket down, which is how the fd race above was found.
}

void PracticeBot::onDisconnected(const std::string &) {
  // Terminal, always. The server exited, the network went, an admin kicked it:
  // all the same, and all final.
  //
  // DO NOT ADD A RECONNECT. A bot that reconnects is a bot nobody can get rid
  // of, and these can be pointed at a real server. The absence of retry logic
  // here is the feature.
  active = false;
}

void PracticeBot::onRoomMembershipChange(const std::string &rawUsername,
                                         bool joined) {
  const std::string username(rawUsername);
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
  std::string ownerName;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
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
  if (joined && !BotNames::looksLikeBot(username)) {
    int otherHumans = 0;
    for (const auto &m : netClient->members())
      if (m.username != username &&
          m.username != botName &&
          !BotNames::looksLikeBot(m.username))
        ++otherHumans;
    if (otherHumans == 0) {
      arrivalDone = false;
      announcedMe = false;
      arrivalTimer->start(arrivalDelayMs());
    }
  }

  if (!isOwnerName(username, ownerName))
    return;

  if (joined) {
    sawOwner = true;
    ownerBack();
    return;
  }

  // Not fatal any more. A departure starts a countdown, because people's
  // connections drop and there is deliberately no reconnect -- so a bot that
  // parted on a thirty-second blip could not be got back at all.
  ownerAbsent(true);
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
  std::string ownerName;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    ownerName = owner;
  }
  if (ownerName.empty())
    return true;

  bool ownerPresent = false;
  for (const auto &m : netClient->members())
    if (isOwnerName(m.username, ownerName)) {
      ownerPresent = true;
      break;
    }

  if (ownerPresent) {
    const bool wasAway = !sawOwner.exchange(true) || graceTimer->isRunning();
    if (wasAway)
      ownerBack();
    return true;
  }

  // Absent before ever arriving is not "left" -- the bots connect before the
  // player does. It still counts down, on the longer clock, so a forgotten
  // room does not sit on a real server for ever.
  ownerAbsent(sawOwner.load());
  return active.load();
}

void PracticeBot::onUserInfoChange() {
  if (!checkOwnerStillHere())
    return;

  std::string wanted;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    wanted = listensTo;
  }
  if (wanted.empty())
    return;

  // Subscribe to exactly one player. Channels arrive over time, so this runs on
  // every change rather than once.
  for (const auto &peer : netClient->peers()) {
    if (peer.username != wanted)
      continue;
    for (const auto &ch : peer.channels)
      if (!ch.recvEnabled)
        netClient->setRecv(peer.username, ch.index, true);
  }
}

void PracticeBot::onServerConfig(int bpm, int bpi) {
  std::lock_guard<std::mutex> sl(stateMutex);
  if (bpm > 0)
    settings.bpm = bpm;
  if (bpi > 0)
    settings.bpi = bpi;
}

BotAddress::Room PracticeBot::currentRoom() const {
  BotAddress::Room room;

  auto add = [&room](const std::string &name, const std::string &channel) {
    BotAddress::Participant p;
    p.username = name;
    p.handle = BotNames::handleOf(p.username);
    p.channel = channel;
    p.isBot = BotNames::looksLikeBot(p.username);
    if (p.isBot) {
      // The instrument is in the username between the bracket and the marker,
      // which is also what a player reads off the mixer.
      const auto open = name.find('[');
      if (open != std::string::npos) {
        const auto rest = name.substr(open + 1);
        const auto end = rest.find("-bot]");
        p.instrument =
            cwtext::lower(end == std::string::npos ? rest : rest.substr(0, end));
      }
    }
    room.participants.push_back(p);
  };

  // Ourselves first, so the scan can find us even in an empty room.
  add(botName, channels.empty() ? std::string() : channels[0]);

  const auto peers = netClient->peers();
  for (const auto &m : netClient->members()) {
    if (m.username == botName)
      continue;
    std::string channel;
    for (const auto &peer : peers)
      if (peer.username == m.username && !peer.channels.empty()) {
        channel = std::string(peer.channels.front().name);
        break;
      }
    add(m.username, channel);
  }

  room.resolveHandles();
  return room;
}

void PracticeBot::onChatMessage(const std::string &rawType,
                                const std::string &rawUsername,
                                const std::string &rawText) {
  // A bot that has parted answers nothing, whatever it is still handed.
  //
  // This used to be the transport's job: `disconnectFromServer` stopped the
  // messages, so the question never arose. The interface makes no such promise
  // -- a minimal client may well deliver what is already in flight -- and
  // relying on a guarantee nobody stated is what breaks when the thing
  // underneath is swapped, which is the entire point of having an interface.
  if (!active.load())
    return;

  const std::string type(rawType), username(rawUsername), text(rawText);
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
  if (BotNames::looksLikeBot(username) &&
      cwtext::contains(cwtext::lower(text),
                     cwtext::lower(BotNames::handleOf(botName))))
    announcedMe = true;

  // Another bot has spoken, so a band-wide line we were about to give has
  // already been given. This is the whole of the arbitration.
  if (BotNames::looksLikeBot(username))
    heardAnotherBot = true;

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
  in.sender = username;
  in.text = text;
  in.isPrivate = isPrivate;
  // Seconds on a monotonic clock: the attention window measures elapsed time,
  // and a wall clock that steps would open or close it wrongly.
  using namespace std::chrono;
  in.at = duration<double>(steady_clock::now().time_since_epoch()).count();

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
      netClient->sendPrivate(username, answer.text);
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
    {
      pendingBandReply = answer.text;
      heardAnotherBot = false;
      bandReplyTimer->start(answer.act != BotChat::Act::None
                                ? speakDelayMs()
                                : speakDelayMs() + kIdleSpeakerPenaltyMs);
    }
    else
      netClient->sendChat(std::string(answer.text));
  }

  switch (answer.act) {
  case BotChat::Act::Part:
    part();
    return;
  case BotChat::Act::Reshuffle:
    shake();
    return;
  case BotChat::Act::SetArticulation: {
    std::lock_guard<std::mutex> sl(stateMutex);
    settings.articulation = answer.value;
    return;
  }
  case BotChat::Act::SetLeadInstrument: {
    std::lock_guard<std::mutex> sl(stateMutex);
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
    std::lock_guard<std::mutex> sl(stateMutex);
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

  if ((int)renderLeft.size() < numSamples) {
    renderLeft.resize((size_t)numSamples);
    renderRight.resize((size_t)numSamples);
  }
  std::fill(renderLeft.begin(), renderLeft.begin() + numSamples, 0.0f);
  std::fill(renderRight.begin(), renderRight.begin() + numSamples, 0.0f);

  // The phase sampled at the top of this interval, so what is rendered and what
  // the state machine thinks are the same thing by construction.
  r(renderLeft.data(), renderRight.data(), numSamples, intervalIndex,
    phaseFor(phase));

  if (!active.load())
    return;
  // Through the interface: the bot hands over pointers and has no idea what
  // happens to them.
  netClient->transmit(renderLeft.data(), renderRight.data(), numSamples);
}
