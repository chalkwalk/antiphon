#include "BotNames.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace BotNames {

namespace {

std::string lowered(const std::string &s) {
  std::string out = s;
  for (auto &c : out)
    c = (char)std::tolower((unsigned char)c);
  return out;
}

// Would this name be ambiguous in a room already containing these people?
//
// Ambiguous means either direction: a participant called `delvo` collides with
// the handle, and so does one called `delvoto`, because a scan for a name
// anywhere in a message cannot tell which was meant. Cheap to avoid at join,
// awkward to live with afterwards.
bool collides(const std::string &name, const std::vector<std::string> &taken) {
  const auto candidate = lowered(name);
  for (const auto &other : taken) {
    const auto theirs = lowered(other);
    if (theirs.find(candidate) != std::string::npos ||
        candidate.find(lowered(handleOf(other))) != std::string::npos)
      return true;
  }
  return false;
}

// Levenshtein, on names of four to six letters, so the obvious implementation
// is the right one.
int editDistance(const std::string &a, const std::string &b) {
  std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j)
    prev[j] = (int)j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = (int)i;
    for (std::size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1,
                         prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    prev = cur;
  }
  return prev[b.size()];
}

// The rime: the vowel onward. Two names that rhyme are near-homophones aloud,
// which is the one thing a spoken address cannot afford even though the address
// itself is typed -- somebody reads the roster out, or a screen reader does.
std::string rimeOf(const std::string &name) {
  const auto lower = lowered(name);
  const auto at = lower.find_first_of("aeiou");
  return at == std::string::npos ? lower : lower.substr(at);
}

// Whether two names can be in the same band.
//
// All three of these are constraints on the BAND rather than on the pool, which
// is why the pool can hold names that conflict with each other: `Vurn` rhymes
// with `Mirn` and `Pemo` starts like `Pundo`, and each is perfectly usable in a
// line-up that does not contain the other.
bool compatible(const std::string &a, const std::string &b) {
  const auto x = lowered(a), y = lowered(b);
  if (x.empty() || y.empty())
    return false;
  if (x[0] == y[0])
    return false;                       // a shared initial defeats near-miss matching
  if (editDistance(x, y) < 2)
    return false;                       // one typo must not reach the other
  return rimeOf(x) != rimeOf(y);        // and they must not rhyme
}

} // namespace

std::vector<std::string> bandFor(int count, std::uint32_t seed,
                                 const std::vector<std::string> &taken) {
  std::vector<std::string> out;
  if (count <= 0)
    return out;

  const auto &names = pool();

  // A rotation rather than a shuffle. The pool is small and the point is only
  // that two rooms with different seeds do not field the same four players in
  // the same order -- not that the assignment is unguessable. A rotation also
  // keeps the pool's ordering, so a collision skips to the next name rather
  // than to an arbitrary one, which makes a failure easy to read.
  const std::uint32_t start =
      names.empty() ? 0u : (seed | 1u) % (std::uint32_t)names.size();

  for (std::size_t step = 0; step < names.size() && (int)out.size() < count;
       ++step) {
    const auto &name = names[(start + step) % names.size()];
    if (collides(name, taken))
      continue;
    if (std::find(out.begin(), out.end(), name) != out.end())
      continue;

    bool ok = true;
    for (const auto &already : out)
      if (!compatible(name, already)) {
        ok = false;
        break;
      }
    if (ok)
      out.push_back(name);
  }

  // If the room is so full of collisions that the pool cannot fill a band under
  // the constraints, take whatever is left and accept the awkwardness. A band
  // with two similar names is better than no band, and section 5's degraded
  // path -- the short handle withdrawn, the full username still working -- is
  // exactly what covers it.
  for (std::size_t i = 0; (int)out.size() < count && i < names.size(); ++i)
    if (std::find(out.begin(), out.end(), names[i]) == out.end())
      out.push_back(names[i]);

  return out;
}

} // namespace BotNames
