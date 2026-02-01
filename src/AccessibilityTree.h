#pragma once

#include "AccessibilityAudit.h"
#include <JuceHeader.h>

#if defined(__GNUC__) || defined(__clang__)
#include <cstdlib>
#include <cxxabi.h>
#endif

// Adapts a live juce::Component tree into the plain shape AccessibilityAudit
// walks. Kept separate from the auditor so the rules stay testable in the
// headless test target, which does not link juce_gui_basics.

namespace AccessibilityAudit {

inline juce::String classNameOf(const juce::Component &c) {
#if defined(__GNUC__) || defined(__clang__)
  int status = 0;
  if (char *d = abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status)) {
    juce::String s(d);
    std::free(d);
    return s;
  }
#endif
  return juce::String(typeid(c).name());
}

inline Node buildAuditTree(const juce::Component &c) {
  Node n;
  n.name = c.getTitle();
  n.description = c.getDescription();
  n.kind = classNameOf(c);
  // A reader lands on anything that takes keyboard focus. Invisible and
  // disabled components are not reachable, so holding them to the same standard
  // would only produce noise -- the server browser's password field is hidden
  // until Anonymous is unticked, and is legitimately unreachable until then.
  n.focusable = c.getWantsKeyboardFocus() && c.isVisible() && c.isEnabled();
  n.ignored = !c.isAccessible();

  for (auto *child : c.getChildren())
    if (child != nullptr)
      n.children.push_back(buildAuditTree(*child));

  return n;
}

// Convenience for the Debug menu: audit a live component and return the report.
inline juce::String auditReport(const juce::Component &c) {
  return format(run(buildAuditTree(c)));
}

} // namespace AccessibilityAudit
