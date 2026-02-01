#pragma once

#include <JuceHeader.h>
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
  juce::String name;        // accessible title -- what gets announced
  juce::String description; // longer explanation, if any
  juce::String kind;        // component class name, to locate it in source
  bool focusable = false;   // reachable by keyboard, so a reader will land here
  bool ignored = false;     // explicitly hidden from the accessibility tree
  std::vector<Node> children;
};

enum class Issue {
  MissingName,       // announced as nothing, or as its raw button glyph
  DuplicateName,     // two controls a reader cannot tell apart
  MissingDescription // reachable and named, but nothing explains what it does
};

struct Finding {
  Issue issue = Issue::MissingName;
  juce::String path;   // "Editor / LocalChannelStrip / TextButton"
  juce::String detail;
};

// Names that are technically present but useless when spoken alone. A reader
// announcing "M" or "+" tells the user nothing, which is the state the UI was
// in before any of this existed.
inline bool isUninformativeName(const juce::String &name) {
  const auto trimmed = name.trim();
  if (trimmed.isEmpty()) return true;
  if (trimmed.length() <= 2) return true;
  return trimmed == "..." || trimmed == "Connect...";
}

std::vector<Finding> run(const Node &root);
juce::String format(const std::vector<Finding> &findings);
juce::String describe(Issue issue);

} // namespace AccessibilityAudit
