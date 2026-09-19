#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <vector>

// Finds controls a screen reader would announce as nothing, or as the same
// thing as their neighbour.
//
// The whole point is that this is checkable without a screen reader. Nobody on
// this project can run VoiceOver against every build, and on Linux JUCE has no
// accessibility backend at all, so a reader-based check would pass vacuously
// there while the annotations rotted.
//
// Deliberately free of juce_gui_basics: it walks a plain tree of `Node`, so the
// rules can be unit-tested in the headless test target, which links only
// juce_core and friends. `buildAuditTree()` in AccessibilityTree.h adapts a live
// juce::Component into this shape.

namespace AccessibilityAudit {

struct Node {
  // What a reader ANNOUNCES -- not `Component::getTitle()`, which for some
  // components is a string nobody ever hears. `buildAuditTree` resolves it the
  // way JUCE's handlers do; see the note there.
  juce::String name;
  juce::String description; // longer explanation, if any

  // What a sighted user READS on the control, where it has visible text at
  // all. Empty for anything that does not, which is most things.
  //
  // Kept beside the name because the defect is their DISAGREEMENT, and an
  // audit holding only one of them cannot see it.
  juce::String visibleLabel;
  juce::String kind;        // component class name, to locate it in source
  // Component::getName(), which is NOT the accessible name. Only used to say
  // *which* one when there is no accessible name -- "juce::TextEditor" is no
  // help in a dialog holding four of them.
  juce::String locator;
  bool focusable = false; // reachable by keyboard, so a reader will land here
  bool ignored = false;   // explicitly hidden from the accessibility tree
  std::vector<Node> children;
};

enum class Issue {
  MissingName,        // announced as nothing, or as its raw button glyph
  DuplicateName,      // two controls a reader cannot tell apart
  MissingDescription, // reachable and named, but nothing explains what it does
  NameHidesLabel      // spoken name does not contain the visible one
};

struct Finding {
  Issue issue = Issue::MissingName;
  juce::String path; // "Editor / LocalChannelStrip / TextButton"
  juce::String detail;
};

// Names that are technically present but useless when spoken alone. A reader
// announcing "M" or "+" tells the user nothing, which is the state the UI was
// in before any of this existed.
inline bool isUninformativeName(const juce::String &name) {
  const auto trimmed = name.trim();
  if (trimmed.isEmpty())
    return true;
  if (trimmed.length() <= 2)
    return true;
  return trimmed == "..." || trimmed == "Connect...";
}

// Whether a visible label is a WORD rather than a glyph.
//
// The name-matches-label rule only makes sense for text. "+" announced as "Add
// channel" and "M" as "Mute (M)" are correct and deliberate -- an icon is not a
// label, and demanding the glyph appear in the spoken name would forbid the
// thing that makes those buttons usable at all.
inline bool isWordLabel(const juce::String &label) {
  const auto trimmed = label.trim();
  if (trimmed.length() <= 2)
    return false;
  return trimmed.containsAnyOf(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

// Letters, digits and single spaces, lowercased.
//
// So that "Connect..." is found inside "Connect (Ctrl O)" and the ellipsis does
// not make a correct pairing look like a defect.
inline juce::String normaliseForMatch(const juce::String &s) {
  juce::String out;
  for (auto c : s) {
    if (juce::CharacterFunctions::isLetterOrDigit(c))
      out << juce::String::charToString(c).toLowerCase();
    else if (!out.endsWithChar(' '))
      out << ' ';
  }
  return out.trim();
}

// WCAG 2.5.3, Label in Name: the accessible name must contain the visible one.
//
// A name may say MORE than the label -- "Sync" announced as "Sync (Ctrl S)" is
// the point of the annotation. What it must not do is say something ELSE,
// because then the two disagree and only one of them is spoken.
inline bool nameContainsLabel(const juce::String &name,
                              const juce::String &visibleLabel) {
  if (!isWordLabel(visibleLabel))
    return true;
  const auto n = normaliseForMatch(name);
  const auto l = normaliseForMatch(visibleLabel);
  return l.isEmpty() || n.contains(l);
}

// What the audit actually looked at.
//
// A report saying "no issues found" cannot be told apart from a report that
// examined nothing -- and the connect dialog spent its whole life outside the
// tree without that being visible. Coverage travels with the verdict, so the
// number can be checked rather than trusted (PRINCIPLES section 5).
struct Coverage {
  int roots = 0;
  int nodes = 0;     // every component walked
  int focusable = 0; // the ones a reader can actually land on
  int ignored = 0;   // explicitly hidden from the accessibility tree

  // A running hash of what the tree SAYS, not just how big it is.
  //
  // Counting components tells two states apart when they differ in which
  // controls exist. It cannot tell them apart when the difference is what one
  // control announces -- and a control reused across states is exactly where
  // the interesting defects live, because that is the only way a name can go
  // stale. Without this, a state that re-purposes the same button reads as
  // "never reached" and its verdict is thrown away.
  std::uint64_t spoken = 0;
};

// Accumulates into `out`, so several roots sum into one figure.
void measure(const Node &root, Coverage &out);

std::vector<Finding> run(const Node &root);
juce::String format(const std::vector<Finding> &findings);
juce::String format(const std::vector<Finding> &findings, const Coverage &);
juce::String describe(Issue issue);

} // namespace AccessibilityAudit
