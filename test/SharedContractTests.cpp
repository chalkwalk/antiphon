#include "../src/BotDsp.h"
#include <JuceHeader.h>

#include <vector>

// SharedContractTests -- the properties that must survive extraction into the
// shared Chalkwalk libraries.
//
// The counterpart of the same suite in the other consumer. The Euclidean table
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
    runPolyBlepSign();
    runHermite();
    runSvfStability();
  }

private:
  // --------------------------------------------------------------------
  // polyBLEP: the sign, which is the whole reason this code was worth
  // sharing. It was inverted at its origin for the life of that project and
  // found within hours of being retyped here; it has since been fixed there.
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
