# Parity with the reference Ninjam client

What we checked against the official client, how, and what the numbers were.

This document exists because the evidence has to outlive the instrument. The
harness in `test/refclient/` is temporary and will be removed before release;
everything it established is recorded here so a future change can be judged
against it without rebuilding the rig.

**Reference implementation:** `references/ninjam/` `NJClient`, driven headless by
`test/refclient/NinjamRefClient` (see `test/refclient/README.md`). It is a
library, not an app, so it runs with no audio backend and takes an explicit
sample rate -- which is what makes a differential test possible at all.

---

## Why differential testing

Our own tests can only show we are self-consistent. They cannot catch a wire
format we have misread the same way twice, or an interval length that is wrong
in a way our clock and our decoder agree about. The reference client is the
implementation every other player on a server is actually running, so
disagreement with it is the definition of the bug we care about.

Three of the defects listed at the bottom were invisible to our own suite and
to listening, and only appeared when a second implementation was in the loop.

---

## Method

Two independent clocks, never ours on both sides:

- **Interval grid** comes from `NJClient::GetPosition()`, sampled alongside the
  captured PCM. Boundaries are therefore marked by the reference client, not by
  us.
- **Signal** is `ninjam::IntervalProbe` (`src/IntervalProbe.h`): a 440 Hz bed
  with 8 ms 3 kHz raised-cosine bursts at 0, 1/4, 1/2 and 3/4 of each interval.
  Shared by the plugin's Test Tone, the tests and the harness, so no two
  definitions can drift apart. A single-sample impulse was rejected: Vorbis
  smears a lone click and does not preserve its amplitude, whereas a few
  milliseconds of tone is exactly what a perceptual codec keeps, and it can be
  located by band energy rather than absolute level.
- **Measurement** is cross-correlation of the 3 kHz energy envelope against the
  expected burst pattern, per interval. Peak-picking was tried first and
  abandoned -- see "Instrument calibration".

Every offset below is quoted against a **reference-to-reference baseline**:
one reference client transmitting the probe, a second receiving it. Without
that baseline an absolute offset is meaningless, because it cannot distinguish
our transmit path from the reference client's own playback alignment.

---

## Instrument calibration

The measurement pipeline is itself verified against synthetic signals with
known ground truth before being believed:

| Ground truth | Reported | Bias |
|---|---|---|
| 0 | -96 | -96 |
| +52 | -32 | -84 |
| +432 | +336 | -96 |
| -100 | -192 | -92 |

A constant instrument bias, subtracted from every figure below. Resolution is
the envelope hop, **16 samples**; nothing finer than that is claimed.

This matters more than it looks. The first two attempts at this measurement
reported standard deviations of 32765 and 15746 samples -- both were the
instrument (a peak-picker latching onto square-wave harmonics), not the client.
Across this project three "failures" have turned out to be measurement error.
Suspect the instrument first.

---

## Results

### Interval grid

Interval length uses the reference client's arithmetic verbatim, truncated
(`njclient.cpp:806`). Agreement was checked where truncation actually bites:

| Tempo | True interval | Both clients | Intervals observed |
|---|---|---|---|
| 100 bpm / 16 bpi @ 48 kHz | 460800.00 | 460800 | 9 |
| 137 bpm / 11 bpi @ 48 kHz | 231240.87 | **231240** | 16 |
| 137 bpm / 16 bpi @ 48 kHz | 336350.36 | 336350 | (earlier run) |

The 137/11 case is the load-bearing one: had either implementation rounded
rather than truncated, the two grids would slip ~0.87 samples per interval and
drift apart steadily. Spacing was exactly 231240 for all 16 intervals with no
observed drift.

### Transmit alignment

| Tempo | Ours -> reference | Reference -> reference | Difference |
|---|---|---|---|
| 100 / 16 | +528, sd 5.3 (n=8) | +528, sd 0.0 (n=9) | **0** |
| 137 / 11 | +364, sd 8.0 (n=10) | +356, sd 0.0 (n=12) | **8 samples (0.17 ms)** |

At both tempos our transmitted audio sits on the reference-to-reference
baseline, within the 16-sample resolution of the measurement.

Note the absolute offset is **not** constant across tempo (528 vs 356). It
changes identically for the official client talking to itself, so it is a
property of the reference client's playback and the codec path, not of our
transmit. Do not "fix" it.

### Chat

Both directions, live, in a DAW (2026-08-08):

```
MSG claudebot@...  Hello from claudebot -- chat test 1 of 2: ...
MSG daniel@...     Yes, I can
```

The reply was typed into the plugin's **inline** chat field, which is the case
the JUCE keyboard-focus patch exists for (see `AGENTS.md`).

### Audio, earlier phases

- Ours -> reference and reference -> ours both decode at 440.0 Hz.
- Level ratio 0.253 in both directions, confirming the -12 dB default remote
  channel convention is symmetric.
- Mid-session tempo and BPI changes (100/16 -> 137/11 by vote) were followed by
  both clients without resync problems.

---

## Corrections this testing forced

**Work item #27 (capture alignment at the interval boundary) was probably never
a real defect.** It was originally recorded as "measured, confirmed real" at
+54.5 samples mean across 5 interval seams, all positive, from an arpeggiator
run with onset detection. Re-measured with the calibrated probe:

- Our interval placement matches the reference-to-reference baseline (above).
- A constant offset cancels out of every inter-onset interval, so a transmit
  alignment error of that kind cannot produce a seam-only error anyway.
- Repeating the original arp measurement after the "fix" gave +52.2 sd 21.5 --
  statistically identical to the +54.5 it was supposed to remove.

The most likely explanation is onset-detection artifact on note attacks near a
seam. The sample-exact capture commit (`02a58b2`) is correct on its own merits
and stays, but it fixed nothing measurable.

**Drift is unmeasured, not clean.** A rounding bug would accumulate ~0.87
samples per interval, i.e. ~8.7 samples over the 10 intervals observed -- below
the 16-sample resolution. A least-squares fit reported +0.48 samples/interval,
but that is an artifact of readings alternating between two quantisation bins;
the same alternating series reproduces the slope exactly. Resolving this needs
a longer run or a finer envelope hop.

---

## Defects the differential rig exposed

Each was invisible to our own suite, and each now has a test that fails without
the fix:

- **Encoder sample rate hardcoded to 48000** regardless of session rate. At
  44.1 kHz we transmitted 8.8% sharp and 8% short; at 96 kHz an octave down.
- **Five unbounded string reads past the end of the payload** in the `0x03` and
  `0xC0` parsers, including a 6-vs-4-byte off-by-two. Reproduced as a heap
  buffer overflow under ASan.
- **`callAsync` use-after-free** in `NinjamClient`, found by ASan as
  `stack-use-after-return` where gdb had nothing useful to say.
- **`writeFull` called unlocked from three threads**, which interleaves frames
  and desynchronises the server -- presenting as a random mid-jam disconnect.
- **`SERVER_AUTH_REPLY` was missing its `errmsg` + `maxchan` tail**, so the
  reference client saw maxchan=0 and refused to transmit to our fake server.
  Exactly the self-referential blind spot the rig exists to find.
- **Jamtaba `JTBv` video intervals queued as audio**, creating a permanently
  silent stream that swapped every interval.

---

## Not covered

Honest gaps, so nobody reads more into this than it supports:

- Multi-channel transmit was not differentially tested; all runs used one
  channel.
- Drift over long sessions (see above).
- Sample rates other than 48 kHz were swept in our own suite but not against
  the reference client live.
- Only Linux, only CLAP, only one host.
