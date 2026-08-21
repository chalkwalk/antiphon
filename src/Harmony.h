#pragma once

#include <chalkwalk/music/Harmony.h>

// Chords, charts, degrees, roman numerals, voice leading and key inference all
// live in `chalkwalk::music::Harmony` now: they are music theory, and the bots
// need them as much as the chat UI does.
//
// An alias rather than a re-export list, because unlike `MusicalKey` there is
// nothing of Antiphon's to add here -- the whole of it moved.
namespace Harmony = chalkwalk::music::Harmony;
