#include "../src/BotDsp.h"
#include "../src/Euclidean.h"
#include <JuceHeader.h>

#include <string>
#include <vector>

// SharedContractTests -- the properties that must survive extraction into the
// shared Chalkwalk libraries (../../ECOSYSTEM.md).
//
// The counterpart of seq_play/tests/SharedContractTest.cpp. The Euclidean table
// below is byte-identical to the one there, deliberately: two repositories, one
// table. If the implementations ever drift apart, one of the two suites goes
// red. That is the closest thing to a shared test available before there is a
// shared repository, and it is why the duplication is the point rather than an
// oversight.
//
// When this code moves to chalkwalk-music and chalkwalk-dsp, THIS FILE MOVES
// WITH IT and must still pass unchanged. Do not relax an expectation here to
// make a port compile.

class SharedContractTests : public juce::UnitTest {
public:
  SharedContractTests() : juce::UnitTest("SharedContract", "ecosystem") {}

  void runTest() override {
    runEuclideanPhaseContract();
    runEuclideanTable();
    runNamedPatterns();
    runEuclideanRotation();
    runHitEquivalence();
    runPolyBlepSign();
    runHermite();
    runSvfStability();
  }

private:
  static std::string render(const std::vector<bool> &p) {
    std::string s;
    s.reserve(p.size());
    for (bool b : p)
      s += b ? 'x' : '.';
    return s;
  }

  struct EuclidCase {
    int length;
    int pulses;
    const char *expected;
  };

  // THE TABLE. Byte-identical to seq_play/tests/SharedContractTest.cpp.
  static const std::vector<EuclidCase> &table() {
    static const std::vector<EuclidCase> t = {
        {4, 1, "x..."},
        {4, 2, "x.x."},
        {4, 3, "x.xx"},
        {8, 1, "x......."},
        {8, 2, "x...x..."},
        {8, 3, "x..x..x."},  // the tresillo, exactly
        {8, 4, "x.x.x.x."},
        {8, 5, "x.x.xx.x"},  // NOT the cinquillo -- see runNamedPatterns
        {8, 7, "x.xxxxxx"},
        {12, 3, "x...x...x..."},
        {12, 4, "x..x..x..x.."},
        {12, 5, "x..x.x..x.x."},
        {16, 4, "x...x...x...x..."},
        {16, 5, "x...x..x..x..x.."},
        {16, 7, "x..x.x.x..x.x.x."},
        {16, 9, "x.x.x.x.xx.x.x.x"},
    };
    return t;
  }

  // --------------------------------------------------------------------
  // The phase contract. This is the property that settles a live
  // disagreement between three implementations.
  //
  // arps-euclidya uses a Bresenham formulation seeded at steps/2. It produces
  // the SAME NECKLACE rotated: over lengths 2..64 the two never disagree about
  // the rhythm, only about where it starts -- but they differ in 98 of the 120
  // patterns with length <= 16.
  //
  // This formulation always puts an onset on step 0, and that is why it wins.
  // The band's kick depends on it: "the kick lands on the downbeat; everything
  // else moves".
  // --------------------------------------------------------------------
  void runEuclideanPhaseContract() {
    beginTest("every pattern starts on the downbeat");
    for (int length = 1; length <= 64; ++length)
      for (int pulses = 1; pulses <= length; ++pulses) {
        const auto p = Euclidean::pattern(length, pulses, 0);
        expect(!p.empty() && p[0], "E(" + juce::String(pulses) + "," +
                                       juce::String(length) +
                                       ") must place an onset on step 0");
      }
  }

  void runEuclideanTable() {
    beginTest("the shared pattern table");
    for (const auto &c : table()) {
      const auto got = render(Euclidean::pattern(c.length, c.pulses, 0));
      expect(got == c.expected, "E(" + juce::String(c.pulses) + "," +
                                    juce::String(c.length) + ") expected " +
                                    c.expected + " got " + got);
    }
  }

  // The names in the comments are worth being exact about, because one of them
  // was wrong and nothing caught it. `EuclideanTests.cpp` has a case called
  // "E(5,8) is the cinquillo" which only ever counted the onsets, so the claim
  // went unchecked for the life of both projects.
  //
  // E(5,8) here is x.x.xx.x. The cinquillo is x.xx.xx.. They are rotations of
  // one another -- the same relationship this formulation has with
  // arps-euclidya's, which is the point. "Which rotation of the necklace do we
  // mean" is exactly the question the shared library has to answer once, for
  // everybody.
  void runNamedPatterns() {
    beginTest("the named patterns are what we say they are");
    expect(render(Euclidean::pattern(8, 3, 0)) == "x..x..x.",
           "E(3,8) is the tresillo");
    expect(render(Euclidean::pattern(8, 5, 0)) == "x.x.xx.x",
           "E(5,8) is a ROTATION of the cinquillo, not the cinquillo");

    bool reachable = false;
    for (int offset = 0; offset < 8; ++offset)
      if (render(Euclidean::pattern(8, 5, offset)) == "x.xx.xx.")
        reachable = true;
    expect(reachable, "the actual cinquillo is reachable by rotating E(5,8)");
  }

  // Rotation is the caller's escape hatch: any phase is reachable, which is
  // what lets arps-euclidya keep its current sound after adopting this by
  // dialling an offset rather than keeping a second implementation.
  void runEuclideanRotation() {
    beginTest("rotation reaches every phase and is a pure right shift");
    const auto base = Euclidean::pattern(8, 3, 0);
    for (int offset = 0; offset < 8; ++offset) {
      const auto rotated = Euclidean::pattern(8, 3, offset);
      expectEquals((int)rotated.size(), 8);

      int onsets = 0;
      for (bool b : rotated)
        if (b)
          ++onsets;
      expectEquals(onsets, 3);

      bool matches = true;
      for (int i = 0; i < 8; ++i) {
        const int src = ((i - offset) % 8 + 8) % 8;
        if (rotated[(size_t)i] != base[(size_t)src])
          matches = false;
      }
      expect(matches, "rotation is a pure right shift by offset");
    }
  }

  void runHitEquivalence() {
    beginTest("hit and pattern cannot disagree");
    for (int length = 1; length <= 32; ++length)
      for (int pulses = 0; pulses <= length; ++pulses)
        for (int offset = -3; offset <= 3; ++offset) {
          const auto p = Euclidean::pattern(length, pulses, offset);
          for (int i = 0; i < length; ++i)
            expect(p[(size_t)i] == Euclidean::hit(i, length, pulses, offset));
        }
  }

  // --------------------------------------------------------------------
  // polyBLEP: the sign, which is the whole reason this code was worth
  // sharing. It was inverted in seq_play for the life of the project and
  // found within hours of being retyped here; seq_play fixed it in 29db3d3.
  // Both are correct now, and this states the property that an inverted sign
  // breaks, in a form both repositories can assert identically.
  // --------------------------------------------------------------------
  void runPolyBlepSign() {
    beginTest("polyBLEP shrinks the step it corrects rather than enlarging it");
    const double inc = 5000.0 / 48000.0;

    const float justBefore = BotDsp::polyBlepSaw(1.0 - inc * 0.5, inc);
    const float justAfter = BotDsp::polyBlepSaw(inc * 0.5, inc);
    const float naiveBefore = (float)((2.0 * (1.0 - inc * 0.5)) - 1.0);
    const float naiveAfter = (float)((2.0 * (inc * 0.5)) - 1.0);

    expect(std::abs(justBefore - justAfter) <
               std::abs(naiveBefore - naiveAfter),
           "if this fails the polyBLEP sign is inverted");

    beginTest("polyBLEP is inert away from the edge");
    for (double phase = 0.2; phase < 0.8; phase += 0.05)
      expect(std::abs(BotDsp::polyBlepSaw(phase, 0.01) -
                      (float)((2.0 * phase) - 1.0)) < 1.0e-6f);

    beginTest("a zero or negative increment is inert");
    expectEquals(BotDsp::polyBlep(0.5, 0.0), 0.0f);
    expectEquals(BotDsp::polyBlep(0.5, -1.0), 0.0f);
  }

  void runHermite() {
    beginTest("hermite4 is exact on a straight line");
    for (double t = 0.0; t <= 1.0; t += 0.05)
      expect(std::abs(BotDsp::hermite4(0.0f, 1.0f, 2.0f, 3.0f, (float)t) -
                      (1.0f + (float)t)) < 1.0e-5f);

    beginTest("hermite4 passes through its samples");
    expect(std::abs(BotDsp::hermite4(3.0f, 7.0f, 11.0f, 2.0f, 0.0f) - 7.0f) <
           1.0e-5f);
    expect(std::abs(BotDsp::hermite4(3.0f, 7.0f, 11.0f, 2.0f, 1.0f) - 11.0f) <
           1.0e-5f);
  }

  void runSvfStability() {
    beginTest("the filter stays bounded everywhere it will be driven");
    for (float cutoff : {20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f})
      for (float q : {0.3f, 0.707f, 4.0f, 20.0f})
        for (int mode = 0; mode < 4; ++mode) {
          BotDsp::Svf f;
          f.set(cutoff, q, 48000.0);
          float worst = 0.0f;
          for (int i = 0; i < 4096; ++i) {
            const float in = (i % 2 == 0) ? 1.0f : -1.0f; // Nyquist
            worst = std::max(
                worst, std::abs(f.process(in, (BotDsp::Svf::Mode)mode)));
          }
          expect(std::isfinite(worst) && worst < 100.0f,
                 "Svf bounded at cutoff " + juce::String(cutoff) + " q " +
                     juce::String(q) + " mode " + juce::String(mode));
        }
  }
};

static SharedContractTests sharedContractTests;
