#include "MusicalKey.h"

#include <chalkwalk/music/Text.h>

// The wire form only. The key itself is `chalkwalk::music::Notation`; see the
// header for why the tag is not.

namespace MusicalKey {

namespace text = chalkwalk::music::text;

Key parseTagged(const std::string &text) {
  const int open = text::indexOfIgnoreCase(text, tagPrefix());
  if (open < 0)
    return {};

  const size_t contentStart = (size_t)open + tagPrefix().size();
  const auto close = text.find(']', contentStart);
  if (close == std::string::npos)
    return {};

  return parseName(text.substr(contentStart, close - contentStart));
}

std::string buildTagged(const Key &key) {
  if (!key.valid)
    return {};
  return "[key: " + displayName(key) + "]";
}

Key parseAnnouncement(const std::string &line) {
  if (const auto tagged = parseTagged(line); tagged.valid)
    return tagged;

  // Line-leading only. Accepting `/key` anywhere would undo the whole point of
  // having a second form: a bot explaining it would trigger it again.
  const auto trimmed = text::trim(line);
  if (!text::startsWithIgnoreCase(trimmed, "/key "))
    return {};
  return parseName(trimmed.substr(5));
}

} // namespace MusicalKey
