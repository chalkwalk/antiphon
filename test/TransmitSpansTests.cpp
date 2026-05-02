#include <JuceHeader.h>

#include "TransmitSpans.h"

#include <vector>

namespace {

class TransmitSpansTests : public juce::UnitTest {
public:
  TransmitSpansTests() : juce::UnitTest("TransmitSpans", "TransmitSpans") {}

  // Renders the mask as a per-sample picture, which is what the spans mean.
  std::vector<bool> expand(const TransmitSpans &t, int length) {
    std::vector<bool> out((size_t)length, false);
    for (int i = 0, n = t.spanCount(length); i < n; ++i) {
      const auto s = t.span(i, length);
      for (int k = 0; k < s.count; ++k)
        out[(size_t)(s.start + k)] = s.on;
    }
    return out;
  }

  void expectPattern(const TransmitSpans &t, int length,
                     const juce::String &expected) {
    // "expected" uses '#' for transmitting and '.' for silent, one character
    // per sample, which makes a failure readable at a glance.
    const auto got = expand(t, length);
    juce::String actual;
    for (int i = 0; i < length; ++i)
      actual += got[(size_t)i] ? "#" : ".";
    expectEquals(actual, expected);
  }

  void runTest() override {
    beginTest("an interval with transmit on throughout is one span");
    {
      TransmitSpans t;
      t.beginInterval(true);
      expectEquals(t.spanCount(10), 1);
      expect(t.span(0, 10).on);
      expectEquals(t.span(0, 10).count, 10);
      expect(t.anyActive(10));
      expectPattern(t, 10, "##########");
    }

    beginTest("an interval with transmit off throughout sends nothing");
    {
      // The rule that decides whether the interval is uploaded at all.
      TransmitSpans t;
      t.beginInterval(false);
      expect(!t.anyActive(10));
      expectPattern(t, 10, "..........");
    }

    beginTest("a change part way through splits the interval");
    {
      TransmitSpans t;
      t.beginInterval(false);
      expect(t.setStateAt(4, true));
      expectEquals(t.spanCount(10), 2);
      expect(t.anyActive(10));
      expectPattern(t, 10, "....######");
    }

    beginTest("toggling repeatedly gives the rhythm you played");
    {
      // The behaviour the per-interval flag could not express at all: on and
      // off within one interval should sound to others like muting in that
      // rhythm.
      TransmitSpans t;
      t.beginInterval(true);
      expect(t.setStateAt(2, false));
      expect(t.setStateAt(4, true));
      expect(t.setStateAt(6, false));
      expect(t.setStateAt(8, true));
      expectPattern(t, 10, "##..##..##");
      expect(t.anyActive(10));
    }

    beginTest("a change to the state it already has is not a transition");
    {
      TransmitSpans t;
      t.beginInterval(true);
      expect(t.setStateAt(3, true));
      expect(t.setStateAt(5, true));
      expectEquals(t.transitionCount(), 0);
      expectPattern(t, 10, "##########");
    }

    beginTest("two changes at the same sample cancel");
    {
      // Otherwise the pair would leave a zero-length span and break the
      // assumption that transition positions strictly increase.
      TransmitSpans t;
      t.beginInterval(true);
      expect(t.setStateAt(4, false));
      expect(t.setStateAt(4, true));
      expectEquals(t.transitionCount(), 0);
      expectPattern(t, 10, "##########");
    }

    beginTest("a change at sample zero is the interval's state");
    {
      TransmitSpans t;
      t.beginInterval(false);
      expect(t.setStateAt(0, true));
      expectEquals(t.transitionCount(), 0);
      expectPattern(t, 10, "##########");
    }

    beginTest("a change at or past the end does not create an empty span");
    {
      TransmitSpans t;
      t.beginInterval(true);
      expect(t.setStateAt(10, false));
      expectEquals(t.spanCount(10), 1);
      expectPattern(t, 10, "##########");
    }

    beginTest("retroactively enabling makes the whole interval transmit");
    {
      // The gesture. Transmit was off, you played, then you held the button:
      // the ordinary toggle already set the state from the press onwards, so
      // making the earlier part match leaves one uniform span.
      TransmitSpans t;
      t.beginInterval(false);
      expect(t.setStateAt(6, true)); // the plain toggle at the press
      expectPattern(t, 10, "......####");

      t.makeWholeInterval(true); // the hold, applied from the press moment
      expectEquals(t.transitionCount(), 0);
      expectPattern(t, 10, "##########");
      expect(t.anyActive(10));
    }

    beginTest("retroactively disabling drops everything played so far");
    {
      TransmitSpans t;
      t.beginInterval(true);
      expect(t.setStateAt(6, false));
      expectPattern(t, 10, "######....");

      t.makeWholeInterval(false);
      expectPattern(t, 10, "..........");
      expect(!t.anyActive(10),
             "an interval retroactively silenced must not be sent");
    }

    beginTest("transmit can be turned back on after a retroactive silence");
    {
      TransmitSpans t;
      t.beginInterval(true);
      t.makeWholeInterval(false);
      expect(t.setStateAt(7, true));
      expectPattern(t, 10, ".......###");
      expect(t.anyActive(10));
    }

    beginTest("running out of transitions drops the change, not the audio");
    {
      TransmitSpans t;
      t.beginInterval(false);
      bool everRefused = false;
      for (int i = 1; i <= TransmitSpans::kMaxTransitions + 20; ++i)
        if (!t.setStateAt(i, i % 2 == 1))
          everRefused = true;

      expect(everRefused, "the list must report when it is full");
      expect(t.overflowed());
      expectEquals(t.transitionCount(), TransmitSpans::kMaxTransitions);
      // Still coherent: it just stops changing after the cap.
      expect(t.spanCount(10000) > 1);
    }

    beginTest("applying the spans silences the off parts and keeps the rest");
    {
      TransmitSpans t;
      t.beginInterval(true);
      t.setStateAt(200, false);

      std::vector<float> l(400, 1.0f), r(400, 1.0f);
      float *chans[2] = {l.data(), r.data()};
      t.applyTo(chans, 2, 400, 8);

      // Well inside the on span, untouched.
      expectWithinAbsoluteError(l[50], 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(r[50], 1.0f, 1.0e-6f);
      // Well inside the off span, silent.
      expectWithinAbsoluteError(l[350], 0.0f, 1.0e-6f);
      expectWithinAbsoluteError(r[350], 0.0f, 1.0e-6f);
    }

    beginTest("every edge is ramped, so switching transmit cannot click");
    {
      // What the other players hear. A hard edge in the transmitted audio is a
      // click baked into everyone else's mix, so it is worth asserting that no
      // single sample step is large.
      TransmitSpans t;
      t.beginInterval(true);
      t.setStateAt(200, false);
      t.setStateAt(300, true);

      std::vector<float> buf(500, 1.0f);
      float *chans[1] = {buf.data()};
      t.applyTo(chans, 1, 500, 32);

      double worst = 0.0;
      for (size_t i = 1; i < buf.size(); ++i)
        worst = std::max(worst, (double)std::abs(buf[i] - buf[i - 1]));
      expect(worst < 0.2, "largest step was " + juce::String(worst, 4) +
                              " -- that is an edge, not a ramp");
    }

    beginTest("a zero-length ramp still produces the right spans");
    {
      // Degenerate but reachable if a sample rate is nonsense.
      TransmitSpans t;
      t.beginInterval(true);
      t.setStateAt(4, false);
      std::vector<float> buf(8, 1.0f);
      float *chans[1] = {buf.data()};
      t.applyTo(chans, 1, 8, 0);
      expectWithinAbsoluteError(buf[0], 1.0f, 1.0e-6f);
      expectWithinAbsoluteError(buf[7], 0.0f, 1.0e-6f);
    }
  }
};

static TransmitSpansTests transmitSpansTests;

} // namespace
