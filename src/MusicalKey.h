#pragma once

#include <JuceHeader.h>

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
// JUCE-light and free of juce_gui_basics, so it is unit-testable in the headless
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
inline juce::String tagPrefix() { return "[key:"; }

// "D minor", "F# Dorian", "Bb major". Returns an invalid Key for anything else.
Key parseName(const juce::String &text);

// Pulls a key out of a chat line or a topic, i.e. finds `[key: ...]` anywhere in
// the string and parses what is inside. Returns an invalid Key when the tag is
// absent -- deliberately, so ordinary chat can never set the key.
Key parseTagged(const juce::String &text);

// The message `/key Dm` sends: "[key: D minor]".
juce::String buildTagged(const Key &key);

// "D minor". Empty for an invalid key.
juce::String displayName(const Key &key);

// The notes of the scale, spelled to match the tonic: "D E F G A Bb C".
// Empty for an invalid key. Useful spoken as well as shown -- a player who
// cannot see the header still gets the one fact they need.
juce::String scaleNotes(const Key &key);

juce::String modeName(Mode mode);

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
