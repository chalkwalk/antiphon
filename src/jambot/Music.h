#pragma once

#include <chalkwalk/music/Harmony.h>

// The music theory the bots use, under the name they use for it.
//
// The band and the answering were written against `Harmony::` when it lived in
// Antiphon. It is `chalkwalk::music::Harmony` now, and this alias is what let
// that move happen without touching several hundred call sites in the same
// commit. It goes when this directory becomes `chalkwalk-jambot` and takes a
// namespace of its own -- a rename worth doing on its own rather than folded
// into a relocation.
//
// `MusicalKey` deliberately does NOT get the same treatment. The bots use it
// for `announcementAdvice`, which produces the `/key D minor` line a player
// should type -- and that is NINJAM, not theory. It stays an outward include
// until the tag lands somewhere both projects can reach.
namespace Harmony = chalkwalk::music::Harmony;
