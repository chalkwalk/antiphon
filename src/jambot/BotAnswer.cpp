#include "Music.h"
#include "BotAnswer.h"

#include <chalkwalk/music/Text.h>


namespace BotAnswer {

namespace text = chalkwalk::music::text;

namespace {

// Bots speak lower case. It is the register the room is in.
std::string chart(const Room &room) {
  // Spelled against the key rather than by one flag for the whole line: a room
  // reads a chart back so it can be pasted, so the reading has to BE the
  // notation. D major takes sharps and its lowered second is still Eb.
  return Harmony::chartText(room.chart, room.key);
}

// Quoted, so it reads as something to type rather than running into the
// sentence. Still inert: the line does not START with `/key`.
std::string advice(const MusicalKey::Key &key) {
  // Straight to the convention rather than through a helper of Antiphon's:
  // what a bot tells a player to type is a NINJAM room convention, and the
  // bots reach it directly so they need nothing from the plugin.
  return "\"" +
         chalkwalk::ninjam::conventions::keyAdviceLine(
             MusicalKey::displayName(key)) +
         "\"";
}

std::string provenance(Source source, const std::string &setBy) {
  switch (source) {
  case Source::Chat:
    return setBy.empty() ? std::string(", said in the room")
                         : ", " + text::lower(setBy) + " said so";
  case Source::Topic:
    // The age matters and is unknowable: the topic is sent only to a joining
    // client, so all we can honestly claim is that nothing has changed it since.
    return " -- from the topic, and nobody has said otherwise since i joined";
  case Source::Defaulted:
    return {};
  }
  return {};
}

} // namespace

// A NOUN PHRASE, so it composes after "we are in". Returning a whole sentence
// here produced "we are in nobody has named a key, so i defaulted to C major",
// which is how the caller found out.
std::string describeKey(const Room &room) {
  if (!room.key.valid)
    return "no key";
  if (room.keySource == Source::Defaulted)
    return MusicalKey::displayName(room.key) + ", which nobody chose";
  return std::string(MusicalKey::displayName(room.key)) +
         provenance(room.keySource, room.keySetBy);
}

// Likewise a noun phrase, to follow "the chart is" or "i am on".
//
// Never "playing on the key alone": there is always a chart, because a key
// arriving sets `Harmony::defaultChart`, and a bot being wrong about what it
// is playing is a bot being wrong about the only thing it is authoritative on.
std::string describeChart(const Room &room) {
  if (room.chart.empty())
    return "no chart";
  if (room.chartSource == Source::Defaulted)
    return chart(room) + ", the default for the key";
  if (room.chartSource == Source::Topic)
    return chart(room) + provenance(Source::Topic, {});
  // From chat: that it is not the default already says somebody put it up.
  return chart(room);
}

std::string answerSetKey(const Room &room, const MusicalKey::Key &wanted) {
  const std::string here = "we are in " + describeKey(room) + ".";

  if (!wanted.valid)
    return "i could not tell which key you meant. put something like " +
           advice(MusicalKey::parseName("G minor")) +
           " at the start of a line and everyone follows it.";

  // It can act, and offers to -- but it explains first, because the explanation
  // works for everyone and outlasts this conversation.
  return "the key is the room's, not mine. " + here + " put " + advice(wanted) +
         " at the start of a line and everyone follows it, or say the word and "
         "i will put it up.";
}

std::string answerSetChart(const Room &room) {
  const std::string how =
      " put one on a line of its own, starting with a bar, and i will play it.";
  if (room.chartSource == Source::Defaulted)
    return "nobody has put a chart up, so i am on " + describeChart(room) + "." +
           how;
  return "the chart is the room's. right now it is " + describeChart(room) +
         "." + how;
}

std::string answerResetChart(const Room &room) {
  const auto standard =
      Harmony::chartText(Harmony::defaultChart(room.key), room.key);

  // Already there. Handing back a line that would change nothing looks like an
  // answer and wastes the paste.
  if (room.chartSource == Source::Defaulted)
    return "we are already on the default for " +
           MusicalKey::displayName(room.key) + ": " + standard + ".";

  return "the default in " + MusicalKey::displayName(room.key) + " is " +
         standard + ". put it up and i will follow.";
}

std::string answerSetTempo(const Room &room, int wantBpm, int wantBpi) {
  // Both, always: 120 at 8 and 120 at 32 are completely different rooms, and
  // one without the other says almost nothing.
  const std::string here = "we are at " + std::to_string(room.bpm) + " bpm, " +
                            std::to_string(room.bpi) + " bpi.";

  if (wantBpm > 0 && !ChatFormat::isVotableBpm(wantBpm))
    return "the tempo vote only goes from 40 to 400 bpm. " + here;
  if (wantBpi > 0 && !ChatFormat::isVotableBpi(wantBpi))
    return "the interval vote only goes from 2 to 64 bpi. " + here;

  std::string how;
  if (wantBpm > 0)
    how = "\"!vote bpm " + std::to_string(wantBpm) + "\"";
  if (wantBpi > 0)
    how = (how.empty() ? std::string() : how + " and ") + "\"!vote bpi " +
          std::to_string(wantBpi) + "\"";
  if (how.empty())
    how = "\"!vote bpm " + std::to_string(room.bpm) + "\" or \"!vote bpi " +
          std::to_string(room.bpi) + "\", with the number you want";

  return "tempo is a server vote, not mine to give. " + here + " type " + how +
         ", and i will back it once the room has.";
}

std::string answerVoteRequest(const Room &room) {
  (void)room;
  return "i do not start votes -- four of us backing one person is that person "
         "having four votes. start it and i will back you once the room has.";
}

} // namespace BotAnswer
