#include <JuceHeader.h>

#include "AccessibilityAudit.h"

namespace {

using namespace AccessibilityAudit;

Node control(const juce::String &name, const juce::String &kind,
             const juce::String &description = "does a thing") {
  Node n;
  n.name = name;
  n.kind = kind;
  n.description = description;
  n.focusable = true;
  return n;
}

int countOf(const std::vector<Finding> &f, Issue issue) {
  int n = 0;
  for (const auto &x : f)
    if (x.issue == issue)
      ++n;
  return n;
}

class AccessibilityAuditTests : public juce::UnitTest {
public:
  AccessibilityAuditTests()
      : juce::UnitTest("AccessibilityAudit", "AccessibilityAudit") {}

  void runTest() override {
    beginTest("an unnamed control says which one it is");
    {
      // The whole difficulty with MISSING NAME is that the thing with no name
      // is the thing you then have to find. "juce::TextEditor" is no help in a
      // dialog holding four of them, so the component's own name is used as a
      // locator -- it is not an accessible name, only a signpost to the source.
      AccessibilityAudit::Node root;
      root.kind = "ServerBrowserDialog";
      root.name = "Connect to a Ninjam server";

      AccessibilityAudit::Node field;
      field.kind = "juce::TextEditor";
      field.locator = "hostInput";
      field.focusable = true;
      root.children.push_back(field);

      const auto findings = AccessibilityAudit::run(root);
      expectEquals((int)findings.size(), 1);
      expect(findings[0].path.contains("hostInput"),
             "the report must name which control is unnamed");
      expect(findings[0].path.contains("Connect to a Ninjam server"),
             "and which root it lives under");
    }

    beginTest("a locator never stands in for an accessible name");
    {
      // A component name is invisible to a screen reader. Having one must not
      // make a control look annotated.
      AccessibilityAudit::Node root;
      root.kind = "Panel";
      root.name = "Panel";

      AccessibilityAudit::Node field;
      field.kind = "juce::TextEditor";
      field.locator = "hostInput";
      field.description = "Host name";
      field.focusable = true;
      root.children.push_back(field);

      const auto findings = AccessibilityAudit::run(root);
      expectEquals((int)findings.size(), 1,
                   "still unnamed as far as a reader is concerned");
      expect(findings[0].issue == AccessibilityAudit::Issue::MissingName);
    }

    beginTest("coverage counts what was examined");
    {
      // A verdict of "no issues" is indistinguishable from having examined
      // nothing, which is how the connect dialog stayed outside the tree
      // unnoticed. The counts are what make the verdict checkable.
      AccessibilityAudit::Node root;
      root.kind = "Editor";
      root.focusable = true;

      AccessibilityAudit::Node named;
      named.kind = "TextButton";
      named.name = "Connect";
      named.description = "Join the server";
      named.focusable = true;

      AccessibilityAudit::Node hidden;
      hidden.kind = "Decoration";
      hidden.ignored = true;

      root.children.push_back(named);
      root.children.push_back(hidden);

      AccessibilityAudit::Coverage cov;
      AccessibilityAudit::measure(root, cov);
      expectEquals(cov.nodes, 3, "root plus both children");
      expectEquals(cov.focusable, 2, "root and the button");
      expectEquals(cov.ignored, 1);

      // Accumulates, so several roots sum into one figure.
      AccessibilityAudit::measure(root, cov);
      expectEquals(cov.nodes, 6, "a second root adds to the same total");
    }

    beginTest("the report states its coverage");
    {
      AccessibilityAudit::Coverage cov;
      cov.roots = 2;
      cov.nodes = 41;
      cov.focusable = 17;
      const auto text = AccessibilityAudit::format({}, cov);
      expect(text.contains("no issues found"));
      expect(text.contains("2 root(s)"), "the verdict must carry its scope");
      expect(text.contains("41 component(s)"));
      expect(text.contains("17 reachable"));
    }

    beginTest("a fully annotated tree reports nothing");
    {
      Node root;
      root.kind = "Editor";
      root.children.push_back(control("Mute, Instrument", "TextButton"));
      root.children.push_back(control("Solo, Instrument", "TextButton"));
      const auto f = run(root);
      expect(f.empty(), format(f));
    }

    beginTest("an unnamed focusable control is caught");
    {
      Node root;
      root.kind = "Editor";
      root.children.push_back(control("", "TextButton"));
      const auto f = run(root);
      expectEquals(countOf(f, Issue::MissingName), 1);
    }

    beginTest("a single-glyph name counts as no name");
    {
      // This is the state the UI was actually in: buttons labelled "M", "S",
      // "TX", "+" and "-" announce as those characters and mean nothing.
      Node root;
      root.kind = "Editor";
      for (auto glyph : {"M", "S", "TX", "+", "-"})
        root.children.push_back(control(glyph, "TextButton"));
      const auto f = run(root);
      expectEquals(countOf(f, Issue::MissingName), 5);
    }

    beginTest("controls that cannot be focused are not required to be named");
    {
      // Decorative labels and painted chrome are not reader targets.
      Node root;
      root.kind = "Editor";
      Node decoration;
      decoration.kind = "Label";
      decoration.focusable = false;
      root.children.push_back(decoration);
      expect(run(root).empty());
    }

    beginTest("an explicitly ignored subtree is skipped whole");
    {
      Node root;
      root.kind = "Editor";
      Node hidden;
      hidden.kind = "Container";
      hidden.ignored = true;
      hidden.children.push_back(control("", "TextButton"));
      root.children.push_back(hidden);
      expect(run(root).empty(), "an ignored parent must hide its children too");
    }

    beginTest("two siblings announcing the same thing are ambiguous");
    {
      Node strip;
      strip.kind = "LocalChannelStrip";
      strip.children.push_back(control("Mute", "TextButton"));
      strip.children.push_back(control("Mute", "TextButton"));
      const auto f = run(strip);
      expectEquals(countOf(f, Issue::DuplicateName), 1);
    }

    beginTest("the same name in different containers is fine");
    {
      // Every channel strip has a Mute. That is not ambiguous -- the strip is
      // the context. Comparing names globally would drown the report in noise
      // and train everyone to ignore it.
      Node root;
      root.kind = "Editor";
      for (int i = 0; i < 3; ++i) {
        Node strip;
        strip.kind = "LocalChannelStrip";
        strip.name = "Channel " + juce::String(i + 1);
        strip.children.push_back(control("Mute", "TextButton"));
        root.children.push_back(strip);
      }
      const auto f = run(root);
      expectEquals(countOf(f, Issue::DuplicateName), 0);
    }

    beginTest("a named control with no description is reported separately");
    {
      Node root;
      root.kind = "Editor";
      root.children.push_back(control("Transmit", "TextButton", ""));
      const auto f = run(root);
      expectEquals(countOf(f, Issue::MissingDescription), 1);
      expectEquals(countOf(f, Issue::MissingName), 0);
    }

    beginTest("an unnamed control is not also reported as undescribed");
    {
      // One control, one actionable finding. Reporting both would double-count
      // and make the summary numbers misleading.
      Node root;
      root.kind = "Editor";
      root.children.push_back(control("", "TextButton", ""));
      const auto f = run(root);
      expectEquals((int)f.size(), 1);
    }

    beginTest("nesting is searched to the bottom");
    {
      Node root;
      root.kind = "Editor";
      Node viewport;
      viewport.kind = "Viewport";
      Node strip;
      strip.kind = "LocalChannelStrip";
      strip.name = "Channel 1";
      strip.children.push_back(control("", "Slider"));
      viewport.children.push_back(strip);
      root.children.push_back(viewport);
      const auto f = run(root);
      expectEquals(countOf(f, Issue::MissingName), 1);
      expect(f[0].path.contains("Slider"),
             "the report must locate the control: " + f[0].path);
    }

    beginTest("the report names the issue and the path");
    {
      Node root;
      root.kind = "Editor";
      root.children.push_back(control("", "TextButton"));
      const auto text = format(run(root));
      expect(text.contains("MISSING NAME"), text);
      expect(text.contains("TextButton"), text);
    }

    beginTest("a clean report says so");
    {
      expect(format({}).contains("no issues found"));
    }
  }
};

static AccessibilityAuditTests accessibilityAuditTests;

} // namespace
