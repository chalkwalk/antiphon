#include "../src/BotNames.h"
#include <JuceHeader.h>

// The names are an addressing mechanism before they are anything else, so these
// are exact tests about properties an address needs -- not about taste.

namespace {

int edits(const juce::String &a, const juce::String &b) {
  std::vector<int> prev((size_t)b.length() + 1), cur((size_t)b.length() + 1);
  for (int j = 0; j <= b.length(); ++j)
    prev[(size_t)j] = j;
  for (int i = 1; i <= a.length(); ++i) {
    cur[0] = i;
    for (int j = 1; j <= b.length(); ++j)
      cur[(size_t)j] =
          juce::jmin(prev[(size_t)j] + 1, cur[(size_t)j - 1] + 1,
                     prev[(size_t)j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1));
    prev = cur;
  }
  return prev[(size_t)b.length()];
}

juce::String rime(const juce::String &s) {
  const auto lower = s.toLowerCase();
  for (int i = 0; i < lower.length(); ++i)
    if (juce::String("aeiou").containsChar(lower[i]))
      return lower.substring(i);
  return lower;
}

} // namespace

class BotNamesTests : public juce::UnitTest {
public:
  BotNamesTests() : juce::UnitTest("BotNames", "music") {}

  void runTest() override {
    beginTest("every name can be sent a private message");
    {
      // The fault that made the old names unreachable, asserted directly.
      // `/msg <user> <text>` splits on the first space in every client there
      // is, so a username containing one addresses somebody else entirely and
      // fails silently.
      for (const auto &name : BotNames::pool()) {
        const juce::String n(name);
        expect(!n.containsChar(' '), "a space in " + n);
        expect(n.isNotEmpty() && n.length() <= 8, "unwieldy: " + n);

        const juce::String full(BotNames::usernameFor(name, "bass"));
        expect(!full.containsChar(' '), "a space in " + full);
        expect(BotNames::looksLikeBot(full.toStdString()),
               full + " does not carry the marker");
        expectEquals(juce::String(BotNames::handleOf(full.toStdString())),
                     n.toLowerCase(), "handle of " + full);
      }
    }

    beginTest("a human's name is not mistaken for the marker");
    {
      // The marker decides who talks, so a false positive silences a person.
      for (const char *human : {"dave", "sam", "bassist", "robot", "bot",
                                "not-a-bot", "Delvo", "delvo[bass]"})
        expect(!BotNames::looksLikeBot(human),
               juce::String(human) + " was taken for a bot");
    }

    beginTest("every band the seed can pick is mutually distinguishable");
    {
      // The constraints are on the BAND rather than on the pool -- the pool
      // deliberately holds names that must not play together, `Vurn` rhyming
      // with `Mirn` and `Pemo` sharing an initial with `Pundo`. This is the
      // assertion that `bandFor` keeps them apart, across every seed.
      for (std::uint32_t seed = 1; seed <= 500; ++seed) {
        const auto band = BotNames::bandFor(4, seed * 2654435761u, {});
        expectEquals((int)band.size(), 4,
                     "seed " + juce::String((int)seed) + " fielded " +
                         juce::String((int)band.size()));

        for (size_t i = 0; i < band.size(); ++i)
          for (size_t j = i + 1; j < band.size(); ++j) {
            const juce::String a(band[i]), b(band[j]);
            const juce::String at = " (" + a + " and " + b + ", seed " +
                                    juce::String((int)seed) + ")";

            expect(a != b, "the same name twice" + at);
            expect(a.toLowerCase()[0] != b.toLowerCase()[0],
                   "a shared initial defeats near-miss matching" + at);
            expect(edits(a.toLowerCase(), b.toLowerCase()) >= 2,
                   "one typo reaches the other" + at);
            expect(rime(a) != rime(b), "these two rhyme" + at);
          }
      }
    }

    beginTest("the same seed brings the same players back");
    {
      // A room is reproducible, which is what makes "shake" mean something and
      // a bug report answerable.
      for (std::uint32_t seed : {1u, 42u, 909u, 4242u})
        expect(BotNames::bandFor(4, seed, {}) == BotNames::bandFor(4, seed, {}),
               "seed " + juce::String((int)seed) + " is not reproducible");

      // And different seeds mostly bring different bands, or the pool is
      // decoration.
      std::set<std::vector<std::string>> seen;
      for (std::uint32_t seed = 1; seed <= 50; ++seed)
        seen.insert(BotNames::bandFor(4, seed * 40503u, {}));
      expect(seen.size() >= 4, "fifty seeds gave only " +
                                   juce::String((int)seen.size()) +
                                   " distinct line-ups");
    }

    beginTest("a name a player is already using is skipped");
    {
      // A handle that collides with somebody in the room costs the bot natural
      // address for the whole session, so it is avoided at join rather than
      // degraded around afterwards.
      for (const auto &occupied : BotNames::pool()) {
        const auto band = BotNames::bandFor(4, 12345u, {occupied});
        expect(std::find(band.begin(), band.end(), occupied) == band.end(),
               "a bot took the name " + juce::String(occupied) +
                   ", which a player already has");
        expectEquals((int)band.size(), 4);
      }

      // Case and substrings both count: somebody called "DELVOTON" makes
      // "delvo" ambiguous in a scan for a name anywhere in a sentence.
      for (const char *human : {"DELVO", "Delvoton", "mirn"}) {
        const auto band = BotNames::bandFor(4, 7u, {human});
        for (const auto &n : band)
          expect(juce::String(n).toLowerCase() !=
                     juce::String(human).toLowerCase().substring(0, 5),
                 juce::String(n) + " collides with " + human);
      }
    }

    beginTest("a hostile room still gets a band");
    {
      // Every name taken. The pool cannot satisfy anybody, and the answer is a
      // band with awkward names rather than no band -- the degraded addressing
      // path exists for exactly this.
      std::vector<std::string> everything = BotNames::pool();
      const auto band = BotNames::bandFor(4, 99u, everything);
      expectEquals((int)band.size(), 4, "a full room got no band at all");
    }

    beginTest("the tutor is a role, not a bandmate");
    {
      // It is addressed by what it is, so it must not turn up in the pool and
      // find itself competing with a name.
      const juce::String tutor(BotNames::tutorName());
      expect(tutor.isNotEmpty());
      for (const auto &n : BotNames::pool())
        expect(!tutor.equalsIgnoreCase(juce::String(n)),
               "the tutor shares a name with a player");
    }
  }
};

static BotNamesTests botNamesTests;
