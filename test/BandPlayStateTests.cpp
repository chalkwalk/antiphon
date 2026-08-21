#include "../src/jambot/BandPlayState.h"
#include <JuceHeader.h>

// The four states a bot's playing goes through, and nothing else. Pure, so the
// transitions are driven directly rather than through a room and a socket --
// which is the only way to test the interval-by-interval timing at all.

namespace {

juce::String nameOf(BandPlayState::State s) {
  switch (s) {
  case BandPlayState::State::Silent: return "Silent";
  case BandPlayState::State::Playing: return "Playing";
  case BandPlayState::State::Wrapping: return "Wrapping";
  case BandPlayState::State::Resolving: return "Resolving";
  }
  return "?";
}

class BandPlayStateTests : public juce::UnitTest {
public:
  BandPlayStateTests() : juce::UnitTest("BandPlayState", "bots") {}

  using S = BandPlayState::State;

  void expectState(const BandPlayState &b, S wanted, const juce::String &why) {
    expect(b.current() == wanted,
           why + ": wanted " + nameOf(wanted) + ", got " + nameOf(b.current()));
  }

  void runTest() override {
    beginTest("an ending is exactly two intervals, and then silence");
    {
      // The shape the whole design rests on: one full interval to wrap up, one
      // to resolve, and quiet after. Anything that takes three intervals to
      // stop, or one, is a different feature (docs/BOT-CHAT.md section 15).
      BandPlayState b;
      b.start();
      expectState(b, S::Playing, "start from silence");

      b.stop();
      expectState(b, S::Wrapping, "stop begins the wrap-up");

      b.advance();
      expectState(b, S::Resolving, "the wrap-up lasts one interval");

      b.advance();
      expectState(b, S::Silent, "the resolve lasts one interval");

      b.advance();
      expectState(b, S::Silent, "silence is where it stays");
    }

    beginTest("playing and silence do not advance on their own");
    {
      // Only an ending has a clock. A bot left playing plays until it is asked
      // to stop, and a bot left silent stays silent -- so `advance` being
      // called every interval forever must be a no-op in both.
      BandPlayState playing;
      playing.start();
      for (int i = 0; i < 100; ++i)
        playing.advance();
      expectState(playing, S::Playing, "a hundred intervals of playing");

      BandPlayState silent;
      for (int i = 0; i < 100; ++i)
        silent.advance();
      expectState(silent, S::Silent, "a hundred intervals of silence");
    }

    beginTest("starting during the wrap-up cancels the ending");
    {
      // "no, keep going" is said in rehearsals constantly, and the wrap-up is
      // the window in which it still means something.
      BandPlayState b;
      b.start();
      b.stop();
      expectState(b, S::Wrapping, "stopping");

      b.start();
      expectState(b, S::Playing, "starting during the wrap-up");

      // And it really is cancelled, rather than merely delayed: the interval
      // that would have been the resolve is an ordinary playing interval.
      b.advance();
      expectState(b, S::Playing, "the interval after the cancel");
    }

    beginTest("nothing escapes the resolve");
    {
      // By then the wrap-up has been heard and the final chord is the only
      // musical way out. Starting again is a NEW start, after the silence.
      BandPlayState b;
      b.start();
      b.stop();
      b.advance();
      expectState(b, S::Resolving, "one interval into the ending");

      b.start();
      expectState(b, S::Resolving, "starting during the resolve");
      b.stop();
      expectState(b, S::Resolving, "stopping during the resolve");

      b.advance();
      expectState(b, S::Silent, "the resolve still finishes");
      b.start();
      expectState(b, S::Playing, "and starting works again afterwards");
    }

    beginTest("asking twice for what is already happening changes nothing");
    {
      BandPlayState b;
      b.stop();
      expectState(b, S::Silent, "stopping a silent bot");

      b.start();
      b.start();
      expectState(b, S::Playing, "starting twice");

      b.stop();
      b.stop();
      expectState(b, S::Wrapping, "stopping twice does not skip the wrap-up");
    }

    beginTest("an empty room gets silence, not an ending");
    {
      // The one transition that skips the ending, and it earns it: an ending
      // is FOR somebody. Played to a room with nobody in it, it is two
      // intervals of encoding and a gesture nobody sees. `silence` is what the
      // owner-departure rule reaches for, and nothing else should.
      BandPlayState b;
      b.start();
      b.silence();
      expectState(b, S::Silent, "silencing a playing bot");
      expect(!b.audible(), "a silenced bot is still audible");

      // From mid-ending too: if the room empties while a tune is ending, the
      // rest of the ending has no audience either.
      BandPlayState ending;
      ending.start();
      ending.stop();
      ending.silence();
      expectState(ending, S::Silent, "silencing during the wrap-up");

      // ...and it is not a way to skip an ending you asked for: `stop` still
      // goes through both intervals.
      BandPlayState asked;
      asked.start();
      asked.stop();
      expectState(asked, S::Wrapping, "stop still wraps up");
    }

    beginTest("only silence is inaudible");
    {
      // What the render path branches on. The two ending states are audible --
      // that is the entire point of them -- so a bot that went quiet the moment
      // it was asked to stop would have no ending at all.
      BandPlayState b;
      expect(!b.audible(), "silence is audible");

      b.start();
      expect(b.audible(), "playing is inaudible");

      b.stop();
      expect(b.audible(), "the wrap-up is inaudible");

      b.advance();
      expect(b.audible(), "the resolve is inaudible");

      b.advance();
      expect(!b.audible(), "silence after the ending is audible");
    }
  }
};

static BandPlayStateTests bandPlayStateTests;

} // namespace
