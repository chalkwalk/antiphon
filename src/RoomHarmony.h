#pragma once

#include "Harmony.h"
#include "MusicalKey.h"
#include <JuceHeader.h>

// What a chat line does to the room's key and chart.
//
// One place, because there are two readers -- the band and the display -- and
// they must agree. They did not: `PracticeBot` learned to read degree charts
// and to move a chart through a key change, the editor did neither, and the
// result was the band following `| ii | V | I |` while the chord row above the
// phase bar went on showing the chart before it. Nothing announced the
// divergence; you had to hear it (`PRINCIPLES` 8).
//
// Pure and JUCE-light, so it can be tested directly. `PluginEditor` cannot be
// compiled into the test target at all, which is exactly why the decision does
// not belong there.

namespace RoomHarmony {

struct State {
  MusicalKey::Key key;
  Harmony::Chart chart;

  // Whether the chart is one somebody wrote, or one the key implied.
  //
  // This is what a key change turns on: preserve what was written, re-derive
  // what was delegated (`DESIGN.md` section 6.4). A chart nobody chose has
  // nothing worth transposing, and moving it would carry the old key's default
  // into a key with a perfectly good default of its own.
  bool chartFromChat = false;
};

enum class Change { None, Key, Chart };

// The subset of chat that needs no address, because its SYNTAX is unmistakable:
// a `[key: Dm]` tag, a `| Am | F |` chart, or a degree chart against the key
// the room is already in. Nobody writes any of them by accident.
inline Change apply(const juce::String &text, State &state) {
  if (const auto key = MusicalKey::parseAnnouncement(text); key.valid) {
    // Re-announcing the key the room is already in is not a change, and acting
    // on it would transpose a chart that has not moved.
    if (key == state.key)
      return Change::None;

    if (state.chartFromChat && state.key.valid)
      state.chart =
          Harmony::resolve(Harmony::toRelative(state.chart, state.key), key);
    else
      state.chart = Harmony::defaultChart(key);

    state.key = key;
    return Change::Key;
  }

  Harmony::Chart chart;
  if (Harmony::parseChart(text, chart) ||
      (state.key.valid && Harmony::parseDegreeChart(text, state.key, chart))) {
    state.chart = std::move(chart);
    state.chartFromChat = true;
    return Change::Chart;
  }

  return Change::None;
}

} // namespace RoomHarmony
