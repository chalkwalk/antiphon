# Intent Commands Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the conductor the band's commands, so that it can state what the band did -- and with that, delete the last of the rank-stagger arbitration.

**Architecture:** One abstract `BandControl` replaces the loose `Recruit` callback: the conductor asks its host for the current interval, the band's phases, a command, and a player. Commands name the interval they take effect FROM, so every member applies the same command in the same interval however late the message arrived. Because the conductor issues the command, it knows whether the band was playing -- which is the fact `kIdleSpeakerPenaltyMs` was approximating -- so command confirmations become one line from one bot and the timer goes.

**Tech Stack:** C++17, chalkwalk-jambot (JUCE-free, Catch2 via `test/JuceUnitShim.h`), Antiphon (`PracticeRoom`).

**Spec:** `docs/superpowers/specs/2026-08-31-conductor-design.md`

## Global Constraints

- **ASCII only in source files.** `--` for an em dash, `->` for arrows.
- **2-space indent, braces on same line, members lowerCamelCase.** C++17.
- **chalkwalk-jambot is JUCE-free** and must not learn about Antiphon types.
- **No comments except for non-obvious invariants.** A comment explains *why*.
- **clang-format 20.1.8 is CI-enforced in Antiphon only.** jambot has no `.clang-format`; do not run it there. Use `/tmp/claude-1002/-home-programming-antiphon/67ee3feb-9a95-4950-bebd-fde1300ad5ab/scratchpad/cfvenv/bin/clang-format`.
- **Iterate with `-DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot`; bump and re-verify with NO override before done.** Antiphon needs `-DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`.
- **Verify the room suite ten times on a quiet machine.** A concurrent build has produced a spurious failure in it before; one green run is not evidence.

## What this changes about timing, exactly

Read before Task 3, because the obvious reading is wrong.

- **The room already acts as a band.** `latchBand()` at tick 0 takes every bot's phase together, so a stop asked mid-interval already lands on the whole band at the next head. This plan does NOT introduce that.
- **What it removes** is the residual window `ROADMAP.md` records under *A command reaches the band one bot at a time*: chat reaches each bot through its own `callAsync`, so a latch landing inside those four dispatches catches some bots before the command and some after.
- **A start still lands mid-interval.** `PracticeRoom::onTick` catches it deliberately -- waiting for the next head puts an interval of silence between asking and hearing, with nothing musical in the gap. The index names the interval a command takes effect FROM; a stop lands at its head, a start as soon as there is time to render inside it.

## File Structure

| File | Responsibility |
|---|---|
| `libs/jambot/src/BandControl.h` (new) | The abstract host: clock, phases, command, recruit. |
| `libs/jambot/src/Conductor.h` / `.cpp` | Holds a `BandControl*`; owns band commands and their one confirmation. |
| `libs/jambot/test/FakeBandControl.h` (new) | The test double, shared by the conductor suite. |
| `libs/jambot/test/ConductorTests.cpp` | Recruit via the interface; commands; the confirmation. |
| `libs/jambot/src/PracticeBot.h` / `.cpp` | Loses `pendingBandReply`, `bandReplyTimer`, `heardAnotherBot`, `speakDelayMs`, `botsPresent`, `rankAmong`, `kSpeakStaggerMs`, `kIdleSpeakerPenaltyMs`. Still ACTS. |
| `src/PracticeRoom.h` / `.cpp` (Antiphon) | Implements `BandControl`. |

---

### Task 1: One interface, and Recruit folded into it

No behaviour changes. This is the migration that stops there being two ways for a host to answer the conductor.

**Files:**
- Create: `libs/jambot/src/BandControl.h`, `libs/jambot/test/FakeBandControl.h`
- Modify: `libs/jambot/src/Conductor.h` / `.cpp`, `libs/jambot/test/ConductorTests.cpp`
- Modify: `src/PracticeRoom.h` / `.cpp` (Antiphon)

**Interfaces:**
- Produces:

```cpp
// What the conductor needs from whoever hosts the room, and nothing else.
//
// One class rather than four callbacks because they are one relationship: a
// host that provides three of them should fail to COMPILE, where four loose
// std::functions fail at runtime, in a room, on a Tuesday.
class BandControl {
public:
  virtual ~BandControl() = default;

  // The interval being rendered now. Commands are named relative to this, so
  // it is the one number the conductor cannot work out for itself.
  virtual int currentInterval() const = 0;

  // What every player is doing. The conductor states one fact about the band
  // and this is where the fact comes from -- it does not infer it from what it
  // last commanded, because a player can be told to stop on its own.
  virtual std::vector<BandPlayState::State> phases() const = 0;

  // Play, or bring it to an end, taking effect FROM `atInterval`. A stop lands
  // at that interval's head; a start lands as soon as there is time inside it.
  virtual void command(BotChat::Act act, int atInterval) = 0;

  // One more player. False means the room said no -- the cap belongs to the
  // host and the conductor never learns what it is.
  //
  // Defaulted, because a room that does not grow is a room: implementing this
  // is a decision, and NOT implementing it should say so rather than looking
  // like nobody wired it up.
  virtual bool addPlayer() { return false; }
};
```

- Replaces: `Conductor::Recruit`, `Conductor::setRecruit`. `void Conductor::setControl(BandControl *c)` takes its place.

- [ ] **Step 1: Write the interface and the fake**

`BandControl.h` as above, plus `test/FakeBandControl.h`:

```cpp
#pragma once

#include "../src/BandControl.h"

#include <vector>

namespace jambot::test {

// A host a suite drives by hand. Records what it was asked and answers with
// whatever the test set.
class FakeBandControl final : public BandControl {
public:
  int interval = 0;
  std::vector<BandPlayState::State> state;
  bool allowGrowth = true;
  int added = 0;

  struct Commanded {
    BotChat::Act act;
    int atInterval;
  };
  std::vector<Commanded> commands;

  int currentInterval() const override { return interval; }
  std::vector<BandPlayState::State> phases() const override { return state; }
  void command(BotChat::Act act, int atInterval) override {
    commands.push_back({act, atInterval});
  }
  bool addPlayer() override {
    if (!allowGrowth)
      return false;
    ++added;
    return true;
  }
};

} // namespace jambot::test
```

- [ ] **Step 2: Migrate the conductor**

In `Conductor.h`, replace `using Recruit`/`setRecruit`/`Recruit recruit;` with:

```cpp
  // The host. Not owned, and it must outlive this conductor -- whoever hosts
  // the room creates both, and destroys the conductor first.
  void setControl(BandControl *c);
```

and a `BandControl *control = nullptr;` member. In `Conductor.cpp`, `setRecruit`
becomes `setControl`, and the recruit path becomes:

```cpp
  BandControl *host = nullptr;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    host = control;
  }
  if (host == nullptr)
    return;

  if (host->addPlayer()) {
```

- [ ] **Step 3: Migrate the tests**

Every `rig.conductor->setRecruit([&] { ... })` in `ConductorTests.cpp` becomes a
`FakeBandControl` on the `Rig` with `setControl(&rig.control)`. The four
recruit cases keep their assertions exactly -- **do not weaken any of them**;
this task changes no behaviour, and the existing tests passing unchanged is the
only thing proving that.

- [ ] **Step 4: Migrate PracticeRoom**

`PracticeRoom` gains `: public BandControl` privately or via a small nested
adapter -- **whichever keeps `PracticeRoom`'s public surface unchanged**, since
`currentInterval`, `phases` and `command` are not things its own callers should
gain. Implement:

- `currentInterval()` -- the room has this at `onTick`; store the last index in
  an `std::atomic<int>` and return it.
- `phases()` -- `bandLatchedPhases()` already exists and is the observable that
  says what will actually be heard. Use that one, not `bandPhases()`.
- `command(act, atInterval)` -- for now, apply immediately as the chat path does
  today; Task 3 gives `atInterval` its meaning. **Say so in a comment**, so the
  ignored parameter is visibly deliberate.
- `addPlayer()` -- already exists and already returns the right bool.

Replace `conductor->setRecruit(...)` with `conductor->setControl(this)`.

- [ ] **Step 5: Run both suites**

```bash
cd /home/programming/chalkwalk-jambot && cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
cmake --build build-intent -j $(nproc) && ctest --test-dir build-intent --output-on-failure
```

Expected: jambot 4/4, Antiphon 8/8, with no test changed except the mechanical
`setRecruit` -> `setControl` migration.

- [ ] **Step 6: Commit**

```bash
git commit -m "Give the conductor one host interface, not four callbacks."
```

---

### Task 2: The conductor owns the command, and says the one thing about it

**Files:**
- Modify: `libs/jambot/src/Conductor.h` / `.cpp`, `libs/jambot/test/ConductorTests.cpp`

**Interfaces:**
- Consumes: `BandControl` (Task 1); `BotLanguage::Intent::StartPlaying`, `Intent::StopPlaying`; `BandPlayState::State`.
- Produces: the conductor acting on start/stop and speaking one confirmation.

- [ ] **Step 1: Write the failing tests**

```cpp
    beginTest("a stop is one line about the band, not four");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Playing,
                           BandPlayState::State::Playing,
                           BandPlayState::State::Silent};
      rig.control.interval = 7;
      rig.client->joins("you");
      const auto before = rig.client->said.size();
      rig.client->say("you", "band, stop");

      expect(rig.control.commands.size() == 1, "the band was not commanded");
      expect(rig.control.commands.front().act == BotChat::Act::StopPlaying);
      expect(rig.control.commands.front().atInterval == 8,
             "a stop takes effect from the NEXT interval, not this one");

      expect(rig.client->said.size() == before + 1,
             "a stop got more or fewer than one answer");
      expect(rig.client->said.back().find("wrapping") != std::string::npos,
             "somebody was playing and the band did not say it was ending: " +
                 rig.client->said.back());
    }

    beginTest("a half-stopped band is still wrapping up");
    {
      // The case the old idle penalty existed for: with some bots playing and
      // some silent, "already stopped" would tell the room nothing was
      // happening while the rest ended the tune. The conductor states one fact
      // and the fact is that the band is stopping.
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent,
                           BandPlayState::State::Playing};
      rig.client->joins("you");
      rig.client->say("you", "band, stop");

      expect(rig.client->said.back().find("wrapping") != std::string::npos,
             rig.client->said.back());
    }

    beginTest("a band that is already silent says so");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent,
                           BandPlayState::State::Silent};
      rig.client->joins("you");
      rig.client->say("you", "band, stop");

      expect(rig.client->said.back().find("already") != std::string::npos,
             rig.client->said.back());
      expect(rig.control.commands.empty(),
             "a silent band was commanded to stop again");
    }

    beginTest("a start takes effect from the interval being played");
    {
      Rig rig;
      rig.control.state = {BandPlayState::State::Silent};
      rig.control.interval = 4;
      rig.client->joins("you");
      rig.client->say("you", "band, play");

      expect(rig.control.commands.size() == 1);
      expect(rig.control.commands.front().act == BotChat::Act::StartPlaying);
      expect(rig.control.commands.front().atInterval == 4,
             "a start waited for the next interval, which is a bar of silence "
             "for no musical reason");
    }
```

- [ ] **Step 2: Run and watch them fail**

Expected: FAIL -- the conductor ignores start and stop today.

- [ ] **Step 3: Implement**

In `Conductor::onChatMessage`, after the `AddPlayer` branch, handle the two
play intents. The shape:

```cpp
  const auto intent = BotLanguage::read(rest).intent;

  if (intent == BotLanguage::Intent::StopPlaying) {
    const auto now = host->phases();
    const bool anyAudible =
        std::any_of(now.begin(), now.end(), [](BandPlayState::State s) {
          return s != BandPlayState::State::Silent;
        });

    if (!anyAudible) {
      say("already stopped.");
      return;
    }

    // From the NEXT interval: an ending is musical, and the head of an interval
    // is where the band can begin one together.
    host->command(BotChat::Act::StopPlaying, host->currentInterval() + 1);
    say("wrapping it up -- ending on the downbeat after this one.");
    return;
  }

  if (intent == BotLanguage::Intent::StartPlaying) {
    // From the interval being played. There is nothing musical in the gap
    // before the next one, so waiting for it is a bar of silence for nothing.
    host->command(BotChat::Act::StartPlaying, host->currentInterval());
    say("coming in.");
    return;
  }
```

**Use the real wording.** `grep -n 'wrapping' libs/jambot/src/BotChat.cpp` for
what the band says today and keep it, so the line a player reads does not change
just because a different bot says it.

- [ ] **Step 4: Run the tests**

Expected: PASS, both suites.

- [ ] **Step 5: Prove the half-stopped case has teeth**

Change `anyAudible` to check `s == BandPlayState::State::Playing` for ALL rather
than ANY, and rerun. Expected: "a half-stopped band is still wrapping up" FAILS.
Revert.

That is the exact bug the idle penalty was written to prevent, so it is the one
worth being unable to reintroduce.

- [ ] **Step 6: Commit**

---

### Task 3: The band stops answering, and the last stagger dies

**Files:**
- Modify: `libs/jambot/src/PracticeBot.h` / `.cpp`
- Modify: `libs/jambot/test/PracticeBotTests.cpp`
- Modify: `src/PracticeRoom.cpp`, `test/PracticeRoomTests.cpp` (Antiphon)

**Interfaces:**
- Produces: `PracticeBot` with no `pendingBandReply`, `bandReplyTimer`, `heardAnotherBot`, `speakDelayMs`, `botsPresent`, `rankAmong`, `kSpeakStaggerMs`, `kIdleSpeakerPenaltyMs`. It still ACTS on band commands; it no longer SPEAKS about them.

- [ ] **Step 1: Make the room honour `atInterval`**

`PracticeRoom::command(act, atInterval)` currently applies immediately. Make it
record the request and apply it at the latch:

- store `{act, atInterval}` under `botsMutex`;
- in `onTick`, at `tick == 0` and **before** `latchBand()`, apply any request
  whose `atInterval <= intervalIndex`, then clear it;
- a request naming an interval already passed when it arrives is **rejected, not
  applied late** -- log nothing, do nothing. A late command must never make one
  bot play something the others did not.

**A start is the exception and must still land mid-interval**: apply a
`StartPlaying` request as soon as it arrives if its interval is the current one,
which is what the existing `bandWantsStart()` path already does.

- [ ] **Step 2: Write the failing test for the reject rule**

This cannot be driven through chat: the conductor only ever names the current
interval or the next one, so a stale command has no way in from outside. The
rule therefore wants testing at the room, which needs the room to expose the
interface it implements:

```cpp
  // The room AS the conductor sees it. Exposed because the command rules --
  // when a request lands, and that a stale one is refused -- are the room's and
  // cannot be reached through chat, since a conductor never names a past
  // interval.
  BandControl &control() { return *this; }
```

Then, in `test/PracticeRoomTests.cpp`:

```cpp
    beginTest("a command for an interval already gone is refused");
    {
      // Late is worse than never: applying it would put one bot an interval out
      // of step with the others, which is the whole thing the index prevents.
      PracticeRoom room;
      expect(room.start(testConfig("you")));

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you), "the band never introduced itself");
      expect(startBand(you, room), "the band would not start");

      const int now = room.control().currentInterval();

      // An interval that has already gone. Two back rather than one, so this
      // cannot pass by landing exactly on a boundary.
      room.control().command(BotChat::Act::StopPlaying, now - 2);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(2500);

      for (auto p : room.bandLatchedPhases())
        expect(p == BandPlayState::State::Playing,
               "a stale command was applied late, which is the split the "
               "interval index exists to prevent");

      // And a fresh one still works, so the test above is not passing because
      // commands are broken outright.
      room.control().command(BotChat::Act::StopPlaying,
                             room.control().currentInterval() + 1);
      expect(waitUntil(
                 [&] {
                   for (auto p : room.bandLatchedPhases())
                     if (p == BandPlayState::State::Playing)
                       return false;
                   return !room.bandLatchedPhases().empty();
                 },
                 10000),
             "a fresh command did not take effect either");
    }
```

**Written against `bandLatchedPhases()`**, not `bandPhases()`: the latch is
what decides what is heard, and the raw states flip the instant a command
lands, which `PracticeRoom.h` records as having made an earlier test in this
file flaky.

The second half is the part that gives the first half meaning. A test that only
asserts "nothing happened" passes just as well when commands never work at all.

- [ ] **Step 3: Strip the reply path from PracticeBot**

Delete `pendingBandReply`, `bandReplyTimer`, `onBandReplyDue`, `heardAnotherBot`,
`speakDelayMs` (both), `botsPresent`, `rankAmong`, `kSpeakStaggerMs`,
`kIdleSpeakerPenaltyMs`. In `onChatMessage`, the `answer.forBand` branch stops
speaking -- the bot still performs `answer.act`, and says nothing.

**Singular replies are untouched.** "Ravo, what are you playing" is still Ravo's
to answer; only `forBand` loses its voice.

- [ ] **Step 4: Confirm the mechanism is gone**

```bash
grep -rn 'speakDelayMs\|rankAmong\|heardAnotherBot\|pendingBandReply\|bandReplyTimer\|kIdleSpeakerPenaltyMs\|kSpeakStaggerMs' libs/jambot/src libs/jambot/test src test
```

Expected: no output.

- [ ] **Step 5: Fix the Antiphon tests that expect a bandmate to answer**

`test/PracticeRoomTests.cpp` has cases asserting exactly one `-bot]` reply to
`band stop`, including the half-stopped one. The replies now come from
`Conductor[bot]`. Widen those filters to `bot]` and **re-read each assertion**:
"with the band half stopped, the one that acts speaks" is now about the
conductor stating a fact, so its name and comment should say that rather than
being left describing a race that no longer exists.

- [ ] **Step 6: Verify, ten times, quiet**

```bash
ctest --test-dir build-intent --output-on-failure
for i in $(seq 1 10); do
  n=$(./build-intent/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests PracticeRoom 2>&1 | grep -c '!!! Test')
  echo "run $i: $n failures"
done
```

Expected: 8/8 and ten clean runs, on a machine doing nothing else.

- [ ] **Step 7: Format, bump, re-verify with NO override, close the roadmap**

Two `ROADMAP.md` items close together:

- *One arbitration primitive, four uses* -- the surviving sub-item says the
  stagger stays for commands "until the conductor OWNS commands". It does now.
  Tick it and say what removed it.
- *A command reaches the band one bot at a time* -- subsumed. The index means
  every bot applies the command in the same interval however late the dispatch
  was, so the window it describes is closed rather than narrowed. Tick it and
  say so.

Also update `libs/jambot/docs/BOT-CHAT.md` section 15 if it describes the band
answering a stop collectively.

---

## Self-review notes

- Spec coverage: the intent model (Task 2), the reject rule (Task 3 Step 1), the
  conductor stating band facts (Task 2), the arbitration deletion (Task 3).
- The `atInterval` parameter is deliberately ignored in Task 1 and given meaning
  in Task 3, with a comment saying so -- an unused parameter that looks like an
  oversight is worse than one that says it is waiting.
- `phases()` uses `bandLatchedPhases()` throughout, never `bandPhases()`, for
  the flakiness reason recorded in `PracticeRoom.h`.
