#include "../src/BotLanguage.h"
#include <JuceHeader.h>

// `test/fixtures/bot-phrases.txt` is the specification, and the number this
// file exists to produce is the FALLBACK RATE over it.
//
// The claim in docs/BOT-CHAT.md is that indirect phrasing works within this
// narrow domain. A claim like that is worth nothing without a measurement
// (`PRINCIPLES §5`), and the measurement is: of 519 lines of what people
// actually type, how many does the bot fail to understand?
//
// A miss is a defect to drive down, not a limit to accept -- so this reports
// the rate rather than only passing or failing, and the threshold moves down
// as the lexicon widens.

class BotLanguageTests : public juce::UnitTest {
public:
  BotLanguageTests() : juce::UnitTest("BotLanguage", "music") {}

  void runTest() override {
    runStageTests();
    runCorpus();
  }

  void runStageTests() {
    beginTest("normalising strips what does not change the question");
    {
      // Half of what makes phrasing indirect is padding.
      const auto a = BotLanguage::normalise("what are you playing");
      const auto b = BotLanguage::normalise("hey, could you just tell me "
                                            "quickly what you're playing please?");
      expect(!a.empty() && !b.empty());
      // Both should still carry the two words that matter.
      auto has = [](const std::vector<std::string> &v, const char *w) {
        return std::find(v.begin(), v.end(), w) != v.end();
      };
      expect(has(a, "playing") && has(b, "playing"), "the verb was lost");
      expect(has(b, "you"), "the subject was lost");
      expect(b.size() <= 6, "padding survived: " + juce::String((int)b.size()) +
                                " tokens");
    }

    beginTest("stemming folds the forms of a word together");
    {
      for (const char *w : {"playing", "plays", "played"})
        expectEquals(juce::String(BotLanguage::stem(w)), juce::String("play"),
                     juce::String(w));
      expectEquals(juce::String(BotLanguage::stem("chords")),
                   juce::String("chord"));

      // An ordinary plural keeps its `e`. Taking it turns `notes` into `not`,
      // which is not merely wrong but is a negation, so it would flip the
      // meaning of any sentence it appeared in.
      expectEquals(juce::String(BotLanguage::stem("notes")),
                   juce::String("note"));
      expectEquals(juce::String(BotLanguage::stem("pulses")),
                   juce::String("puls"));
      // ...but after a sibilant the `e` is doing work.
      expectEquals(juce::String(BotLanguage::stem("matches")),
                   juce::String("match"));

      // Nouns built from verbs and adjectives reduce to the root, so the
      // lexicon carries one spelling rather than four.
      expectEquals(juce::String(BotLanguage::stem("progression")),
                   juce::String("progress"));
      expectEquals(juce::String(BotLanguage::stem("tonality")),
                   juce::String("tonal"));
      // And leaves short words alone rather than mangling them.
      for (const char *w : {"key", "bpm", "is", "us"})
        expectEquals(juce::String(BotLanguage::stem(w)), juce::String(w),
                     juce::String(w) + " was mangled");
    }

    beginTest("negation separates two sentences with the same words");
    {
      // "be quiet" and "do not be quiet" share every content word, so this flag
      // is the only thing between them.
      const auto quiet = BotLanguage::read("be quiet");
      const auto loud = BotLanguage::read("dont be quiet");
      expect(quiet.intent == BotLanguage::Intent::SetQuiet,
             juce::String("be quiet -> ") +
                 BotLanguage::intentName(quiet.intent));
      expect(loud.negated, "negation was not seen");
      expect(loud.intent != BotLanguage::Intent::SetQuiet,
             "negation did not change the answer");
    }

    beginTest("what it cannot do, it does not pretend to");
    {
      // A question about how it sounds TO THE LISTENER is not a question about
      // its patch. The honest answer is the fallback, which says so.
      // Taken from the corpus rather than invented, because the corpus is the
      // specification and a test that asserts something else is asserting my
      // guess about the specification.
      for (const char *cannot : {"can you hear me", "anyone else hearing that",
                                 "sounds good", "that sounded great"}) {
        const auto r = BotLanguage::read(cannot);
        expect(r.intent == BotLanguage::Intent::None,
               juce::String(cannot) + " was answered as " +
                   BotLanguage::intentName(r.intent));
      }
    }
  }

  void runCorpus() {
    const auto file = fixtureFile();
    if (!file.existsAsFile()) {
      beginTest("the phrase corpus is present");
      expect(false, "not found: " + file.getFullPathName());
      return;
    }

    beginTest("the phrase corpus, and the fallback rate over it");

    juce::String section;
    int total = 0, correct = 0, fallback = 0, wrong = 0, clarified = 0;
    juce::StringArray misses;

    for (const auto &raw : juce::StringArray::fromLines(file.loadFileAsString())) {
      auto line = raw.upToFirstOccurrenceOf("#", false, false).trim();
      if (line.isEmpty())
        continue;
      if (line.startsWithChar('[') && line.endsWithChar(']')) {
        section = line.substring(1, line.length() - 1).trim();
        continue;
      }
      if (section.isEmpty())
        continue;

      ++total;
      const auto r = BotLanguage::read(line.toStdString());
      const juce::String got =
          r.ambiguous ? "CLARIFY" : juce::String(BotLanguage::intentName(r.intent));

      if (got == section) {
        ++correct;
        continue;
      }
      if (got == "NONE")
        ++fallback;
      else if (got == "CLARIFY" || section == "CLARIFY")
        ++clarified;
      else
        ++wrong;

      if (misses.size() < 30)
        misses.add("  [" + section + "] \"" + line + "\" -> " + got);
    }

    for (const auto &m : misses)
      logMessage(m);

    const double rate = total > 0 ? 100.0 * fallback / total : 0.0;
    const double wrongRate = total > 0 ? 100.0 * wrong / total : 0.0;
    const double clarifyRate = total > 0 ? 100.0 * clarified / total : 0.0;
    logMessage("corpus: " + juce::String(correct) + " of " +
               juce::String(total) + " correct (" +
               juce::String(100.0 * correct / total, 1) + "%)  fallback " +
               juce::String(rate, 1) + "%  clarify " +
               juce::String(clarifyRate, 1) + "%  wrong " +
               juce::String(wrongRate, 1) + "%");

    // Three failures, and they do not cost the same, which is why they are
    // counted apart.
    //
    // A FALLBACK is honest: the bot names what it recognised and what it can
    // do. Disappointing, not misleading.
    //
    // A CLARIFY is nearly free: it asks which of two, and the two are named, so
    // the next message resolves it. On a line the corpus says is unambiguous it
    // is still a miss, but a mild one.
    //
    // A WRONG answer is confidently unhelpful, which is the only one that
    // actively misleads, so it carries the tightest bound.
    //
    // These are RATCHETS at the measured rate rather than aspirations. Each
    // widening of the lexicon should lower them, and the corpus header says how:
    // add the phrasing that missed, watch this go red, then widen.
    expect(wrongRate <= 17.5,
           "answering the wrong question " + juce::String(wrongRate, 1) +
               "% of the time");
    expect(clarifyRate <= 12.0,
           "asking which of two on " + juce::String(clarifyRate, 1) + "%");
    expect(rate <= 9.0,
           "falling back on " + juce::String(rate, 1) + "% of real phrasings");
  }

private:
  static juce::File fixtureFile() {
    auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                   .getParentDirectory();
    for (int i = 0; i < 8; ++i) {
      const auto candidate = dir.getChildFile("test/fixtures/bot-phrases.txt");
      if (candidate.existsAsFile())
        return candidate;
      dir = dir.getParentDirectory();
    }
    return {};
  }
};

static BotLanguageTests botLanguageTests;
