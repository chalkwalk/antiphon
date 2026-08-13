#include "BotNames.h"

#include <algorithm>
#include <cctype>

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
    // And not against each other, which matters once the pool is being skipped
    // through rather than taken in order.
    if (std::find(out.begin(), out.end(), name) != out.end())
      continue;
    out.push_back(name);
  }

  // If the room is so full of collisions that the pool runs out, fall back to
  // the pool in order and accept the ambiguity. A band with an awkward name is
  // better than no band, and section 5's degraded path -- the short handle is
  // withdrawn, the full username still works -- is exactly what covers this.
  for (std::size_t i = 0; (int)out.size() < count && i < names.size(); ++i)
    if (std::find(out.begin(), out.end(), names[i]) == out.end())
      out.push_back(names[i]);

  return out;
}

} // namespace BotNames
