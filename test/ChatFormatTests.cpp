#include <JuceHeader.h>

#include "ChatFormat.h"

namespace {

using namespace ChatFormat;

class ChatFormatTests : public juce::UnitTest {
public:
  ChatFormatTests() : juce::UnitTest("ChatFormat", "ChatFormat") {}

  void runTest() override {
    beginTest("a server notice is not attributed to a player");
    {
      // The server sends its own notices as MSG with an empty username, which
      // the old renderer turned into "<> text" -- every server notice appeared
      // to come from a player with no name.
      const auto l = render("MSG", "", "server is going down", "me");
      expect(l.category == Category::ServerNotice);
      expectEquals(l.text, juce::String("*** server is going down"));
    }

    beginTest("your own messages are distinguished from everyone else's");
    {
      const auto mine = render("MSG", "daniel", "hello", "daniel");
      const auto theirs = render("MSG", "sam", "hello", "daniel");
      expect(mine.category == Category::SelfMessage);
      expect(theirs.category == Category::OtherMessage);
      // Both keep the same prefix shape: the distinction is carried by colour,
      // and colour must never be the only carrier.
      expectEquals(mine.text, juce::String("<daniel> hello"));
      expectEquals(theirs.text, juce::String("<sam> hello"));
    }

    beginTest("without a known self name nothing is claimed as yours");
    {
      const auto l = render("MSG", "sam", "hello", "");
      expect(l.category == Category::OtherMessage);
    }

    beginTest("actions, private messages, topic and join/part are separated");
    {
      expect(render("MSG", "sam", "/me waves", "me").category == Category::Action);
      expectEquals(render("MSG", "sam", "/me waves", "me").text,
                   juce::String("* sam waves"));
      expect(render("PRIVMSG", "sam", "psst", "me").category ==
             Category::PrivateMessage);
      expect(render("TOPIC", "Server", "jam night", "me").category ==
             Category::Topic);
      expect(render("JOIN", "Server", "sam joined", "me").category ==
             Category::JoinPart);
      expect(render("PART", "Server", "sam left", "me").category ==
             Category::JoinPart);
    }

    beginTest("every category keeps a text prefix");
    {
      // Colour alone is not enough: a screen reader announces text, and a
      // colour-blind user needs the same information as everyone else.
      const juce::String self = "me";
      for (const auto &l :
           {render("MSG", "", "notice", self), render("TOPIC", "s", "t", self),
            render("JOIN", "s", "sam joined", self),
            render("MSG", "sam", "hi", self),
            render("PRIVMSG", "sam", "hi", self),
            render("MSG", "sam", "/me waves", self),
            render("MSG", "", "[voting system] setting BPM to 120", self)}) {
        expect(l.text.startsWith("***") || l.text.startsWith("<") ||
                   l.text.startsWith("[PM]") || l.text.startsWith("*") ||
                   l.text.startsWith("~~"),
               "no prefix on: " + l.text);
      }
    }

    beginTest("a vote in progress is parsed");
    {
      // The exact wording is the only contract the server offers -- there is no
      // structured vote message on the wire (usercon.cpp:1074).
      const auto v = parseVote(
          "[voting system] leading candidate: 3/5 votes for 137 BPM "
          "[each vote expires in 60s]");
      expect(v.valid);
      expect(!v.settled);
      expect(v.isBpm);
      expectEquals(v.target, 137);
      expectEquals(v.votes, 3);
      expectEquals(v.needed, 5);
      expectEquals(v.timeoutSeconds, 60);
    }

    beginTest("a BPI vote is told apart from a BPM vote");
    {
      const auto v = parseVote(
          "[voting system] leading candidate: 1/2 votes for 11 BPI "
          "[each vote expires in 60s]");
      expect(v.valid);
      expect(!v.isBpm, "11 BPI must not be read as a BPM proposal");
      expectEquals(v.target, 11);
      expectEquals(v.votes, 1);
      expectEquals(v.needed, 2);
    }

    beginTest("a carried vote is marked settled");
    {
      const auto bpm = parseVote("[voting system] setting BPM to 137");
      expect(bpm.valid && bpm.settled && bpm.isBpm);
      expectEquals(bpm.target, 137);

      const auto bpi = parseVote("[voting system] setting BPI to 11");
      expect(bpi.valid && bpi.settled && !bpi.isBpm);
      expectEquals(bpi.target, 11);
    }

    beginTest("ordinary chat is never mistaken for a vote");
    {
      // A player can type anything, including something that looks like the
      // server's wording. Only lines the server itself prefixes count.
      for (const auto *s :
           {"hello", "leading candidate: 3/5 votes for 137 BPM",
            "setting BPM to 137", "[voting] setting BPM to 137", ""})
        expect(!parseVote(juce::String(s)).valid,
               "wrongly read as a vote: " + juce::String(s));
    }

    beginTest("a vote line that cannot be read shows no chip");
    {
      expect(!parseVote("[voting system] leading candidate: garbage").valid);
      expect(!parseVote("[voting system] something new we do not know").valid);
    }

    beginTest("chord progressions are recognised, using Jamtaba's own vectors");
    {
      // Taken from
      // references/JamTaba/tests/auto/chords/TestChatChordsProgressionParser.cpp
      // so we accept what people already type in Ninjam rooms.
      for (const auto *s : {"| C    | F    | G    | F    ", "|C    |F    |G    |F",
                            "|C|F|G|F", "|  C|  F|  G|  F", "|C| F| G |F",
                            "|C|F||G|F",
                            "|C |Fmaj7 |G7 |Am7 |Am7/G |F#m7(b5) |Fmaj9"})
        expect(isChordProgression(s),
               juce::String("not recognised: ") + s);
    }

    beginTest("prose with pipes is not a chord progression");
    {
      // Jamtaba's parser accepts "!", "I" and "l" as measure separators, so it
      // reads these two as progressions -- both are real entries in their test
      // suite. Only "|" separates here, so they cannot reach us; the rest guard
      // the cases that could.
      for (const auto *s : {"I AM TIRED ...", "LETS TAKE A BREAK", "|C",
                            "| lets play something", "hello", "", "|",
                            "|| ||", "| C | and then something else"})
        expect(!isChordProgression(s),
               juce::String("wrongly read as a progression: ") + s);
    }

    beginTest("a key announcement is its own category");
    {
      const auto l = render("MSG", "daniel", "[key: D minor]", "me");
      expect(l.category == Category::Key);
      expect(l.text.startsWith("~~"), "must keep a prefix like every category");

      expect(render("MSG", "daniel", "lets play in D minor", "me").category ==
                 Category::OtherMessage,
             "free text must stay an ordinary message");
    }

    beginTest("a progression is categorised whoever sends it");
    {
      const auto l = render("MSG", "sam", "|Dm7 |G7 |Bb |Am7", "me");
      expect(l.category == Category::ChordProgression);
      expect(l.text.startsWith("~~"));
    }

    beginTest("a voting line is categorised as voting, whoever sends it");
    {
      const auto l = render(
          "MSG", "", "[voting system] leading candidate: 1/2 votes for 137 BPM "
                     "[each vote expires in 60s]", "me");
      expect(l.category == Category::Voting);
      expect(l.text.startsWith("~~"));
    }
  }
};

static ChatFormatTests chatFormatTests;

} // namespace
