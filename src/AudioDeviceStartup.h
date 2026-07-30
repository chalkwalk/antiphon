#pragma once

#include <cstdint>

// Opening the audio device, when the audio device might never answer.
//
// JUCE's stock standalone opens the device inside the window's constructor --
// StandaloneFilterWindow builds its StandalonePluginHolder as a constructor
// argument, so deviceManager.initialise() runs to completion before any window
// exists. If the backend blocks there, the app has no window, no error and no
// route to the device picker: it just sits at the command line. That is not
// hypothetical. An ALSA-to-PipeWire open against a host running two competing
// pipewire daemons deadlocks in a futex, having spawned its worker threads but
// never opened a PCM device or connected to the X server.
//
// So Antiphon shows its window first and opens the device on a worker thread,
// giving it a budget. This is the policy for that: pure and free of JUCE so the
// transitions can be tested directly, with no audio hardware in the loop.

class AudioDeviceStartup {
public:
  enum class State {
    Opening, // probe running, still inside its budget
    Ready,   // device open, audio running
    Failed,  // the backend answered, and the answer was no
    TimedOut // the budget expired with no answer; presumed wedged
  };

  struct Inputs {
    bool probeFinished = false;
    bool probeSucceeded = false;
    int64_t elapsedMs = 0;
  };

  explicit AudioDeviceStartup(int64_t timeoutMs) : budgetMs(timeoutMs) {}

  // Returns true when this call changed the state, so the caller can update
  // the UI on the edge rather than every tick.
  bool update(const Inputs &in) {
    // Every terminal state is sticky. TimedOut especially: by the time it is
    // reached the user has been told the device did not respond and handed the
    // settings dialog, and a wedged probe thread that finally returns minutes
    // later must not yank the app out from under them.
    if (isTerminal())
      return false;

    if (in.probeFinished) {
      state = in.probeSucceeded ? State::Ready : State::Failed;
      return true;
    }

    // The boundary counts as expiry, so a budget of 0 fails immediately rather
    // than waiting one tick.
    if (in.elapsedMs >= budgetMs) {
      state = State::TimedOut;
      return true;
    }

    return false;
  }

  State get() const { return state; }

  bool isTerminal() const { return state != State::Opening; }

  // Starts over -- for the second attempt after the user picks another device.
  void reset() { state = State::Opening; }

  static const char *describe(State s) {
    switch (s) {
    case State::Opening:
      return "Opening the audio device...";
    case State::Ready:
      return "Audio device open";
    case State::Failed:
      return "The audio device could not be opened -- choose another below";
    case State::TimedOut:
      return "The audio device did not respond. It may be in use by another "
             "program. Choose another below";
    }
    return "";
  }

private:
  State state = State::Opening;
  int64_t budgetMs;
};
