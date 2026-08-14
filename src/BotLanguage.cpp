#include "BotLanguage.h"

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
};

// Politeness, hedging and filler. None of it changes what was asked, and all of
// it is in the way.
const char *kFiller[] = {
    "please", "pls",     "sorry",  "just",   "quickly", "mate",
    "man",    "dude",    "hey",    "hi",     "hello",   "ok",     "okay",
    "so",     "well",    "um",     "uh",     "erm",     "like",   "actually",
    "really", "maybe",   "perhaps","kinda",  "sort",    "bit",    "very",
    "thanks", "thank",   "cheers", "again",  "now",     "then",   "there",
    "here",   "a",       "an",     "the",    "of",      "for",    "to",
    "at",     "in",      "on",     "and",    "or",      "me",     "my",
    "us",     "we",      "it",     "its",    "this",    "that",   "some",
    "any",    "all",     "bro",    "buddy",  "friend",  "guys",   "everyone"};

// `again` is filler in "tell me again" and meaningful in "again" alone, so it
// is only dropped when something else survives. Same for a couple of others.
const char *kFillerUnlessAlone[] = {"again", "now", "more", "up", "it"};

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

} // namespace

std::vector<std::string> normalise(const std::string &text) {
  auto tokens = split(text);

  // Idioms first, because they are two tokens meaning one thing and the
  // stemmer will never get there on its own.
  for (size_t i = 0; i + 1 < tokens.size(); ++i) {
    if (tokens[i] == "up" && tokens[i + 1] == "to") {
      tokens[i] = "doing";
      tokens.erase(tokens.begin() + (long)i + 1);
    } else if (tokens[i] == "playing" && i + 1 == tokens.size() - 1 &&
               (tokens[i + 1] == "in" || tokens[i + 1] == "over" ||
                tokens[i + 1] == "on")) {
      // "what are we playing in" asks the key; "what are we playing over" asks
      // the chart. One preposition carries the whole difference, and it is
      // about to be stripped as filler, so it is read here first.
      tokens[i] = tokens[i + 1] == "in" ? "key" : "chords";
      tokens.erase(tokens.begin() + (long)i + 1);
    } else if (tokens[i] == "sound" && tokens[i + 1] == "like") {
      tokens[i] = "sound";
      tokens.erase(tokens.begin() + (long)i + 1);
    }
  }

  // Expand contractions, which can turn one token into two.
  std::vector<std::string> expanded;
  for (const auto &t : tokens) {
    bool did = false;
    for (const auto &e : kExpansions)
      if (t == e.from) {
        for (const auto &piece : split(e.to))
          expanded.push_back(piece);
        did = true;
        break;
      }
    if (!did)
      expanded.push_back(t);
  }

  // Drop a leading vocative: "kit," / "hey kit" / "@delvo". Addressing has
  // already happened by the time we get here, so the name is noise.
  //
  // Only the first token or two, and only when something is left afterwards.
  if (expanded.size() > 1 && inList(kFiller, expanded[0]))
    expanded.erase(expanded.begin());

  // A leading instrument word is a VOCATIVE -- "kit, what are you playing" --
  // and by the time a message reaches here, addressing has already been decided.
  // Left in, it reads as a topic and ties the sentence against itself.
  static const char *kVocative[] = {"kit",  "drums", "drum",   "bass", "keys",
                                    "lead", "piano", "guitar", "tutor",
                                    "band", "everyone"};
  if (expanded.size() > 1 && inList(kVocative, expanded[0]))
    expanded.erase(expanded.begin());

  // An auxiliary before a pronoun is grammar, not content: the `do` in "what
  // do you sound like" is not the `do` in "what are you doing", and leaving it
  // in makes those two sentences score identically.
  static const char *kAux[] = {"do", "does", "did", "are", "is", "was",
                               "can", "could", "will", "would", "have", "has"};
  static const char *kPronoun[] = {"you", "it", "that", "they", "we", "i",
                                   "this", "he", "she"};
  std::vector<std::string> deauxed;
  for (size_t i = 0; i < expanded.size(); ++i) {
    if (inList(kAux, expanded[i]) && i + 1 < expanded.size() &&
        inList(kPronoun, expanded[i + 1]))
      continue;
    deauxed.push_back(expanded[i]);
  }
  expanded = deauxed;

  std::vector<std::string> kept;
  for (const auto &t : expanded)
    if (!inList(kFiller, t))
      kept.push_back(t);

  // If the filter ate everything, the filler WAS the message -- "thanks",
  // "hello" -- and the caller needs to see it rather than an empty list.
  if (kept.empty())
    return expanded;

  std::vector<std::string> out;
  for (const auto &t : kept)
    if (!(inList(kFillerUnlessAlone, t) && kept.size() > 1))
      out.push_back(t);
  return out.empty() ? kept : out;
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
  // alone: `progression` -> `progress`, `tonality` -> `tonal`.
  if (endsWith("ion"))
    chop(3);
  else if (endsWith("ity"))
    chop(3);
  else if (endsWith("ment"))
    chop(4);
  else if (endsWith("ness"))
    chop(4);

  // A trailing silent `e` after a consonant: `timbre` -> `timbr`, `figure` ->
  // `figur`, `change` -> `chang`. Cheap, and it saves the lexicon from carrying
  // both spellings of every word.
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
    {"do", Concept::Part},        {"doing", Concept::Part},
    {"perform", Concept::Part},   {"accent", Concept::Part},
    {"fill", Concept::Part},      {"note", Concept::Part},
    {"shape", Concept::Part},     {"phras", Concept::Part},
    {"tick", Concept::Part},      {"hit", Concept::Part},
    {"count", Concept::Part},
    {"puls", Concept::Part},      {"onset", Concept::Part},
    {"go", Concept::Part},        {"root", Concept::Key},
    {"tonal", Concept::Key},      {"setup", Concept::Tone},
    {"lay", Concept::Part},       {"got", Concept::Part},

    {"sound", Concept::Tone},     {"tone", Concept::Tone},
    {"timbr", Concept::Tone},     {"patch", Concept::Tone},
    {"voic", Concept::Tone},      {"charact", Concept::Tone},
    {"preset", Concept::Tone},    {"tune", Concept::Tone},
    {"tuned", Concept::Tone},     {"instrument", Concept::Tone},
    {"kit", Concept::Tone},       {"bright", Concept::Tone},
    {"dark", Concept::Tone},      {"warm", Concept::Tone},

    {"key", Concept::Key},        {"scale", Concept::Key},
    {"tonic", Concept::Key},      {"mode", Concept::Key},
    {"major", Concept::Key},      {"minor", Concept::Key},

    {"chord", Concept::Chart},    {"chang", Concept::Chart},
    {"progress", Concept::Chart}, {"chart", Concept::Chart},
    {"sequenc", Concept::Chart},  {"harmoni", Concept::Chart},
    {"loop", Concept::Chart},     {"agre", Concept::Chart},
    {"bar", Concept::Chart},
    {"over", Concept::Chart},     {"turnaround", Concept::Chart},

    {"tempo", Concept::Tempo},    {"bpm", Concept::Tempo},
    {"speed", Concept::Tempo},    {"interv", Concept::Tempo},
    {"fast", Concept::Tempo},     {"slow", Concept::Tempo},
    {"bpi", Concept::Tempo},      {"time", Concept::Tempo},
    {"pace", Concept::Tempo},     {"click", Concept::Tempo},
    {"quick", Concept::Tempo},    {"run", Concept::Tempo},
    {"long", Concept::Tempo},     {"metronom", Concept::Tempo},

    {"shake", Concept::Change},   {"reroll", Concept::Change},
    {"roll", Concept::Change},    {"new", Concept::Change},
    {"differ", Concept::Change},  {"anoth", Concept::Change},
    {"mix", Concept::Change},     {"switch", Concept::Change},
    {"vari", Concept::Change},    {"els", Concept::Change},
    {"redo", Concept::Change},    {"random", Concept::Change},

    {"quiet", Concept::Quiet},    {"hush", Concept::Quiet},
    {"shush", Concept::Quiet},    {"silent", Concept::Quiet},
    {"silenc", Concept::Quiet},   {"mute", Concept::Quiet},
    {"stop", Concept::Quiet},     {"enough", Concept::Quiet},
    {"shut", Concept::Quiet},     {"zip", Concept::Quiet},

    {"speak", Concept::Loud},     {"talk", Concept::Loud},
    {"unmute", Concept::Loud},    {"chatti", Concept::Loud},
    {"resum", Concept::Loud},     {"back", Concept::Loud},

    {"who", Concept::Identity},   {"help", Concept::Identity},
    {"purpos", Concept::Identity},
    {"bot", Concept::Identity},   {"robot", Concept::Identity},
    {"human", Concept::Identity}, {"real", Concept::Identity},
    {"person", Concept::Identity},{"thing", Concept::Identity},

    {"leav", Concept::Leave},     {"evict", Concept::Leave},
    {"away", Concept::Leave},     {"lost", Concept::Leave},
    {"done", Concept::Leave},     {"dismiss", Concept::Leave},
    {"remov", Concept::Leave},
    {"bye", Concept::Leave},      {"exit", Concept::Leave},
    {"quit", Concept::Leave},     {"away", Concept::Leave},
    {"home", Concept::Leave},     {"off", Concept::Leave},
    {"out", Concept::Leave},      {"begon", Concept::Leave},

    {"kick", Concept::Drum},      {"snare", Concept::Drum},
    {"hat", Concept::Drum},
    {"hihat", Concept::Drum},     {"cymbal", Concept::Drum},
    {"tom", Concept::Drum},       {"drum", Concept::Drum},

    {"tell", Concept::Speak},     {"say", Concept::Speak},
    {"walk", Concept::Speak},     {"through", Concept::Speak},
    {"describ", Concept::Speak},  {"explain", Concept::Speak},
    {"give", Concept::Speak},     {"show", Concept::Speak},
    {"about", Concept::Speak},    {"know", Concept::Speak},

    {"hear", Concept::Hear},      {"listen", Concept::Hear},
    {"loud", Concept::Hear},      {"level", Concept::Hear},
    {"volum", Concept::Hear},     {"good", Concept::Hear},
    {"nice", Concept::Hear},      {"bad", Concept::Hear},
    {"great", Concept::Hear},     {"awesom", Concept::Hear},
    {"lovely", Concept::Hear},    {"terribl", Concept::Hear},
    {"awful", Concept::Hear},     {"rough", Concept::Hear},
    {"muddy", Concept::Hear},     {"harsh", Concept::Hear},
    {"balanc", Concept::Hear},    {"mix", Concept::Hear},
};

// `kick` is both a drum and an eviction, and `part` is both a figure and a
// command. Resolved by the scorer rather than the table, which is why both
// entries are allowed to exist.

// ---------------------------------------------------------------------------
// 4. Typo repair, against the lexicon only. A word that matches nothing gets
//    one edit up to five characters and two beyond.
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
      if (i > 1 && j > 1 && a[(size_t)i - 1] == b[(size_t)j - 2] &&
          a[(size_t)i - 2] == b[(size_t)j - 1])
        best = std::min(best, d[(size_t)i - 2][(size_t)j - 2] + 1);
      d[(size_t)i][(size_t)j] = best;
    }
  return d[(size_t)n][(size_t)m];
}

// Words that are never a typo for anything.
//
// The same rule the addressing engine needed, and for the same reason: `hat` is
// three letters, so `what`, `that` and half the function words in English are
// one edit from it. A real word is not a mistyped one, and without this the
// concept DRUM turns up in almost every sentence.
const char *kNeverATypo[] = {
    "what", "how",  "who",  "why",   "when", "where", "which", "that",
    "this", "you",  "your", "are",   "is",   "was",   "be",    "been",
    "get",  "got",  "up",   "out",   "many", "much",  "more",  "most",
    "i",    "we",   "they", "he",    "she",  "him",   "her",   "them",
    "if",   "but",  "as",   "by",    "with", "from",  "than",  "too",
    "also", "only", "even", "ever",  "still","yet",   "own",   "same",
    "both", "each", "few",  "other", "over", "under", "once",  "not",
    "no",   "yes",  "one",  "two",   "am",   "an",    "at",    "on",
    "off",  "put",  "let",  "make",  "want", "need",  "give",  "come"};

const char *kQuestionWords[] = {"what", "who",  "how",  "why",  "when",
                                "where", "which", "whats"};
const char *kAuxiliaries[] = {"are", "is",  "do",   "does", "can", "could",
                              "will", "would", "have", "has", "did", "am",
                              "shall", "should", "may", "might"};
const char *kNegations[] = {"not", "no", "never", "stop", "dont", "cant"};
const char *kSecondPerson[] = {"you", "your", "yours", "yourself"};

} // namespace

const char *intentName(Intent i) {
  switch (i) {
  case Intent::DescribePart: return "DESCRIBE_PART";
  case Intent::DescribeSound: return "DESCRIBE_SOUND";
  case Intent::ReportKey: return "REPORT_KEY";
  case Intent::ReportChart: return "REPORT_CHART";
  case Intent::ReportTempo: return "REPORT_TEMPO";
  case Intent::Reshuffle: return "RESHUFFLE";
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
  const auto tokens = normalise(text);
  if (tokens.empty())
    return r;

  // -- shape, read off the tokens before they are stemmed ------------------
  r.question = text.find('?') != std::string::npos ||
               inList(kQuestionWords, tokens[0]) ||
               inList(kAuxiliaries, tokens[0]);
  for (const auto &t : tokens) {
    if (inList(kNegations, t))
      r.negated = true;
    if (inList(kSecondPerson, t))
      r.secondPerson = true;
  }
  // An instruction: a leading verb with no subject. The earlier version had
  // this as "not a question", which is true of almost every sentence and
  // therefore told us nothing -- and it silently disabled the rule below that
  // depends on it.
  r.imperative = false;
  if (!r.question && !tokens.empty()) {
    const auto first = stem(tokens[0]);
    for (const auto &w : kLexicon)
      if ((first == w.word || tokens[0] == w.word) &&
          (w.concept == Concept::Speak || w.concept == Concept::Change ||
           w.concept == Concept::Quiet || w.concept == Concept::Loud ||
           w.concept == Concept::Leave))
        r.imperative = true;
  }

  // -- words to concepts ---------------------------------------------------
  std::map<Concept, int> weight;
  auto note = [&](Concept c) {
    if (!r.has(c))
      r.concepts.push_back(c);
    weight[c] += 1;
  };

  for (const auto &raw : tokens) {
    const auto s = stem(raw);
    bool matched = false;
    for (const auto &w : kLexicon)
      if (s == w.word || raw == w.word) {
        note(w.concept);
        matched = true;
      }
    if (matched)
      continue;

    // A typo, if it is unambiguously one -- and only if the word is not an
    // ordinary one to begin with.
    if (inList(kNeverATypo, s) || inList(kNeverATypo, raw))
      continue;
    const int budget = s.size() <= 5 ? 1 : 2;
    Concept best = Concept::Part;
    int bestDistance = 99, runnerUp = 99;
    for (const auto &w : kLexicon) {
      // Short entries are matched exactly or not at all: at three letters
      // almost anything is one edit away.
      if (std::char_traits<char>::length(w.word) < 4)
        continue;
      const int d = editDistance(s, w.word);
      if (d > budget)
        continue;
      if (d < bestDistance) {
        if (best != w.concept)
          runnerUp = bestDistance;
        bestDistance = d;
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
    if (bestDistance <= budget && bestDistance < runnerUp)
      note(best);
  }

  if (r.concepts.empty()) {
    // "what are you", "who is this", "what is this thing" -- a question with a
    // subject and no topic is asking what the thing IS. A shape rather than a
    // word, which is why removing `what` from the lexicon did not lose it.
    bool aboutThis = r.secondPerson;
    for (const auto &t : tokens)
      if (t == "this" || t == "that" || t == "thing")
        aboutThis = true;
    if (r.question && aboutThis)
      r.intent = Intent::ExplainSelf;
    return r;
  }

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
  if (weight.count(Concept::Change)) add(Intent::Reshuffle, 4);
  if (weight.count(Concept::Quiet)) add(Intent::SetQuiet, 4);
  if (weight.count(Concept::Loud)) add(Intent::SetLoud, 4);
  if (weight.count(Concept::Identity)) add(Intent::ExplainSelf, 3);
  if (weight.count(Concept::Leave)) add(Intent::Leave, 3);

  // A drum name on its own is the ambiguity the corpus is full of: "tell me
  // about your kick" could be the part or the sound. Push both, equally, and
  // let the margin rule decide there is no answer.
  if (weight.count(Concept::Drum) && !weight.count(Concept::Part) &&
      !weight.count(Concept::Tone)) {
    add(Intent::DescribePart, 3);
    add(Intent::DescribeSound, 3);
  }

  // "stop talking" is quiet, not leave; "stop" plus nothing else is quiet too.
  if (weight.count(Concept::Quiet) && weight.count(Concept::Loud))
    add(Intent::SetQuiet, 3);

  // Negation flips the two settings, since "don't be quiet" and "be quiet"
  // share every content word and differ only here.
  if (r.negated) {
    if (weight.count(Concept::Quiet)) {
      score[Intent::SetQuiet] -= 6;
      add(Intent::SetLoud, 4);
    }
    if (weight.count(Concept::Loud)) {
      score[Intent::SetLoud] -= 6;
      add(Intent::SetQuiet, 4);
    }
  }

  // Asking is not instructing. "what are you playing" wants the part; "shake"
  // wants a reroll; a question containing a change word is usually still a
  // question about something else.
  if (r.question && weight.count(Concept::Change) &&
      (weight.count(Concept::Part) || weight.count(Concept::Tone) ||
       weight.count(Concept::Key) || weight.count(Concept::Chart)))
    score[Intent::Reshuffle] -= 4;

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

  // What we cannot do. A question about how it SOUNDS to the listener is not
  // a question about our patch, and pretending otherwise is the dishonest
  // answer -- so these do not score at all and fall to the floor.
  if (weight.count(Concept::Hear) && !weight.count(Concept::Tone) &&
      !weight.count(Concept::Part))
    return r;

  // "how many beats in a bar" carries both TEMPO and CHART, and is a question
  // about duration. The leading "how many"/"how long" is what says so.
  if (tokens.size() >= 2 && tokens[0] == "how" &&
      (tokens[1] == "many" || tokens[1] == "long") &&
      weight.count(Concept::Tempo))
    add(Intent::ReportTempo, 3);

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
  if (bestScore < 3)
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

} // namespace BotLanguage
