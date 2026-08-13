#pragma once

#include <string>
#include <vector>

// Who is a message for?
//
// This is the question `docs/BOT-CHAT.md` section 5 says decides whether talking
// bots are tolerable at all. Four bots answering one question is the failure the
// whole design exists to avoid, and it would happen on the very first "what are
// you playing".
//
// The rule: exactly the bots that were addressed answer, and nobody is addressed
// by default.
//
// There is NO sentence parsing here and none is needed. A bot knows every
// username in the room, that list is short, and the names in it are proper
// nouns -- so addressing is a scan of a message's tokens against a tiny known
// vocabulary, which is a far easier problem than working out what a sentence is
// doing. What position a name falls in changes only how strongly it counts.
//
// JUCE-free so the corpus in `test/fixtures/bot-addressing.txt` can drive it in
// the headless suite. 150 cases, and they are the specification.

namespace BotAddress {

// Somebody in the room, as a bot understands them.
struct Participant {
  std::string username;   // "Delvo[bass-bot]", or "dave"
  std::string handle;     // "delvo", "dave" -- lowercase, and how you address them
  std::string instrument; // "bass" -- empty for a human
  std::string channel;    // what their channel is called, lowercase
  bool isBot = false;

  // A bot whose handle collides with somebody else's name loses it: the full
  // username still works, and so does the instrument. Silence beats a wrong
  // answer, and this is "never answer a message aimed at somebody else" seen
  // from the other side.
  bool handleUsable = true;
};

struct Room {
  std::vector<Participant> participants;

  // Fills in `handleUsable` by checking every handle against every other
  // participant's name. Call after building the list.
  void resolveHandles();

  const Participant *find(const std::string &username) const;
};

// What a message turned out to be, for one particular bot.
enum class Address {
  Ignore,       // not for me: the default, and the commonest answer by far
  Private,      // a private message, which is addressed by construction
  Named,        // explicitly addressed in the room
  Opener,       // my name alone -- greet, and open the attention window
  Collective,   // everyone, all, band
  Continuation, // unaddressed, but my window is open and this is its owner
  PartAll,      // the whole band is being sent home
  PartMe,       // just me
};

// One bot's memory of a conversation. Belongs to a PERSON, not to the room:
// two other people talking are not talking to the bot, and assuming otherwise
// is the commonest way a design like this becomes insufferable.
struct Attention {
  std::string owner; // empty when closed
  double openedAt = 0.0;
  int turnsLeft = 0;

  bool openFor(const std::string &who, double now, double windowSeconds) const {
    return !owner.empty() && owner == who && turnsLeft > 0 &&
           now - openedAt <= windowSeconds;
  }
};

inline constexpr double kWindowSeconds = 60.0;
inline constexpr int kWindowTurns = 6;

struct Incoming {
  std::string sender;
  std::string text;
  bool isPrivate = false;
  double at = 0.0; // seconds, for the window
};

// The decision. `attention` is read and updated: being addressed opens the
// window, somebody else being addressed closes it.
Address classify(const Room &room, const std::string &me, const Incoming &msg,
                 Attention &attention);

// Exposed for testing, because each is a rule in its own right.
bool isPartCommand(const std::string &text);
bool isCourtesy(const std::string &text);
std::vector<std::string> tokenise(const std::string &text);

} // namespace BotAddress
