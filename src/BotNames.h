#pragma once

#include <cstdint>
#include <string>
#include <vector>

// What the bots are called, and why they are called anything.
//
// A name here is an ADDRESS before it is a personality. `docs/BOT-CHAT.md`
// section 5 addresses a bot by scanning a message's tokens against the room's
// user list, and how safely that can be done depends entirely on how rare the
// name is: `delvo` can be matched anywhere in a sentence, so "what are the
// changes delvo" works, while an instrument word like `bass` can only be
// matched where a name would go, because "the bass is too loud" is an ordinary
// remark and must not summon anybody.
//
// So the criteria are mechanical rather than a matter of taste:
//
//   - not an ordinary English word, a personal name, or a brand, so it can be
//     matched anywhere in a sentence;
//   - one obvious pronunciation. `Ravo` and `Pemo` were rejected for having
//     two apiece, with nothing to choose between them;
//   - a rime an English reader already owns. This matters more than syllable
//     count, which an earlier draft asked for instead: `Mirn` is one syllable
//     and reads instantly because `-irn` is fern, burn, turn, while `Nolm`,
//     `Selm`, `Velk` and `Cralt` are the same length and read as truncations,
//     their clusters having no familiar English pattern behind them;
//   - one token, no spaces. Every Ninjam client sends a private message as
//     `/msg <user> <text>` and splits on the first space, so the original
//     `Keys [bot]` could not be sent one at all -- it addressed a user called
//     `Keys`, who does not exist, and failed silently;
//   - distinct first letters, and at least two edits apart, so a near-miss on a
//     typo stays unambiguous.
//
// The four in use were chosen by searching thirty candidates and keeping the
// least occupied -- not by counting results, which no search engine reports,
// but by asking whether the word is already a person, a handle or a brand that
// somebody might turn up using. Eighteen were struck for being exactly that,
// including a Premier League goalkeeper, a techno producer on Drumcode (the
// worst possible collision for a music program), an AI chat app, and several
// ordinary given names. What is left is owned by a dog chew, some industrial
// screwdrivers, a Bhutanese stone-throwing sport and a gas-meter acronym.

namespace BotNames {

// The pool.
//
// Four names for four players, which is a KNOWN GAP rather than an oversight:
// `docs/BOT-CHAT.md` wants spares so that a name colliding with somebody
// already in the room can be skipped at join, and with exactly four there is
// nothing to skip to -- a collision falls through to the degraded path instead,
// where the short handle is withdrawn and the full username still works.
//
// It is four rather than six because the two obvious spares failed the criteria
// above and it would be dishonest to smuggle them in as reserves: `Vessa` reads
// as a shortened Vanessa, which is the plausible-human-name fault that ruled out
// `Hollis` and `Wren`, and `Ravo` has two pronunciations with nothing to choose
// between them. A name that is not good enough to use is not good enough to
// keep in reserve.
//
// Ordered, and the order is part of the contract -- `bandFor` rotates through
// it, so the same seed brings the same players back.
inline const std::vector<std::string> &pool() {
  static const std::vector<std::string> names = {"Mirn", "Delvo", "Pundo",
                                                 "Quado"};
  return names;
}

// The tutor is not one of them.
//
// It is a role rather than a bandmate, and a role is addressed by what it is:
// `tutor:` is what anybody would type without being told, and nobody says the
// word casually in a jam. Matched in the address position only, like `band`.
inline const char *tutorName() { return "Tutor"; }

// The suffix that makes a bot legible as one, to a human reading the mixer and
// to other bots deciding whether to answer.
//
// It identifies nothing and is trivially spoofable, which is fine, because it
// decides only who talks. A human naming themselves this way is choosing to be
// ignored, which is not an attack (`docs/BOT-CHAT.md` section 5).
inline std::string usernameFor(const std::string &name,
                               const std::string &instrument) {
  return name + "[" + instrument + "-bot]";
}

// The short handle a bot answers to, lowercased: the part before the bracket.
inline std::string handleOf(const std::string &username) {
  const auto bracket = username.find('[');
  std::string handle = username.substr(0, bracket);
  for (auto &c : handle)
    c = (char)std::tolower((unsigned char)c);
  return handle;
}

// True if this username carries the bot marker.
inline bool looksLikeBot(const std::string &username) {
  return username.size() > 5 &&
         username.compare(username.size() - 5, 5, "-bot]") == 0;
}

// Pick `count` names, skipping any that collide with somebody already in the
// room, deterministically from the seed.
//
// A collision is checked against the whole of each participant's name and
// against its handle, case-insensitively, because the risk is not that a human
// is called `Delvo[bass-bot]` -- it is that one is called `delvo`, which makes
// the short handle ambiguous and would cost the bot the ability to be addressed
// naturally. Skipping at join is cheaper than degrading afterwards.
std::vector<std::string> bandFor(int count, std::uint32_t seed,
                                 const std::vector<std::string> &taken);

} // namespace BotNames
