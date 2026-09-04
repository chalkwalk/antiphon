# Foundation Stratum Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the kit and the bass one shared rhythmic part to select from,
changing nothing about what the band sounds like, so that a phrase period has
somewhere to live.

**Architecture:** The foundation stratum computes a **full part** on the fine
grid -- the union of the kick's onsets and the bass's doubled Euclidean -- and
its two members select from it: the kit takes the kick-marked subset, the bass
takes the whole. `renderBass` already computes exactly that union inline, so
this is an extraction rather than a change, and it is verified as one.

**Tech Stack:** C++17, `chalkwalk-jambot` (JUCE-free), `chalkwalk-music` for
Euclidean figures and harmony, Catch2 through `test/JuceUnitShim.h`,
`AntiphonVoiceLab` for rendering.

**Spec:** `docs/superpowers/specs/2026-09-03-foundation-stratum-and-phrases.md`

**This plan covers section 8 step 1 only** -- the stratum, byte-identical.
Step 2 (`performanceSeed` and the phrase period) is deliberately NOT planned
here: the spec leaves three thresholds open in its section 7 because they want
ears and `AntiphonVoiceLab`, and writing bite-sized steps for them now would
mean inventing the answers the spec declined to invent. It gets its own plan
once this lands and the thresholds have been listened to.

## Global Constraints

- **Two repositories.** `BotBand` lives in `libs/jambot` (`chalkwalk-jambot`,
  MIT, JUCE-free). `AntiphonVoiceLab` lives in Antiphon's `tools/`. Iterate
  with `-DCHALKWALK_JAMBOT_DIR=...`; bump the submodule before calling
  anything done.
- **ASCII only in source files.** `--` for an em dash, `->` for an arrow.
- **2-space indent, braces on the same line, members lowerCamelCase.**
- **No comments except for non-obvious invariants**, and a comment explains
  *why*. Cite a reference implementation as
  `(justinfrankel/ninjam njclient.cpp:806)` where one applies.
- **JUCE-free.** `libs/jambot` must not gain a `juce::` type. The
  `music-layer-is-juce-free` ctest enforces the equivalent for
  `chalkwalk-music`; jambot's own suite links no JUCE.
- **The audio must not change.** Every task ends with the parity check from
  Task 1 reporting no differences. A task that changes a hash has failed, not
  discovered something.
- **Build:** `cmake -B build-vote -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`
  then `cmake --build build-vote -j $(nproc)`. The JUCE submodule is
  deinitialised in this checkout, so the flag is required.

---

### Task 1: A parity check that can prove the audio did not change

**Files:**
- Modify: `tools/VoiceLabMain.cpp` (Antiphon)

**Interfaces:**
- Produces: `AntiphonVoiceLab parity` -- prints one line per rendered
  interval, `voice bpm bpi seed index hash`, to stdout. Later tasks diff two
  runs of it.

**Why a tool and not a committed fixture.** A hash of rendered floats is not
stable across platforms: the synthesis uses `sin`, `exp` and friends, libm
differs between macOS, Windows and Linux, and `-O2` may contract multiplies
into FMAs on one and not another. CI builds all three, so a committed audio
fixture would fail there for reasons that have nothing to do with this work.
A tool compares two runs of ONE binary on ONE machine, which is exactly the
claim being made. The permanent, cross-platform guard is the onset-level test
in Task 3, and onsets are integers.

- [ ] **Step 1: Add the subcommand**

In `tools/VoiceLabMain.cpp`, beside the existing `if (o.voice == "bench")`
dispatch, add:

```cpp
  if (o.voice == "parity") {
    // FNV-1a over the raw bytes of the rendered buffer. Not a checksum with
    // any cryptographic claim -- it only has to change when a sample does.
    auto hashOf = [](const std::vector<float> &v) {
      std::uint64_t h = 1469598103934665603ull;
      const auto *bytes = reinterpret_cast<const unsigned char *>(v.data());
      const std::size_t n = v.size() * sizeof(float);
      for (std::size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
      }
      return h;
    };

    // Wide enough that a change to one voice at one metre cannot hide. The
    // bass and the drums are the ones this branch touches; the keys and the
    // lead are here to prove they were NOT touched.
    const int bpms[] = {90, 120, 137};
    const int bpis[] = {8, 16};
    const std::uint32_t seeds[] = {1u, 12345u, 99999u};

    for (auto voice : {BotBand::Voice::Drums, BotBand::Voice::Bass,
                       BotBand::Voice::Keys, BotBand::Voice::Lead})
      for (int bpm : bpms)
        for (int bpi : bpis)
          for (auto seed : seeds)
            for (int index = 0; index < 4; ++index) {
              auto key = MusicalKey::parseName(o.keyName.toStdString());
              if (!key.valid)
                return 1;
              const auto s = BotBand::defaults(key, bpm, bpi, o.sampleRate, seed);
              const int n = (int)(o.sampleRate * 60.0 / bpm) * bpi;
              std::vector<float> buf((std::size_t)n, 0.0f);
              BotBand::renderInterval(voice, s, index, buf.data(), n);
              std::printf("%-6s %3d %2d %6u %d %016llx\n",
                          BotBand::voiceName(voice), bpm, bpi, (unsigned)seed,
                          index, (unsigned long long)hashOf(buf));
            }
    return 0;
  }
```

- [ ] **Step 2: Build and capture the baseline**

```bash
cmake --build build-vote --target AntiphonVoiceLab -j $(nproc)
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-before.txt
wc -l /tmp/parity-before.txt   # expect 288
```

Expected: 288 lines (4 voices x 3 bpm x 2 bpi x 3 seeds x 4 intervals).

- [ ] **Step 3: Prove the check has teeth**

Temporarily perturb one constant and confirm the hashes move, then revert:

```bash
# In libs/jambot/src/BotBand.cpp, change kDrumHeadroom slightly, rebuild, then:
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-teeth.txt
diff /tmp/parity-before.txt /tmp/parity-teeth.txt | head
```

Expected: the Drums lines differ. Revert the perturbation, rebuild, and
confirm `diff` is empty again before continuing. **A check that has not been
seen to fail is not a check.**

- [ ] **Step 4: Commit**

```bash
git add tools/VoiceLabMain.cpp
git commit -m "Add a parity render, so a refactor can prove it changed nothing."
```

---

### Task 2: Make the foundation's onsets a named thing

**Files:**
- Modify: `libs/jambot/src/BotBand.h`
- Modify: `libs/jambot/src/BotBand.cpp` (`renderBass`)
- Test: `libs/jambot/test/BotBandTests.cpp`

**Interfaces:**
- Produces: `BotBand::Foundation::Onset`, `BotBand::Foundation::onsets(const
  Settings &)`. Task 3 consumes both.

A pure extraction. The loop moves out of `renderBass` and nothing else changes.

- [ ] **Step 1: Declare the type and the function**

In `libs/jambot/src/BotBand.h`, after the `Figure` declarations:

```cpp
// The foundation stratum: the kit and the bass, and the one rhythmic ground
// they both play from (docs/BOT-CHAT.md section 16.1).
//
// The part is at the BASS's resolution rather than the kick's, and the kick is
// the strong subset of it that a drum plays. That is the opposite way round
// from how 16.2 first read, and it is forced rather than chosen: a phrase
// shorter than the interval repeats the shared part, and the riff is mostly
// made of the bass's own onsets -- so a part coarse enough for the kit to play
// whole is a part in which nothing recurs.
namespace Foundation {

struct Onset {
  int step = 0;          // on the fine grid
  bool fromKick = false; // the kick lands here, so the drum plays it
  bool onChange = false; // the harmony moves here; a pitched member re-roots
};

// The full part for one interval. `steps` is the fine grid; `stepsPerBeat` is
// how many of those go to a beat, which is what maps onto the harmony's own
// grid (Harmony::kStepsPerBeat).
struct Part {
  int steps = 0;
  int stepsPerBeat = 1;
  std::vector<Onset> onsets;
};

Part part(const Settings &s);

} // namespace Foundation
```

- [ ] **Step 2: Move the loop, unchanged**

In `libs/jambot/src/BotBand.cpp`, add the definition. The body is the loop
currently inside `renderBass`, verbatim -- do not tidy it, because the claim
being made is that nothing changed:

```cpp
namespace Foundation {

Part part(const Settings &s) {
  Part p;
  const Figure f = figureFor(Voice::Bass, s);
  p.steps = f.steps;
  p.stepsPerBeat = std::max(1, f.steps / std::max(1, s.bpi));

  const auto layout = layoutOf(s);
  const Figure kick = figureFor(Voice::Drums, s);
  const int stepsPerBeat = p.stepsPerBeat;

  auto layoutStepOf = [stepsPerBeat](int step) {
    return step * Harmony::kStepsPerBeat / stepsPerBeat;
  };

  for (int step = 0; step < f.steps; ++step) {
    const bool onChange =
        step == 0 ||
        (step * Harmony::kStepsPerBeat % stepsPerBeat == 0 &&
         Harmony::changesAtStep(layout, layoutStepOf(step)));

    const bool onKick =
        step % stepsPerBeat == 0 &&
        chalkwalk::music::hit(step / stepsPerBeat, kick.steps, kick.pulses,
                              kick.rotation);

    if (!onChange && !onKick &&
        !chalkwalk::music::hit(step, f.steps, f.pulses, f.rotation))
      continue;

    p.onsets.push_back({step, onKick, onChange});
  }
  return p;
}

} // namespace Foundation
```

- [ ] **Step 3: Call it from `renderBass`**

Replace the onset-collecting loop in `renderBass` with:

```cpp
  const auto foundation = Foundation::part(s);
  std::vector<int> onsets;
  std::vector<bool> isChange;
  onsets.reserve(foundation.onsets.size());
  isChange.reserve(foundation.onsets.size());
  for (const auto &o : foundation.onsets) {
    onsets.push_back(o.step);
    isChange.push_back(o.onChange);
  }
```

Leave everything after that untouched: `stepsPerBeat`, `stepSamples`, the
pitch selection and the render call all still read the same locals.

- [ ] **Step 4: Write the characterisation test**

In `libs/jambot/test/BotBandTests.cpp`, inside `runFigureTests()`:

```cpp
    beginTest("the foundation's part contains every kick, and the bass's own");
    {
      // The two claims that make it a shared ground rather than two figures:
      // the kick is entirely inside it, and it is denser than the kick alone.
      for (std::uint32_t seed = 1; seed <= 20; ++seed)
        for (int bpi : {8, 16}) {
          const auto s = settingsFor("C major", 120, bpi, seed);
          const auto part = BotBand::Foundation::part(s);
          const auto kick = BotBand::figureFor(BotBand::Voice::Drums, s);

          expect(!part.onsets.empty(), "empty part at seed " +
                                           std::to_string(seed));

          int kickOnsets = 0;
          for (const auto &o : part.onsets) {
            expect(o.step >= 0 && o.step < part.steps, "step out of range");
            if (o.fromKick)
              ++kickOnsets;
          }

          // Every kick hit appears, and is marked.
          int expected = 0;
          for (int k = 0; k < kick.steps; ++k)
            if (chalkwalk::music::hit(k, kick.steps, kick.pulses, kick.rotation))
              ++expected;
          expectEquals(kickOnsets, expected,
                       "the part lost a kick at seed " + std::to_string(seed));

          expect((int)part.onsets.size() >= kickOnsets,
                 "the part is thinner than the kick it contains");
        }
    }

    beginTest("the onsets rise, and none repeats");
    {
      const auto s = settingsFor("C major", 120, 16, 7);
      const auto part = BotBand::Foundation::part(s);
      for (std::size_t i = 1; i < part.onsets.size(); ++i)
        expect(part.onsets[i].step > part.onsets[i - 1].step,
               "onsets are not strictly increasing");
    }

    beginTest("step 0 is always a chord change");
    {
      // An interval opens on its first chord, so the bass always states it.
      const auto s = settingsFor("C major", 120, 16, 3);
      const auto part = BotBand::Foundation::part(s);
      expect(!part.onsets.empty());
      expectEquals(part.onsets.front().step, 0);
      expect(part.onsets.front().onChange, "the downbeat did not re-root");
    }
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build-vote -j $(nproc)
./build-vote/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests > /tmp/t.log 2>&1
tail -3 /tmp/t.log
```

Expected: PASSED, 0 failures.

- [ ] **Step 6: Prove the audio did not move**

```bash
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-after.txt
diff /tmp/parity-before.txt /tmp/parity-after.txt && echo "IDENTICAL"
```

Expected: `IDENTICAL`. **If it differs, the extraction was not an extraction.**
Do not adjust the baseline; find what moved.

- [ ] **Step 7: Commit**

```bash
cd libs/jambot
git add src/BotBand.h src/BotBand.cpp test/BotBandTests.cpp
git commit -m "Name the foundation's onsets, so two members can share them."
```

---

### Task 3: The kit selects from the part

**Files:**
- Modify: `libs/jambot/src/BotBand.h`
- Modify: `libs/jambot/src/BotBand.cpp` (`renderDrums`)
- Test: `libs/jambot/test/BotBandTests.cpp`

**Interfaces:**
- Consumes: `Foundation::Part`, `Foundation::Onset` from Task 2.
- Produces: `Foundation::Selection`, `Foundation::select(const Part &,
  Selection)`.

This is the task that makes the part shared rather than the bass's private
business, and it is where the permanent cross-platform guard lands.

- [ ] **Step 1: Declare the selection policy**

In `libs/jambot/src/BotBand.h`, inside `namespace Foundation`:

```cpp
// How a member relates to the shared part. The rule behind the three is one
// rule: a member's figure is a function of the part, never an independent
// idea (docs/BOT-CHAT.md section 16.2, as amended).
//
// There is no Union. Union is how the part is FORMED -- the kick's onsets
// together with the bass's figure -- not how a member selects from it.
enum class Selection {
  Whole,      // the bass: it plays the ground entire
  Kick,       // the kit: the strong subset a drum plays
  Complement, // only where the kick is not; nothing uses it until perc exists
};
// `Kick` is 16.2's `Subset` policy, named for the only marked subset the
// foundation has. When a second stratum needs a differently-marked one, the
// mark moves into the enum rather than a second enum appearing beside it.

std::vector<Onset> select(const Part &p, Selection how);
```

- [ ] **Step 2: Write the failing test**

```cpp
    beginTest("the kit selects the kick, and the bass the whole ground");
    {
      using BotBand::Foundation::Selection;
      for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        const auto s = settingsFor("C major", 120, 16, seed);
        const auto part = BotBand::Foundation::part(s);

        const auto whole = BotBand::Foundation::select(part, Selection::Whole);
        const auto kick = BotBand::Foundation::select(part, Selection::Kick);
        const auto rest =
            BotBand::Foundation::select(part, Selection::Complement);

        expectEquals((int)whole.size(), (int)part.onsets.size());
        expectEquals((int)kick.size() + (int)rest.size(),
                     (int)part.onsets.size(),
                     "the kick and its complement do not partition the part");

        for (const auto &o : kick)
          expect(o.fromKick, "a non-kick onset was selected as the kick's");
        for (const auto &o : rest)
          expect(!o.fromKick, "a kick onset leaked into the complement");
      }
    }
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build-vote -j $(nproc) 2>&1 | grep -E " error" | head
```

Expected: a compile error naming `select` as undeclared. That IS the failing
state for a function that does not exist yet.

- [ ] **Step 4: Implement `select`**

```cpp
std::vector<Onset> select(const Part &p, Selection how) {
  std::vector<Onset> out;
  out.reserve(p.onsets.size());
  for (const auto &o : p.onsets) {
    switch (how) {
    case Selection::Whole:
      out.push_back(o);
      break;
    case Selection::Kick:
      if (o.fromKick)
        out.push_back(o);
      break;
    case Selection::Complement:
      if (!o.fromKick)
        out.push_back(o);
      break;
    }
  }
  return out;
}
```

- [ ] **Step 5: Route the kit's kick through it**

In `renderDrums`, the kick loop currently walks `kick.steps` and reads
`kickVel[step]`. Replace the loop's HIT TEST with the selection, keeping the
velocities exactly as they are:

```cpp
  const auto foundation = Foundation::part(s);
  const auto kickOnsets = Foundation::select(foundation, Foundation::Selection::Kick);

  for (const auto &onset : kickOnsets) {
    // Back to the coarse grid: the part is at the bass's resolution and a
    // kick lands on beats.
    const int step = onset.step / foundation.stepsPerBeat;
    const int at = step * beatSamples;
    if (at >= numSamples)
      break;
    const int v = kickVel[(size_t)step];
    if (v > 0)
      BotVoice::renderKick(out + at, numSamples - at, s.sampleRate,
                           kDrumHeadroom *
                               (v >= chalkwalk::music::kAccentedVelocity ? 0.9f
                                                                        : 0.65f));
  }
```

Leave the snare, hat and fill loops untouched -- they are the kit's
realisation, not the shared ground (spec section 3.1).

- [ ] **Step 6: Run the suite and the parity check**

```bash
cmake --build build-vote -j $(nproc)
./build-vote/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests > /tmp/t.log 2>&1
tail -3 /tmp/t.log
./build-vote/tools/AntiphonVoiceLab_artefacts/RelWithDebInfo/AntiphonVoiceLab \
  parity --key "C major" > /tmp/parity-after.txt
diff /tmp/parity-before.txt /tmp/parity-after.txt && echo "IDENTICAL"
```

Expected: PASSED, and `IDENTICAL`.

**If the drums differ, there are exactly two candidates, and the second is the
likely one.**

The coarse-grid mapping: the part marks a kick at fine step `k * stepsPerBeat`,
so dividing back must land on exactly `k`.

More likely: **`accents()` and `hit()` must agree about what a hit is.** The old
loop walked every coarse step and played where `kickVel[step] > 0`; the new one
walks where `Foundation::part` marked `fromKick`, which comes from
`chalkwalk::music::hit(...)`. Those are two different functions in
`chalkwalk-music` answering the same question, and this task silently depends on
them agreeing. Assert it directly rather than inferring it from the parity diff:

```cpp
    beginTest("accents and hit agree about where the kick lands");
    {
      // The kit's velocities come from accents() and the shared part's marks
      // come from hit(). Nothing has ever required them to agree before.
      for (std::uint32_t seed = 1; seed <= 40; ++seed)
        for (int bpi : {8, 16}) {
          const auto s = settingsFor("C major", 120, bpi, seed);
          const auto kick = BotBand::figureFor(BotBand::Voice::Drums, s);
          const auto vel = chalkwalk::music::accents(kick.steps, kick.pulses,
                                                    kick.rotation, kick.accents);
          for (int k = 0; k < kick.steps; ++k)
            expectEquals(vel[(size_t)k] > 0,
                         chalkwalk::music::hit(k, kick.steps, kick.pulses,
                                               kick.rotation),
                         "disagreement at step " + std::to_string(k) +
                             " seed " + std::to_string(seed));
        }
    }
```

Add this test BEFORE step 5, and run it before touching `renderDrums`. If it
fails, stop: the kit's onsets and the part's marks are not the same set, and
the extraction needs the part to carry the velocity rather than the mark. That
is a real possibility and finding it here costs one test; finding it through a
parity diff costs an afternoon.

- [ ] **Step 7: Prove the new test has teeth**

Temporarily make `Selection::Kick` fall through to `Whole`, rebuild, and
confirm the partition assertion fails. Revert and confirm green.

- [ ] **Step 8: Commit**

```bash
cd libs/jambot
git add src/BotBand.h src/BotBand.cpp test/BotBandTests.cpp
git commit -m "Let the kit select from the ground the bass plays."
```

---

### Task 4: Correct the design docs the code just contradicted

**Files:**
- Modify: `libs/jambot/docs/BOT-CHAT.md` (section 16.2)
- Modify: `ROADMAP.md` (Antiphon)
- Modify: `AGENTS.md` (Antiphon, layout map)

**Interfaces:** none; documentation only.

- [ ] **Step 1: Amend `BOT-CHAT.md` 16.2**

Two corrections, both to the role table and the rule under it:

- Role 1 (rhythm/kit) selects the **kick-marked subset**, not "the whole
  figure". The part is at the bass's resolution and contains the bass's own
  onsets; a kit playing all of them would be playing the bass line on a drum.
- Role 2 (bass) selects the **whole part**, not "complement of the kick". The
  shipped code takes the union and says why: *"locking to the kick has to mean
  actually landing on it"*. A complement would put no bass note on the
  downbeat.

Replace "always a subset or a complement of the first" with:

> A member's figure is a function of the shared part, never an independent
> idea.

Keep the original wording visible in an `AMENDED, 2026-09-03` note with the
reason, the way section 7's amendment does -- the error is instructive and the
next reader should not have to rediscover it.

- [ ] **Step 2: Update `ROADMAP.md`**

Under *Form: repetition, tension and release*, add a completed item above
*Phrase length inside the interval*:

```markdown
- [x] **The foundation stratum.** The kit and the bass select from one shared
      part rather than the bass deriving its figure from the kick's by
      doubling. Byte-identical, proved by `AntiphonVoiceLab parity` before and
      after rather than by the suite passing.

      **This was not on the list, and the phrase item cannot be built without
      it.** `figureFor(Bass)` nudged its pulse count coprime with its steps SO
      THAT the figure would not repeat inside the interval -- because a
      repeating scaled copy of the kick is the kick again. A phrase period
      fights that rule, and the rule was right; what was wrong was deriving one
      figure from another at all.
```

- [ ] **Step 3: Update `AGENTS.md`**

In the layout map's `libs/jambot/` entry, after the existing description, add:

```
                            #   The band's rhythmic ground is the FOUNDATION
                            #   stratum's shared part -- the kit and the bass
                            #   select from one figure rather than the bass
                            #   deriving its own from the kick's. New voices
                            #   attach to a stratum; they do not invent a
                            #   rhythm (docs/BOT-CHAT.md section 16).
```

- [ ] **Step 4: Full suite, then commit both repos**

```bash
cmake --build build-vote -j $(nproc)
ctest --test-dir build-vote --output-on-failure
```

Expected: 8/8.

```bash
cd libs/jambot && git add -A && git commit -m "Amend 16.2: the kit takes a subset and the bass takes the whole." && \
  git push origin HEAD:main
cd /home/programming/antiphon && git add -A && \
  git commit -m "Record the foundation stratum, and why it came before the phrase." && \
  git fetch -q origin && git merge-base --is-ancestor origin/main HEAD && git push origin main
```

---

## What lands next, and why it is not here

`performanceSeed` and the phrase period, per spec section 8 step 2. They need
this task list finished first, and they need the three questions in spec
section 7 answered by listening rather than by argument:

1. how many common tones is "enough" before a pitch contour repeats literally;
2. whether the seed's multiple of the chart period should be bounded by the
   interval;
3. whether the fraction fallback should align to bar lines.

`AntiphonVoiceLab band --seed N` and the `parity` mode added in Task 1 are the
instruments for all three -- and there the parity diff is expected to be
non-empty, which is the point of having proved it empty here first.
