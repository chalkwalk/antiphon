#include <JuceHeader.h>

// Console runner for the juce::UnitTest suite.
//
// JUCE_UNIT_TESTS is deliberately NOT defined for this target: juce::UnitTest
// itself is compiled into juce_core unconditionally, and the flag would only
// add JUCE's own internal test suite to runAllTests().
//
// Usage: NinjamTests [name-substring ...]
//   With no arguments, runs everything. With arguments, runs only those tests
//   whose name contains one of the given substrings (case-insensitive).

int main(int argc, char **argv) {
  juce::ScopedJuceInitialiser_GUI juceInit;

  juce::StringArray filters;
  for (int i = 1; i < argc; ++i)
    filters.add(juce::String(argv[i]));

  juce::Array<juce::UnitTest *> selected;
  for (auto *t : juce::UnitTest::getAllTests()) {
    if (filters.isEmpty()) {
      selected.add(t);
      continue;
    }
    for (const auto &f : filters) {
      if (t->getName().containsIgnoreCase(f)) {
        selected.add(t);
        break;
      }
    }
  }

  if (selected.isEmpty()) {
    std::cout << "No tests matched." << std::endl;
    return 1;
  }

  juce::UnitTestRunner runner;
  runner.setAssertOnFailure(false);
  runner.setPassesAreLogged(false);
  runner.runTests(selected);

  int failures = 0, passes = 0;
  for (int i = 0; i < runner.getNumResults(); ++i) {
    const auto *r = runner.getResult(i);
    failures += r->failures;
    passes += r->passes;
  }

  std::cout << "\n=========================================\n"
            << (failures == 0 ? "PASSED" : "FAILED") << ": " << passes
            << " passes, " << failures << " failures" << std::endl;

  return failures == 0 ? 0 : 1;
}
