#include "AccessibilityAudit.h"

#include <map>

namespace AccessibilityAudit {

namespace {

juce::String join(const juce::String &path, const Node &n) {
  const juce::String label = n.name.isNotEmpty() ? n.name : n.kind;
  return path.isEmpty() ? label : path + " / " + label;
}

// Siblings are compared within their nearest enclosing container rather than
// globally: "Mute" appearing once per channel strip is fine and expected, but
// two "Mute" buttons inside the SAME strip are genuinely ambiguous.
void walk(const Node &node, const juce::String &path,
          std::vector<Finding> &out) {
  if (node.ignored) return;

  std::map<juce::String, int> siblingNames;

  for (const auto &child : node.children) {
    if (child.ignored) continue;

    const juce::String childPath = join(path, child);

    if (child.focusable) {
      if (isUninformativeName(child.name)) {
        out.push_back({Issue::MissingName,
                       path.isEmpty() ? child.kind : path + " / " + child.kind,
                       child.name.isEmpty()
                           ? juce::String("no accessible name")
                           : "name \"" + child.name +
                                 "\" says nothing when spoken alone"});
      } else if (++siblingNames[child.name] > 1) {
        out.push_back({Issue::DuplicateName, childPath,
                       "a sibling already announces as \"" + child.name + "\""});
      }

      if (child.description.trim().isEmpty() &&
          !isUninformativeName(child.name)) {
        out.push_back({Issue::MissingDescription, childPath,
                       "reachable and named, but nothing explains what it does"});
      }
    }

    walk(child, childPath, out);
  }
}

} // namespace

std::vector<Finding> run(const Node &root) {
  std::vector<Finding> out;
  // The root itself is checked too, so a nameless top-level editor is caught.
  if (root.focusable && !root.ignored && isUninformativeName(root.name))
    out.push_back({Issue::MissingName, root.kind, "no accessible name"});
  walk(root, join({}, root), out);
  return out;
}

juce::String describe(Issue issue) {
  switch (issue) {
  case Issue::MissingName:
    return "MISSING NAME";
  case Issue::DuplicateName:
    return "DUPLICATE NAME";
  case Issue::MissingDescription:
    return "NO DESCRIPTION";
  }
  return "UNKNOWN";
}

juce::String format(const std::vector<Finding> &findings) {
  if (findings.empty())
    return "Accessibility audit: no issues found.\n";

  int names = 0, dupes = 0, descs = 0;
  for (const auto &f : findings) {
    if (f.issue == Issue::MissingName) ++names;
    else if (f.issue == Issue::DuplicateName) ++dupes;
    else ++descs;
  }

  juce::String s;
  s << "Accessibility audit: " << (int)findings.size() << " issue(s) -- "
    << names << " unnamed, " << dupes << " ambiguous, " << descs
    << " undescribed.\n\n";
  for (const auto &f : findings)
    s << "  [" << describe(f.issue) << "] " << f.path << "\n      " << f.detail
      << "\n";
  return s;
}

} // namespace AccessibilityAudit
