#include <JuceHeader.h>

#include "AccessibleNaming.h"
#include "Announcer.h"

namespace {

class AccessibleNamingTests : public juce::UnitTest {
public:
  AccessibleNamingTests()
      : juce::UnitTest("AccessibleNaming", "AccessibleNaming") {}

  void runTest() override {
    beginTest("turning speech off still says so");
    {
      // The one announcement that cannot be made at the setting it announces.
      // A reader told "Announcements: Off" by a channel already switched off
      // hears nothing at all, and a control whose confirmation is the thing it
      // suppresses is one nobody can tell they have used.
      Announcer a;
      a.setVerbosity(Announcer::Verbosity::Off);
      expect(Announcer::verbosityName(Announcer::Verbosity::Off) == "Off");
      expect(Announcer::verbosityName(Announcer::Verbosity::Important) ==
             "Important only");
      expect(Announcer::verbosityName(Announcer::Verbosity::All) == "All");
    }

    beginTest("the quiet tier is what a busy room fills with");
    {
      // `important` does not mean "interesting", it means "aimed at you".
      // Everything the app says in response to a keystroke is important by
      // construction; the room's background -- ordinary chat, joins, parts,
      // server notices -- is what somebody turns down first, and the only
      // thing that HAS a quiet tier to be turned down to.
      Announcer a;
      a.setVerbosity(Announcer::Verbosity::Important);
      expect(a.getVerbosity() == Announcer::Verbosity::Important);

      a.setVerbosity(Announcer::Verbosity::All);
      expect(a.getVerbosity() == Announcer::Verbosity::All);
    }

    beginTest("important sync announcements are not dropped or cut off by "
              "room chatter");
    {
      Announcer a;
      a.setVerbosity(Announcer::Verbosity::All);

      // A topic or join line arrives at t=800 ms, and onJoin fires at t=1000 ms
      // (within the 400 ms chat gap). The important announcement must still go
      // through rather than being silently dropped.
      expect(a.shouldSay("Topic: Welcome", false, 800));
      expect(a.shouldSay(
          "Connected to ninbot.com. Room 110 BPM, your DAW is at 95: not "
          "synced, but you can hear the room. Set your DAW to 110 to sync.",
          true, 1000));

      // Background room chatter arriving shortly after the important
      // announcement is held off so the screen reader can finish reading the
      // tempo warning.
      expect(!a.shouldSay("guitarist joined", false, 1500));
      expect(!a.shouldSay("drummer: hello", false, 3000));
      expect(a.shouldSay("drummer: ready", false, 3600));
    }

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

    beginTest("formatToggle produces human-readable state announcements");
    {
      expectEquals(Announcer::formatToggle("Instrument", "transmit", true),
                   juce::String("Instrument transmit on"));
      expectEquals(Announcer::formatToggle("Instrument", "transmit", false),
                   juce::String("Instrument transmit off"));
      expectEquals(Announcer::formatToggle("Instrument", "mute", true),
                   juce::String("Instrument mute on"));
      expectEquals(Announcer::formatToggle("Instrument", "solo", true),
                   juce::String("Instrument solo on"));
      expectEquals(Announcer::formatToggle("alice", "mute", false),
                   juce::String("alice mute off"));
      expectEquals(Announcer::formatToggle("", "mute all", true),
                   juce::String("Mute all on"));
      expectEquals(Announcer::formatToggle("", "mute all", false),
                   juce::String("Mute all off"));
      expectEquals(Announcer::formatToggle("", "transmit", true),
                   juce::String("Transmit on"));
    }
  }
};

static AccessibleNamingTests accessibleNamingTests;

} // namespace
