#include "../src/Euclidean.h"
#include <JuceHeader.h>

// Ported alongside the generator from chalkwalk/seq_play
// (tests/EuclideanTest.cpp), plus the cases this codebase cares about: that
// `hit` and `pattern` cannot disagree, since the bots use the first in the
// render loop and the second to reason about a bar.

class EuclideanTests : public juce::UnitTest {
public:
  EuclideanTests() : juce::UnitTest("Euclidean", "music") {}

  void runTest() override {
    runClassicPatterns();
    runEdgeCases();
    runRotation();
    runEquivalence();
    runAccents();
  }

  void runClassicPatterns() {
    beginTest("E(3,8) is the tresillo");
    {
      const auto r = Euclidean::pattern(8, 3);
      expectEquals((int)r.size(), 8);
      expect(r[0] && !r[1] && !r[2] && r[3] && !r[4] && !r[5] && r[6] && !r[7],
             "E(3,8) should be 1,0,0,1,0,0,1,0");
      expectEquals(countOnsets(r), 3);
    }

    beginTest("E(5,8) is the cinquillo");
    {
      const auto r = Euclidean::pattern(8, 5);
      expectEquals((int)r.size(), 8);
      expectEquals(countOnsets(r), 5);
    }

    beginTest("E(4,16) is four on the floor");
    {
      const auto r = Euclidean::pattern(16, 4);
      expect(r[0] && r[4] && r[8] && r[12]);
      expect(!r[1] && !r[5] && !r[9] && !r[13]);
      expectEquals(countOnsets(r), 4);
    }

    beginTest("onsets are as evenly spread as the length allows");
    {
      // The property that makes these musical rather than arbitrary: no two
      // gaps differ by more than one step.
      for (int length = 2; length <= 32; ++length)
        for (int pulses = 1; pulses <= length; ++pulses) {
          const auto r = Euclidean::pattern(length, pulses);
          std::vector<int> gaps;
          int last = -1;
          for (int i = 0; i < length; ++i)
            if (r[(size_t)i]) {
              if (last >= 0)
                gaps.push_back(i - last);
              last = i;
            }
          if (gaps.size() < 2)
            continue;
          const int lo = *std::min_element(gaps.begin(), gaps.end());
          const int hi = *std::max_element(gaps.begin(), gaps.end());
          expect(hi - lo <= 1, "E(" + juce::String(pulses) + "," +
                                   juce::String(length) + ") gaps " +
                                   juce::String(lo) + ".." + juce::String(hi));
        }
    }
  }

  void runEdgeCases() {
    beginTest("degenerate inputs produce something, not a crash");
    {
      const auto none = Euclidean::pattern(8, 0);
      expectEquals((int)none.size(), 8);
      expectEquals(countOnsets(none), 0);

      const auto full = Euclidean::pattern(8, 8);
      expectEquals(countOnsets(full), 8);

      const auto clamped = Euclidean::pattern(4, 10);
      expectEquals((int)clamped.size(), 4);
      expectEquals(countOnsets(clamped), 4);

      expect(Euclidean::pattern(0, 0).empty());
      expect(Euclidean::pattern(-3, 2).empty());

      const auto negative = Euclidean::pattern(8, -1);
      expectEquals(countOnsets(negative), 0);

      expect(!Euclidean::hit(0, 0, 1));
      expect(!Euclidean::hit(0, 8, 0));
      expect(Euclidean::hit(3, 8, 8), "all-onset should hit everywhere");
    }
  }

  void runRotation() {
    beginTest("rotation moves onsets forward");
    {
      const auto base = Euclidean::pattern(8, 3);
      const auto plus1 = Euclidean::pattern(8, 3, 1);
      for (int i = 0; i < 8; ++i)
        expect(plus1[(size_t)i] == base[(size_t)((i + 8 - 1) % 8)],
               "step " + juce::String(i));
    }

    beginTest("rotation wraps in both directions and by more than a cycle");
    {
      const auto base = Euclidean::pattern(8, 3);
      expect(Euclidean::pattern(8, 3, 8) == base, "a full turn is no turn");
      expect(Euclidean::pattern(8, 3, -8) == base);
      expect(Euclidean::pattern(8, 3, 9) == Euclidean::pattern(8, 3, 1));
      expect(Euclidean::pattern(8, 3, -1) == Euclidean::pattern(8, 3, 7));
    }
  }

  void runEquivalence() {
    beginTest("hit agrees with pattern everywhere, at every rotation");
    {
      // The bots call hit in the render loop and pattern when reasoning about a
      // bar. If these ever disagreed the drums would not match themselves.
      for (int length = 1; length <= 24; ++length)
        for (int pulses = 0; pulses <= length; ++pulses)
          for (int rot = -length; rot <= length; ++rot) {
            const auto p = Euclidean::pattern(length, pulses, rot);
            for (int i = 0; i < length; ++i)
              if (Euclidean::hit(i, length, pulses, rot) != p[(size_t)i]) {
                expect(false, "disagreement at E(" + juce::String(pulses) + "," +
                                  juce::String(length) + ") rot " +
                                  juce::String(rot) + " step " +
                                  juce::String(i));
                return;
              }
          }
      expect(true);
    }

    beginTest("hit is stable outside the first cycle");
    {
      for (int i = 0; i < 8; ++i) {
        expect(Euclidean::hit(i, 8, 3) == Euclidean::hit(i + 8, 8, 3));
        expect(Euclidean::hit(i, 8, 3) == Euclidean::hit(i - 8, 8, 3));
      }
    }
  }

  void runAccents() {
    beginTest("accents fall on onsets and nowhere else");
    {
      const auto v = Euclidean::accents(8, 3, 0, 1);
      const auto p = Euclidean::pattern(8, 3);
      expectEquals((int)v.size(), 8);
      for (int i = 0; i < 8; ++i)
        expect((v[(size_t)i] > 0) == p[(size_t)i], "step " + juce::String(i));

      int accented = 0;
      for (int x : v)
        if (x == Euclidean::kAccentedVelocity)
          ++accented;
      expectEquals(accented, 1);
    }

    beginTest("asking for no accents still velocities the onsets");
    {
      const auto v = Euclidean::accents(8, 3, 0, 0);
      for (int i = 0; i < 8; ++i)
        if (v[(size_t)i] != 0)
          expectEquals(v[(size_t)i], Euclidean::kOnsetVelocity);
    }

    beginTest("more accents than onsets is clamped, not overflowed");
    {
      const auto v = Euclidean::accents(8, 3, 0, 99);
      int accented = 0;
      for (int x : v)
        if (x == Euclidean::kAccentedVelocity)
          ++accented;
      expectEquals(accented, 3, "every onset should accent, and no more");
    }

    beginTest("no onsets means no velocities");
    {
      const auto v = Euclidean::accents(8, 0, 0, 2);
      expectEquals((int)v.size(), 8);
      for (int x : v)
        expectEquals(x, 0);
    }
  }

private:
  static int countOnsets(const std::vector<bool> &p) {
    int n = 0;
    for (bool b : p)
      if (b)
        ++n;
    return n;
  }
};

static EuclideanTests euclideanTests;
