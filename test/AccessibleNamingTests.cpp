#include <JuceHeader.h>

#include "AccessibleNaming.h"

namespace {

class AccessibleNamingTests : public juce::UnitTest {
public:
  AccessibleNamingTests()
      : juce::UnitTest("AccessibleNaming", "AccessibleNaming") {}

  void runTest() override {
    beginTest("a control adopts the label attached to it");
    {
      juce::Component root;
      juce::ComboBox box;
      juce::Label label({}, "Output:");
      root.addAndMakeVisible(box);
      root.addAndMakeVisible(label);
      label.attachToComponent(&box, true);

      AccessibleNaming::adoptLabelNames(root);
      expectEquals(box.getTitle(), juce::String("Output"),
                   "the trailing colon is not spoken");
      expect(box.getDescription().isNotEmpty());
    }

    beginTest("a name it already had is never overwritten");
    {
      juce::Component root;
      juce::ComboBox box;
      box.setTitle("Output device");
      juce::Label label({}, "Output:");
      root.addAndMakeVisible(box);
      root.addAndMakeVisible(label);
      label.attachToComponent(&box, true);

      AccessibleNaming::adoptLabelNames(root);
      expectEquals(box.getTitle(), juce::String("Output device"),
                   "a deliberate name outranks a visual one");
    }

    beginTest("it reaches controls nested any depth down");
    {
      juce::Component root, middle;
      juce::ComboBox box;
      juce::Label label({}, "Sample rate:");
      root.addAndMakeVisible(middle);
      middle.addAndMakeVisible(box);
      middle.addAndMakeVisible(label);
      label.attachToComponent(&box, true);

      AccessibleNaming::adoptLabelNames(root);
      expectEquals(box.getTitle(), juce::String("Sample rate"));
    }

    beginTest("an unattached or empty label names nothing");
    {
      juce::Component root;
      juce::ComboBox box;
      juce::Label loose({}, "Not attached");
      juce::Label blank({}, "   ");
      root.addAndMakeVisible(box);
      root.addAndMakeVisible(loose);
      root.addAndMakeVisible(blank);
      blank.attachToComponent(&box, true);

      AccessibleNaming::adoptLabelNames(root);
      expect(box.getTitle().isEmpty(),
             "whitespace is not a name; leave it for the audit to catch");
    }
  }
};

static AccessibleNamingTests accessibleNamingTests;

} // namespace
