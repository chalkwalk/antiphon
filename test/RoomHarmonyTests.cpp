#include "../src/RoomHarmony.h"
#include <JuceHeader.h>

// What a chat line does to the room's key and chart, in ONE place.
//
// It was two: `PracticeBot` learned to read degree charts and to move a chart
// through a key change, and the editor did neither -- so the band followed
// `| ii | V | I |` while the chord row above the phase bar went on showing the
// chart before it, and a key change transposed what you heard and not what you
// read. Two paths that must agree and had no reason to (`PRINCIPLES` 8).

namespace {

MusicalKey::Key keyOf(const char *name) {
  auto k = MusicalKey::parseName(name);
  jassert(k.valid);
  return k;
}

class RoomHarmonyTests : public juce::UnitTest {
public:
  RoomHarmonyTests() : juce::UnitTest("RoomHarmony", "music") {}

  void runTest() override {
    beginTest("a chart somebody typed survives a key change, transposed");
    {
      RoomHarmony::State st;
      st.key = keyOf("C major");
      expectEquals((int)RoomHarmony::apply("| Am | F | C | G |", st),
                   (int)RoomHarmony::Change::Chart);
      expect(st.chartFromChat, "a chart from chat was not recorded as one");

      expectEquals((int)RoomHarmony::apply("[key: D major]", st),
                   (int)RoomHarmony::Change::Key);
      expectEquals(Harmony::chartText(st.chart, st.key),
                   std::string("| Bm | G | D | A |"),
                   "the chart did not travel with the key");
    }

    beginTest("a chart the key implied is rebuilt, not transposed");
    {
      // Nothing was written down, so there is nothing to preserve: the new key
      // gets its own default rather than the old key's default moved.
      RoomHarmony::State st;
      st.key = keyOf("C major");
      st.chart = Harmony::defaultChart(st.key);

      expectEquals((int)RoomHarmony::apply("[key: A minor]", st),
                   (int)RoomHarmony::Change::Key);
      expectEquals(Harmony::chartText(st.chart, st.key),
                   Harmony::chartText(Harmony::defaultChart(keyOf("A minor")),
                                      keyOf("A minor")),
                   "a defaulted chart was moved instead of rebuilt");
    }

    beginTest("degrees are read against the key the room is in");
    {
      RoomHarmony::State st;
      st.key = keyOf("C major");
      expectEquals((int)RoomHarmony::apply("| ii | V | I |", st),
                   (int)RoomHarmony::Change::Chart);
      expectEquals(Harmony::chartText(st.chart, st.key),
                   std::string("| Dm | G | C |"));
      expect(st.chartFromChat, "a degree chart is still a chart somebody wrote");

      // ...and they mean something else in another key, which is the point.
      RoomHarmony::State minor;
      minor.key = keyOf("A minor");
      expectEquals((int)RoomHarmony::apply("| ii | V | I |", minor),
                   (int)RoomHarmony::Change::Chart);
      expect(Harmony::chartText(minor.chart, minor.key) !=
                 Harmony::chartText(st.chart, st.key),
             "degrees resolved to the same chords in two different keys");
    }

    beginTest("degrees need a key, and prose is never a chart");
    {
      RoomHarmony::State none;
      expectEquals((int)RoomHarmony::apply("| ii | V | I |", none),
                   (int)RoomHarmony::Change::None,
                   "degrees were resolved against no key at all");

      RoomHarmony::State st;
      st.key = keyOf("C major");
      for (const char *prose :
           {"I AM TIRED", "what are the chords", "sounds good", "",
            "| not | a | chart |"})
        expectEquals((int)RoomHarmony::apply(prose, st),
                     (int)RoomHarmony::Change::None,
                     juce::String(prose) + " was taken for a chart");
    }

    beginTest("announcing the key twice changes nothing the second time");
    {
      RoomHarmony::State st;
      st.key = keyOf("C major");
      expectEquals((int)RoomHarmony::apply("| Am | F |", st),
                   (int)RoomHarmony::Change::Chart);
      const auto before = Harmony::chartText(st.chart, st.key);

      expectEquals((int)RoomHarmony::apply("[key: D minor]", st),
                   (int)RoomHarmony::Change::Key);
      const auto moved = Harmony::chartText(st.chart, st.key);
      expect(moved != before, "the key change did nothing at all");

      // The same key again is not a change, and must not transpose twice --
      // which is the bug this shape of state is easiest to write.
      expectEquals((int)RoomHarmony::apply("[key: D minor]", st),
                   (int)RoomHarmony::Change::None);
      expectEquals(Harmony::chartText(st.chart, st.key), moved,
                   "re-announcing the key transposed the chart again");
    }
  }
};

static RoomHarmonyTests roomHarmonyTests;

} // namespace
