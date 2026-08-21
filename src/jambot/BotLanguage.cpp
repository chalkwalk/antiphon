#include "BotLanguage.h"

#include "BotDictionary.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace BotLanguage {

namespace {

// ---------------------------------------------------------------------------
// 1. Normalise. Half of what makes phrasing "indirect" is padding, and taking
//    it away turns a hard sentence into an easy one.
// ---------------------------------------------------------------------------

struct Expansion {
  const char *from;
  const char *to;
};

// Contractions, written the way people type them: usually without the
// apostrophe, because chat has no time for it.
const Expansion kExpansions[] = {
    {"whats", "what is"},   {"what's", "what is"},  {"whatre", "what are"},
    {"what're", "what are"},{"hows", "how is"},     {"how's", "how is"},
    {"wheres", "where is"}, {"whos", "who is"},     {"who's", "who is"},
    {"youre", "you are"},   {"you're", "you are"},  {"dont", "do not"},
    {"don't", "do not"},    {"cant", "can not"},    {"can't", "can not"},
    {"wont", "will not"},   {"won't", "will not"},  {"isnt", "is not"},
    {"isn't", "is not"},    {"arent", "are not"},   {"aren't", "are not"},
    {"im", "i am"},         {"i'm", "i am"},        {"ive", "i have"},
    {"lets", "let us"},     {"let's", "let us"},    {"thats", "that is"},
    {"that's", "that is"},  {"ur", "your"},         {"u", "you"},
    {"pls", "please"},      {"plz", "please"},      {"r", "are"},
    {"n", "and"},           {"abt", "about"},       {"bout", "about"},
    {"gonna", "going to"},  {"wanna", "want to"},   {"gimme", "give me"},
    {"tellme", "tell me"},  {"couldya", "could you"},
    {"whatve", "what have"}, {"what've", "what have"},
    {"ill", "i will"},      {"i'll", "i will"},     {"id", "i would"},
    {"youll", "you will"},  {"you'll", "you will"}, {"weve", "we have"},
    {"shouldnt", "should not"}, {"couldnt", "could not"},
    {"wouldnt", "would not"}, {"aint", "is not"},
};

// Padding, politeness, and grammar. None of it changes what was asked.
//
// Grammatical words are dropped HERE rather than ignored later, because an
// unrecognised word is now evidence that the message is not about us at all
// (see `unknownWords`), and "are" must not be that evidence.
const char *kFiller[] = {
    // politeness and filler
    "please", "pls",     "sorry",  "just",   "quickly", "mate",
    "man",    "dude",    "hey",    "hi",     "hello",   "ok",     "okay",
    "so",     "well",    "um",     "uh",     "erm",     "like",   "actually",
    "really", "maybe",   "perhaps","kinda",  "sort",    "bit",    "very",
    "thanks", "thank",   "cheers", "now",    "then",    "there",
    "here",   "bro",     "buddy",  "friend", "guys",    "everyone",
    "yo",     "oi",      "hmm",    "hold",   "wait",    "hang",   "right",
    // determiners, prepositions, conjunctions
    "a",      "an",      "the",    "of",     "for",     "to",     "at",
    "in",     "on",      "and",    "or",     "some",    "any",    "all",
    "with",   "from",    "than",   "too",    "also",    "only",   "even",
    "ever",   "still",   "yet",    "own",    "same",    "both",   "each",
    "few",    "other",   "under",  "once",   "if",      "but",    "as",
    "by",     "into",    "onto",   "about_", // about_ never matches; see kLexicon
    // pronouns and possessives
    "me",     "my",      "mine",   "us",     "our",     "ours",   "we",
    "it",     "its",     "this",   "that",   "these",   "those",  "you",
    "your",   "yours",   "i",      "they",   "them",    "their",  "he",
    "she",    "him",     "her",    "one",    "ones",
    // auxiliaries and copulas
    "is",     "are",     "was",    "were",   "be",      "been",   "being",
    "am",     "do",      "does",   "did",    "can",     "could",  "will",
    "would",  "shall",   "should", "may",    "might",   "must",   "have",
    "has",    "had",
    // interrogatives that carry no topic of their own
    "what",   "how",     "why",    "when",   "where",   "which",
    // pro-forms standing in for a topic
    "something", "anything", "everything", "thing", "things", "stuff",
    "anyone", "anybody", "someone", "somebody", "everybody", "nobody",
    "exactly", "moment", "kind", "type", "future"};

// `more` is filler in "tell me more about it" and is half the message in "one
// more". Dropped only when something else survives.
const char *kFillerUnlessAlone[] = {"more", "up"};

bool inList(const char *const *list, size_t n, const std::string &s) {
  for (size_t i = 0; i < n; ++i)
    if (s == list[i])
      return true;
  return false;
}

template <size_t N> bool inList(const char *const (&l)[N], const std::string &s) {
  return inList(l, N, s);
}

std::vector<std::string> split(const std::string &text) {
  std::vector<std::string> out;
  std::string current;
  for (char c : text) {
    if (std::isalnum((unsigned char)c) != 0 || c == '\'') {
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

// A content token, carrying the word that stood before it in the unstripped
// sentence. That predecessor is the whole of the word-class machinery: a
// determiner or a possessive in front of a word makes it a noun, and a subject
// pronoun or a modal in front of it makes it a verb.
struct Tok {
  std::string word;
  std::string prev;
  bool first = false; // first word of the sentence
};

struct Prepared {
  std::vector<std::string> raw; // expanded, lowercased, nothing removed
  std::vector<Tok> toks;        // content only
  bool askingCharacter = false; // a trailing "like": what is it LIKE
  bool exclamation = false;     // "what a tune" -- not a question at all
};

const char *kDeterminer[] = {"the", "a",     "an",   "your", "my",  "our",
                             "their", "this", "that", "these", "those",
                             "its",   "his",  "her",  "some", "any"};
// Words that can only modify a noun, which is the determiner test's blind
// spot: "the standard changes" puts an adjective where "the" would be, so the
// determiner is no longer adjacent to the word being classed and "changes" was
// read as the verb. Anything that can only be an adjective does the
// determiner's job for whatever follows it.
const char *kNounModifier[] = {"default", "standard", "usual",
                               "normal",  "ordinary", "typical"};
// Only the second person makes a following verb a REQUEST. "can you change it"
// is an instruction; "how does it go" is a description asked for, and treating
// its subject the same way answered it by leaving the room.
const char *kSubject[] = {"you"};
// Who is being spoken to, not what is being asked. Addressing has already been
// decided by the time a message reaches this file, so a name here is noise.
const char *kVocative[] = {"kit",  "drums", "drum",   "bass",  "keys",
                           "lead", "piano", "guitar", "tutor", "band",
                           "everyone", "hey"};
const char *kModal[] = {"can",  "could", "will",  "would", "shall",
                        "should", "may", "might", "must",  "do",
                        "does",   "did", "please", "to"};

Prepared prepare(const std::string &text) {
  Prepared p;
  auto tokens = split(text);

  // A pronoun object inside a phrasal verb -- "kick it off", "wrap it up",
  // "fire it up" -- hides the two halves from each other. Drop the "it" so the
  // idiom rules below see an adjacent pair, which is what they are.
  for (size_t i = 0; i + 2 < tokens.size(); ++i) {
    if (tokens[i + 1] != "it")
      continue;
    const auto &v = tokens[i];
    const auto &particle = tokens[i + 2];
    const bool phrasal =
        (v == "kick" && particle == "off") || (v == "wrap" && particle == "up") ||
        (v == "pick" && particle == "up") ||
        (v == "fire" && particle == "up") || (v == "cut" && particle == "out") ||
        (v == "take" && particle == "away") || (v == "lay" && particle == "out");
    if (phrasal)
      tokens.erase(tokens.begin() + (long)i + 1);
  }

  // Idioms first: two tokens meaning one thing, which the stemmer will never
  // reach on its own.
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    const auto &a = tokens[i];
    const auto &b = tokens[i + 1];
    auto fuse = [&](const char *with) {
      tokens[i] = with;
      tokens.erase(tokens.begin() + (long)i + 1);
    };
    if (a == "up" && b == "to")
      fuse("doing");
    // Phrasal verbs of starting and stopping. Each is two tokens meaning one
    // thing, and the halves point opposite ways on their own -- "kick" is a
    // drum, "wrap" is nothing, and "out" and "off" are both leaving words.
    else if (a == "kick" && b == "off")
      fuse("start");
    else if (a == "fire" && b == "up")
      fuse("start");
    else if (a == "hit" && b == "it")
      fuse("start");
    else if (a == "carry" && b == "on")
      fuse("start");
    else if (a == "keep" && b == "going")
      fuse("start");
    else if (a == "pick" && b == "up")
      fuse("start");
    else if (a == "back" && b == "to")
      // "back to it", "back to the tune". Resuming, and the only other reading
      // -- "back to" as a direction -- is not something anybody says to a bot.
      fuse("start");
    else if ((a == "come" || a == "back") && b == "in" && tokens.size() == 2)
      // The whole message, or it is not a cue: "back in five" is somebody
      // saying when they will return.
      fuse("start");
    else if (a == "get" && b == "going")
      fuse("start");
    else if (a == "lay" && b == "out")
      fuse("stop");
    else if (a == "hold" && b == "it")
      fuse("stop");
    else if (a == "take" && b == "five")
      fuse("stop");
    // "i am done with you" is a dismissal; "we are done" is the end of a tune.
    // One preposition carries the whole difference.
    else if (a == "done" && b == "with")
      fuse("dismiss");
    else if (a == "playing" && i + 2 == tokens.size() &&
             (b == "in" || b == "over" || b == "on"))
      // "what are we playing in" asks the key; "what are we playing over" asks
      // the chart. One preposition carries the whole difference, and it is
      // about to be stripped as filler, so it is read here first.
      fuse(b == "in" ? "key" : "chords");
    else if (a == "sound" && b == "like")
      fuse("sound");
    else if (a == "sounds" && b == "like")
      fuse("sounds");
    else if (a == "going" && b == "on")
      fuse("situation");
    else if (a == "see" && b == "you")
      fuse("bye");
    else if ((a == "one" || a == "once") && b == "more")
      fuse("another");
    else if ((a == "keep" || a == "pipe" || a == "settle" || a == "calm") &&
             b == "down")
      fuse("hush");
    else if (a == "shut" && b == "up")
      fuse("hush");
    else if (a == "no" && b == "more")
      fuse("less");
    else if (a == "speed" && b == "up")
      fuse("faster");
    else if (a == "slow" && b == "down")
      // "calm down" is handled below as a request for quiet; only the tempo
      // sense reaches here.
      fuse("slower");
    else if (a == "go" && b == "ahead")
      fuse("goahead");
    else if (a == "going" && b == "to")
      // "i am going to get a coffee" is the future tense, not somebody going.
      fuse("future");
    else if ((a == "am" || a == "im" || a == "i'm") && b == "lost")
      // "get lost" evicts us; "im lost" asks for help. Same word, opposite ask.
      fuse("confused");
    else if (a == "back" && b == "on")
      fuse("resume");
    else if (a == "running" && b == "at")
      fuse("tempo");
    else if ((a == "not" || a == "no") && (b == "that" || b == "this"))
      fuse("another");
    else if ((a == "like" || a == "want" || a == "need") && b == "that")
      // "i dont like that one" -- the dissatisfaction is the whole request.
      fuse("liking");
  }

  // "keep it down", "quiet down": the particle is what makes it an
  // instruction, and it can sit one word away from its verb.
  for (size_t i = 0; i + 1 < tokens.size(); ++i)
    if (tokens[i] == "keep" || tokens[i] == "pipe" || tokens[i] == "settle" ||
        tokens[i] == "calm")
      for (size_t j = i + 1; j < tokens.size() && j <= i + 2; ++j)
        if (tokens[j] == "down") {
          tokens[i] = "hush";
          tokens.erase(tokens.begin() + (long)j);
          break;
        }

  // "what are we in" is the key, the same way "what are we playing in" is.
  if (tokens.size() >= 2 && tokens.back() == "in")
    tokens.back() = "key";

  // "whats going on" asks about the part; "whats going on here" asks what this
  // whole thing is. The adverb is the entire difference.
  for (size_t i = 0; i + 1 < tokens.size(); ++i)
    if (tokens[i] == "situation" && tokens[i + 1] == "here") {
      tokens[i] = "purpose";
      tokens.erase(tokens.begin() + (long)i + 1);
      break;
    }

  // A trailing "like" is asking what a thing is LIKE -- its character. It is
  // not a topic of its own, and it is about to be stripped as filler, so it is
  // read off here as a flag.
  if (tokens.size() >= 2 && tokens.back() == "like")
    p.askingCharacter = true;
  // "what a tune" is an exclamation wearing a question word.
  if (tokens.size() >= 2 && tokens[0] == "what" &&
      (tokens[1] == "a" || tokens[1] == "an"))
    p.exclamation = true;

  // Expand contractions, which can turn one token into two.
  for (const auto &t : tokens) {
    bool did = false;
    for (const auto &e : kExpansions)
      if (t == e.from) {
        for (const auto &piece : split(e.to))
          p.raw.push_back(piece);
        did = true;
        break;
      }
    if (!did)
      p.raw.push_back(t);
  }

  // A leading instrument word is a VOCATIVE -- "kit, what are you playing" --
  // and by the time a message reaches here, addressing has already been
  // decided. Left in, it reads as a topic and ties the sentence against itself.
  // Strip the WHOLE address, not one word of it. "hey kit, whats your part"
  // left `kit` behind as a topic and tied the sentence against itself, which
  // the clause level exposed rather than caused.
  size_t start = 0;
  while (start + 1 < p.raw.size() && inList(kVocative, p.raw[start]))
    ++start;

  for (size_t i = start; i < p.raw.size(); ++i) {
    const auto &w = p.raw[i];
    if (inList(kFiller, w))
      continue;
    Tok t;
    t.word = w;
    t.prev = i > start ? p.raw[i - 1] : std::string();
    t.first = i == start;
    p.toks.push_back(t);
  }

  if (p.toks.size() > 1) {
    std::vector<Tok> out;
    for (const auto &t : p.toks)
      if (!inList(kFillerUnlessAlone, t.word))
        out.push_back(t);
    if (!out.empty())
      p.toks = out;
  }
  return p;
}

} // namespace

std::vector<std::string> normalise(const std::string &text) {
  const auto p = prepare(text);
  std::vector<std::string> out;
  for (const auto &t : p.toks)
    out.push_back(t.word);
  // If the filter ate everything, the filler WAS the message -- "thanks",
  // "hello" -- and the caller needs to see it rather than an empty list.
  return out.empty() ? p.raw : out;
}

// ---------------------------------------------------------------------------
// 2. Stem, so `playing`, `plays`, `played` and `play` are one word.
//
// A cut-down Porter: the suffix strips that matter for this vocabulary, without
// the measure-counting machinery of the full algorithm. The corpus is the test
// of whether that is enough, and it is -- the words here are short and ordinary.
// ---------------------------------------------------------------------------

std::string stem(const std::string &word) {
  std::string w = word;
  auto endsWith = [&w](const char *suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return w.size() > n + 2 && w.compare(w.size() - n, n, suffix) == 0;
  };
  auto chop = [&w](size_t n) { w.erase(w.size() - n); };

  if (endsWith("ing")) {
    chop(3);
    // "playing" -> "play", but "running" -> "runn" -> "run".
    if (w.size() > 2 && w[w.size() - 1] == w[w.size() - 2])
      chop(1);
  } else if (endsWith("edly")) {
    chop(4);
  } else if (endsWith("ies")) {
    chop(3);
    w += "y";
  } else if (endsWith("ed")) {
    chop(2);
  } else if (endsWith("es")) {
    // Only after a sibilant, where the `e` is doing work: `boxes` -> `box`,
    // `matches` -> `match`. Everywhere else it is an ordinary plural and the
    // `e` belongs to the word -- taking it turns `notes` into `not`, which is
    // both wrong and, since `not` is a negation, actively harmful.
    const char before = w[w.size() - 3];
    chop(before == 's' || before == 'x' || before == 'z' || before == 'h' ? 2
                                                                         : 1);
  } else if (endsWith("ly")) {
    chop(2);
  } else if (w.size() > 3 && w.back() == 's' &&
             w[w.size() - 2] != 's' && w[w.size() - 2] != 'u') {
    chop(1);
  }
  // Nouns built from verbs and adjectives, so the lexicon can carry the root
  // alone: `progression` -> `progress`, `tonality` -> `tonal`. The length guard
  // is the whole rule: without it `option` becomes `opt`, and the corpus asks
  // "what are my options" often enough for that to matter.
  auto chopTo = [&](const char *suffix, size_t n) {
    if (endsWith(suffix) && w.size() - n >= 5) {
      chop(n);
      return true;
    }
    return false;
  };
  chopTo("ion", 3) || chopTo("ity", 3) || chopTo("ment", 4) ||
      chopTo("ness", 4);

  // A trailing silent `e` after a consonant: `timbre` -> `timbr`, `figure` ->
  // `figur`, `change` -> `chang`. The length guard is load-bearing: at four
  // characters it would take `note` to `not`, which is a negation.
  if (w.size() > 4 && w.back() == 'e' &&
      std::string("aeiou").find(w[w.size() - 2]) == std::string::npos)
    w.erase(w.size() - 1);
  return w;
}

namespace {

// ---------------------------------------------------------------------------
// 3. The lexicon: surface words onto concepts.
//
// The single highest-value artefact here, and it is plain data. Robustness
// lives in this table rather than in any cleverness downstream -- every entry
// is one more way of saying a thing that now works.
// ---------------------------------------------------------------------------

struct Word {
  const char *word;
  Concept concept;
};

const Word kLexicon[] = {
    {"part", Concept::Part},      {"pattern", Concept::Part},
    {"groove", Concept::Part},    {"figur", Concept::Part},
    {"rhythm", Concept::Part},    {"line", Concept::Part},
    {"beat", Concept::Part},      {"play", Concept::Part},
    {"doing", Concept::Part},     {"perform", Concept::Part},
    {"accent", Concept::Part},    {"fill", Concept::Part},
    {"note", Concept::Part},      {"shape", Concept::Part},
    {"phras", Concept::Part},     {"tick", Concept::Part},
    {"hit", Concept::Part},       {"count", Concept::Part},
    {"puls", Concept::Part},      {"onset", Concept::Part},
    {"lay", Concept::Part},       {"sit", Concept::Part},
    {"got", Concept::Part},
    {"syncopat", Concept::Part},  {"subdivi", Concept::Part},

    {"sound", Concept::Tone},     {"tone", Concept::Tone},
    {"timbr", Concept::Tone},     {"patch", Concept::Tone},
    {"voic", Concept::Tone},      {"charact", Concept::Tone},
    {"preset", Concept::Tone},    {"tune", Concept::Tone},
    {"tuned", Concept::Tone},     {"tun", Concept::Tone},
    {"setup", Concept::Tone},     {"character", Concept::Tone},
    {"bright", Concept::Tone},    {"dark", Concept::Tone},
    {"warm", Concept::Tone},      {"thick", Concept::Tone},
    {"thin", Concept::Tone},      {"kit", Concept::Tone},
    {"instrument", Concept::Tone},

    {"key", Concept::Key},        {"scale", Concept::Key},
    {"tonic", Concept::Key},      {"mode", Concept::Key},
    {"major", Concept::Key},      {"minor", Concept::Key},
    {"dorian", Concept::Key},     {"phrygian", Concept::Key},
    {"lydian", Concept::Key},     {"mixolydian", Concept::Key},
    {"aeolian", Concept::Key},    {"locrian", Concept::Key},
    {"ionian", Concept::Key},
    {"root", Concept::Key},       {"tonal", Concept::Key},

    {"chord", Concept::Chart},    {"progress", Concept::Chart},
    {"chart", Concept::Chart},    {"sequenc", Concept::Chart},
    {"harmoni", Concept::Chart},  {"harmony", Concept::Chart},
    {"agre", Concept::Chart},     {"agree", Concept::Chart},
    {"loop", Concept::Chart},     {"bar", Concept::Chart},
    {"turnaround", Concept::Chart},

    {"tempo", Concept::Tempo},    {"bpm", Concept::Tempo},
    {"speed", Concept::Tempo},    {"interv", Concept::Tempo},
    {"fast", Concept::Tempo},     {"slow", Concept::Tempo},
    {"bpi", Concept::Tempo},      {"pace", Concept::Tempo},
    {"interval", Concept::Tempo}, {"length", Concept::Tempo},
    {"click", Concept::Tempo},    {"quick", Concept::Tempo},
    {"long", Concept::Tempo},     {"metronom", Concept::Tempo},
    {"vote", Concept::Tempo},     {"faster", Concept::Tempo},
    {"slower", Concept::Tempo},

    {"default", Concept::Standard}, {"standard", Concept::Standard},
    {"usual", Concept::Standard},   {"normal", Concept::Standard},
    {"ordinary", Concept::Standard},{"typical", Concept::Standard},
    {"reset", Concept::Standard},   {"revert", Concept::Standard},
    {"restor", Concept::Standard},

    {"shake", Concept::Change},   {"reroll", Concept::Change},
    {"roll", Concept::Change},    {"new", Concept::Change},
    {"differ", Concept::Change},  {"different", Concept::Change},
    {"anoth", Concept::Change},   {"another", Concept::Change},
    {"switch", Concept::Change},  {"vari", Concept::Change},
    {"vary", Concept::Change},    {"alter", Concept::Change},
    {"rework", Concept::Change},  {"els", Concept::Change},
    {"else", Concept::Change},    {"redo", Concept::Change},
    {"random", Concept::Change},  {"again", Concept::Change},
    {"fresh", Concept::Change},   {"swap", Concept::Change},
    {"liking", Concept::Change},   {"try", Concept::Change},

    {"quiet", Concept::Quiet},    {"hush", Concept::Quiet},
    {"shush", Concept::Quiet},    {"silent", Concept::Quiet},
    {"silenc", Concept::Quiet},   {"mute", Concept::Quiet},
    {"zip", Concept::Quiet},      {"button", Concept::Quiet},

    {"unmute", Concept::Loud},    {"resum", Concept::Loud},
    {"goahead", Concept::Loud},   {"welcom", Concept::Loud},

    {"stop", Concept::Cease},     {"enough", Concept::Cease},
    {"less", Concept::Cease},     {"ceas", Concept::Cease},
    {"halt", Concept::Cease},     {"wrap", Concept::Cease},
    {"finish", Concept::Cease},   {"end", Concept::Cease},
    {"cut", Concept::Cease},      {"done", Concept::Cease},

    {"start", Concept::Begin},    {"begin", Concept::Begin},
    {"music", Concept::Begin},    {"top", Concept::Begin},
    {"readi", Concept::Begin},    {"ready", Concept::Begin},

    {"chat", Concept::Chat},      {"talk", Concept::Chat},
    {"speak", Concept::Chat},     {"say", Concept::Chat},
    {"messag", Concept::Chat},    {"commentari", Concept::Chat},
    {"commentary", Concept::Chat},
    {"comment", Concept::Chat},   {"chatti", Concept::Chat},
    {"natter", Concept::Chat},    {"waffl", Concept::Chat},

    {"who", Concept::Identity},   {"help", Concept::Identity},
    {"purpos", Concept::Identity},{"bot", Concept::Identity},
    {"robot", Concept::Identity}, {"human", Concept::Identity},
    {"real", Concept::Identity},  {"person", Concept::Identity},
    {"command", Concept::Identity}, {"option", Concept::Identity},
    {"yourself", Concept::Identity}, {"work", Concept::Identity},
    {"confus", Concept::Identity}, {"understand", Concept::Identity},

    {"situation", Concept::Part},

    {"leav", Concept::Leave},     {"evict", Concept::Leave},
    {"dismiss", Concept::Leave},  {"remov", Concept::Leave},
    {"bye", Concept::Leave},      {"goodby", Concept::Leave},
    {"exit", Concept::Leave},     {"disconnect", Concept::Leave},
    {"begon", Concept::Leave},    {"scram", Concept::Leave},
    {"away", Concept::Leave},     {"go", Concept::Leave},
    {"out", Concept::Leave},      {"off", Concept::Leave},
    {"home", Concept::Leave},     {"quit", Concept::Leave},
    {"lost", Concept::Leave},

    {"kick", Concept::Drum},      {"snare", Concept::Drum},
    {"hat", Concept::Drum},       {"hihat", Concept::Drum},
    {"cymbal", Concept::Drum},    {"tom", Concept::Drum},
    {"drum", Concept::Drum},

    {"bass", Concept::Instrument},   {"guitar", Concept::Instrument},
    {"piano", Concept::Instrument},
    {"synth", Concept::Instrument},  {"pad", Concept::Instrument},
    {"drummer", Concept::Instrument},{"bassist", Concept::Instrument},
    {"rhode", Concept::Instrument},

    {"tell", Concept::Speak},     {"walk", Concept::Speak},
    {"through", Concept::Speak},  {"describ", Concept::Speak},
    {"explain", Concept::Speak},  {"give", Concept::Speak},
    {"show", Concept::Speak},     {"about", Concept::Speak},
    {"run", Concept::Speak},      {"summari", Concept::Speak},

    {"hear", Concept::Hear},      {"listen", Concept::Hear},
    {"loud", Concept::Hear},      {"level", Concept::Hear},
    {"volum", Concept::Hear},     {"good", Concept::Hear},
    {"nice", Concept::Hear},      {"bad", Concept::Hear},
    {"great", Concept::Hear},     {"awesom", Concept::Hear},
    {"lovely", Concept::Hear},    {"terribl", Concept::Hear},
    {"awful", Concept::Hear},     {"rough", Concept::Hear},
    {"muddy", Concept::Hear},     {"harsh", Concept::Hear},
    {"balanc", Concept::Hear},    {"mix", Concept::Hear},
    {"sounds", Concept::Hear},
};

// Words whose CLASS decides their concept. The determiner test is the whole
// mechanism: "the changes" is a noun and names the chart, "change it" is a verb
// and asks for a reroll. `mix` is the same word twice over -- "the mix" is what
// you hear, "mix it up" is an instruction.
struct ClassedWord {
  const char *word;
  Concept asNoun;
  Concept asVerb;
};

const ClassedWord kClassed[] = {
    {"chang", Concept::Chart, Concept::Change},
    {"mix", Concept::Hear, Concept::Change},
    {"play", Concept::Part, Concept::Part},
    {"go", Concept::Part, Concept::Leave},
};

// ---------------------------------------------------------------------------
// 4. Typo repair, against the lexicon only.
//
// Two rules do the work, and both come from how people actually mistype rather
// than from edit distance being a tidy idea:
//
//   - A REAL WORD IS NOT A TYPO. `chat` is not a mistyped `chart`, and `oops`
//     is not a mistyped `loop`. This used to be a hand-maintained list of
//     seventy exceptions, which is a list nobody can keep correct -- `chat` and
//     `right` and `room` were all missing from it and all three produced a
//     confident wrong answer. It is now a generated dictionary
//     (`BotDictionary.h`).
//   - NEAREST WINS, and only a tie is ambiguous.
//
// A weighted metric was tried here and removed. Typing errors are not uniform
// -- adjacent keys are the commonest substitution, and the first letter is
// rarely the wrong one -- so the cost was weighted to match: adjacent-key slips
// at half an edit, a wrong first letter at double, transpositions cheap. It
// made no difference to a single line of the corpus, including the twenty-six
// mechanically generated typos added to measure exactly this, and mutating each
// weight away in turn changed nothing either.
//
// The reason is the gate above. Once a real word can no longer be "repaired",
// almost nothing reaches the metric, and what does reach it is a slip of one
// character with no near rival. Refining how the distance is counted answers a
// question the gate has already settled. Plain Damerau-Levenshtein it is.
// ---------------------------------------------------------------------------

int editDistance(const std::string &a, const std::string &b) {
  const int n = (int)a.size(), m = (int)b.size();
  if (std::abs(n - m) > 2)
    return 99;
  std::vector<std::vector<int>> d((size_t)n + 1, std::vector<int>((size_t)m + 1));
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
      // A transposition is one slip of two fingers, not two mistakes.
      if (i > 1 && j > 1 && a[(size_t)i - 1] == b[(size_t)j - 2] &&
          a[(size_t)i - 2] == b[(size_t)j - 1])
        best = std::min(best, d[(size_t)i - 2][(size_t)j - 2] + 1);
      d[(size_t)i][(size_t)j] = best;
    }
  return d[(size_t)n][(size_t)m];
}

const char *kQuestionWords[] = {"what", "who",  "how",  "why",  "when",
                                "where", "which", "whats"};
const char *kAuxiliaries[] = {"are", "is",  "do",   "does", "can", "could",
                              "will", "would", "have", "has", "did", "am",
                              "shall", "should", "may", "might"};
const char *kNegations[] = {"not", "no", "never", "nothing", "none", "without"};
const char *kSecondPerson[] = {"you", "your", "yours", "yourself"};
const char *kPossessive[] = {"your", "yours", "yourself"};
const char *kFirstPerson[] = {"i", "me", "my", "we", "us", "our"};

// A question about what we can be asked, rather than about what we are playing.
const char *kCapability[] = {"do",   "know",  "understand", "work", "use",
                             "ask",  "say",   "help",       "offer",
                             "command", "option", "support"};

// Whole messages that are conversation, not instruction. The same shape as
// `BotAddress::isCourtesy`, and for the same reason: these are greetings, and a
// greeting scored word by word looks exactly like a question about our part.
const char *kSmallTalk[] = {
    "how are you",      "how are you doing", "how is it going",
    "how is everyone",  "you alright",       "alright",
    "right",            "back",              "i am back",
    "oops",             "oh",                "hello",
    "hi",               "hey",               "good morning",
    "good evening",     "morning",           "evening",
    "brb",              "bbl",               "gtg",
    "wb",               "welcome back",      "nice one",
    "how goes it",      "you there",         "anyone there",
    "long time no see", "good to see you",   "nice one thanks"};

std::string joined(const std::vector<std::string> &v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i)
      s += ' ';
    s += v[i];
  }
  return s;
}

} // namespace

const char *intentName(Intent i) {
  switch (i) {
  case Intent::DescribePart: return "DESCRIBE_PART";
  case Intent::DescribeSound: return "DESCRIBE_SOUND";
  case Intent::ReportKey: return "REPORT_KEY";
  case Intent::ReportChart: return "REPORT_CHART";
  case Intent::ReportTempo: return "REPORT_TEMPO";
  case Intent::SetKey: return "SET_KEY";
  case Intent::SetTempo: return "SET_TEMPO";
  case Intent::SetChart: return "SET_CHART";
  case Intent::ResetChart: return "RESET_CHART";
  case Intent::Reshuffle: return "RESHUFFLE";
  case Intent::StopPlaying: return "STOP_PLAYING";
  case Intent::StartPlaying: return "START_PLAYING";
  case Intent::SetQuiet: return "SET_QUIET";
  case Intent::SetLoud: return "SET_LOUD";
  case Intent::ExplainSelf: return "EXPLAIN_SELF";
  case Intent::Leave: return "LEAVE";
  case Intent::None: return "NONE";
  }
  return "NONE";
}

bool Reading::has(Concept c) const {
  return std::find(concepts.begin(), concepts.end(), c) != concepts.end();
}

Reading read(const std::string &text) {
  Reading r;
  const auto p = prepare(text);
  if (p.raw.empty())
    return r;

  // -- shape, read off the UNSTRIPPED tokens -------------------------------
  //
  // Read here rather than after filler removal, which is a correction: the
  // stripping now removes pronouns and auxiliaries, and reading `secondPerson`
  // afterwards silently found it false for every sentence containing "you".
  // Read at the first word that is not padding. "hold on whats the bpm again"
  // is a question, and testing only raw[0] made it an instruction to reroll --
  // a discourse marker is exactly what people put in front of a question.
  // Only DISCOURSE MARKERS are skipped, not padding in general. Skipping
  // anything in kFiller went wrong twice over: the interrogatives are in that
  // list, so the scan ran straight past the question word, and so are the
  // pronouns, so "i will try" stopped on `will` and became a question.
  static const char *kDiscourse[] = {"yo",  "oi",   "hmm",  "hold", "wait",
                                     "hang", "on",  "ok",   "okay", "so",
                                     "well", "hey", "hi",   "um",   "uh",
                                     "erm",  "right", "sorry", "mate", "dude",
                                     "man",  "bro",  "guys", "everyone"};
  size_t head = 0;
  while (head + 1 < p.raw.size() && inList(kDiscourse, p.raw[head]))
    ++head;
  r.question = text.find('?') != std::string::npos ||
               inList(kQuestionWords, p.raw[head]) ||
               inList(kAuxiliaries, p.raw[head]);
  if (p.exclamation)
    r.question = false;

  bool firstPerson = false;
  for (const auto &t : p.raw) {
    if (inList(kNegations, t))
      r.negated = true;
    if (inList(kSecondPerson, t))
      r.secondPerson = true;
    if (inList(kPossessive, t))
      r.possessive = true;
    if (inList(kFirstPerson, t))
      firstPerson = true;
  }
  // A polite request: a modal, the second person, and then the verb that is
  // actually being asked for. It parses as a question and it is not one, and
  // the whole difference between "can you change your part" and "what is your
  // part" sits in those two leading words.
  //
  // `do`/`does`/`did` are deliberately absent: "do you know the key" really is
  // a question.
  static const char *kRequestModal[] = {"can", "could", "would", "will",
                                        "please"};
  r.request = (inList(kRequestModal, p.raw[0]) && p.raw.size() > 1 &&
               (p.raw[1] == "you" || p.raw[1] == "we")) ||
              p.raw[0] == "please";

  const bool suggestion = p.raw.size() > 1 && p.raw[1] == "about" &&
                          (p.raw[0] == "how" || p.raw[0] == "what");

  r.continuation = p.raw[0] == "and" || p.raw[0] == "so";
  // "let us do another" and "shall we go again" are proposals between players.
  // They are not instructions to us, however much they look like one.
  r.proposal = (p.raw[0] == "let" && p.raw.size() > 1 && p.raw[1] == "us") ||
               (p.raw[0] == "shall" && p.raw.size() > 1 && p.raw[1] == "we");

  // An instruction: a leading VERB with no subject. The earlier version had
  // this as "not a question", which is true of almost every sentence and
  // therefore told us nothing -- and it silently disabled the rule below that
  // depends on it. "Leading word" alone is not enough either: "nice playing" is
  // a compliment, and treating it as an instruction is how it got answered with
  // a description of the part.
  r.imperative = false;
  if (!r.question && !p.toks.empty() && p.toks[0].first) {
    const auto first = stem(p.toks[0].word);
    for (const auto &w : kLexicon)
      if ((first == w.word || p.toks[0].word == w.word) &&
          (w.concept == Concept::Speak || w.concept == Concept::Change ||
           w.concept == Concept::Quiet || w.concept == Concept::Loud ||
           w.concept == Concept::Cease || w.concept == Concept::Chat ||
           w.concept == Concept::Leave))
        r.imperative = true;
  }

  if (inList(kSmallTalk, joined(p.raw)))
    return r;
  // "what a tune" is admiration. It carries a tone word and asks nothing, and
  // the question word at the front of it is not doing a question's work.
  if (p.exclamation)
    return r;

  // A statement the speaker is making about themselves. Not a question, not
  // aimed at us, and not a complaint -- so nothing in it is an instruction.
  bool firstPersonSubject = false;
  for (const auto &t : p.raw)
    if (t == "i" || t == "we")
      firstPersonSubject = true;
  const bool selfReport =
      firstPersonSubject && !r.secondPerson && !r.question && !r.negated;

  // Naming a VALUE is what separates "whats the key" from "play in g minor".
  // Both carry the KEY concept; only the second says which key, and without
  // that distinction every request to change one was answered by reporting it.
  static const char *kKeyValue[] = {"minor",  "major",     "dorian", "phrygian",
                                    "lydian", "mixolydian", "aeolian",
                                    "locrian", "ionian"};
  static const char *kTempoValue[] = {"fast", "slow", "quick", "faster",
                                      "slower", "vote"};
  bool keyValue = false, tempoValue = false;

  // -- words to concepts ---------------------------------------------------
  std::map<Concept, int> weight;
  auto note = [&](Concept c) {
    if (!r.has(c))
      r.concepts.push_back(c);
    weight[c] += 1;
  };

  for (const auto &tok : p.toks) {
    const auto s = stem(tok.word);
    bool matched = false;

    if (inList(kKeyValue, tok.word) || inList(kKeyValue, s))
      keyValue = true;
    if (inList(kTempoValue, tok.word) || inList(kTempoValue, s))
      tempoValue = true;
    // A bare number beside a tempo word is a tempo: "vote for 120", "make it
    // 96 bpm".
    if (!tok.word.empty() &&
        tok.word.find_first_not_of("0123456789") == std::string::npos)
      tempoValue = true;

    // "ill try", "im trying that now": the speaker saying what THEY will do.
    // A change word is only an instruction when somebody else is its subject,
    // and the giveaway is that it arrives as a verb -- with a determiner in
    // front of it ("i dont need the commentary") it is a thing, not an act.
    // Negation is excluded because a complaint about what we are playing is a
    // request however it is phrased: "i dont like that one".
    if (selfReport && !inList(kDeterminer, tok.prev)) {
      bool change = false;
      for (const auto &w : kLexicon)
        if ((stem(tok.word) == w.word || tok.word == w.word) &&
            w.concept == Concept::Change)
          change = true;
      if (change)
        continue;
    }

    // `keys` is the instrument and `key` is the tonic, and the stemmer folds
    // the first onto the second. "can i hear the keys part" asked about the
    // part and was answered with the key.
    if (tok.word == "keys") {
      note(Concept::Instrument);
      continue;
    }

    // A reflexive is the OBJECT of "mute yourself" and the TOPIC of "tell me
    // about yourself". Only the second is a question about who we are, and the
    // preposition in front of it is what says so.
    if (tok.word == "yourself" && tok.prev != "about" && tok.prev != "of")
      continue;

    // Word class first, for the handful of words where it decides the concept.
    // `play` is the one word that both asks and instructs, and WHERE IT SITS is
    // the difference. First in the clause, or straight after a modal or a "let
    // us", it is an instruction to start; anywhere else it is the ordinary word
    // for what a bot is doing.
    //
    // Position rather than the absence of a question mark, deliberately. Keying
    // this off "no question detected" turned every phrasing whose question we
    // failed to spot -- "wat r u playin" -- into a confident command, which is
    // the worst way to be wrong here. The default has to stay DESCRIBE_PART.
    // "lets" is expanded to "let us" upstream, so the token before the verb in
    // a proposal is "us" rather than anything that looks like "let".
    if ((s == "play" || tok.word == "play") && !r.possessive &&
        (tok.first || inList(kModal, tok.prev) ||
         (r.proposal && tok.prev == "us"))) {
      note(Concept::Begin);
      continue;
    }

    for (const auto &c : kClassed)
      if (s == c.word || tok.word == c.word) {
        const bool noun = inList(kDeterminer, tok.prev) ||
                          inList(kNounModifier, tok.prev);
        const bool verb = inList(kSubject, tok.prev) ||
                          inList(kModal, tok.prev) ||
                          (tok.first && !r.question);
        // With no evidence either way, a question is asking about a thing and
        // a statement is telling us to do one.
        const bool asking = r.question && !r.request;
        note(noun ? c.asNoun : verb ? c.asVerb : (asking ? c.asNoun : c.asVerb));
        matched = true;
      }
    if (matched)
      continue;

    for (const auto &w : kLexicon)
      if (s == w.word || tok.word == w.word) {
        note(w.concept);
        matched = true;
      }
    if (matched)
      continue;

    // A word that asks what we can be asked is not a topic and not an unknown:
    // it is the question itself, read by the capability rule below.
    if (inList(kCapability, tok.word) || inList(kCapability, s))
      continue;

    // A real English word is not a mistyped one. This is the single rule that
    // stopped `chat` becoming `chart`, `room` becoming `root` and `oops`
    // becoming `loop` -- three confidently wrong answers from one missing idea.
    if (BotDictionary::isWord(s) || BotDictionary::isWord(tok.word)) {
      ++r.unknownWords;
      continue;
    }

    const int budget = s.size() <= 5 ? 1 : 2;
    Concept best = Concept::Part;
    int bestCost = 99, runnerUp = 99;
    for (const auto &w : kLexicon) {
      // Short entries are matched exactly or not at all: at three letters
      // almost anything is one edit away.
      if (std::char_traits<char>::length(w.word) < 4)
        continue;
      const int d = editDistance(s, w.word);
      if (d > budget)
        continue;
      if (d < bestCost) {
        if (best != w.concept)
          runnerUp = bestCost;
        bestCost = d;
        best = w.concept;
      } else if (w.concept != best) {
        runnerUp = std::min(runnerUp, d);
      }
    }
    // Nearest wins, and only a TIE is ambiguous. Requiring no other candidate
    // within budget threw away words that were plainly closer to one thing than
    // another: `timbre` is one edit from `timbr` and two from `time`, which is
    // not a hard question, and discarding it lost the only real word in the
    // sentence.
    if (bestCost <= budget && bestCost < runnerUp)
      note(best);
    else
      ++r.unknownWords;
  }

  // "let us do another" is two players talking; "let us play in e minor" names
  // something we can act on, and the difference is whether a value was given.
  // Naming WHICH chart counts as naming a value the same way a key does: "lets
  // have the default chords" is as specific as a request gets.
  // "lets stop", "lets play", "lets wrap it up" -- beginning and ceasing are as
  // specific as a request gets, so a proposal carrying one is aimed at us.
  if (r.proposal && !keyValue && !tempoValue &&
      !weight.count(Concept::Begin) && !weight.count(Concept::Cease) &&
      !(weight.count(Concept::Chart) && (weight.count(Concept::Change) ||
                                         weight.count(Concept::Standard))))
    return r;

  const bool topic =
      weight.count(Concept::Part) || weight.count(Concept::Tone) ||
      weight.count(Concept::Key) || weight.count(Concept::Chart) ||
      weight.count(Concept::Tempo) || weight.count(Concept::Drum) ||
      weight.count(Concept::Instrument);

  // A capability question: "what can you do", "what do you know", "what do i
  // say". About the conversation itself rather than about the music, and the
  // giveaway is a question whose only verb is one of these.
  //
  // It has to be ABOUT somebody -- "what can you do", "what do i say" -- or
  // "do it again" reads as one, since `do` is both the auxiliary that makes a
  // question and the verb that asks what we are for. And a word we did not
  // recognise rules it out: "what interface do you use" is the same shape and
  // is not our business.
  bool capability = false;
  if ((r.question || weight.count(Concept::Speak)) && !topic &&
      (r.secondPerson || firstPerson) && r.unknownWords == 0)
    for (const auto &t : p.raw)
      if (inList(kCapability, t) &&
          (!weight.count(Concept::Chat) || firstPerson))
        capability = true;

  // "i dont know what to do" is asking for help; "i dont know this one" is
  // somebody talking about the tune. The wh-clause is the difference -- what
  // follows "know" is a question, and a question is what we can answer.
  if (!capability && firstPerson && r.negated && !topic) {
    bool know = false, wh = false;
    for (const auto &t : p.raw) {
      if (t == "know" || t == "understand")
        know = true;
      else if (know && (t == "what" || t == "how" || t == "which"))
        wh = true;
    }
    capability = wh;
  }

  // Asking about a thing of ours without naming the thing -- "tell me about
  // it", "describe it", "what about you", "and you?". The topic is whatever was
  // last discussed, which we do not track, so the honest answer is to ask which
  // of the two we could describe. Naming both is what makes that useful rather
  // than a shrug.
  const bool actionable =
      weight.count(Concept::Change) || weight.count(Concept::Quiet) ||
      weight.count(Concept::Loud) || weight.count(Concept::Cease) ||
      weight.count(Concept::Leave) || weight.count(Concept::Chat) ||
      weight.count(Concept::Identity);
  const bool anaphora =
      !capability && !topic && !actionable &&
      (weight.count(Concept::Speak) || p.askingCharacter || r.continuation ||
       (r.possessive && p.toks.empty()));
  if (anaphora && r.unknownWords == 0) {
    r.ambiguous = true;
    r.intent = Intent::DescribePart;
    r.alternative = Intent::DescribeSound;
    return r;
  }

  // Bare "part" is the Ninjam command, not a noun phrase. Everywhere else it is
  // the figure being played, which is why the lexicon still carries it -- and
  // the test is the WHOLE message, not what survived stripping, or "whats your
  // part" reduces to the same one token and is answered by leaving.
  if (p.raw.size() == 1 && p.raw[0] == "part" && !r.question) {
    r.intent = Intent::Leave;
    return r;
  }

  if (capability && r.concepts.empty()) {
    r.intent = Intent::ExplainSelf;
    return r;
  }

  if (p.toks.empty()) {
    // Nothing but function words survived. A question is then asking about the
    // situation -- "what is this", "what now" -- and a statement is small talk.
    if (r.question && r.unknownWords == 0)
      r.intent = Intent::ExplainSelf;
    return r;
  }

  if (r.concepts.empty())
    return r;

  // -- score ---------------------------------------------------------------
  //
  // Each intent is a small weighted bag: what counts for it, what counts
  // against, and a bonus for the right sentence shape.
  std::map<Intent, int> score;
  auto add = [&](Intent i, int n) { score[i] += n; };

  const bool aboutMe = r.secondPerson;

  if (weight.count(Concept::Part)) {
    add(Intent::DescribePart, 3 * weight[Concept::Part]);
    if (aboutMe) add(Intent::DescribePart, 2);
  }
  if (weight.count(Concept::Tone)) {
    add(Intent::DescribeSound, 3 * weight[Concept::Tone]);
    if (aboutMe) add(Intent::DescribeSound, 2);
  }
  // A named topic beats the general one. "what key are you playing in" has both
  // KEY and PART in it, and it is a question about the key -- `part` is simply
  // what you get when nothing more specific was said.
  if (weight.count(Concept::Key)) add(Intent::ReportKey, 7);
  if (weight.count(Concept::Chart)) add(Intent::ReportChart, 7);
  if (weight.count(Concept::Tempo)) add(Intent::ReportTempo, 7);
  if (weight.count(Concept::Change)) add(Intent::Reshuffle, 5);

  // Asked to CHANGE the key or the tempo rather than to report it.
  //
  // The bots have no authority over either -- a tempo is a server vote and a
  // key is whatever the room agrees -- but that is a fact about what they may
  // DO, not about what they should understand. Recognising the request is what
  // lets a bot say "i cannot decide that, but i will vote for it"; failing to
  // recognise it produces an answer that looks responsive and ignores what was
  // actually asked, which is the most expensive failure this file has.
  //
  // Three guards, each of them a whole class of corpus line:
  //   - a SPEAK word makes it a report after all ("can you tell me the key")
  //   - a first-person subject is somebody thinking aloud, not instructing us
  //     ("i dont know the key")
  //   - a question that is not a polite request is asking, not telling
  //     ("is it major or minor", "whats the key")
  //
  // The first-person guard yields to a request, because "can we go faster" is
  // the commonest way anybody asks for a tempo change and the `we` in it is
  // not somebody talking to themselves.
  // The first-person guard is about the SUBJECT: "i dont know the key" is
  // thinking aloud, but the "me" in "give me a minor key" is an object and
  // says nothing about who is instructing whom.
  //
  // A SPEAK word makes it a report -- unless a value was named, because "give
  // me a minor key" specifies rather than asks, where "tell me the key" asks.
  const bool proposing = r.request || r.proposal || suggestion;
  const bool instructing =
      (proposing || !r.question) &&
      (!weight.count(Concept::Speak) || keyValue || tempoValue) &&
      (!firstPersonSubject || proposing);
  const bool setKey = instructing && weight.count(Concept::Key) &&
                      (keyValue || weight.count(Concept::Change));
  const bool setTempo = instructing && weight.count(Concept::Tempo) &&
                        (tempoValue || weight.count(Concept::Change));
  // A chart has no short value word the way a key has "minor" -- a chart value
  // IS a chord chart, and a message that is one never reaches here (a bare
  // "| Am | F |" is an announcement, and `Harmony::looksLikeChart` claims it
  // upstream). So asking to change the chart is the whole of what is left.
  const bool setChart = instructing && weight.count(Concept::Chart) &&
                        weight.count(Concept::Change);
  if (setKey)
    add(Intent::SetKey, 9);
  if (setTempo)
    add(Intent::SetTempo, 9);
  if (setChart)
    add(Intent::SetChart, 9);
  // "switch to g major" and "can we change the tempo" carry a change word, but
  // what is being changed is named right there beside it. Rerolling our own
  // part instead would be a confident answer to a question nobody asked.
  if (setKey || setTempo || setChart)
    score[Intent::Reshuffle] -= 9;

  // WHICH chart, rather than a different one. "the default chords for this
  // key" and "can we change the chords" share their only topic word, and the
  // answers are opposites: one names the chart the key implies, the other asks
  // for anything but. Everything else the sentence could be read as is pushed
  // down together, because every one of them is a confident wrong answer --
  // "the usual changes for the key" read as SET_KEY, and reporting the chart
  // we are already playing answers a question nobody asked.
  if (weight.count(Concept::Standard) && weight.count(Concept::Chart)) {
    add(Intent::ResetChart, 11);
    for (auto i : {Intent::ReportChart, Intent::SetChart, Intent::Reshuffle,
                   Intent::SetKey, Intent::ReportKey})
      score[i] -= 8;
  }
  if (weight.count(Concept::Quiet)) add(Intent::SetQuiet, 6);
  if (weight.count(Concept::Loud)) add(Intent::SetLoud, 7);
  if (weight.count(Concept::Identity)) add(Intent::ExplainSelf, 6);
  if (weight.count(Concept::Leave)) add(Intent::Leave, 5);
  if (capability) add(Intent::ExplainSelf, 8);

  // Talking is our topic, and what is being ASKED about it is the whole
  // question. "stop chatting" and "chat away" share their only content word.
  //
  // Unless something else is the topic: "talk me through your sound" uses the
  // same verb to ask for a description, and answering it by unmuting is the
  // kind of literalism that makes a bot feel like a parser.
  if (weight.count(Concept::Chat)) {
    if (r.negated || weight.count(Concept::Quiet) || weight.count(Concept::Cease))
      add(Intent::SetQuiet, 8);
    else if (topic || weight.count(Concept::Speak)) {
      add(Intent::DescribePart, 1);
      add(Intent::DescribeSound, 1);
    } else
      add(Intent::SetLoud, 7);
  }

  // Ceasing WHAT. With talk in the sentence it is the talk; with anything else,
  // or nothing at all, it is the playing -- and to stop playing is to leave.
  if (weight.count(Concept::Cease) && !weight.count(Concept::Chat)) {
    // To stop playing is NOT to leave. It used to be, which put the least
    // destructive phrase in a jam on the most destructive act a bot can do:
    // "stop playing" sent the whole band home (docs/BOT-CHAT.md section 15).
    add(Intent::StopPlaying, 8);
    score[Intent::DescribePart] -= 4;
  }
  // The mirror of it. What is being begun is decided the same way -- by what
  // else is in the sentence -- and with nothing else named it is the playing.
  if (weight.count(Concept::Begin) && !weight.count(Concept::Chat)) {
    add(Intent::StartPlaying, 8);
    // "stop the music" names both directions and means the first one. Ceasing
    // wins, because the thing being ceased is what the other word named.
    if (weight.count(Concept::Cease))
      score[Intent::StartPlaying] -= 9;
  }

  // A drum or an instrument on its own is the ambiguity the corpus is full of:
  // "tell me about your kick" could be the part or the sound. Push both,
  // equally, and let the margin rule decide there is no answer.
  if ((weight.count(Concept::Drum) || weight.count(Concept::Instrument)) &&
      !weight.count(Concept::Part) && !weight.count(Concept::Tone)) {
    add(Intent::DescribePart, 4);
    add(Intent::DescribeSound, 4);
  } else if (weight.count(Concept::Drum) || weight.count(Concept::Instrument)) {
    // Named alongside a topic, it is what the topic is ABOUT and reinforces it.
    if (weight.count(Concept::Part)) add(Intent::DescribePart, 3);
    if (weight.count(Concept::Tone)) add(Intent::DescribeSound, 3);
  }

  // "what is it like" is asking about character. With a thing named it is that
  // thing's sound; with nothing named it is the anaphora case below.
  if (p.askingCharacter)
    add(Intent::DescribeSound, 4);

  // Negation flips the two settings, since "don't be quiet" and "be quiet"
  // share every content word and differ only here.
  if (r.negated) {
    if (weight.count(Concept::Quiet)) {
      score[Intent::SetQuiet] -= 9;
      add(Intent::SetLoud, 5);
    }
    if (weight.count(Concept::Loud)) {
      score[Intent::SetLoud] -= 9;
      add(Intent::SetQuiet, 5);
    }
  }

  // An instruction carrying a change word is a reroll, whatever else is in it:
  // "play something different" names the part only to say which part to change.
  //
  // Except beside a talking word, where it means resume rather than reroll:
  // "talk again" and "speak again" are asking for the commentary back, and
  // rerolling the part instead is a wrong answer that costs a bar of music.
  // Change words come in two senses that the concept does not separate: ask for
  // something DIFFERENT, or ask for the same thing AGAIN. Everywhere else they
  // mean the same, and beside a starting word they are opposites.
  bool wantsNewContent = false;
  for (const auto &t : p.raw)
    if (t == "different" || t == "differently" || t == "else" || t == "new" ||
        t == "another" || t == "other" || t == "fresh" || t == "vary" ||
        t == "varied" || t == "alter" || t == "switch" || t == "swap" ||
        t == "random" || t == "shake" || t == "reroll" || t == "redo" ||
        t == "rework")
      wantsNewContent = true;

  if (weight.count(Concept::Change) && (!r.question || r.request)) {
    if (weight.count(Concept::Chat) && !topic)
      add(Intent::SetLoud, 4);
    else if ((weight.count(Concept::Begin) || weight.count(Concept::Cease)) &&
             !wantsNewContent)
      // Beside a starting or stopping word, a REPEAT word is resuming rather
      // than rerolling: asking a silent band to start again asks for what it
      // was already playing, not for something new. The same shape as the rule
      // above, which is what "talk again" needed for the same reason.
      //
      // Reported from a real room: "start playing again" got "ok, something
      // else", which is a confident answer to a question nobody asked.
      //
      // Only a repeat word, though. "play something different" carries a
      // starting word too and is a reroll, and the change words divide cleanly
      // into the two senses -- which is why the distinction is drawn on the
      // word rather than on the concept they share.
      score[Intent::Reshuffle] -= 6;
    else
      add(Intent::Reshuffle, 4);
  }

  // Asking is not instructing. "what are you playing" wants the part; "shake"
  // wants a reroll; a question containing a change word is usually still a
  // question about something else.
  if (r.question && !r.request && weight.count(Concept::Change) && topic)
    score[Intent::Reshuffle] -= 4;

  // "how many beats in a bar" carries both TEMPO and CHART, and is a question
  // about duration. The leading "how many"/"how long" is what says so -- but
  // only over something already temporal. "how many pulses" counts a figure and
  // "how many bars" counts a chart, so the phrase alone decides nothing.
  if (p.raw.size() >= 2 && p.raw[0] == "how" &&
      (p.raw[1] == "many" || p.raw[1] == "long")) {
    bool beats = false;
    for (const auto &t : p.raw)
      if (t == "beat" || t == "beats")
        beats = true;
    if (weight.count(Concept::Tempo) || beats)
      add(Intent::ReportTempo, 8);
  }

  // Speaking words are a request to describe, not a topic of their own.
  if (weight.count(Concept::Speak) && !weight.count(Concept::Identity)) {
    add(Intent::DescribePart, 1);
    add(Intent::DescribeSound, 1);
  }

  // A judgement is not a question. "sounds good", "that sounded great" carry a
  // tone word and ask nothing -- and answering a compliment with a description
  // of your patch is exactly the wall this file exists to avoid.
  if (weight.count(Concept::Hear) && !r.question && !r.imperative)
    return r;
  if (weight.count(Concept::Hear) && weight.count(Concept::Tone) && !r.question)
    return r;

  // A negated statement about playing, with nobody addressed in it, is a player
  // talking about themselves: "never played this before", "i havent got a part
  // yet". Answering with a description of ours is a non-sequitur.
  //
  // A named, reportable topic is excluded: "i cant remember the chords" and "i
  // dont know the key" are how people ask for those things, and the negation is
  // the reason they are asking rather than a reason to stay quiet.
  if (r.negated && !r.secondPerson && !r.question && !r.imperative &&
      !weight.count(Concept::Change) && !weight.count(Concept::Chat) &&
      !weight.count(Concept::Quiet) && !weight.count(Concept::Cease) &&
      !weight.count(Concept::Key) && !weight.count(Concept::Chart) &&
      !weight.count(Concept::Tempo))
    return r;

  // What we cannot do. A question about how it SOUNDS to the listener is not
  // a question about our patch, and pretending otherwise is the dishonest
  // answer -- so these do not score at all and fall to the floor.
  if (weight.count(Concept::Hear) && !weight.count(Concept::Tone) &&
      !weight.count(Concept::Part))
    return r;

  if (score.empty())
    return r;

  Intent best = Intent::None, second = Intent::None;
  int bestScore = 0, secondScore = 0;
  for (const auto &entry : score) {
    if (entry.second > bestScore) {
      second = best;
      secondScore = bestScore;
      best = entry.first;
      bestScore = entry.second;
    } else if (entry.second > secondScore) {
      second = entry.first;
      secondScore = entry.second;
    }
  }

  // The floor, and then the margin. The same shape as Harmony::inferKey: score
  // the candidates, require a clear winner, and when there is not one, say so
  // rather than guess. One idea used twice.
  //
  // The floor RISES with the words we did not recognise, because an
  // unrecognised word is the clearest sign that a grammatical, addressed
  // message is about something else entirely.
  //
  // It only counts when nothing but IDENTITY was recognised, because that is
  // the small-talk signature -- "who wrote this", "where are you based" -- and
  // IDENTITY is the concept a bare "who" or "what" produces on its own. Where
  // something specific WAS named, an unknown word beside it is usually just a
  // word we did not need: "leave the room" and "has anyone set a key" both say
  // plainly what they want.
  const bool weakOnly = !topic && !weight.count(Concept::Leave) &&
                        !weight.count(Concept::Change) &&
                        !weight.count(Concept::Chat) &&
                        !weight.count(Concept::Quiet) &&
                        !weight.count(Concept::Cease) &&
                        !weight.count(Concept::Loud);
  if (bestScore < 3 + (weakOnly ? 4 * r.unknownWords : 0))
    return r;

  if (secondScore >= bestScore) {
    r.ambiguous = true;
    r.intent = best;
    r.alternative = second;
    return r;
  }

  r.intent = best;
  return r;
}

// ---------------------------------------------------------------------------
// 8. Clause segmentation.
//
// Deliberately the LAST thing in the file and the FIRST level of the cascade,
// because it is the level that was missing rather than one that was rebuilt:
// `read` is untouched by it, so every number the corpus reports is unchanged.
// ---------------------------------------------------------------------------

namespace {

// What separates two requests. `but` and `then` are here for the same reason
// `and` is; a comma is here because half the room does not type the word.
const char *kConnective[] = {"and", "then", "but", "also", "plus"};

std::vector<std::string> clauses(const std::string &text) {
  std::vector<std::string> out;
  std::string current, word;
  auto flushWord = [&]() {
    if (word.empty())
      return;
    std::string lowered;
    for (char c : word)
      lowered += (char)std::tolower((unsigned char)c);
    if (inList(kConnective, lowered) && !current.empty()) {
      out.push_back(current);
      current.clear();
    } else {
      if (!current.empty())
        current += ' ';
      current += word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ',' || c == ';') {
      flushWord();
      if (!current.empty()) {
        out.push_back(current);
        current.clear();
      }
    } else if (std::isspace((unsigned char)c) != 0) {
      flushWord();
    } else {
      word += c;
    }
  }
  flushWord();
  if (!current.empty())
    out.push_back(current);
  return out;
}

} // namespace

std::vector<Reading> readAll(const std::string &text) {
  const auto parts = clauses(text);
  if (parts.size() > 1) {
    std::vector<Reading> found;
    for (const auto &part : parts) {
      // A clause that is only an address is not a request. The comma in
      // "hey kit, whats your part" is punctuation around a vocative, and
      // reading it as its own clause invented a question about the kit.
      bool addressOnly = true;
      for (const auto &w : split(part))
        if (!inList(kVocative, w) && !inList(kFiller, w))
          addressOnly = false;
      if (addressOnly)
        continue;

      const auto r = read(part);
      // Only a definite reading counts. An ambiguous one is a conjunct of the
      // request beside it -- "the drums" in "shake the bass and the drums" --
      // and promoting it to a second request would invent one.
      if (r.intent == Intent::None || r.ambiguous)
        continue;
      bool seen = false;
      for (const auto &had : found)
        if (had.intent == r.intent)
          seen = true;
      if (!seen)
        found.push_back(r);
    }
    if (found.size() > 1)
      return found;
  }
  return {read(text)};
}

} // namespace BotLanguage
