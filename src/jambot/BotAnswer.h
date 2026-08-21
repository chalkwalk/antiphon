#pragma once

#include "Music.h"

#include <chalkwalk/music/Duration.h>
#include <JuceHeader.h>

// What a bot SAYS when asked about the room, as pure functions over what the
// room is. `BotLanguage` decides what was asked; this decides the words.
//
// Separated from PracticeBot for the usual reason -- PracticeBot needs the
// plugin's defines and cannot be compiled into the test target -- but also
// because the wording is the part most likely to be wrong in a way no
// compiler notices, and it is worth being able to read every line a bot can
// say without starting a room.
//
// Two rules run through all of it, and both were learned the hard way.
//
// SAY WHERE IT CAME FROM. A key and a chart both always have a value, and both
// may have arrived by nobody choosing them: the room starts in C major, and a
// key with no chart gets `Harmony::defaultChart`. Reporting either as though it
// were a decision tells somebody the room agreed on something it did not, and
// a stale topic makes that worse -- it can be hours old and there is no way to
// tell from the value alone.
//
// NEVER SAY THE TAG. `MusicalKey::parseTagged` matches `[key:` anywhere in a
// line, so a reply explaining the tag would set the key by explaining it. Every
// string here goes through `MusicalKey::announcementAdvice`, which produces the
// line-leading `/key` form, and `test/BotAnswerTests.cpp` asserts that nothing
// this file produces parses as a key announcement.

namespace BotAnswer {

// How the room came to be in this key, or on this chart. The distinction is
// the whole point: only `Chat` is somebody deciding.
enum class Source {
  Defaulted, // nobody said anything: C major, or the chart the key implies
  Topic,     // read from the server topic, of unknown age -- possibly stale
  Chat,      // said in the room, and we heard it
};

struct Room {
  MusicalKey::Key key;
  Source keySource = Source::Defaulted;
  juce::String keySetBy; // who said it; empty unless keySource == Chat

  Harmony::Chart chart;
  Source chartSource = Source::Defaulted;

  int bpm = 120;
  int bpi = 8;

  // How much of the space between two onsets the band's notes fill: 0 clipped,
  // 50 as the metre and the harmony asked for, 100 running into each other.
  // Here rather than in `Self` because it is a property of the band -- asked to
  // play more legato, everyone does.
  int articulation = chalkwalk::music::kArticulationNatural;

  // The owner is the one player whose client we know for certain, because they
  // are running the plugin the bots came from. Nobody else's client is
  // knowable, so nothing client-specific is ever said to the room.
  bool toOwner = false;
};

// FRAGMENTS, not messages. Both are noun phrases meant to follow "we are in" or
// "the chart is", and neither may be sent on its own -- `describeChart` returns
// text beginning with a bar line, which any client would read as somebody
// announcing a chart. The answer* functions below are the complete replies.
//
// They are noun phrases because returning sentences produced "we are in nobody
// has named a key, so i defaulted to C major".
juce::String describeKey(const Room &room);
juce::String describeChart(const Room &room);

// Asked to change the key. `wanted` invalid means we could not tell which key
// was meant, which is answered rather than guessed: putting up the wrong key is
// worse than putting up none.
juce::String answerSetKey(const Room &room, const MusicalKey::Key &wanted);

// Asked to change the chart. Never acts: a chart must lead its line, so a
// request for one essentially never carries a chart to echo, and the portable
// form is easy enough to type that there is nothing to translate.
//
// The example is the chart it is ACTUALLY PLAYING, which is both the honest
// answer and the safe one -- a generic example pasted into a room in another
// key would silently move the harmony.
juce::String answerSetChart(const Room &room);

// Asked for the chords the KEY implies -- "use the default chords for this
// key". Askable because a key change no longer imposes them: a chart somebody
// wrote now travels with the key rather than being discarded (`DESIGN.md`
// section 6.4), which is right, and leaves the old behaviour with no way to
// ask for it.
//
// Offers rather than acts, for the same reason `answerSetChart` does: a chart
// is the room's, and a bot that quietly reverted its own would be playing
// something nobody else in the room could see.
juce::String answerResetChart(const Room &room);

// Asked to change the tempo. `wantBpm`/`wantBpi` are what was asked for; zero
// means "not this one". Out-of-range values are refused here rather than by the
// server, whose answer to one is a complaint about the command's parameters.
juce::String answerSetTempo(const Room &room, int wantBpm, int wantBpi);

// Asked to cast a vote directly. A bot never starts one -- four bots voting on
// one person's say-so is that person having four votes.
juce::String answerVoteRequest(const Room &room);

} // namespace BotAnswer
