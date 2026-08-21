#include <JuceHeader.h>

#include "MusicalKey.h"

namespace {

using namespace MusicalKey;

class MusicalKeyTests : public juce::UnitTest {
public:
  MusicalKeyTests() : juce::UnitTest("MusicalKey", "MusicalKey") {}

  void runTest() override {
    beginTest("the shorthand people actually type");
    {
      // What gets typed in a jam is "Dm", not "D minor".
      const auto dm = parseName("Dm");
      expect(dm.valid);
      expectEquals(displayName(dm), std::string("D minor"));

      const auto d = parseName("D");
      expect(d.valid);
      expectEquals(displayName(d), std::string("D major"),
                   "a bare tonic means major");

      expectEquals(displayName(parseName("Bb")), std::string("Bb major"));
      expectEquals(displayName(parseName("Bbm")), std::string("Bb minor"));
      expectEquals(displayName(parseName("F#")), std::string("F# major"));
    }

    beginTest("a flat in second position is never a mode");
    {
      // "b" is the one character that could be either an accidental or the
      // start of a mode name. No mode begins with it, so the reading is
      // unambiguous -- but "Bm" must still be B minor, not B flat anything.
      const auto bFlat = parseName("Bb");
      const auto bMinor = parseName("Bm");
      expect(bFlat.valid && bMinor.valid);
      expectEquals(displayName(bFlat), std::string("Bb major"));
      expectEquals(displayName(bMinor), std::string("B minor"));
      expect(bFlat.tonic != bMinor.tonic, "Bb and B are different tonics");
    }

    beginTest("every mode round-trips through its own name");
    {
      for (const auto *name : {"major", "minor", "Ionian", "Dorian", "Phrygian",
                               "Lydian", "Mixolydian", "Aeolian", "Locrian"}) {
        const juce::String spelled = std::string("D ") + name;
        const auto key = parseName(spelled.toStdString());
        expect(key.valid, "did not parse: " + spelled);
        expectEquals(displayName(key), spelled.toStdString(),
                     "did not round-trip: " + spelled);
      }
    }

    beginTest(
        "minor and Aeolian stay distinct even though they are the same scale");
    {
      // Someone who typed "D minor" should be told "D minor" back. The scales
      // are identical; the words are not.
      expectEquals(displayName(parseName("D minor")), std::string("D minor"));
      expectEquals(displayName(parseName("D Aeolian")),
                   std::string("D Aeolian"));
      expectEquals(scaleNotes(parseName("D minor")),
                   scaleNotes(parseName("D Aeolian")),
                   "the notes must be the same even if the names are not");
    }

    beginTest("case and spacing do not matter");
    {
      for (const auto *s : {"dm", "DM", "D m", " Dm ", "d minor", "D MINOR"})
        expectEquals(displayName(parseName(s)), std::string("D minor"),
                     std::string("failed on: ") + s);
    }

    beginTest("the scale is spelled to match the tonic");
    {
      expectEquals(scaleNotes(parseName("D minor")),
                   std::string("D E F G A Bb C"));
      expectEquals(scaleNotes(parseName("C major")),
                   std::string("C D E F G A B"));
      // A mode is not just a relabelled major scale: F Dorian has four flats.
      expectEquals(scaleNotes(parseName("F Dorian")),
                   std::string("F G Ab Bb C D Eb"));
    }

    beginTest("prose is never a key");
    {
      // The whole reason the tagged form exists. Jamtaba's chord parser reads
      // "I AM TIRED ..." as a progression because it treats I and l as measure
      // separators -- that is in their own test suite. Guessing at prose gives
      // you a header that lies.
      for (const auto *s : {"I AM TIRED ...", "LETS TAKE A BREAK", "hello", "",
                            "   ", "H minor", "D quantum", "8", "Dmm"})
        expect(!parseName(s).valid,
               std::string("wrongly read as a key: ") + s);
    }

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
        expect(TextUtil::startsWith(message, "[key:"));
        const auto received = parseTagged(message);
        expect(received.valid, "did not survive the round trip: " + juce::String(message));
        expect(received == original, "changed in the round trip: " + juce::String(message));
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
      const auto advice =
          "the key is the room's. type \"" +
          announcementAdvice(parseName("G minor")) + "\" to change it.";
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

static MusicalKeyTests musicalKeyTests;

} // namespace
