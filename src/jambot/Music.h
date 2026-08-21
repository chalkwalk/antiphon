#pragma once

#include <chalkwalk/music/Harmony.h>
#include <chalkwalk/music/Notation.h>
#include <chalkwalk/ninjam/RoomConventions.h>

// What the bots know about music, and about how a room talks about it.
//
// Both come from shared libraries now. `Harmony` and the key itself are music
// theory (`chalkwalk-music`); the `[key: ...]` envelope and what a `!vote` will
// take are NINJAM room conventions (`chalkwalk-ninjam`). Neither belongs to the
// bots, and neither belongs to Antiphon: the two are siblings, so the shared
// things live beneath both.
//
// The aliases are what let that move happen without touching several hundred
// call sites, and they go when this directory becomes `chalkwalk-jambot` and
// takes a namespace of its own.

namespace Harmony = chalkwalk::music::Harmony;

// The key itself, under the name the bots already use for it. A using-directive
// rather than definitions of its own: Antiphon opens the same namespace to add
// the tag, and two headers defining the same function is not a boundary, it is
// a collision.
namespace MusicalKey {
using namespace chalkwalk::music::Notation;
} // namespace MusicalKey

namespace ChatFormat = chalkwalk::ninjam::conventions;
