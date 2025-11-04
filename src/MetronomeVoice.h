#pragma once

// One-shot metronome click.
//
// Split out of the interval loop so its pitch can be verified independently.
// The previous inline formula swept 2*pi*freq/bpm radians across a click
// lasting 0.05 beats, which sounds every click a fixed factor below its
// nominal pitch and never references the sample rate at all.

class MetronomeVoice {
public:
  void prepare(double sampleRate);

  // Selects pitch and level from the beat position: beat 0 is the interval
  // downbeat, every fourth beat is a bar start, everything else is a plain
  // beat. Restarts the click from the beginning.
  void trigger(int beatIndex);

  // Adds the click into dst[0 .. numSamples), scaled by gain. Does nothing
  // when idle.
  void render(float *dst, int numSamples, float gain);

  bool isActive() const { return samplesRemaining > 0; }
  void reset() { samplesRemaining = 0; }

  // Exposed for tests.
  double currentFrequency() const { return frequency; }
  int clickLengthSamples() const { return samplesTotal; }

private:
  static constexpr double kClickSeconds = 0.025;

  double sampleRate = 0.0;
  double frequency = 0.0;
  double phase = 0.0;
  double phaseIncrement = 0.0;
  float amplitude = 0.0f;
  int samplesTotal = 0;
  int samplesRemaining = 0;
};
