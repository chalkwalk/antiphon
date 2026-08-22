#include "../src/MusicalKey.h"
#include <chalkwalk/music/Text.h>
#include <JuceHeader.h>

// The tag, and nothing else.
//
// Reading and writing a key -- "D minor", its spelling, its scale -- moved to
// `chalkwalk::music::Notation` and is tested there. What is left is the part
// that is Antiphon's: how a key travels over Ninjam chat, which is a protocol
// decision rather than a musical one.

namespace {

class KeyTagTests : public juce::UnitTest {
public:
  KeyTagTests() : juce::UnitTest("KeyTag", "music") {}

  void runTest() override {
    using namespace MusicalKey;

    beginTest("only the tagged form is picked up from a chat line");
    {
      expect(!parseTagged("lets play in D minor").valid,
             "free text must not set the key");
      expect(!parseTagged("key: D minor").valid, "the bracket is required");

      const auto tagged = parseTagged("[key: D minor]");
      expect(tagged.valid);
      expectEquals(displayName(tagged), std::string("D minor"));
    }

    beginTest("a tag is found wherever it sits in the line");
    {
      // It arrives inside a topic that may say other things too.
      for (const auto *s :
           {"[key: Dm]", "jam night -- [key: Dm] -- all welcome",
            "trailing [key: Dm]", "[KEY: Dm]"})
        expectEquals(displayName(parseTagged(s)), std::string("D minor"),
                     std::string("failed on: ") + s);
    }

    beginTest("a malformed tag yields no key rather than a wrong one");
    {
      for (const auto *s : {"[key:", "[key: ]", "[key: bananas]", "[key Dm]",
                            "[key: Dm", "]key: Dm["})
        expect(!parseTagged(s).valid,
               std::string("wrongly read as a key: ") + s);
    }

    beginTest("what we send is what we parse");
    {
      // The round trip that makes the convention work between two clients.
      for (const auto *s : {"Dm", "F# Dorian", "Bb major", "C Locrian"}) {
        const auto original = parseName(s);
        expect(original.valid);
        const auto message = buildTagged(original);
        expect(chalkwalk::music::text::startsWith(message, "[key:"));
        const auto received = parseTagged(message);
        expect(received.valid,
               "did not survive the round trip: " + juce::String(message));
        expect(received == original,
               "changed in the round trip: " + juce::String(message));
      }
    }

    beginTest("an invalid key builds and displays as nothing");
    {
      Key none;
      expect(buildTagged(none).empty());
      expect(displayName(none).empty());
      expect(scaleNotes(none).empty());
    }

    beginTest("a key announcement has two forms, and only one is sayable");
    {
      // The tag, matched anywhere, so it can ride in the server topic.
      expect(parseAnnouncement("[key: D minor]").valid);
      expect(parseAnnouncement("blues jam [key: D minor] all welcome").valid);
      expectEquals(displayName(parseAnnouncement("nice [key: G minor] one")),
                   std::string("G minor"));

      // The command form, line-leading only.
      expectEquals(displayName(parseAnnouncement("/key G minor")),
                   std::string("G minor"));
      expectEquals(displayName(parseAnnouncement("  /key Dm  ")),
                   std::string("D minor"));
      expect(parseAnnouncement("/KEY Am").valid, "case is not the point");

      // ...and THAT is the whole reason the second form exists. A bot must be
      // able to say how the key is set without setting it, which it can never
      // do with the tag, because the tag is matched anywhere.
      const auto advice = "the key is the room's. type \"" +
                          announcementAdvice(parseName("G minor")) +
                          "\" to change it.";
      expect(!parseAnnouncement(advice).valid,
             "a bot explaining the key would have set it: " + advice);

      // The same sentence built round the tag DOES set it -- kept as a test so
      // nobody reintroduces the tag into reply text.
      expect(parseAnnouncement("type \"[key: G minor]\" to change it").valid,
             "the tag really is unsayable; this is why announcementAdvice "
             "exists");

      // Not a key announcement at all.
      for (const char *no : {"what key are we in", "/keys are broken",
                             "i said /key earlier", "key: G minor"})
        expect(!parseAnnouncement(no).valid, juce::String(no) + " set the key");
    }
  }
};

static KeyTagTests keyTagTests;

} // namespace
