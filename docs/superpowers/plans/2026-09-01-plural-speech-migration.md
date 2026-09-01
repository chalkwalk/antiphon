# Plural Speech Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the arrival roster to the `Conductor`, delete the rank-stagger arbitration it existed to coordinate, and with it the race that fails about one run in seven.

**Architecture:** The roster is one fact about the room -- who is here -- so under the spec's *one answer or many* rule it belongs to the conductor, which is alone and therefore needs no arbitration at all. `PracticeBot` stops announcing, and `speakDelayMs`, `rankAmong`, `arrivalDelayMs`, `announcedMe`, `arrivalDone` and `arrivalTimer` go with it. Command confirmations (`band stop` and friends) are deliberately NOT moved -- they need band state the conductor does not own until plan 4.

**Tech Stack:** C++17, chalkwalk-jambot (JUCE-free, Catch2 via `test/JuceUnitShim.h`), Antiphon (`PracticeRoom`).

**Spec:** `docs/superpowers/specs/2026-08-31-conductor-design.md`

## Global Constraints

- **ASCII only in source files.** No non-ASCII anywhere. `--` for an em dash, `->` for arrows.
- **2-space indent, braces on same line, members lowerCamelCase.**
- **C++17.** **chalkwalk-jambot is JUCE-free** -- no JUCE header in `libs/jambot`.
- **No comments except for non-obvious invariants.** A comment explains *why*.
- **clang-format is CI-enforced at version 20.1.8**, which disagrees with newer versions about wrapping. Use the pinned one: `/tmp/claude-1002/-home-programming-antiphon/67ee3feb-9a95-4950-bebd-fde1300ad5ab/scratchpad/cfvenv/bin/clang-format`, or `pip install clang-format==20.1.8` in a venv.
- **Iterate on jambot with `-DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot`; bump the submodule and re-verify with NO override before calling anything done.**
- **Antiphon needs `-DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`.**

## What is NOT in this plan

Named so nobody starts them here:

- **Command confirmations.** `band stop` / `band play` replies stay exactly where they are, on `pendingBandReply` / `bandReplyTimer` / `heardAnotherBot`. The conductor cannot state whether the band wrapped up or was already stopped until it OWNS the command, which is plan 4. Moving them now would have the conductor guessing at state it does not have.
- **`heardAnotherBot`, `pendingBandReply`, `bandReplyTimer`, `kIdleSpeakerPenaltyMs`** therefore all survive this plan.
- Intent commands, membership, voting.

## File Structure

| File | Responsibility |
|---|---|
| `libs/jambot/src/Conductor.h` / `.cpp` | Gains the arrival window and the roster: who is here, named once. |
| `libs/jambot/test/ConductorTests.cpp` | Its tests, including that it does not repeat itself. |
| `libs/jambot/src/PracticeBot.h` / `.cpp` | Loses `onArrivalDue`, `arrivalDelayMs`, `speakDelayMs`, `rankAmong`, `announcedMe`, `arrivalDone`, `arrivalTimer`, `botsPresent`, `setBandmates`. |
| `libs/jambot/test/PracticeBotTests.cpp` | Loses the stagger test, which pins a mechanism that no longer exists. |
| `src/PracticeRoom.cpp` (Antiphon) | Stops calling `setBandmates`; tells the conductor the band name instead. |
| `test/PracticeRoomTests.cpp` (Antiphon) | The roster now comes from `Conductor[bot]`, not from a player. |

---

### Task 1: The conductor names the room

**Files:**
- Modify: `libs/jambot/src/Conductor.h`, `libs/jambot/src/Conductor.cpp`
- Modify: `libs/jambot/test/ConductorTests.cpp`

**Interfaces:**
- Consumes: `Conductor(std::string, std::unique_ptr<BotClient::Client>)`, `join`, `part`, `say`, `owner`, `client()` from plan 1. `BotClient::Client::members()`, `createTimer(std::function<void()>)`, `BotClient::Timer::start(int)/stop()`. `BotNames::looksLikeBandmate`, `BotNames::handleOf`.
- Produces:
  - `void Conductor::setBandName(std::string name)`
  - `static constexpr int Conductor::kArrivalDelayMs = 4000`
  - protected `virtual void onArrivalDue()` -- so `TutorBot` can extend it later without this plan touching `TutorBot`.

- [ ] **Step 1: Write the failing test**

Add to `ConductorTests.cpp`, inside `runTest()`:

```cpp
    beginTest("names the room once, after the arrival window");
    {
      Rig rig;
      rig.conductor->setBandName("The Understudies");
      rig.client->joins("Quado[kit-bot]");
      rig.client->joins("Vessa[bass-bot]");

      expect(rig.client->said.empty(),
             "it announced before the room had finished assembling");

      rig.client->fireDueTimers();

      expect(rig.client->said.size() == 1, "expected exactly one roster line");
      const auto &line = rig.client->said.front();
      expect(line.find("The Understudies") != std::string::npos, line);
      expect(line.find("quado") != std::string::npos, line);
      expect(line.find("vessa") != std::string::npos, line);
    }

    beginTest("it does not name the room twice");
    {
      Rig rig;
      rig.client->joins("Quado[kit-bot]");
      rig.client->fireDueTimers();
      const auto after = rig.client->said.size();

      // A second wake, however it is caused, must not produce a second roster.
      rig.client->fireDueTimers();
      expect(rig.client->said.size() == after,
             "the conductor introduced the room twice");
    }

    beginTest("the roster is the band, not everybody present");
    {
      Rig rig;
      rig.conductor->setOwner("you");
      rig.client->joins("Quado[kit-bot]");
      rig.client->joins("you");
      rig.client->fireDueTimers();

      expect(rig.client->said.size() == 1);
      expect(rig.client->said.front().find("you") == std::string::npos,
             "the roster listed a human: " + rig.client->said.front());
    }
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cd /home/programming/chalkwalk-jambot
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j $(nproc)
```

Expected: FAIL -- `'class Conductor' has no member named 'setBandName'`.

- [ ] **Step 3: Implement**

`Conductor.h`, public:

```cpp
  // What the band calls itself, if anything. Empty means the roster is just a
  // list of names.
  void setBandName(std::string name);

  // Long enough for the join notices to finish scrolling before the one line
  // anybody is meant to read. NOT staggered: there is one conductor, so there
  // is nobody to stagger against -- which is the whole point of moving this
  // here. `PracticeBot` used 4000ms plus a per-bot rank offset, and the offset
  // is what raced.
  static constexpr int kArrivalDelayMs = 4000;

protected:
  // Virtual so a subclass can extend the arrival without this file knowing
  // about it.
  virtual void onArrivalDue();
```

private members:

```cpp
  std::string bandName;
  std::unique_ptr<BotClient::Timer> arrivalTimer;
  std::atomic<bool> arrivalDone{false};
```

`Conductor.cpp`, in the constructor after `addListener`:

```cpp
  if (netClient)
    arrivalTimer = netClient->createTimer([this] { onArrivalDue(); });
```

**Arm it at the end of `join()`, NOT from `onConnected()`.** This matters and is
easy to get wrong:

```cpp
  active = true;
  netClient->connect(host, port, botName, "");

  // Armed here rather than from onConnected, because a SUBCLASS overrides that
  // -- TutorBot greets from it -- and an override does not chain by default. A
  // conductor whose arrival never fired would post no roster at all, and the
  // room that would break is the DEFAULT one, since `withTutor` is on at the
  // command line while every test fixture turns it off. That is a bug nothing
  // here would have caught.
  if (arrivalTimer)
    arrivalTimer->start(kArrivalDelayMs);
  return true;
```

In `part()`, before the disconnect:

```cpp
  if (arrivalTimer)
    arrivalTimer->stop();
```

Add:

```cpp
void Conductor::setBandName(std::string name) {
  std::lock_guard<std::mutex> sl(stateMutex);
  bandName = std::move(name);
}

void Conductor::onArrivalDue() {
  // Once per room. There is no "unless somebody else already did it" here --
  // that question only existed because there were peers who might have.
  if (!isActive() || arrivalDone.exchange(true))
    return;

  std::vector<std::string> entries;
  for (const auto &m : netClient->members()) {
    if (!BotNames::looksLikeBandmate(m.username))
      continue;
    const auto handle = BotNames::handleOf(m.username);
    const auto open = m.username.find('[');
    std::string instrument;
    if (open != std::string::npos) {
      const auto rest = m.username.substr(open + 1);
      const auto end = rest.find("-bot]");
      instrument = end == std::string::npos ? rest : rest.substr(0, end);
    }
    entries.push_back(instrument.empty() ? handle
                                         : handle + " (" + instrument + ")");
  }

  if (entries.empty())
    return;

  std::sort(entries.begin(), entries.end());

  std::string roster;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    if (!bandName.empty())
      roster = bandName + " -- ";
  }
  for (std::size_t i = 0; i < entries.size(); ++i)
    roster += (i ? ", " : "") + entries[i];
  roster += ".";

  say(roster);
}
```

Add `#include "BotNames.h"`, `<algorithm>` and `<vector>` to `Conductor.cpp`. **Do not add an `onConnected` override to `Conductor`** -- see the note above.

- [ ] **Step 4: Run the test**

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Expected: PASS, all suites.

- [ ] **Step 5: Prove a tutor room still gets a roster**

The override trap above is invisible unless something exercises it. Add:

```cpp
    beginTest("a conductor that also teaches still names the room");
    {
      auto owned = std::make_unique<FakeClient>();
      auto *client = owned.get();
      TutorBot tutor("Tutor[bot]", std::move(owned));
      tutor.setBandName("The Understudies");
      tutor.join("127.0.0.1", 2049, 48000.0);
      client->joins("Quado[kit-bot]");

      const auto beforeRoster = client->said.size();
      client->fireDueTimers();

      bool named = false;
      for (auto i = beforeRoster; i < client->said.size(); ++i)
        if (client->said[i].find("quado") != std::string::npos)
          named = true;
      expect(named,
             "the tutor's onConnected override swallowed the conductor's "
             "arrival, so the default room would have had no roster");
    }
```

This needs `#include "../src/TutorBot.h"` in `ConductorTests.cpp`. Run it, and
confirm it passes. Then temporarily move the arm back into an
`onConnected()` override on `Conductor` and confirm this test FAILS while the
others still pass -- that is the whole reason it exists.

- [ ] **Step 6: Prove the once-only guard has teeth**

Temporarily change `if (!isActive() || arrivalDone.exchange(true))` to `if (!isActive())` and rerun. Expected: "the conductor introduced the room twice" FAILS. Revert.

- [ ] **Step 7: Commit**

```bash
git add src/Conductor.h src/Conductor.cpp test/ConductorTests.cpp
git commit -m "Let the conductor name the room, since it is one fact about it."
```

---

### Task 2: The band stops announcing, and the stagger goes

This is the task that deletes the race. It is deliberately one task: removing the announcement without removing the machinery would leave dead code that still looks load-bearing.

**Files:**
- Modify: `libs/jambot/src/PracticeBot.h`, `libs/jambot/src/PracticeBot.cpp`
- Modify: `libs/jambot/test/PracticeBotTests.cpp`

**Interfaces:**
- Consumes: Task 1's `Conductor::onArrivalDue`.
- Produces: `PracticeBot` with no `setBandmates`, no `speakDelayMs`, no `rankAmong`, no `arrivalDelayMs`, no `onArrivalDue`, no `botsPresent`, no `announcedMe`, no `arrivalDone`, no `arrivalTimer`. `humansPresent()`, `pendingBandReply`, `bandReplyTimer`, `heardAnotherBot` and `kIdleSpeakerPenaltyMs` all REMAIN.

- [ ] **Step 1: Delete the stagger test first**

`test/PracticeBotTests.cpp` has `beginTest("no two bots in a band wake close enough to race")`, which asserts a minimum separation between `speakDelayMs` values. It pins the mechanism being removed, so it goes with it. Delete the whole `beginTest` block.

**Do not replace it with a conductor-side equivalent.** There is nothing to separate any more: the property it defended -- two bots must not wake close together -- is not weakened by this change, it is made meaningless, and a test asserting a property that cannot fail is worse than no test.

- [ ] **Step 2: Remove the announcement from PracticeBot**

In `PracticeBot.cpp` delete, in full:

- `rankAmong` (file-static, near `speakDelayMs`)
- `PracticeBot::speakDelayMs` (both the static and the one-argument member)
- `PracticeBot::arrivalDelayMs`
- `PracticeBot::onArrivalDue` -- the whole function, including the roster construction and the joining-instructions line
- `PracticeBot::botsPresent`
- `PracticeBot::setBandmates`
- the `arrivalTimer` creation in the constructor and its `stop()` in `part()`
- the re-arm block in `onRoomMembershipChange` that sets `arrivalDone = false; announcedMe = false; arrivalTimer->start(...)`
- the `announcedMe = true` assignment in `onChatMessage`

In `PracticeBot.h` delete the matching declarations and members: `setBandmates`, `speakDelayMs` (both), `arrivalDelayMs`, `onArrivalDue`, `botsPresent`, `arrivalTimer`, `announcedMe`, `arrivalDone`, `bandmates`, `bandName`, and `kSpeakStaggerMs` if nothing else uses it.

**Check `kSpeakStaggerMs` before deleting it** -- `pendingBandReply` still arms `bandReplyTimer` with `speakDelayMs()`, which is going. That call site needs a replacement delay:

```cpp
      bandReplyTimer->start(answer.act != BotChat::Act::None
                                ? kBandReplyDelayMs
                                : kBandReplyDelayMs + kIdleSpeakerPenaltyMs);
```

with, in `PracticeBot.h`:

```cpp
  // Command confirmations still race, and still arbitrate by delay. That is
  // knowingly left: the conductor cannot say whether the band wrapped up or was
  // already stopped until it OWNS the command, which is a later plan. What
  // survives is the part that was never about rank -- a bot that ACTED answers
  // ahead of one that had nothing to do, because "already stopped" from an idle
  // bot would tell the room nothing was happening while three bots ended the
  // tune.
  static constexpr int kBandReplyDelayMs = 220;
```

- [ ] **Step 3: Build and run**

```bash
cd /home/programming/chalkwalk-jambot
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Expected: PASS. If `PracticeBotTests` references anything just deleted, remove those assertions -- but read each one first and say in the commit message what behaviour stopped being tested.

- [ ] **Step 4: Confirm the mechanism is gone rather than hidden**

```bash
grep -rn 'speakDelayMs\|rankAmong\|announcedMe\|arrivalDone\|arrivalDelayMs\|botsPresent\|setBandmates' src/ test/
```

Expected: no output. Anything left is a survivor to explain or delete.

- [ ] **Step 5: Commit**

```bash
git add src/PracticeBot.h src/PracticeBot.cpp test/PracticeBotTests.cpp
git commit -m "Take the roster off the band, and the race with it."
```

---

### Task 3: The room wires it up, and the race stops failing

**Files:**
- Modify: `src/PracticeRoom.cpp` (Antiphon)
- Modify: `test/PracticeRoomTests.cpp` (Antiphon)

**Interfaces:**
- Consumes: `Conductor::setBandName(std::string)` (Task 1); `PracticeBot` without `setBandmates` (Task 2).
- Produces: a room whose roster is posted by `Conductor[bot]`.

- [ ] **Step 1: Point Antiphon at the working tree**

```bash
cd /home/programming/antiphon
cmake -B build-plural -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
```

Confirm it prints `chalkwalk-jambot: OVERRIDE at ...`.

- [ ] **Step 2: Replace the setBandmates calls**

`src/PracticeRoom.cpp` calls `b->setBandmates(names, cfg.bandName.toStdString())` at two sites (around :128 and :246 before this plan; find them with grep, the line numbers move). Delete both. After the conductor is created in `start()`, add:

```cpp
  conductor->setBandName(cfg.bandName.toStdString());
```

before the `conductor->join(...)` call, so the name is set before the arrival window can fire.

- [ ] **Step 3: Update the room tests that expect a player to post the roster**

```bash
grep -n 'roster\|Understudies\|introduce' test/PracticeRoomTests.cpp | head -20
```

Every assertion that the roster came from a `-bot]` player now expects `Conductor[bot]`. The latecomer case -- which asserted that a bot joining later introduces itself and names the whole room -- is the one that changes most: under the conductor the ROSTER is not re-posted at all, so read that test and decide with fresh eyes whether the behaviour it defends still exists. **If it does not, delete it and say so in the commit message rather than weakening it into something that passes.**

- [ ] **Step 4: Run the full suite, repeatedly**

The failure this plan exists to remove was one run in seven, so a single green run proves nothing.

```bash
cmake --build build-plural -j $(nproc)
for i in 1 2 3 4 5 6 7 8 9 10; do
  n=$(./build-plural/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests PracticeRoom 2>&1 | grep -c '!!! Test')
  echo "run $i: $n failures"
done
```

Expected: ten runs, zero failures. **Fewer than ten runs is not evidence** -- the bug being fixed appeared once in seven.

- [ ] **Step 5: Run everything**

```bash
ctest --test-dir build-plural --output-on-failure
```

Expected: 8/8.

- [ ] **Step 6: Format, with the version CI uses**

```bash
CF=/tmp/claude-1002/-home-programming-antiphon/67ee3feb-9a95-4950-bebd-fde1300ad5ab/scratchpad/cfvenv/bin/clang-format
$CF --dry-run -Werror src/*.cpp src/*.h test/*.cpp test/*.h tools/*.cpp
```

Fix anything it reports with `$CF -i`. The local system clang-format is a different version and disagrees about wrapping.

- [ ] **Step 7: Push jambot, bump, and re-verify with NO override**

```bash
cd /home/programming/chalkwalk-jambot && git push origin main && git rev-parse --short HEAD
cd /home/programming/antiphon/libs/jambot && git fetch origin && git checkout <sha>
cd /home/programming/antiphon
rm -rf build-verify && cmake -B build-verify -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
cmake --build build-verify -j $(nproc) && ctest --test-dir build-verify --output-on-failure
```

Expected: configure prints NO `OVERRIDE` line, and 8/8 pass. An override build does not count as done.

- [ ] **Step 8: Commit and update the roadmap**

The `ROADMAP.md` entry *One arbitration primitive, four uses* carries a sub-item recording this race as unfixed. Tick it, and say what actually removed it. The arbitration item itself is NOT complete -- command confirmations still use a timer -- so leave that box open.

```bash
git add src/PracticeRoom.cpp test/PracticeRoomTests.cpp libs/jambot ROADMAP.md
git commit -m "Let the conductor introduce the band, and stop the roster racing."
```

---

## Self-review notes

Checked against the spec:

- *Speech: one answer or many* -- Task 1 implements the roster as one answer. Command confirmations are explicitly out of scope, per the spec's own ordering constraint.
- *Why the axis matters* -- the `kIdleSpeakerPenaltyMs` reasoning is preserved verbatim in the comment Task 2 adds, so the surviving delay says why it is still there.
- The spec's claim that the conductor "deletes rank-stagger arbitration" is only PARTLY delivered here, and the plan says so in *What is NOT in this plan*. The arrival stagger goes; the band-reply delay stays until plan 4.
