#pragma once

#include <JuceHeader.h>

#include <optional>

// What the chat field does with Up, Down and Tab, kept out of the component so
// it can be tested without one. The field itself is ChatInputEditor.

namespace ChatEntry {

// Messages you have sent, newest last, walked with Up and Down the way a shell
// walks its history. What you were typing when you first pressed Up is kept, and
// Down past the newest entry gives it back rather than an empty field.
class History {
public:
  static constexpr int kMaxEntries = 50;

  // Empty lines are not history, and sending the same line twice in a row
  // stores it once. Either way the walk starts again from the newest.
  void add(const juce::String &sent) {
    resetWalk();
    if (sent.isEmpty() ||
        (!entries.isEmpty() && entries[entries.size() - 1] == sent))
      return;
    entries.add(sent);
    if (entries.size() > kMaxEntries)
      entries.remove(0);
  }

  // The entry before the one showing, or nothing at the oldest. `current` is
  // what the field holds now; on the first step back it becomes the draft.
  std::optional<juce::String> older(const juce::String &current) {
    if (entries.isEmpty())
      return std::nullopt;
    if (position < 0) {
      draft = current;
      position = entries.size();
    }
    if (position == 0)
      return std::nullopt;
    return entries[--position];
  }

  // The entry after the one showing, and past the newest, the draft. Nothing
  // when not walking.
  std::optional<juce::String> newer() {
    if (position < 0)
      return std::nullopt;
    if (++position >= entries.size()) {
      resetWalk();
      return draft;
    }
    return entries[position];
  }

  void resetWalk() { position = -1; }
  int size() const { return entries.size(); }

private:
  juce::StringArray entries;
  juce::String draft;
  int position = -1; // -1: not walking
};

// What a name is typed as. Anonymous logins arrive as "name@1.2.3.x", and the
// practice room's bots as "Ravo[keys-bot]"; nobody types the address or the
// tag, and the bots answer to the bare name (chalkwalk-jambot BotAddress).
inline juce::String typedName(const juce::String &username) {
  auto name = username.upToFirstOccurrenceOf("@", false, false);
  if (name.endsWithChar(']') && name.indexOfChar('[') > 0)
    name = name.upToFirstOccurrenceOf("[", false, false);
  return name;
}

struct Completion {
  // False: Tab is not ours, and moves focus as it always does. That is the
  // case for an empty word or a word nobody's name starts with, so a keyboard
  // user can always Tab out of the field.
  bool applies = false;
  juce::String text;
  int caret = 0;
  // Every name the word could still become; one when the completion is whole.
  juce::StringArray candidates;
};

// Completes the word that ends at `caret` against `names` (as typed, see
// typedName), case-insensitively. One match completes in full: followed by
// ": " when it is the first word, which is how a message is addressed, and by a
// space elsewhere. Several matches complete as far as they agree and list the
// candidates, as a shell does; nothing cycles, so there is no state to keep.
inline Completion completeName(const juce::String &text, int caret,
                               const juce::StringArray &names) {
  Completion out;
  caret = juce::jlimit(0, text.length(), caret);
  int start = caret;
  while (start > 0 && !juce::CharacterFunctions::isWhitespace(text[start - 1]))
    --start;
  const juce::String word = text.substring(start, caret);
  if (word.isEmpty())
    return out;

  juce::StringArray matches;
  for (const auto &n : names)
    if (n.startsWithIgnoreCase(word) && !matches.contains(n))
      matches.add(n);
  if (matches.isEmpty())
    return out;
  matches.sortNatural();

  juce::String completed;
  if (matches.size() == 1) {
    // No second space when one already follows the caret.
    const bool spaceFollows =
        caret < text.length() &&
        juce::CharacterFunctions::isWhitespace(text[caret]);
    completed =
        matches[0] + (start == 0 ? ":" : "") + (spaceFollows ? "" : " ");
  } else {
    // As far as every candidate agrees, ignoring case, and keeping what was
    // typed when the candidates disagree about its case.
    int common = matches[0].length();
    for (const auto &m : matches) {
      int i = 0;
      while (i < common && i < m.length() &&
             juce::CharacterFunctions::toLowerCase(m[i]) ==
                 juce::CharacterFunctions::toLowerCase(matches[0][i]))
        ++i;
      common = i;
    }
    completed = common > word.length() ? matches[0].substring(0, common) : word;
  }

  out.applies = true;
  out.text = text.substring(0, start) + completed + text.substring(caret);
  out.caret = start + completed.length();
  out.candidates = matches;
  return out;
}

} // namespace ChatEntry
