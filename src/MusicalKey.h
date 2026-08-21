#pragma once

#include <chalkwalk/music/Notation.h>
#include <chalkwalk/ninjam/RoomConventions.h>

#include <string>

// The key a jam is in, and how it travels.
//
// The KEY ITSELF -- a tonic, a mode, how to spell it, how to read "D minor"
// and write it back -- lives in `chalkwalk::music::Notation`, because it is
// music theory and two projects need it. This header re-exports it under the
// name every call site here already uses, and adds the one part that is NOT
// theory and must never go to a music library: the wire form.
//
// Ninjam has no field for a key. The protocol carries audio, chat and tempo and
// nothing else. So the key travels as an ordinary chat message in a tagged
// form, `[key: D minor]`, which every other client shows as plain text and
// which we parse for display. That is the same shape Jamtaba uses for chord
// progressions, and it clears all three fences in NON-GOALS.md by
// construction: it needs no protocol change, no server change, and no
// agreement from anybody else in the room.
namespace MusicalKey {

// Re-exported from the music library. Named individually rather than with a
// namespace alias, because this namespace also holds the tag below -- and
// because the list is then an honest statement of what Antiphon takes.
using chalkwalk::music::Notation::Key;
using chalkwalk::music::Notation::Mode;
using chalkwalk::music::Notation::kScaleDegrees;

using chalkwalk::music::Notation::degreeToMidi;
using chalkwalk::music::Notation::displayName;
using chalkwalk::music::Notation::modeName;
using chalkwalk::music::Notation::noteName;
using chalkwalk::music::Notation::parseName;
using chalkwalk::music::Notation::scaleNotes;
using chalkwalk::music::Notation::scaleSteps;
using chalkwalk::music::Notation::usesFlats;

// ---------------------------------------------------------------------------
// The tag, which is a NINJAM room convention rather than music theory.
//
// The ENVELOPE -- the brackets, the line-leading slash, what a `!vote` will
// take -- is `chalkwalk::ninjam::conventions`, because the bots need it too and
// neither project is beneath the other. What is left here is the three-line
// composition of envelope and notation, which is glue rather than knowledge:
// the convention itself is single-sourced.

inline std::string tagPrefix() {
  return chalkwalk::ninjam::conventions::keyTagPrefix();
}

// `[key: D minor]` anywhere in a line.
inline Key parseTagged(const std::string &text) {
  return parseName(chalkwalk::ninjam::conventions::extractKeyTag(text));
}

// The line to send. Only this form sets the key.
inline std::string buildTagged(const Key &key) {
  if (!key.valid)
    return {};
  return chalkwalk::ninjam::conventions::buildKeyTag(displayName(key));
}

// A key from a chat line: the tag anywhere, or a line-leading `/key`.
inline Key parseAnnouncement(const std::string &line) {
  return parseName(
      chalkwalk::ninjam::conventions::extractKeyAnnouncement(line));
}

// What a bot should tell somebody to type. Deliberately NOT the tag, because
// saying the tag sets the key.
inline std::string announcementAdvice(const Key &key) {
  return chalkwalk::ninjam::conventions::keyAdviceLine(displayName(key));
}

} // namespace MusicalKey
