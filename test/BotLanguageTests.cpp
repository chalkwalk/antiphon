#include "../src/BotLanguage.h"
#include <JuceHeader.h>

// `test/fixtures/bot-phrases.txt` is the specification, and the number this
// file exists to produce is the MISS RATE over it.
//
// The claim in docs/BOT-CHAT.md is that indirect phrasing works within this
// narrow domain. A claim like that is worth nothing without a measurement
// (`PRINCIPLES §5`), and the measurement is: of 617 lines of what people
// actually type, how many does the bot fail to understand?
//
// A miss is a defect to drive down, not a limit to accept -- so this reports
// the rate rather than only passing or failing, and the threshold moves down
// as the lexicon widens.
//
// -- Why the corpus is split -------------------------------------------------
//
// Every fourth line of each section is HELD OUT. Nothing was tuned against
// those lines, so the rate over them is the only number here that says anything
// about phrasing nobody has thought of yet -- which is the entire claim.
//
// It matters. Measured together at the start of this work the two rates were
// 74.9% and 72.8%, close enough to look like one number; by the end of tuning
// the tune set read 99.7% and the holdout 92.8%, and the seven-point gap IS the
// overfitting, visible only because the split existed. The honest figure for
// this engine on phrasing it has never seen is the second one.
//
// The holdout has since been revealed once and its nine misses repaired, which
// spends it: the rate over it is now optimistic in the same way the tune set
// is, and only lines added from here on restore an independent measurement. Add
// new phrasings to the END of a section so the every-fourth split keeps
// allocating roughly a quarter of them to a holdout that has never been read.

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
      expect(b.size() <= 3, "padding survived: " + juce::String((int)b.size()) +
                                " tokens");

      // The subject is stripped along with the rest of the grammar, and that is
      // deliberate: an unrecognised word is now evidence that a message is not
      // about us, so "you" must not be that evidence. What it carried is read
      // off the sentence BEFORE stripping and survives as a flag -- which is
      // the correction that made `secondPerson` mean anything at all, since
      // reading it afterwards found it false for every sentence containing it.
      expect(BotLanguage::read("hey, could you just tell me quickly what "
                               "you're playing please?").secondPerson,
             "the subject was lost");
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

    beginTest("a real word is not a mistyped one");
    {
      // Typo repair without this test is worse than no repair at all: it turns
      // an honest fallback into a confident wrong answer. Each of these was a
      // live defect, and each named a lexicon entry one or two edits away.
      const struct { const char *text; const char *notThis; } kReal[] = {
          {"stop chatting", "REPORT_CHART"},  // chat -> chart
          {"leave the room", "REPORT_KEY"},   // room -> root
          {"oops", "REPORT_CHART"},           // oops -> loop
          {"what are you playing right now", "DESCRIBE_SOUND"}, // right -> bright
      };
      for (const auto &c : kReal) {
        const auto r = BotLanguage::read(c.text);
        expect(juce::String(BotLanguage::intentName(r.intent)) != c.notThis,
               juce::String(c.text) + " was repaired into " + c.notThis);
      }

      // ...but a word that is NOT English still gets repaired, or the rule
      // would have bought its accuracy by refusing to do its job. Note that
      // `temp` would NOT be repaired to `tempo`, and should not be: it is an
      // ordinary word, and the gate cannot read minds.
      const auto typo = BotLanguage::read("whats the tepmo");
      expect(typo.intent == BotLanguage::Intent::ReportTempo,
             juce::String("tepmo -> ") + BotLanguage::intentName(typo.intent));
      const auto slip = BotLanguage::read("giv me somthing else");
      expect(slip.intent == BotLanguage::Intent::Reshuffle,
             juce::String("giv/somthing -> ") +
                 BotLanguage::intentName(slip.intent));
    }

    beginTest("word class decides the concept where the word cannot");
    {
      // The Brill-style contextual rule, and the case that motivated it: one
      // determiner is the whole difference between a question and an order.
      const auto noun = BotLanguage::read("what are the changes");
      expectEquals(juce::String(BotLanguage::intentName(noun.intent)),
                   juce::String("REPORT_CHART"), "\"the changes\" is a noun");
      const auto verb = BotLanguage::read("change your part");
      expectEquals(juce::String(BotLanguage::intentName(verb.intent)),
                   juce::String("RESHUFFLE"), "\"change ...\" is a verb");

      // Second person makes a following verb a request; anything else leaves it
      // descriptive. "how does it go" asked about the part and was answered by
      // leaving the room until this separated them.
      const auto asked = BotLanguage::read("how does it go");
      expectEquals(juce::String(BotLanguage::intentName(asked.intent)),
                   juce::String("DESCRIBE_PART"));
      const auto told = BotLanguage::read("go away");
      expectEquals(juce::String(BotLanguage::intentName(told.intent)),
                   juce::String("LEAVE"));
    }

    beginTest("a polite request is a question in form only");
    {
      // The clause-level pattern MODAL + "you" + verb. Found by probing
      // phrasings the corpus did not contain rather than by reading failures:
      // every one of these resolved to a DESCRIPTION of the thing it was
      // politely asking us to change, and nothing was red.
      for (const char *ask : {"can you change your part",
                              "could you play something different",
                              "would you mind changing your part",
                              "please shake"}) {
        const auto r = BotLanguage::read(ask);
        expect(r.intent == BotLanguage::Intent::Reshuffle,
               juce::String(ask) + " -> " + BotLanguage::intentName(r.intent));
      }

      // ...and the same two leading words in front of a real question must
      // still leave it a question, or the rule has simply moved the error.
      const auto real = BotLanguage::read("do you know the key");
      expectEquals(juce::String(BotLanguage::intentName(real.intent)),
                   juce::String("REPORT_KEY"));
      const auto cannot = BotLanguage::read("can you hear me");
      expect(cannot.intent == BotLanguage::Intent::None,
             juce::String("can you hear me -> ") +
                 BotLanguage::intentName(cannot.intent));
      const auto tell = BotLanguage::read("can you tell me your part");
      expectEquals(juce::String(BotLanguage::intentName(tell.intent)),
                   juce::String("DESCRIBE_PART"));
    }

    beginTest("an unrecognised word is evidence the message is not ours");
    {
      // Small talk is grammatical, addressed, and none of our business. The
      // only thing marking it out is the word we did not know.
      for (const char *away : {"who wrote this", "what daw are you on",
                               "where are you based", "how old is this song"}) {
        const auto r = BotLanguage::read(away);
        expect(r.intent == BotLanguage::Intent::None,
               juce::String(away) + " was answered as " +
                   BotLanguage::intentName(r.intent));
      }
      // The same shape, but naming something we do know, must still work --
      // or the rule is just a mute button.
      const auto ours = BotLanguage::read("has anyone set a key");
      expectEquals(juce::String(BotLanguage::intentName(ours.intent)),
                   juce::String("REPORT_KEY"));
    }

    beginTest("asking what the key is, and asking for a different one");
    {
      // The bots have no authority over either of these. That is a fact about
      // what they may DO; understanding the request is separate, and answering
      // "the key is Am" to somebody who asked for G minor is a miss that looks
      // like an answer.
      const struct { const char *text; const char *want; } kCases[] = {
          {"can you play in g minor", "SET_KEY"},
          {"play something in dorian", "SET_KEY"},
          {"switch to g major", "SET_KEY"},
          {"lets play in e minor", "SET_KEY"},
          {"give me a minor key", "SET_KEY"},
          {"can we change the key", "SET_KEY"},
          {"can you slow down", "SET_TEMPO"},
          {"can we go faster", "SET_TEMPO"},
          {"speed up", "SET_TEMPO"},
          {"can you vote for 120 bpm", "SET_TEMPO"},
          {"can we change the chords", "SET_CHART"},
          {"new chords please", "SET_CHART"},
          {"lets use different chords", "SET_CHART"},

          // ...and the reports, which share every topic word. A bare topic, a
          // `tell me`, a yes/no question and somebody thinking aloud are all
          // still questions.
          {"whats the key", "REPORT_KEY"},
          {"key?", "REPORT_KEY"},
          {"tell me the key", "REPORT_KEY"},
          {"can you tell me the key", "REPORT_KEY"},
          {"do you know the key", "REPORT_KEY"},
          {"has anyone set a key", "REPORT_KEY"},
          {"i dont know the key", "REPORT_KEY"},
          {"whats the tempo", "REPORT_TEMPO"},
          {"how long is one interval", "REPORT_TEMPO"},
          {"whats the chart again", "REPORT_CHART"},
          {"tell me the chords", "REPORT_CHART"},
          {"i cant remember the chords", "REPORT_CHART"},

          // A suggestion with nothing to act on is still just conversation.
          {"lets do another", "NONE"},
          {"how about the snare", "CLARIFY"},
      };
      for (const auto &c : kCases) {
        const auto r = BotLanguage::read(c.text);
        const juce::String got =
            r.ambiguous ? "CLARIFY"
                        : juce::String(BotLanguage::intentName(r.intent));
        expectEquals(got, juce::String(c.want), juce::String(c.text));
      }
    }

    beginTest("a message can ask for two things");
    {
      // The clause level. Without it the engine answered the first request and
      // dropped the second in silence.
      const struct { const char *text; const char *first; const char *second; }
      kPairs[] = {
          {"whats the key and can you shake it", "REPORT_KEY", "RESHUFFLE"},
          {"shake it and tell me the tempo", "RESHUFFLE", "REPORT_TEMPO"},
          {"tell me the key then be quiet", "REPORT_KEY", "SET_QUIET"},
          {"whats the chart, and what key", "REPORT_CHART", "REPORT_KEY"},
          {"whats the key and tempo", "REPORT_KEY", "REPORT_TEMPO"},
          {"describe your part and your sound", "DESCRIBE_PART", "DESCRIBE_SOUND"},
      };
      for (const auto &c : kPairs) {
        const auto all = BotLanguage::readAll(c.text);
        expectEquals((int)all.size(), 2, juce::String(c.text));
        if (all.size() != 2)
          continue;
        expectEquals(juce::String(BotLanguage::intentName(all[0].intent)),
                     juce::String(c.first), juce::String(c.text));
        expectEquals(juce::String(BotLanguage::intentName(all[1].intent)),
                     juce::String(c.second), juce::String(c.text));
      }

      // Half the room does not type the connective, so a comma splits too.
      const auto comma = BotLanguage::readAll("shake it, tell me the tempo");
      expectEquals((int)comma.size(), 2, "a comma did not separate two requests");

      // A conjunction INSIDE one request is not two requests. Getting this
      // wrong is worse than not splitting at all, because it invents an
      // instruction nobody gave -- and the comma is where that nearly happened:
      // "hey kit, whats your part" is one question with an address in front.
      for (const char *single : {"shake the bass and the drums",
                                 "tell me about your kick and snare",
                                 "hey kit, whats your part",
                                 "sorry, what key are we in",
                                 "tell me the tempo, thanks",
                                 "whats your part", "be quiet"}) {
        expectEquals((int)BotLanguage::readAll(single).size(), 1,
                     juce::String(single) + " was split");
      }

      // Addressing is settled before this file ever sees a message, so the
      // whole vocative goes -- not the first word of it.
      for (const char *addressed : {"kit whats your part",
                                    "hey kit, whats your part",
                                    "hey kit whats your part"}) {
        const auto r = BotLanguage::read(addressed);
        expect(r.intent == BotLanguage::Intent::DescribePart && !r.ambiguous,
               juce::String(addressed) + " -> " +
                   juce::String(r.ambiguous ? "CLARIFY/" : "") +
                   BotLanguage::intentName(r.intent));
      }
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

  struct Tally {
    int total = 0, correct = 0, fallback = 0, wrong = 0, clarified = 0;
    double pc(int n) const { return total > 0 ? 100.0 * n / total : 0.0; }
  };

  void runCorpus() {
    const auto file = fixtureFile();
    if (!file.existsAsFile()) {
      beginTest("the phrase corpus is present");
      expect(false, "not found: " + file.getFullPathName());
      return;
    }

    beginTest("the phrase corpus, tuned and held out");

    juce::String section;
    int index = 0;
    Tally tune, held;
    juce::StringArray misses;

    for (const auto &raw : juce::StringArray::fromLines(file.loadFileAsString())) {
      auto line = raw.upToFirstOccurrenceOf("#", false, false).trim();
      if (line.isEmpty())
        continue;
      if (line.startsWithChar('[') && line.endsWithChar(']')) {
        section = line.substring(1, line.length() - 1).trim();
        index = 0;
        continue;
      }
      if (section.isEmpty())
        continue;

      const bool holdout = (index++ % 4) == 3;
      Tally &t = holdout ? held : tune;
      ++t.total;

      const auto r = BotLanguage::read(line.toStdString());

      // Every corpus line is one request, so clause segmentation must be a
      // no-op over all of them. This is what makes the corpus measure `readAll`
      // as well: the two can only differ where a message really does ask twice,
      // and no line here does.
      const auto all = BotLanguage::readAll(line.toStdString());
      expect(all.size() == 1 && all[0].intent == r.intent,
             "\"" + line + "\" was split into " +
                 juce::String((int)all.size()) + " clauses");
      const juce::String got =
          r.ambiguous ? "CLARIFY" : juce::String(BotLanguage::intentName(r.intent));

      if (got == section) {
        ++t.correct;
        continue;
      }
      if (got == "NONE")
        ++t.fallback;
      else if (got == "CLARIFY" || section == "CLARIFY")
        ++t.clarified;
      else
        ++t.wrong;

      if (misses.size() < 30)
        misses.add("  " + juce::String(holdout ? "[held] " : "       ") + "[" +
                   section + "] \"" + line + "\" -> " + got);
    }

    for (const auto &m : misses)
      logMessage(m);

    auto report = [this](const char *name, const Tally &t) {
      logMessage(juce::String(name) + ": " + juce::String(t.correct) + " of " +
                 juce::String(t.total) + " correct (" +
                 juce::String(t.pc(t.correct), 1) + "%)  fallback " +
                 juce::String(t.pc(t.fallback), 1) + "%  clarify " +
                 juce::String(t.pc(t.clarified), 1) + "%  wrong " +
                 juce::String(t.pc(t.wrong), 1) + "%");
    };
    report("tune   ", tune);
    report("holdout", held);

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
    for (const auto &pair : {std::make_pair("tune", tune),
                             std::make_pair("holdout", held)}) {
      const juce::String where = pair.first;
      const Tally &t = pair.second;
      expect(t.pc(t.wrong) <= 1.0, where + ": answering the wrong question " +
                                       juce::String(t.pc(t.wrong), 1) +
                                       "% of the time");
      expect(t.pc(t.clarified) <= 1.0,
             where + ": asking which of two on " +
                 juce::String(t.pc(t.clarified), 1) + "%");
      expect(t.pc(t.fallback) <= 1.0,
             where + ": falling back on " + juce::String(t.pc(t.fallback), 1) +
                 "% of real phrasings");
    }
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
