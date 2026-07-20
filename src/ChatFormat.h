#pragma once

#include <JuceHeader.h>

// What a chat line is, and what the voting system is saying.
//
// Pure and JUCE-light on purpose: it uses juce::String but nothing from
// juce_gui_basics, so the rules can be unit-tested in the headless test target.
// PluginEditor cannot be compiled there at all, so anything left inside it is
// untestable by construction -- which is how the disconnected chat stayed
// unreadable for as long as it did.
//
// Colour is never the only carrier of meaning: every category keeps its text
// prefix ("***", "<user>", "[PM]", "~~"), so a screen reader and a colour-blind
// user get the same information as everyone else (PRINCIPLES 11).

namespace ChatFormat {

enum class Category {
  Topic,        // the server's topic line
  ServerNotice, // anything else the server says
  JoinPart,     // someone arrived or left
  SelfMessage,  // something you said
  OtherMessage, // something another player said
  PrivateMessage,
  Action,       // "/me waves"
  Voting,       // the voting system, including tempo changes
  Key,          // "[key: D minor]" -- see MusicalKey.h
  ChordProgression // "| Dm7 | G7 |" -- Jamtaba's convention, display only
};

struct Line {
  Category category = Category::ServerNotice;
  juce::String text; // the whole rendered line, prefix included
};

// Renders one incoming message. `selfUsername` decides SelfMessage vs
// OtherMessage; pass what you connected as.
Line render(const juce::String &type, const juce::String &username,
            const juce::String &text, const juce::String &selfUsername);

// The server's voting system, parsed from the chat text it broadcasts. The
// exact formats are fixed in the server source and quoted at the parser.
struct VoteState {
  bool valid = false;
  bool isBpm = true;  // false means BPI
  int target = 0;     // the value being voted for
  int votes = 0;      // votes so far
  int needed = 0;     // votes required to carry
  int timeoutSeconds = 0;
  bool settled = false; // "setting BPM to N" -- the vote carried
};

// Returns valid == false for any line that is not from the voting system.
VoteState parseVote(const juce::String &text);

// Whether a line is a chord progression in the convention Jamtaba established:
// measures separated by bars, as in "| Dm7 | G7 | Bb | Am7".
//
// Detection only -- we render the line as its own kind of message and do
// nothing else with it. A chart synced to the interval is Jamtaba's feature and
// stays theirs; what people here actually do is pick a key and let the chords
// follow, which MusicalKey covers.
//
// Deliberately stricter than Jamtaba's parser, which also accepts "!", "I" and
// "l" as separators and consequently reads "I AM TIRED ..." and "LETS TAKE A
// BREAK" as progressions -- both are real cases in their own test suite
// (elieserdejesus/JamTaba,
// tests/auto/chords/TestChatChordsProgressionParser.cpp).
// Only "|" is a separator here, and at least two measures are required.
bool isChordProgression(const juce::String &text);

} // namespace ChatFormat
