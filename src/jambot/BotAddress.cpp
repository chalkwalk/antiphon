#include "BotAddress.h"

#include <algorithm>
#include <cctype>

namespace BotAddress {

namespace {

std::string lowered(const std::string &s) {
  std::string out = s;
  for (auto &c : out)
    c = (char)std::tolower((unsigned char)c);
  return out;
}

bool isWordChar(char c) {
  return std::isalnum((unsigned char)c) != 0 || c == '\'';
}

// Damerau-Levenshtein, capped. A transposition counts as one edit because
// `kti` for `kit` is one slip of the fingers, not two.
int editDistance(const std::string &a, const std::string &b) {
  const int n = (int)a.size(), m = (int)b.size();
  std::vector<std::vector<int>> d((size_t)n + 1,
                                  std::vector<int>((size_t)m + 1, 0));
  for (int i = 0; i <= n; ++i)
    d[(size_t)i][0] = i;
  for (int j = 0; j <= m; ++j)
    d[0][(size_t)j] = j;

  for (int i = 1; i <= n; ++i)
    for (int j = 1; j <= m; ++j) {
      const int cost = a[(size_t)i - 1] == b[(size_t)j - 1] ? 0 : 1;
      int best = std::min({d[(size_t)i - 1][(size_t)j] + 1,
                           d[(size_t)i][(size_t)j - 1] + 1,
                           d[(size_t)i - 1][(size_t)j - 1] + cost});
      if (i > 1 && j > 1 && a[(size_t)i - 1] == b[(size_t)j - 2] &&
          a[(size_t)i - 2] == b[(size_t)j - 1])
        best = std::min(best, d[(size_t)i - 2][(size_t)j - 2] + 1);
      d[(size_t)i][(size_t)j] = best;
    }
  return d[(size_t)n][(size_t)m];
}

// How far a token may stray and still be recognised. Short words have to be
// held tighter or every three-letter typo hits something.
int nearMissBudget(const std::string &target) {
  if (target.size() <= 3)
    return 1;
  return 2;
}

// A near miss has to start the same way.
//
// Without this the budget alone is far too generous on short words: "fast" is
// two edits from "bass" and would summon the bass player out of "i think the
// tempo is too fast". People mistype the middle and the end of a word, and
// almost never its first letter -- so this costs nothing real and removes a
// whole class of false address.
bool couldBeTypoOf(const std::string &token, const std::string &target) {
  if (token.empty() || target.empty() || token[0] != target[0])
    return false;
  return editDistance(token, target) <= nearMissBudget(target);
}

// The words that name an instrument rather than a player. Only ever matched
// where a name would go, or under the conditions in `instrumentAddressed`,
// because every one of them is also an ordinary noun.
struct InstrumentWord {
  const char *word;
  const char *instrument;
};

const InstrumentWord kInstrumentWords[] = {
    {"kit", "kit"},       {"drums", "kit"},    {"drum", "kit"},
    {"drummer", "kit"},   {"bass", "bass"},    {"bassist", "bass"},
    {"keys", "keys"},     {"piano", "keys"},   {"pad", "keys"},
    {"keyboard", "keys"}, {"lead", "lead"},    {"soloist", "lead"},
    {"melody", "lead"},   {"tutor", "tutor"},  {"teacher", "tutor"},
};

const char *kCollectives[] = {"everyone", "everybody", "all", "band", "yall"};

const char *kQuestionOpeners[] = {
    "what",  "whats", "who",   "whos",  "how",   "hows",  "why",
    "when",  "where", "which", "is",    "are",   "does",  "do",
    "did",   "can",   "could", "will",  "would", "shall", "should",
    "has",   "have",  "am",    "any"};

const char *kArticles[] = {"the", "a",   "an",   "this", "that",
                           "those", "these", "my", "your", "our", "his",
                           "her",  "their"};

// An indefinite addressee means the message is aimed at the room in general,
// which is nobody. "can someone turn the keys down" is a request to whoever is
// listening and emphatically not an instruction to the keyboard player.
const char *kIndefinite[] = {"someone", "somebody", "anyone", "anybody",
                             "everyone else"};

// Words that are never typos.
//
// Near-miss matching exists so a slip of the fingers does not cost you an
// answer, and it must not be allowed to turn ordinary English into an address.
// "hey" is two edits from "keys" and "key" is one -- both would summon the
// keyboard player out of a sentence that was not about them. A real word is not
// a typo, and that is the whole rule.
const char *kCommonWords[] = {
    "a",    "an",    "and",   "are",   "ask",   "at",    "be",    "but",
    "by",   "can",   "do",    "does",  "for",   "from",  "get",   "go",
    "got",  "has",   "have",  "hes",   "hey",   "hi",    "how",   "i",
    "if",   "in",    "is",    "it",    "its",   "just",  "key",   "like",
    "me",   "more",  "my",    "no",    "not",   "now",   "of",    "off",
    "ok",   "on",    "one",   "or",    "our",   "out",   "part",  "play",
    "so",   "some",  "than",  "that",  "the",   "them",  "then",  "there",
    "they", "this",  "to",    "too",   "two",   "up",    "us",    "was",
    "band", "we",   "well",  "what",  "when",  "who",   "why",   "will",  "with",
    "yes",  "you",   "your",  "time",  "tell",  "else",  "about", "shall",
    "nice", "loud",  "great", "think", "love",  "turn",  "down",  "change",
    // The leave vocabulary. "leave" is two edits from "lead" and shares its
    // first letter, so without this, sending a bot home summons the soloist.
    "leave", "exit", "stop",
    "sound", "sounds", "make", "made",  "keep",  "let",   "see",  "know"};

const char *kCourtesy[] = {
    "thanks",    "thank you", "thanks!",  "ta",        "cheers",
    "nice one",  "nice",      "ok",       "okay",      "cool",
    "great",     "got it",    "gotcha",   "makes sense", "understood",
    "right",     "sure",      "yep",      "yes",       "no worries",
    "np",        "lovely",    "perfect",  "sweet"};

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

template <size_t N> bool inList(const char *const (&list)[N],
                                const std::string &s) {
  for (size_t i = 0; i < N; ++i)
    if (s == list[i])
      return true;
  return false;
}

template <size_t N>
bool inList(const InstrumentWord (&list)[N], const std::string &s) {
  for (size_t i = 0; i < N; ++i)
    if (s == list[i].word)
      return true;
  return false;
}

} // namespace

std::vector<std::string> tokenise(const std::string &text) {
  std::vector<std::string> out;
  std::string current;
  for (char c : text) {
    if (isWordChar(c)) {
      current += (char)std::tolower((unsigned char)c);
    } else {
      if (!current.empty())
        out.push_back(current);
      current.clear();
    }
  }
  if (!current.empty())
    out.push_back(current);
  return out;
}

bool isPartCommand(const std::string &text) {
  // The whole message and nothing else.
  //
  // "part" is NOT among these, deliberately. It is ordinary jam vocabulary --
  // "what's your part", "the bass part", "learn my part" -- and by far its
  // commonest use, so a destructive command sat one word away from the most
  // ordinary question in the room. A player found it the obvious way: asking
  // a bot what its part was sent the whole band home. IRC spells it `/part`,
  // and a slash form would be unambiguous; a bare word cannot be.
  //
  // "stop" is NOT among these either, and for the same reason turned up to
  // eleven: to a musician it is the least destructive thing you can say, and it
  // was wired to the most destructive thing a bot can do. Stopping and leaving
  // are separate states now, and "stop" belongs to the reversible one
  // (docs/BOT-CHAT.md section 15).
  //
  // Nor is bare "go", which on its own is as likely to mean start as leave.
  // Leaving takes a phrase that can only mean leaving.
  const auto tokens = tokenise(text);
  if (tokens.size() == 1)
    return tokens[0] == "leave" || tokens[0] == "exit";
  return tokens.size() == 2 && tokens[0] == "go" &&
         (tokens[1] == "away" || tokens[1] == "home");
}

namespace {

// Does the message OPEN with "name:"? The colon is the only address form that
// is unambiguous without knowing who is in the room -- a comma is ordinary
// punctuation ("ok, shake") and a bare leading word is just a word.
//
// Deliberately narrow: one leading token, then a colon. It is used only to
// decide that a message is for somebody ELSE, so a miss costs nothing and a
// false positive would silence a bot that was being spoken to.
bool addressesSomebodyByColon(const std::string &text) {
  size_t i = 0;
  while (i < text.size() && std::isspace((unsigned char)text[i]) != 0)
    ++i;

  const size_t start = i;
  while (i < text.size() && isWordChar(text[i]))
    ++i;

  return i > start && i < text.size() && text[i] == ':';
}

} // namespace

bool isCourtesy(const std::string &text) {
  const auto tokens = tokenise(text);
  if (tokens.empty() || tokens.size() > 3)
    return false;

  std::string joined;
  for (size_t i = 0; i < tokens.size(); ++i)
    joined += (i ? " " : "") + tokens[i];
  return inList(kCourtesy, joined);
}

void Room::resolveHandles() {
  for (auto &p : participants) {
    p.handleUsable = !p.handle.empty();
    if (!p.handleUsable)
      continue;
    for (const auto &other : participants) {
      if (&other == &p)
        continue;
      const auto theirs = lowered(other.username);
      const auto theirHandle = lowered(other.handle);
      // Either direction: somebody called `delvo` makes the handle ambiguous,
      // and so does somebody called `delvoton`, because a scan for a name
      // anywhere in a message cannot tell which was meant.
      if (theirs.find(p.handle) != std::string::npos ||
          (!theirHandle.empty() && p.handle.find(theirHandle) != std::string::npos))
        p.handleUsable = false;
    }
  }
}

const Participant *Room::find(const std::string &username) const {
  for (const auto &p : participants)
    if (p.username == username)
      return &p;
  return nullptr;
}

namespace {

// The positions a NAME would occupy: the front of the message, the very end, or
// anywhere inside a leading run of names joined by commas and "and".
std::vector<bool> addressPositions(const std::vector<std::string> &tokens,
                                   const Room &room) {
  std::vector<bool> out(tokens.size(), false);
  if (tokens.empty())
    return out;

  out[0] = true;
  out[tokens.size() - 1] = true;

  auto namesSomebody = [&room](const std::string &t) {
    for (const auto &p : room.participants) {
      if (p.handleUsable && t == p.handle)
        return true;
      if (t == lowered(p.username))
        return true;
    }
    return inList(kInstrumentWords, t) || inList(kCollectives, t);
  };

  // Walk forward while everything so far is a name or a connective.
  for (size_t i = 1; i < tokens.size(); ++i) {
    bool allNamesSoFar = true;
    for (size_t j = 0; j < i; ++j)
      if (!namesSomebody(tokens[j]) && tokens[j] != "and" && tokens[j] != "hey")
        allNamesSoFar = false;
    if (!allNamesSoFar)
      break;
    out[i] = true;
  }
  return out;
}

// Just the opening run: the first token, and anything after it that is still
// part of an unbroken sequence of names and connectives.
std::vector<bool> leadingPositions(const std::vector<std::string> &tokens,
                                   const Room &room) {
  auto out = addressPositions(tokens, room);
  if (!tokens.empty())
    out[tokens.size() - 1] = tokens.size() == 1;
  return out;
}

bool looksInterrogative(const std::vector<std::string> &tokens,
                        const std::string &raw) {
  if (raw.find('?') != std::string::npos)
    return true;
  return !tokens.empty() && inList(kQuestionOpeners, tokens[0]);
}

} // namespace

Address classify(const Room &room, const std::string &me, const Incoming &msg,
                 Attention &attention) {
  // A bot never triggers a bot. Stated as a property of what can cause speech
  // at all rather than as "ignore each other", so a loop has no step a bot's
  // own output could start.
  if (msg.sender == me)
    return Address::Ignore;
  const auto *sender = room.find(msg.sender);
  if (sender != nullptr && sender->isBot)
    return Address::Ignore;

  const auto *self = room.find(me);
  if (self == nullptr)
    return Address::Ignore;

  // Full usernames first, because they do not survive tokenising.
  //
  // `Delvo[bass-bot]` splits into "delvo", "bass" and "bot", so a message that
  // addresses a bot by its full name would otherwise match nothing -- and that
  // is exactly the case where it matters most, because the full username is
  // what you fall back to when the short handle has been withdrawn for
  // colliding with a player. Matched longest first and blanked out afterwards,
  // so a human called `delvo` is not also credited with a hit from inside
  // `Delvo[bass-bot]`.
  std::string remaining = lowered(msg.text);
  std::vector<const Participant *> byLength;
  for (const auto &p : room.participants)
    byLength.push_back(&p);
  std::sort(byLength.begin(), byLength.end(),
            [](const Participant *a, const Participant *b) {
              return a->username.size() > b->username.size();
            });

  std::vector<const Participant *> namedInFull;
  for (const auto *p : byLength) {
    const auto needle = lowered(p->username);
    if (needle.empty())
      continue;
    auto at = remaining.find(needle);
    bool found = false;
    while (at != std::string::npos) {
      found = true;
      remaining.replace(at, needle.size(), std::string(needle.size(), ' '));
      at = remaining.find(needle);
    }
    if (found)
      namedInFull.push_back(p);
  }

  // Two tokenisations, and the difference matters. `tokens` is what is left
  // after full usernames were blanked, and is what the name scan walks.
  // `rawTokens` is the message as written, and is what anything positional has
  // to use -- blanking a username shifts every index after it, so "you lot ..."
  // would lose its opening word and stop being a collective address.
  const auto tokens = tokenise(remaining);
  const auto rawTokens = tokenise(msg.text);
  if (rawTokens.empty())
    return Address::Ignore;

  // Leaving is the one thing that works with no address at all, because the
  // failure mode of getting it wrong is bots nobody can remove.
  if (isPartCommand(msg.text))
    return Address::PartAll;

  const auto positions = addressPositions(rawTokens, room);
  const auto leadingRun = leadingPositions(rawTokens, room);

  // Two-word collectives, which the token scan cannot see.
  bool collectivePhrase = false;
  if (rawTokens.size() >= 2 && rawTokens[0] == "you" &&
      (rawTokens[1] == "lot" || rawTokens[1] == "all" || rawTokens[1] == "two"))
    collectivePhrase = true;
  const bool interrogative = looksInterrogative(rawTokens, msg.text);

  bool indefinite = false;
  for (const auto &t : tokens)
    if (inList(kIndefinite, t))
      indefinite = true;

  // ---- who is named -------------------------------------------------------

  bool namesMe = false, namesAnotherBot = false, namesHuman = false;
  bool collective = collectivePhrase;
  bool onlyMyName = !rawTokens.empty();

  auto noteHit = [&](const Participant &p) {
    if (p.username == me) {
      namesMe = true;
    } else if (p.isBot) {
      namesAnotherBot = true;
    } else if (p.username != msg.sender) {
      // Somebody else. The speaker naming THEMSELVES is not an address -- and
      // it is common, because "you" is both a plausible username and the
      // commonest pronoun in the language. "what are you playing", said by a
      // player called `you`, is a question, not a message for themselves.
      namesHuman = true;
    }
  };

  for (const auto *p : namedInFull) {
    noteHit(*p);
    onlyMyName = false;
  }

  for (size_t i = 0; i < rawTokens.size(); ++i) {
    const auto &t = rawTokens[i];
    if (!contains(tokens, t))
      continue; // part of a full username already accounted for
    bool hit = false;

    // A handle, anywhere in the sentence. This is what rare names buy: "what
    // are the changes delvo" is a sentence rather than a command.
    for (const auto &p : room.participants) {
      if (p.handleUsable && t == p.handle) {
        noteHit(p);
        hit = true;
      }
    }

    // A near miss on a handle, if it is unambiguous. A typo must not cost you
    // the answer; reaching two bots must not cost somebody else's silence.
    if (!hit && !inList(kCommonWords, t))
      for (const auto &p : room.participants) {
        if (!p.handleUsable || p.handle.size() < 4)
          continue;
        if (!couldBeTypoOf(t, p.handle))
          continue;
        int reached = 0;
        for (const auto &q : room.participants)
          if (q.handleUsable && couldBeTypoOf(t, q.handle))
            ++reached;
        if (reached == 1) {
          noteHit(p);
          hit = true;
        }
      }

    // Only where it OPENS the message, unlike a name.
    //
    // "band" and "all" are ordinary words in a room full of musicians -- "nice
    // band", "the band is tight", "that's all" -- and a bot answering those is
    // the poltergeist this whole file exists to prevent. Every collective
    // address anybody actually writes puts the word first.
    if (inList(kCollectives, t) && leadingRun[i]) {
      collective = true;
      hit = true;
    }

    if (!hit)
      onlyMyName = false;
  }

  // ---- instrument words, which are ordinary nouns and need more care ------

  if (!namesHuman && !indefinite) {
    for (size_t i = 0; i < rawTokens.size(); ++i) {
      if (!contains(tokens, rawTokens[i]))
        continue;
      std::string instrument;
      for (const auto &w : kInstrumentWords)
        if (rawTokens[i] == w.word)
          instrument = w.instrument;

      // A near miss, but only in a position a name could occupy -- a mangled
      // ordinary noun mid-sentence is a typo, not an address.
      if (instrument.empty() && positions[i] && rawTokens[i].size() >= 2 &&
          !inList(kCommonWords, rawTokens[i])) {
        int reached = 0;
        std::string candidate;
        for (const auto &w : kInstrumentWords) {
          const std::string word = w.word;
          if (couldBeTypoOf(rawTokens[i], word)) {
            if (candidate.empty() || candidate == w.instrument) {
              candidate = w.instrument;
              ++reached;
            } else {
              reached = 99; // ambiguous between two different instruments
            }
          }
        }
        if (reached >= 1 && reached < 99)
          instrument = candidate;
      }

      if (instrument.empty())
        continue;

      // Where it counts. In the address position always; elsewhere only when
      // it is not being talked ABOUT -- "whats the bass doing" is a question
      // for the bass player, "the bass is a bit loud" is a remark to the room.
      const bool precededByArticle =
          i > 0 && inList(kArticles, rawTokens[i - 1]);
      const bool counts = positions[i] ||
                          (!precededByArticle) ||
                          (precededByArticle && interrogative);
      if (!counts)
        continue;

      for (const auto &p : room.participants)
        if (p.isBot && p.instrument == instrument)
          noteHit(p);
      onlyMyName = false;
    }
  }

  // ---- the decision -------------------------------------------------------

  // A message naming somebody else is not for me, and that test comes before
  // everything: no understanding of the sentence is required.
  if (namesHuman)
    return Address::Ignore;

  // An ADDRESS plus the command, and nothing else -- the same rule
  // `isPartCommand` applies to an unaddressed message, for the same reason.
  //
  // This used to accept any message merely ENDING with the word, which sent a
  // bot home for "Ravo: what's your part". That is not an exotic phrasing: it
  // is a line in the DESCRIBE_PART corpus and the most ordinary question in
  // the room. The addressing corpus could not catch it, because it records who
  // answers and PartMe and Named are both "that bot".
  const bool addressedPart =
      rawTokens.size() == 2 && isPartCommand(rawTokens.back());

  if (namesMe) {
    attention.owner = msg.sender;
    attention.openedAt = msg.at;
    attention.turnsLeft = kWindowTurns;
    if (addressedPart)
      return Address::PartMe;
    if (onlyMyName)
      return Address::Opener;
    return Address::Named;
  }

  if (collective) {
    attention.owner = msg.sender;
    attention.openedAt = msg.at;
    attention.turnsLeft = kWindowTurns;
    return addressedPart ? Address::PartAll : Address::Collective;
  }

  if (namesAnotherBot) {
    // Somebody else has the floor. Close my window so a follow-up meant for
    // them is not answered by me as well.
    if (attention.owner == msg.sender)
      attention = Attention{};
    return Address::Ignore;
  }

  if (msg.isPrivate)
    return addressedPart ? Address::PartMe : Address::Private;

  // "name: something" is aimed at that name, and by here it is established
  // that the name is not mine, not a collective, and nobody I know -- so this
  // is somebody addressing a player I cannot see. Answering it because a
  // window happened to be open is the rudest thing in the design: it is a bot
  // replying to a message that visibly says who it is for.
  //
  // The name being unknown is not exotic. A player who joined a moment ago is
  // not in my list yet, a bot that has left is gone from it, and either way an
  // explicit address is the clearest signal a conversation has moved on.
  if (!rawTokens.empty() && addressesSomebodyByColon(msg.text)) {
    if (attention.owner == msg.sender)
      attention = Attention{};
    return Address::Ignore;
  }

  // Unaddressed. The only way through is a conversation already open with this
  // person -- and courtesy ends a turn rather than starting one.
  if (attention.openFor(msg.sender, msg.at, kWindowSeconds)) {
    if (isCourtesy(msg.text))
      return Address::Ignore;
    --attention.turnsLeft;
    attention.openedAt = msg.at;
    return Address::Continuation;
  }

  return Address::Ignore;
}

std::string withoutAddress(const Room &room, const std::string &self,
                           const std::string &text) {
  const auto *me = room.find(self);
  if (me == nullptr)
    return text;

  // Every name this bot answers to, longest first so "Ravo[keys-bot]" is tried
  // before "Ravo" and does not leave "[keys-bot]" behind.
  std::vector<std::string> names{me->username, me->instrument, me->channel};
  if (me->handleUsable)
    names.push_back(me->handle);
  // A collective is an address too, and leaving it in the body is not
  // harmless: "band" is not a word the lexicon knows, so it counted as an
  // unrecognised one and silenced the rules that require a sentence to be
  // fully understood. "band what are you" fell to the catch-all where "ravo:
  // what are you" answered.
  for (const auto *c : kCollectives)
    names.push_back(c);
  std::sort(names.begin(), names.end(),
            [](const std::string &a, const std::string &b) {
              return a.size() > b.size();
            });

  std::string body = text;
  // Leading whitespace first, so "  ravo: shake" is handled.
  size_t begin = body.find_first_not_of(" \t");
  if (begin == std::string::npos)
    return text;
  body = body.substr(begin);

  const auto low = lowered(body);
  for (const auto &name : names) {
    if (name.empty() || name.size() >= low.size())
      continue;
    if (low.compare(0, name.size(), lowered(name)) != 0)
      continue;
    // It has to BE the name, not merely start with it: `kitten` is not `kit`.
    size_t after = name.size();
    if (isWordChar(body[after]))
      continue;
    // ...and it has to be used as an address, which is what the punctuation
    // or the space after it says.
    while (after < body.size() &&
           (body[after] == ',' || body[after] == ':' || body[after] == ' ' ||
            body[after] == '\t'))
      ++after;
    if (after >= body.size())
      return text; // the name alone is an opener, not a command
    return body.substr(after);
  }
  return body;
}

} // namespace BotAddress
