#pragma once

#include "MusicalKey.h"

#include "Harmony.h"
#include <chalkwalk/ninjam/RoomConventions.h>

#include <string>

// Which of the two a chat line is, and nothing else.
//
// The subset of chat that needs no address, because its SYNTAX is unmistakable:
// a `[key: Dm]` tag, a `| Am | F |` chart, or a degree chart against the key
// the room is already in. Nobody writes any of them by accident.
//
// What each MEANS is `Harmony::Session` in chalkwalk-music -- preserve what was
// written, re-derive what was delegated -- and how a key TRAVELS is
// `chalkwalk::ninjam::conventions`. This is the seven lines that put the two
// together, and it is deliberately nothing more: the band has the same seven
// lines inside `PracticeBot`, because duplicating a dispatch is cheaper than
// giving glue a home of its own, and because what must not be duplicated --
// the rule and the convention -- is not.
namespace RoomHarmony {

using State = Harmony::Session;
enum class Change { None, Key, Chart };

inline Change apply(const std::string &line, State &state) {
  if (const auto keyName =
          chalkwalk::ninjam::conventions::extractKeyAnnouncement(line);
      !keyName.empty())
    return Harmony::applyKey(keyName, state) == Harmony::Applied::Key
               ? Change::Key
               : Change::None;

  return Harmony::applyChart(line, state) == Harmony::Applied::Chart
             ? Change::Chart
             : Change::None;
}

} // namespace RoomHarmony
