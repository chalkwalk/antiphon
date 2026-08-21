#pragma once

#include "TextUtil.h"
#include <string>

// The key a jam is in: a tonic and a mode.
//
// Ninjam has no field for this -- the protocol carries audio, chat and tempo and
// nothing else. So the key travels as an ordinary chat message in a tagged form,
// `[key: D minor]`, which every other client shows as plain text and which we
// parse for display. That is the same shape Jamtaba uses for chord progressions,
// and it clears all three fences in NON-GOALS.md by construction: it needs no
// protocol extension, no cooperation from other clients, and nothing to change
// on the server.
//
// Parsed ONLY from that tagged form, never from free chat text. Jamtaba's chord
// parser treats "I" and "l" as measure separators and consequently reads
// "I AM TIRED ..." as a chord progression -- that is a real entry in their test
// suite (elieserdejesus/JamTaba,
// tests/auto/chords/TestChatChordsProgressionParser.cpp).
// Guessing at prose is how you get a header that lies.
//
// JUCE-FREE, like the rest of the music-theory layer: this and `Harmony` are
// used by the bots and by the plugin's chat UI alike, so they belong to neither.
//
// THIS FILE IS THREE THINGS, and they have three different destinations. Worth
// saying here, because "move it to chalkwalk-music" is the obvious reading and
// it is wrong for two thirds of it:
//
//   - The SCALE. `Key{tonic, Mode}` is already duplicated by
//     `chalkwalk::music::KeySig`, whose `brightness` axis IS the mode -- see
//     `toKeySig` in BotBand.cpp, which maps the seven one for one. KeySig is
//     strictly more expressive (any note count, named modifiers), so this half
//     should COLLAPSE INTO IT rather than move: deleted, not relocated.
//   - The NOTATION. Spelling a pitch class as Bb or A#, parsing "D minor",
//     displaying it back, naming the scale's notes. `chalkwalk-music` has none
//     of this -- it has `modeName(brightness)` and nothing that reads or spells
//     -- so this half is a genuine ADDITION to that library.
//   - The TAG. `[key: ...]` is how a key travels over Ninjam chat. That is
//     wire protocol, not theory, and it belongs to Antiphon or to
//     `chalkwalk-ninjam`. It must not go to the music library at all.
//
// Unit-testable in the headless
// test target -- PluginEditor cannot be compiled there at all.

namespace MusicalKey {

// The seven diatonic modes plus the two everyone actually says. Major and Minor
// are kept distinct from Ionian and Aeolian even though they are the same
// scale: someone who typed "D minor" should see "D minor" back, not "D Aeolian".
enum class Mode {
  Major,
  Minor,
  Ionian,
  Dorian,
  Phrygian,
  Lydian,
  Mixolydian,
  Aeolian,
  Locrian
};

struct Key {
  bool valid = false;
  int tonic = 0;     // semitones above C, 0-11
  bool flat = false; // spell the tonic with a flat rather than a sharp
  Mode mode = Mode::Major;

  bool operator==(const Key &o) const {
    return valid == o.valid && tonic == o.tonic && mode == o.mode;
  }
  bool operator!=(const Key &o) const { return !(*this == o); }
};

// The tag a key travels in. Chosen to be unmistakable in a chat log and still
// readable to someone whose client knows nothing about it.
inline std::string tagPrefix() { return "[key:"; }

// "D minor", "F# Dorian", "Bb major". Returns an invalid Key for anything else.
Key parseName(const std::string &text);

// Pulls a key out of a chat line or a topic, i.e. finds `[key: ...]` anywhere in
// the string and parses what is inside. Returns an invalid Key when the tag is
// absent -- deliberately, so ordinary chat can never set the key.
Key parseTagged(const std::string &text);

// The message `/key Dm` sends: "[key: D minor]".
std::string buildTagged(const Key &key);

// A key announcement in EITHER of the two forms the room understands.
//
// There are two because neither can do the other's job:
//
//   `[key: D minor]`  matched ANYWHERE in the line, so it can ride in the
//                     server topic -- the only room state NINJAM makes
//                     persistent, since it replays no chat to a late arrival.
//   `/key D minor`    matched only at the START of a line, and containing no
//                     `[key:`, so a bot can quote it in a sentence without
//                     setting the key by explaining it.
//
// The second exists precisely because the first is unsayable. `parseTagged`
// finding the tag anywhere means any advice about it performs it, so without a
// line-leading form a bot could never tell anyone how to change the key -- it
// could only change it for them. It is also typeable in any client: other
// clients pass an unknown slash command through as ordinary chat.
//
// Use THIS on anything arriving from the wire. `parseTagged` remains for the
// places that specifically mean the tag.
Key parseAnnouncement(const std::string &line);

// "D minor". Empty for an invalid key.
std::string displayName(const Key &key);

// What a bot should tell somebody to type. Deliberately NOT the tag, because
// saying the tag sets the key.
inline std::string announcementAdvice(const Key &key) {
  return "/key " + displayName(key);
}

// The notes of the scale, spelled to match the tonic: "D E F G A Bb C".
// Empty for an invalid key. Useful spoken as well as shown -- a player who
// cannot see the header still gets the one fact they need.
std::string scaleNotes(const Key &key);

std::string modeName(Mode mode);

// A pitch class as a note name, spelled sharp or flat as asked: "C#" or "Db".
//
// Exported because spelling a chord root is the same problem as spelling a
// scale note, and a second accidental table in Harmony.cpp would be a second
// place to be wrong (`PRINCIPLES §8`).
std::string noteName(int semitone, bool flat);

// Whether a key is conventionally written with flats, derived from its relative
// major. What `scaleNotes` uses, and what a chord name should use, so a chord in
// D minor spells Bb rather than A#.
bool usesFlats(int tonic, Mode mode);

// The seven scale degrees as semitones above the tonic, for anything that has
// to make a note rather than name one. `scaleNotes` spells them for a reader;
// this is the same information for a synthesiser.
//
// Always seven entries, and always the mode's own steps -- Major and Ionian
// coincide here, as do Minor and Aeolian, because the distinction between them
// is one of naming rather than of pitch.
static constexpr int kScaleDegrees = 7;
const int *scaleSteps(Mode mode);

// MIDI note number for a scale degree, where degree 0 is the tonic in `octave`
// and degrees run on past 6 into the octaves above (or below, if negative).
int degreeToMidi(const Key &key, int degree, int octave = 4);

} // namespace MusicalKey
