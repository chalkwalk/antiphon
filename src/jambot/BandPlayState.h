#pragma once

// Whether a bot is playing, and how it stops.
//
// A jam is not one continuous take: you play a tune, you stop, you agree a new
// key and a tempo, and you start again. A bot that plays from the moment it
// connects until it is evicted has no state for any of that, and the only way
// to make it stop is to make it leave.
//
// Stopping is TWO INTERVALS, not an off switch. A band ending a tune plays a
// last time through -- lead laying out, kit filling -- and then lands together
// on the final chord. A chord arriving on a downbeat with nothing leading into
// it is not an ending, it is a dropout with a note on the front.
//
// Designed in `docs/BOT-CHAT.md` section 15. Pure and free of JUCE so the
// interval-by-interval timing can be driven directly: through a room and a
// socket it is only observable as several seconds of audio.

class BandPlayState {
public:
  enum class State {
    Silent,   // present, not transmitting. Where a bot waits between tunes.
    Playing,  // the groove
    Wrapping, // one interval: play it out, and say the end is coming
    Resolving // one interval: the final chord, then quiet
  };

  State current() const { return state; }

  // Only silence is inaudible. The two ending states transmit -- that is the
  // whole point of them, and a bot that fell silent the moment it was asked to
  // stop would have no ending at all.
  bool audible() const { return state != State::Silent; }

  // One interval has passed. Only an ending has a clock: playing and silence
  // are where a bot stays until somebody asks for something, so this is a
  // no-op in both and is called every interval regardless.
  void advance() {
    if (state == State::Wrapping)
      state = State::Resolving;
    else if (state == State::Resolving)
      state = State::Silent;
  }

  // Asked to play. From the wrap-up this CANCELS the ending -- "no, keep
  // going" is said in rehearsals constantly, and the wrap-up is the window in
  // which it still means something.
  //
  // Not from the resolve. By then the wrap-up has been heard and the final
  // chord is the only musical way out; starting again is a new start, after
  // the silence.
  void start() {
    if (state != State::Resolving)
      state = State::Playing;
  }

  // Cut, with no ending at all.
  //
  // For the one case that earns it: the room has emptied, so there is nobody
  // to play an ending TO. Two intervals of wrapping up and resolving to an
  // audience of nobody is encoding for its own sake, and the gesture is only a
  // gesture if somebody hears it.
  //
  // Nothing a PLAYER asks for reaches this. "stop" goes through both intervals,
  // because that is what makes it an ending rather than a mute.
  void silence() { state = State::Silent; }

  // Asked to stop. Only from playing: stopping something already stopping
  // would skip the wrap-up, which is the half that makes the ending an ending.
  void stop() {
    if (state == State::Playing)
      state = State::Wrapping;
  }

private:
  State state = State::Silent;
};
