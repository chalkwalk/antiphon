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

  // Tells the voice how long the interval is, so it can decide whether a bar
  // accent means anything.
  //
  // A 4-beat bar accent only makes sense when the interval divides into whole
  // bars. At BPI 11 accenting every fourth beat gives 1, 5, 9 and then a group
  // of only 3 before the interval wraps, which reads as the click losing time
  // even though the grid is exact. The reference client sidesteps this by
  // having no bar concept at all -- it accents beat 0 and nothing else
  // (references/libninjam/ninjam/njclient.cpp:1478) -- and that is what we fall
  // back to for a BPI that is not whole bars.
  void setBeatsPerInterval(int bpi);

  // Selects pitch and level from the beat position: beat 0 is the interval
  // downbeat, a bar start is every fourth beat where the interval divides into
  // whole bars, and everything else is a plain beat. Restarts the click.
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

  // 4 when the interval divides into whole bars, 0 when it does not (meaning
  // no bar accent at all). Defaults to 4, the BPI-16 case.
  int beatsPerBar = 4;

  double sampleRate = 0.0;
  double frequency = 0.0;
  double phase = 0.0;
  double phaseIncrement = 0.0;
  float amplitude = 0.0f;
  int samplesTotal = 0;
  int samplesRemaining = 0;
};
