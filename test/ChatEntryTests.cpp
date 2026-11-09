#include <JuceHeader.h>

#include "ChatEntry.h"

namespace {

class ChatEntryTests : public juce::UnitTest {
public:
  ChatEntryTests() : juce::UnitTest("ChatEntry", "ChatEntry") {}

  void runTest() override {
    using ChatEntry::completeName;
    using ChatEntry::History;

    beginTest("Up walks back through what was sent, newest first");
    {
      History h;
      h.add("one");
      h.add("two");
      h.add("three");
      expectEquals(h.older("").value_or("?"), juce::String("three"));
      expectEquals(h.older("three").value_or("?"), juce::String("two"));
      expectEquals(h.older("two").value_or("?"), juce::String("one"));
      expect(!h.older("one").has_value(), "nothing older than the oldest");
    }

    beginTest("Down past the newest gives back what you were typing");
    {
      History h;
      h.add("sent");
      expectEquals(h.older("half a thou").value_or("?"), juce::String("sent"));
      expectEquals(h.newer().value_or("?"), juce::String("half a thou"));
      expect(!h.newer().has_value(), "not walking any more");
    }

    beginTest("Down without walking does nothing");
    {
      History h;
      h.add("sent");
      expect(!h.newer().has_value());
    }

    beginTest("an empty history leaves the field alone");
    {
      History h;
      expect(!h.older("typing").has_value());
    }

    beginTest("empty lines and a repeated line are stored once");
    {
      History h;
      h.add("again");
      h.add("again");
      h.add("");
      expectEquals(h.size(), 1);
    }

    beginTest("sending ends the walk, so Up starts from the newest again");
    {
      History h;
      h.add("a");
      h.add("b");
      (void)h.older("");
      (void)h.older("b");
      h.add("c");
      expectEquals(h.older("").value_or("?"), juce::String("c"));
    }

    beginTest("history is bounded");
    {
      History h;
      for (int i = 0; i < History::kMaxEntries + 10; ++i)
        h.add(juce::String(i));
      expectEquals(h.size(), History::kMaxEntries);
    }

    const juce::StringArray room{"Ravo", "Rhea", "jackjl", "Tutor"};

    beginTest("one match completes the name and addresses the message");
    {
      const auto c = completeName("ra", 2, room);
      expect(c.applies);
      expectEquals(c.text, juce::String("Ravo: "));
      expectEquals(c.caret, 6);
      expectEquals(c.candidates.size(), 1);
    }

    beginTest("mid-message, a name is followed by a space, not a colon");
    {
      const auto c = completeName("ask tu", 6, room);
      expectEquals(c.text, juce::String("ask Tutor "));
      expectEquals(c.caret, 10);
    }

    beginTest("the word at the caret is completed, and the rest kept");
    {
      const auto c = completeName("hey j there", 5, room);
      expectEquals(c.text, juce::String("hey jackjl there"));
      expectEquals(c.caret, 10);
    }

    beginTest("several matches complete as far as they agree, and are listed");
    {
      const auto c = completeName("R", 1, room);
      expect(c.applies);
      expectEquals(c.text, juce::String("R"));
      expectEquals(c.candidates.joinIntoString(","), juce::String("Ravo,Rhea"));

      const juce::StringArray close{"drummer1", "drummer2"};
      const auto d = completeName("dr", 2, close);
      expectEquals(d.text, juce::String("drummer"));
      expectEquals(d.caret, 7);
    }

    beginTest("Tab is not taken when there is nothing to complete");
    {
      expect(!completeName("", 0, room).applies, "empty field");
      expect(!completeName("hello ", 6, room).applies, "after a space");
      expect(!completeName("xyz", 3, room).applies, "no such name");
    }

    beginTest("an anonymous login is typed without its address");
    {
      expectEquals(ChatEntry::typedName("jackjl@98.42.3.x"),
                   juce::String("jackjl"));
      expectEquals(ChatEntry::typedName("Ravo"), juce::String("Ravo"));
      expectEquals(ChatEntry::typedName("Ravo[keys-bot]"),
                   juce::String("Ravo"));
      expectEquals(ChatEntry::typedName("[odd]"), juce::String("[odd]"));
    }
  }
};

static ChatEntryTests chatEntryTests;

} // namespace
