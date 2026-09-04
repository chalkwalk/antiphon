# Phrases Inside The Interval Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A figure whose period is a fraction of the interval, repeated, with
each repeat performed differently -- so the band plays a riff rather than eight
bars of through-composed line.

**Architecture:** The foundation's full part gains a **period** and is tiled
across the interval. The period comes from the chart's internal repeat where it
has one and from a seed-chosen fraction where it does not. `performanceSeed`
adds an interval term to the per-hit noise seeds, so a returning figure is the
same music and a different take. The bass's pitch choice becomes a function of
the phrase-relative step, so the riff repeats as a shape while its landing notes
re-root under it.

**Tech Stack:** C++17, `chalkwalk-music` (chart analysis), `chalkwalk-jambot`
(the band), `AntiphonVoiceLab` for rendering and for the parity check.

**Spec:** `docs/superpowers/specs/2026-09-03-foundation-stratum-and-phrases.md`
section 8 step 2.

**Prerequisite, already landed:** the foundation stratum
(`docs/superpowers/plans/2026-09-03-foundation-stratum.md`). The kit and the
bass select from one shared part, so a period applies to one figure rather than
to two that would drift apart.

## Two of the spec's three open questions are now closed, and not by listening

**Section 7 question 1 -- how many common tones before a contour repeats
literally -- does not arise for this landing.** It was written expecting the
repeat to carry absolute pitches that might clash with a chord they were not
written over. The bass does not work that way: `renderBass` picks
`semitoneAboveRoot` from {root, octave, the chord's own fifth} and adds it to
the CURRENT chord's root. The pitch material is already expressed relative to
the harmony, so repeating the CHOICE over a different chord yields that chord's
root, octave or fifth. It is compatible by construction, and no threshold is
needed.

The question returns when the lead gets a phrase, because a lead line is
absolute MIDI chosen against a contour. The lead is not in this landing.

**Section 7 question 2 -- whether the seed's multiple of the chart period should
be bounded by the interval -- is arithmetic.** There is no form yet, so nothing
exists for a phrase longer than an interval to mean. The period must divide the
part's step count so that tiling is exact; that bounds it.

**Section 7 question 3 -- whether the fraction fallback should align to bar
lines -- is the one that still wants ears**, and Task 5 is where it gets them.

## Global Constraints

- **Three repositories.** Task 1 is in `libs/music` (`chalkwalk-music`), Tasks
  2-4 in `libs/jambot`, and the parity instrument is Antiphon's
  `AntiphonVoiceLab`. Iterate with `-DCHALKWALK_MUSIC_DIR=...` and
  `-DCHALKWALK_JAMBOT_DIR=...`; bump each submodule before calling anything
  done.
- **ASCII only in source files.** `--` for an em dash, `->` for an arrow.
- **2-space indent, braces on the same line, members lowerCamelCase.**
- **No comments except for non-obvious invariants**, explaining *why*.
- **THE AUDIO CHANGES IN EXACTLY ONE COMMIT.** Tasks 1 and 2 are pure functions
  that nothing calls yet, and must leave `AntiphonVoiceLab parity` identical.
  Task 3 is the audible landing and its parity diff is EXPECTED to be
  non-empty -- reviewed by listening, not diffed to zero. Tasks 4 and 5 must be
  silent again.
- **`performanceSeed` and the phrase land together, in Task 3.** A fresh jitter
  per interval changes the audio for no benefit until something returns for it
  to differentiate, and these commits go straight to `main`, so an intermediate
  commit is a shipped state (`ROADMAP.md`, *Form*).
- **Build:** `cmake -B build-vote -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`,
  then `cmake --build build-vote -j $(nproc)` **from the repository root**. A
  `cd` into a submodule persists between tool calls and silently resolves
  `build-vote` against the wrong directory; use absolute paths or return to the
  root first.
- **Capture a parity baseline before Task 3 and keep it**, so the audible change
  can be described rather than merely observed:
  `AntiphonVoiceLab parity --key "C major" > /tmp/parity-before.txt`

---

### Task 1: A chart's internal period

**Files:**
- Modify: `libs/music/include/chalkwalk/music/Harmony.h`
- Modify: `libs/music/src/Harmony.cpp`
- Test: `libs/music/test/HarmonyTests.cpp`

**Interfaces:**
- Produces: `int chartPeriod(const Chart &chart)` -- the number of leading bars
  that, repeated, reproduce the whole chart. Task 3 consumes it.

**Why here.** It is a property of a chart and nothing else, and `Chord` already
has `operator==`. Putting it in the band would make the band the place charts
are analysed.

- [ ] **Step 1: Write the failing test**

In `libs/music/test/HarmonyTests.cpp`:

```cpp
TEST_CASE("a chart's period is its shortest repeating prefix", "[harmony]") {
  const auto key = m::Notation::parseKey("C major");
  auto barOf = [&](int degree) {
    return m::Bar{{m::diatonicTriad(key, degree)}};
  };

  // I V I V repeats after two bars.
  const m::Chart repeating{barOf(0), barOf(4), barOf(0), barOf(4)};
  REQUIRE(m::chartPeriod(repeating) == 2);

  // I V vi IV -- the default progression -- does not repeat inside itself.
  const m::Chart plain{barOf(0), barOf(4), barOf(5), barOf(3)};
  REQUIRE(m::chartPeriod(plain) == 4);

  // One bar is its own period; so is an empty chart, vacuously.
  REQUIRE(m::chartPeriod(m::Chart{barOf(0)}) == 1);
  REQUIRE(m::chartPeriod(m::Chart{}) == 0);

  // A period must DIVIDE the chart. I V I is not "period 2 with a bit left
  // over" -- a phrase that does not tile is not a phrase.
  const m::Chart ragged{barOf(0), barOf(4), barOf(0)};
  REQUIRE(m::chartPeriod(ragged) == 3);

  // Four of the same bar has period one.
  const m::Chart flat{barOf(0), barOf(0), barOf(0), barOf(0)};
  REQUIRE(m::chartPeriod(flat) == 1);

  // Two chords in a bar are part of the bar's identity.
  m::Bar two{{m::diatonicTriad(key, 0), m::diatonicTriad(key, 4)}};
  const m::Chart mixed{two, barOf(0), two, barOf(0)};
  REQUIRE(m::chartPeriod(mixed) == 2);
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake -S libs/music -B /tmp/cm-phrase -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/cm-phrase -j $(nproc) 2>&1 | grep -E " error" | head -3
```

Expected: a compile error naming `chartPeriod` as undeclared.

- [ ] **Step 3: Declare it**

In `Harmony.h`, beside `layoutChart`:

```cpp
// The number of leading bars that, repeated, reproduce the whole chart.
//
// `| Dm7 | G7 | Dm7 | G7 |` is 2; `| I | V | vi | IV |` is 4, because it does
// not repeat inside itself. Returns 0 for an empty chart.
//
// The period must DIVIDE the chart's length. A prefix that repeats and then
// stops part way is not a period: a phrase built on it would not tile, and the
// last repetition would be cut off mid-idea.
[[nodiscard]] int chartPeriod(const Chart &chart);
```

- [ ] **Step 4: Implement it**

In `Harmony.cpp`:

```cpp
int chartPeriod(const Chart &chart) {
  const int n = (int)chart.size();
  if (n == 0)
    return 0;

  for (int p = 1; p <= n; ++p) {
    if (n % p != 0)
      continue;
    bool holds = true;
    for (int i = p; i < n && holds; ++i)
      holds = chart[(size_t)i].chords == chart[(size_t)(i % p)].chords;
    if (holds)
      return p;
  }
  return n;
}
```

- [ ] **Step 5: Run it**

```bash
cmake --build /tmp/cm-phrase -j $(nproc) && /tmp/cm-phrase/test/chalkwalk_music_tests "[harmony]" | tail -3
```

Expected: passing.

- [ ] **Step 6: Prove it has teeth**

Change `n % p != 0` to `false` so a non-dividing prefix is accepted, rebuild,
and confirm the `ragged` case now returns 2 and the test fails. Revert.

- [ ] **Step 7: Commit and push**

```bash
cd libs/music
git add -A && git commit -m "Say what a chart's own period is."
git merge-base --is-ancestor origin/main HEAD && git push origin main
```

---

### Task 2: How long a phrase is

**Files:**
- Modify: `libs/jambot/src/BotBand.h`
- Modify: `libs/jambot/src/BotBand.cpp`
- Test: `libs/jambot/test/BotBandTests.cpp`

**Interfaces:**
- Consumes: `Harmony::chartPeriod` from Task 1.
- Produces: `int Foundation::phraseSteps(const Settings &s)` -- the period of
  the shared part, in fine steps. Task 3 consumes it.

**Nothing calls it yet.** Parity must be identical at the end of this task.

- [ ] **Step 1: Declare it**

In `BotBand.h`, inside `namespace Foundation`:

```cpp
// How long the part is before it repeats, in fine steps.
//
// Equal to `part(s).steps` when the figure spans the interval, which is what
// the band did before phrases existed -- so that value is the identity case
// and is asserted to render byte-identically.
//
// It must DIVIDE the part's steps, or the last repetition is cut off mid-idea.
// It is not bounded by anything else, because there is no form yet: a phrase
// longer than an interval has nothing to mean until one exists.
int phraseSteps(const Settings &s);
```

- [ ] **Step 2: Write the failing test**

`BotBandTests.cpp` needs `#include <set>` for the third of these.

```cpp
    beginTest("a phrase divides the interval, and can be the whole of it");
    {
      for (std::uint32_t seed = 1; seed <= 40; ++seed)
        for (int bpi : {8, 16, 32}) {
          const auto s = settingsFor("C major", 120, bpi, seed);
          const auto part = BotBand::Foundation::part(s);
          const int p = BotBand::Foundation::phraseSteps(s);

          expect(p > 0, "a phrase of no length at seed " + std::to_string(seed));
          expect(p <= part.steps, "a phrase longer than the interval");
          expectEquals(part.steps % p, 0,
                       "a phrase that does not tile: " + std::to_string(p) +
                           " into " + std::to_string(part.steps));
        }
    }

    beginTest("a repeating chart gives a phrase the length of its repeat");
    {
      // | I | V | I | V | over sixteen beats: the chart repeats every two
      // bars, which is eight beats, which is half the part.
      auto s = settingsFor("C major", 120, 16, 5);
      const auto key = keyOf("C major");
      s.chart = {Harmony::Bar{{Harmony::diatonicTriad(key, 0)}},
                 Harmony::Bar{{Harmony::diatonicTriad(key, 4)}},
                 Harmony::Bar{{Harmony::diatonicTriad(key, 0)}},
                 Harmony::Bar{{Harmony::diatonicTriad(key, 4)}}};
      const auto part = BotBand::Foundation::part(s);
      expectEquals(BotBand::Foundation::phraseSteps(s), part.steps / 2);
    }

    beginTest("the default chart falls back to a seed-chosen fraction");
    {
      // I V vi IV does not repeat inside itself, so the chart says nothing and
      // the seed chooses. Across seeds it must choose more than one answer, or
      // the fallback is a constant wearing a seed's clothes.
      std::set<int> chosen;
      for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto part = BotBand::Foundation::part(s);
        chosen.insert(part.steps / BotBand::Foundation::phraseSteps(s));
      }
      expect(chosen.size() > 1,
             "every seed chose the same fraction, so the seed does nothing");
    }
```

- [ ] **Step 3: Run it and watch it fail**

Expected: a compile error naming `phraseSteps`.

- [ ] **Step 4: Implement it**

In `BotBand.cpp`, inside `namespace Foundation`:

```cpp
int phraseSteps(const Settings &s) {
  const Figure f = figureFor(Voice::Bass, s);
  const int steps = f.steps;
  if (steps <= 0)
    return 1;

  // Divisors of the part, longest first, and never shorter than a beat: a
  // "phrase" of two fine steps is a subdivision, not an idea.
  const int stepsPerBeat = std::max(1, steps / std::max(1, s.bpi));
  std::vector<int> candidates;
  for (int p = steps; p >= stepsPerBeat; --p)
    if (steps % p == 0)
      candidates.push_back(p);
  if (candidates.empty())
    return steps;

  // The chart first, where it has something to say. A repeat that lands on the
  // chord it landed on last time is a riff; one that does not is a riff being
  // dragged across the changes, which is a choice rather than a default.
  const int bars = (int)s.chart.size();
  const int period = Harmony::chartPeriod(s.chart);
  if (bars > 0 && period > 0 && period < bars && (steps * period) % bars == 0) {
    const int wanted = steps * period / bars;
    for (int c : candidates)
      if (c == wanted)
        return c;
  }

  // Otherwise the seed picks. Held for the session, like every other decision
  // about WHAT is played.
  Rng rng(figureSeed(Voice::Bass, s, Hold::Session, 0) ^ 0x5EED17u);
  return candidates[(size_t)rng.range(0, (int)candidates.size() - 1)];
}
```

- [ ] **Step 5: Run the suite and the parity check**

```bash
cd /home/programming/antiphon
cmake --build build-vote --target chalkwalk_jambot_tests AntiphonVoiceLab -j $(nproc)
./build-vote/libs/jambot/test/chalkwalk_jambot_tests "bot band" | tail -3
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-task2.txt
diff /tmp/parity-before.txt /tmp/parity-task2.txt && echo "IDENTICAL"
```

Expected: tests pass, and `IDENTICAL` -- nothing calls `phraseSteps` yet.

- [ ] **Step 6: Commit**

```bash
cd libs/jambot
git add -A && git commit -m "Decide how long a phrase is, before anything plays one."
```

---

### Task 3: The audible landing

**Files:**
- Modify: `libs/jambot/src/BotBand.h`
- Modify: `libs/jambot/src/BotBand.cpp`
- Test: `libs/jambot/test/BotBandTests.cpp`

**Interfaces:**
- Consumes: `Foundation::phraseSteps` from Task 2.
- Produces: `std::uint32_t performanceSeed(Voice, const Settings &, int
  intervalIndex, std::uint32_t salt)`.

**This is the one commit in this plan where the audio changes.** Three changes
that only make sense together: the part tiles, the bass's pitch choice becomes
phrase-relative so the riff repeats, and the performance seeds gain an interval
term so the repeats differ.

- [ ] **Step 1: Write the identity test first**

Before changing any behaviour, pin the property that makes the whole change
reviewable: **a phrase equal to the interval must render exactly as today.**

```cpp
    beginTest("a phrase as long as the interval is the band as it was");
    {
      // The identity case. Every other phrase length is a departure from this
      // one, so if this drifts there is no baseline to judge them against.
      for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto part = BotBand::Foundation::part(s);
        const auto tiled = BotBand::Foundation::partWithPhrase(s, part.steps);
        expectEquals((int)tiled.onsets.size(), (int)part.onsets.size(),
                     "seed " + std::to_string(seed));
        for (std::size_t i = 0; i < part.onsets.size() && i < tiled.onsets.size();
             ++i) {
          expectEquals(tiled.onsets[i].step, part.onsets[i].step);
          expect(tiled.onsets[i].fromKick == part.onsets[i].fromKick);
          expect(tiled.onsets[i].onChange == part.onsets[i].onChange);
        }
      }
    }
```

- [ ] **Step 2: Add the tiling form of the part**

Declare in `BotBand.h`, inside `namespace Foundation`:

```cpp
// The part with an explicit period. `part(s)` is this with the period the
// settings imply; the two-argument form exists so a test can pin the identity
// case and so a tool can render a phrase length the seed did not choose.
Part partWithPhrase(const Settings &s, int phrase);
```

`Part` gains the period, so `renderBass` can read it back:

```cpp
struct Part {
  int steps = 0;
  int stepsPerBeat = 1;
  int phrase = 0; // the period; equal to `steps` when nothing repeats
  std::vector<Onset> onsets;
};
```

Implement it by generalising `part`: the figures are built over `phrase` steps
rather than over the whole interval, and the interval is filled by repeating
them.

```cpp
Part partWithPhrase(const Settings &s, int phrase) {
  Part p;
  const Figure f = figureFor(Voice::Bass, s);
  p.steps = f.steps;
  p.stepsPerBeat = std::max(1, f.steps / std::max(1, s.bpi));
  if (phrase <= 0 || p.steps % phrase != 0)
    phrase = p.steps;
  p.phrase = phrase;

  const auto layout = layoutOf(s);
  const Figure kick = figureFor(Voice::Drums, s);
  const int stepsPerBeat = p.stepsPerBeat;
  const int kickPhrase = std::max(1, phrase / stepsPerBeat);

  // The figures, scaled to the phrase. Pulse counts scale with the length so
  // that density is a property of the groove rather than of how often it
  // comes round.
  const int bassPulses =
      phrase == p.steps
          ? f.pulses
          : std::max(1, chalkwalk::music::nearestCoprimePulses(
                            phrase, std::max(1, f.pulses * phrase / p.steps),
                            true));
  const int kickPulses =
      kickPhrase == kick.steps
          ? kick.pulses
          : std::max(1, kick.pulses * kickPhrase / std::max(1, kick.steps));

  auto layoutStepOf = [stepsPerBeat](int step) {
    return step * Harmony::kStepsPerBeat / stepsPerBeat;
  };

  for (int step = 0; step < p.steps; ++step) {
    // The harmony is NOT tiled: it is what it is at this point in the
    // interval, which is what makes a repeated rhythm adapt to the changes
    // rather than fight them.
    const bool onChange =
        step == 0 ||
        (step * Harmony::kStepsPerBeat % stepsPerBeat == 0 &&
         Harmony::changesAtStep(layout, layoutStepOf(step)));

    const int inPhrase = step % phrase;
    const bool onKick =
        step % stepsPerBeat == 0 &&
        chalkwalk::music::hit((inPhrase / stepsPerBeat) % kickPhrase, kickPhrase,
                              kickPulses, kick.rotation);

    if (!onChange && !onKick &&
        !chalkwalk::music::hit(inPhrase, phrase, bassPulses, f.rotation))
      continue;

    p.onsets.push_back({step, onKick, onChange});
  }
  return p;
}

Part part(const Settings &s) { return partWithPhrase(s, phraseSteps(s)); }
```

**The coprime nudge moves to the phrase's scale rather than being dropped.** It
existed so the bass figure would not repeat inside the interval, because a
repeating scaled copy of the kick is the kick again. That argument is unchanged
at the new scale: a figure that sub-repeats INSIDE its own phrase is still a
metronome. So it is applied to `phrase` rather than to `steps`.

- [ ] **Step 3: Run the identity test**

```bash
cd /home/programming/antiphon
cmake --build build-vote --target chalkwalk_jambot_tests -j $(nproc)
./build-vote/libs/jambot/test/chalkwalk_jambot_tests "bot band" | tail -3
```

Expected: the identity test passes. **If it fails, stop.** Every later
judgement rests on it.

- [ ] **Step 4: Add `performanceSeed`**

Declare beside `figureSeed` in `BotBand.h`:

```cpp
// The seed for a decision about HOW something is played -- the per-hit noise,
// the drift, the velocity variation.
//
// Different every interval, which `figureSeed` at `Hold::Session` deliberately
// is not. Before this the two were the same value, so every interval was
// jittered identically and what made two consecutive drum intervals differ was
// the hat rotation and nothing else. A phrase that returns played exactly the
// same way twice is a loop; played fractionally differently it is a band.
std::uint32_t performanceSeed(Voice v, const Settings &s, int intervalIndex,
                              std::uint32_t salt = 0);
```

Implement:

```cpp
std::uint32_t performanceSeed(Voice v, const Settings &s, int intervalIndex,
                              std::uint32_t salt) {
  // A different multiplier from figureSeed's, so a performance decision and a
  // figure decision at the same interval never collide on one value.
  return saltedSeed(v, s.seed) + salt + 2654435761u * (std::uint32_t)intervalIndex;
}
```

- [ ] **Step 5: Use it at the groove-path sites**

Replace `saltedSeed(Voice::X, s.seed) + <salt>` with
`performanceSeed(Voice::X, s, intervalIndex, <salt>)` at these call sites only:

| what | current salt |
|---|---|
| snare backbeat | `(std::uint32_t)step` |
| hat | `977u * (std::uint32_t)step` |
| fill snare | `31u * (std::uint32_t)sub` |
| bass string | `131u * (std::uint32_t)step` |
| keys pad, both sites | `2654435761u * note + ...` |
| lead, both sites | `613u * step` and `613u * lastStep` |

**Leave the ending's three sites alone** (`renderKick`, `renderBassString` and
`renderPad` in the resolve). An ending happens once per performance and there
is nothing for it to differ from; varying it by interval index would make the
same ending sound different depending on when the tune happened to stop.

**Leave the patch choices alone** -- `bassPatch`, `leadPatch`, `keysPatch` and
`kitPattern` are session-held timbre and must not move.

- [ ] **Step 6: Make the bass's pitch choice phrase-relative**

Today the choice is a sequential draw from one `Rng` walked down the interval,
so the second tile draws different numbers from the first and the riff does not
repeat. Make it a function of the phrase-relative step:

```cpp
    // The riff repeats as a SHAPE, not as a set of pitches. The choice is
    // taken on the phrase-relative step, so tile two makes the same choice as
    // tile one -- and because the choice is root, octave or the chord's own
    // fifth, applying it over a different chord gives that chord's root,
    // octave or fifth. Recontextualised rather than transposed, and correct by
    // construction rather than by a compatibility test.
    int semitoneAboveRoot = 0;
    if (!onChange) {
      Rng pick(figureSeed(Voice::Bass, s, Hold::Session, 0) +
               8191u * (std::uint32_t)(step % foundation.phrase));
      const int roll = pick.range(0, 9);
      if (roll < 5)
        semitoneAboveRoot = 0;
      else if (roll < 8)
        semitoneAboveRoot = 12;
      else
        semitoneAboveRoot = chord.toneCount > 2 ? chord.tones[2] : 7;
    }
```

`phrase` is `foundation.phrase`, added to `Part` in step 2.

**This changes the bass's pitches even at phrase == steps**, because the draws
are now indexed rather than sequential. That is a real audible change and it is
the reason this step is in the audible commit rather than an earlier one.

- [ ] **Step 7: Write the repetition tests**

```cpp
    beginTest("a phrase that returns returns the same figure");
    {
      const auto s = settingsFor("C major", 120, 16, 9);
      const auto part = BotBand::Foundation::partWithPhrase(s, 16);
      // Onsets that are not chord changes must recur at the same offset in
      // every tile. Changes are exempt: the harmony is not tiled.
      std::set<int> firstTile, secondTile;
      for (const auto &o : part.onsets) {
        if (o.onChange)
          continue;
        if (o.step < 16)
          firstTile.insert(o.step);
        else if (o.step < 32)
          secondTile.insert(o.step - 16);
      }
      expect(!firstTile.empty(), "no onsets to compare");
      expect(firstTile == secondTile, "the phrase did not repeat");
    }

    beginTest("two intervals of the same figure are not the same audio");
    {
      // The interlock the roadmap flags: repetition is identical in its figure
      // and never in its performance.
      const auto s = settingsFor("C major", 120, 16, 4);
      const auto a = render(BotBand::Voice::Drums, s, 0);
      const auto b = render(BotBand::Voice::Drums, s, 1);
      expect(a != b, "two intervals rendered identically");
    }

    beginTest("the performance varies while the figure does not");
    {
      // Stronger than the above, which the hat rotation alone could satisfy.
      // The BASS has no rotation, so if two of its intervals differ it is the
      // performance seed that did it.
      const auto s = settingsFor("C major", 120, 16, 4);
      const auto a = render(BotBand::Voice::Bass, s, 0);
      const auto b = render(BotBand::Voice::Bass, s, 1);
      expect(a != b, "the bass played two intervals identically");

      // The part is a figure decision, so it does not take an interval index
      // at all: the same settings must give the same onsets, every time.
      const auto one = BotBand::Foundation::part(s);
      const auto two = BotBand::Foundation::part(s);
      expectEquals((int)two.onsets.size(), (int)one.onsets.size());
      for (std::size_t i = 0; i < one.onsets.size() && i < two.onsets.size();
           ++i) {
        expectEquals(two.onsets[i].step, one.onsets[i].step);
        expect(two.onsets[i].fromKick == one.onsets[i].fromKick);
      }
    }
```

- [ ] **Step 8: Run everything, and LOOK at the parity diff**

```bash
cd /home/programming/antiphon
cmake --build build-vote -j $(nproc)
ctest --test-dir build-vote --output-on-failure 2>&1 | tail -5
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-after.txt
diff /tmp/parity-before.txt /tmp/parity-after.txt | grep '^<' | awk '{print $2}' | sort | uniq -c
```

Expected: 8/8, and **all four voices changed** -- 72 lines each. Keys and Lead
change because `performanceSeed` reaches them; Kit and Bass change for that and
for the phrase. **If a voice did NOT change, a call site was missed.**

- [ ] **Step 9: Listen before committing**

```bash
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  band --seed 12345 --key "D minor" --bpm 100 --bpi 16 -o /tmp/after.wav
```

Render two or three seeds and listen for the two failure modes this change can
produce: a riff so short it ticks, and a bass that has stopped landing on the
changes. Both are real risks of the tiling and neither shows in a test.

- [ ] **Step 10: Commit**

```bash
cd libs/jambot
git add -A && git commit -m "Play a phrase and come back to it, differently each time."
```

---

### Task 4: Tune the fraction fallback

**Files:**
- Modify: `libs/jambot/src/BotBand.cpp` (`phraseSteps` candidate bounds)
- Test: `libs/jambot/test/BotBandTests.cpp`

**Interfaces:** none new.

Spec section 7 question 3, and the only one left: **should the fraction fallback
align to bar lines?** A half-interval phrase in an interval whose chart has
three bars does not land on a bar line. It is not obvious whether that is a
fault or a syncopation, and it is not decidable from a document.

- [ ] **Step 1: Render both, at the same seed**

The restriction, applied temporarily to `phraseSteps` so both can be heard. A
phrase is bar-aligned when it spans a whole number of the chart's bars:

```cpp
  // TUNING: bar-aligned candidates only.
  const int bars = std::max(1, (int)s.chart.size());
  std::vector<int> aligned;
  for (int c : candidates)
    if ((c * bars) % steps == 0)
      aligned.push_back(c);
  if (!aligned.empty())
    candidates = aligned;
```

Render with it in place, keep the file, then remove it and render again.

```bash
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  band --seed 12345 --key "D minor" --bpm 100 --bpi 16 --lufs -20 -o /tmp/aligned.wav
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  band --seed 12345 --key "D minor" --bpm 100 --bpi 16 --lufs -20 -o /tmp/free.wav
```

`--lufs` matched, so the comparison is about the music and not the level.

- [ ] **Step 2: Decide, and write down the method**

Whichever wins, record in `ROADMAP.md` **what was compared, at which seeds, and
what was heard** -- not just the answer. `PRINCIPLES §5`: a number quoted
anywhere needs a method, and so does a choice.

- [ ] **Step 3: Pin the decision with a test**

If bar-aligned wins, assert that every chosen phrase length divides evenly into
bars. If free wins, assert that at least one seed chooses a non-aligned length,
so the freedom is exercised rather than nominal.

- [ ] **Step 4: Full suite, then commit**

Expected: 8/8. The parity diff against `/tmp/parity-after.txt` is empty unless
the decision changed the default, in which case say so in the commit.

---

### Task 5: The documentation

**Files:**
- Modify: `ROADMAP.md`, `AGENTS.md` (Antiphon)
- Modify: `libs/jambot/docs/BOT-CHAT.md`
- Modify: `docs/superpowers/specs/2026-09-03-foundation-stratum-and-phrases.md`

- [ ] **Step 1: Close the roadmap items**

Mark *The performance seeds are session-held too* and *Phrase length inside the
interval* done. Record what the interlock now rests on: two consecutive drum
intervals differ because of `performanceSeed` rather than because of the hat
rotation, which is what makes AABA safe to build next.

- [ ] **Step 2: Answer the spec's open questions in the spec**

Section 7's three questions are now two answers and one measurement. Write the
answers in, with the reasons -- question 1 dissolved because the bass's pitch is
chord-relative, question 2 was arithmetic, question 3 was decided by ear in Task
4 and the method is recorded.

- [ ] **Step 3: Update `BOT-CHAT.md` section 16**

The foundation's part now has a period. Say so where the stratum is described,
and note that the harmony is deliberately not tiled with it.

- [ ] **Step 4: Full suite, commit and push all three repos**

```bash
cd /home/programming/antiphon
cmake --build build-vote -j $(nproc) && ctest --test-dir build-vote --output-on-failure
```

Expected: 8/8.

---

## What this leaves for next time

*Phrases that return* -- the form across intervals, AABA -- is now unblocked and
is the next item in `ROADMAP.md`'s order. It was third for a reason that this
plan has just satisfied: it is the first thing that can put two A intervals
adjacent, and it is only safe once the performance actually varies between them.
