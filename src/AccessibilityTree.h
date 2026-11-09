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
  if (char *d =
          abi::__cxa_demangle(typeid(c).name(), nullptr, nullptr, &status)) {
    juce::String s(d);
    std::free(d);
    return s;
  }
#endif
  return {typeid(c).name()};
}

// What the READER will announce, and what a sighted user reads, resolved the
// way JUCE's own handlers resolve them.
//
// Asking `getAccessibilityHandler()` would be the ground truth and cannot be
// used: it returns null whenever `getWindowHandle()` is null, which is always
// here -- the audit runs headless, off the desktop, on a platform with no
// accessibility backend at all. So the rules are reproduced rather than
// queried, and there are only two that differ from the default.
//
// A LABEL never announces its title. `LabelAccessibilityHandler::getTitle()`
// returns `label.getText()` unconditionally, so `setTitle` on a Label is dead
// code -- and an audit reading `getTitle()` was grading a string no user could
// ever hear. `dawOnlyNote` reads "DAW only" on screen and carried the title
// "Plugin only", and both the audit and the reader were satisfied, separately,
// by different text.
//
// A BUTTON announces its title when it has one and its text otherwise
// (`ButtonAccessibilityHandler` does not override `getTitle`, so the component
// title wins). That is what let one chip button announce "Cast vote" while
// reading "Set key".
inline void resolveAccessibleText(const juce::Component &c, Node &n) {
  if (const auto *label = dynamic_cast<const juce::Label *>(&c)) {
    n.visibleLabel = label->getText();
    n.name = label->getText();
    return;
  }

  if (const auto *button = dynamic_cast<const juce::Button *>(&c)) {
    n.visibleLabel = button->getButtonText();
    n.name = c.getTitle().isNotEmpty() ? c.getTitle() : button->getButtonText();
    return;
  }

  n.name = c.getTitle();
}

inline Node buildAuditTree(const juce::Component &c) {
  Node n;
  resolveAccessibleText(c, n);
  n.description = c.getDescription();
  n.kind = classNameOf(c);
  n.locator = c.getName();
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

// Audit several roots as one report.
//
// A window is a root of its own. The connect dialog is a real top-level window
// with its own peer -- which is exactly why text entry works in it (DESIGN.md
// section 10.8) -- so walking the editor never reaches it, and every control in
// it went unaudited. Anything reachable but rooted elsewhere has to be passed
// in explicitly.
inline juce::String
auditReport(const std::vector<const juce::Component *> &roots) {
  std::vector<Finding> all;
  Coverage cov;
  for (const auto *c : roots) {
    if (c == nullptr)
      continue;
    const auto tree = buildAuditTree(*c);
    ++cov.roots;
    measure(tree, cov);
    auto found = run(tree);
    all.insert(all.end(), found.begin(), found.end());
  }
  return format(all, cov);
}

} // namespace AccessibilityAudit
