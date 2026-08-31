# Conductor Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the band a `Conductor` -- an instrument-less bot that joins a room and speaks -- and make `TutorBot` a subclass of it, so a room contains exactly one silent-instrument bot in every case.

**Architecture:** `Conductor` owns the client lifecycle that `TutorBot` currently duplicates: a username, a `BotClient::Client`, `join`/`part`, the owner's name, and speech. `TutorBot` keeps only its teaching thread. `PracticeRoom` gains one, always. Nothing about commands, plural speech migration, membership or voting is in this plan.

**Tech Stack:** C++17, chalkwalk-jambot (JUCE-free, its own Catch2-via-shim suite), Antiphon (JUCE, `PracticeRoom`).

**Spec:** `docs/superpowers/specs/2026-08-31-conductor-design.md`

## Global Constraints

- **ASCII only in source files.** No non-ASCII anywhere -- comments, string literals or identifiers. `--` for an em dash, `->` for arrows. `juce::String(const char*, size_t)` asserts ASCII validity.
- **2-space indent, braces on same line, members lowerCamelCase.**
- **C++17.**
- **No comments except for non-obvious invariants or protocol workarounds.** No narration. A comment explains *why*.
- **chalkwalk-jambot is JUCE-free.** Nothing in `libs/jambot` may include a JUCE header.
- **A new `src/*.cpp` in Antiphon must be added to BOTH `src/CMakeLists.txt` and `test/CMakeLists.txt`**, and to `tools/CMakeLists.txt` if a tool uses it.
- **Iterate on jambot with `-DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot`; bump the submodule and re-verify with NO override before calling anything done.** Configure prints `OVERRIDE` when one is in use.
- **Antiphon needs `-DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`** -- its JUCE submodule is deinitialised in favour of the shared checkout.

## File Structure

| File | Responsibility |
|---|---|
| `libs/jambot/test/FakeBotClient.h` (new) | The `BotClient::Client` test double, shared. Currently duplicated inside two test files. |
| `libs/jambot/src/Conductor.h` (new) | The band-leader bot: identity, client lifecycle, owner, speech. |
| `libs/jambot/src/Conductor.cpp` (new) | Its implementation. |
| `libs/jambot/test/ConductorTests.cpp` (new) | Its suite. |
| `libs/jambot/src/TutorBot.h` (modify) | Becomes `class TutorBot : public Conductor`. Loses the members Conductor now owns. |
| `libs/jambot/src/TutorBot.cpp` (modify) | Loses `join`, `part`, `setOwner`, and its copies of the members. |
| `libs/jambot/test/CMakeLists.txt` (modify) | Adds `ConductorTests.cpp`. |
| `src/PracticeRoom.h` / `.cpp` (modify) | Owns one `Conductor`, always. |
| `test/PracticeRoomTests.cpp` (modify) | Asserts the room has one and that it is not counted as a player. |

---

### Task 1: Share the fake client

`FakeClient` is defined inside `test/TutorBotTests.cpp` and again inside `test/PracticeBotTests.cpp`. The Conductor suite needs a third. Extract one before writing a third copy.

**Files:**
- Create: `libs/jambot/test/FakeBotClient.h`
- Modify: `libs/jambot/test/TutorBotTests.cpp` (delete its local `FakeClient`, include the header)
- Modify: `libs/jambot/test/PracticeBotTests.cpp` (same, if its double is compatible -- see Step 2)

**Interfaces:**
- Consumes: `BotClient::Client`, `BotClient::Listener` from `src/BotClient.h`.
- Produces: `jambot::test::FakeClient`, a `BotClient::Client` recording `said`, `whispered`, `room`, `channelNames`, `connected`, and NEW `sawChannelCall` / `lastChannels`, with helpers `say(who, what)`, `plays(who, numSamples)`, `playsQuietly(who, numSamples)`, `playsClipped(who, numSamples)`, `sends(who, block)`.

**The two doubles disagree, and the disagreement matters.** `PracticeBotTests`'s
`setChannels` DISCARDS an empty list:

```cpp
std::vector<std::string> channelNames;
void setChannels(const std::vector<std::string> &names) override {
  if (!names.empty())
    channelNames.push_back(names.front());
}
```

So `setChannels({})` records nothing, and a test asserting "it declared no
channels" against that double would pass whether or not the call ever happened.
That is why `TutorBotTests`'s double carries a separate `sawChannelCall` flag --
the instrument-less bots are exactly the callers whose empty call is the
behaviour under test.

The merged double must keep `channelNames` behaving EXACTLY as it does now, so
the player suites are unaffected, and add two members that record the call
itself:

```cpp
  // Whether setChannels was called at all, and with what. Separate from
  // `channelNames` because that one drops an empty list, and an empty list is
  // the whole assertion for a bot with no instrument.
  bool sawChannelCall = false;
  std::vector<std::string> lastChannels;

  void setChannels(const std::vector<std::string> &names) override {
    sawChannelCall = true;
    lastChannels = names;
    if (!names.empty())
      channelNames.push_back(names.front());
  }
```

- [ ] **Step 1: Read both existing doubles and diff them**

```bash
cd /home/programming/chalkwalk-jambot
sed -n '/class FakeClient/,/^};/p' test/TutorBotTests.cpp  > /tmp/fake-tutor.txt
sed -n '/class FakeClient/,/^};/p' test/PracticeBotTests.cpp > /tmp/fake-practice.txt
diff /tmp/fake-tutor.txt /tmp/fake-practice.txt
```

The tutor's is deliberately smaller -- its comment says *"Only what a tutor touches. It never transmits, never uses a timer and never whispers, so this is much smaller than the fake the players need."* The shared header must be the UNION (the player's, which has the timer and transmit surface), because a double that cannot do what one caller needs is not shared, it is forked.

**If the two have diverged in behaviour rather than only in coverage, stop and report it rather than merging them.** Two test doubles that disagree about what the real client does is a finding, not a merge conflict.

- [ ] **Step 2: Create the shared header from the larger double**

Move `PracticeBotTests.cpp`'s `FakeClient` verbatim into `test/FakeBotClient.h`, wrapped in `namespace jambot::test`, with `#pragma once` and its includes. Add the tutor's `plays`/`playsQuietly`/`playsClipped` helpers if the player's double lacks them.

```cpp
#pragma once

#include "../src/BotClient.h"

#include <string>
#include <vector>

namespace jambot::test {

// The BotClient::Client a suite drives instead of a socket. Shared because
// three suites need one and a forked double stops telling you the same truth.
class FakeClient final : public BotClient::Client {
  // ... the union of the two existing doubles, verbatim ...
};

} // namespace jambot::test
```

- [ ] **Step 3: Point both existing suites at it**

Replace each local `class FakeClient ... };` with `#include "FakeBotClient.h"` and a `using jambot::test::FakeClient;` inside the file's anonymous namespace.

- [ ] **Step 4: Run the full jambot suite -- this is the regression gate**

```bash
cd /home/programming/chalkwalk-jambot
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
```

Expected: PASS, same count as before the change (4 suites). This task adds no test; the existing ones passing unchanged IS the test, because the only risk is that the merged double behaves differently from either original.

- [ ] **Step 5: Commit**

```bash
git add test/FakeBotClient.h test/TutorBotTests.cpp test/PracticeBotTests.cpp
git commit -m "Share the fake client, before writing a third copy of it."
```

---

### Task 2: Conductor exists, joins instrument-less, and parts

**Files:**
- Create: `libs/jambot/src/Conductor.h`, `libs/jambot/src/Conductor.cpp`
- Create: `libs/jambot/test/ConductorTests.cpp`
- Modify: `libs/jambot/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `BotClient::Client`, `BotClient::Listener`, `jambot::test::FakeClient` (Task 1).
- Produces:
  - `class Conductor : protected BotClient::Listener`
  - `Conductor(std::string username, std::unique_ptr<BotClient::Client> client)`
  - `virtual ~Conductor()`
  - `void setOwner(std::string ownerUsername)`
  - `std::string owner() const`
  - `bool join(const std::string &host, int port, double rate)`
  - `void part()`
  - `bool isActive() const`
  - `const std::string &name() const`
  - protected: `BotClient::Client *client()`, `double sampleRate() const`

  `protected BotClient::Listener` rather than `private`, so `TutorBot` can override the callbacks in Task 4.

- [ ] **Step 1: Write the failing test**

Create `libs/jambot/test/ConductorTests.cpp`:

```cpp
#include "../src/Conductor.h"
#include "FakeBotClient.h"
#include "JuceUnitShim.h"

#include <memory>
#include <string>

namespace {

using jambot::test::FakeClient;

struct Rig {
  FakeClient *client = nullptr;
  std::unique_ptr<Conductor> conductor;

  Rig() {
    auto owned = std::make_unique<FakeClient>();
    client = owned.get();
    conductor = std::make_unique<Conductor>("Vell[bot]", std::move(owned));
    conductor->join("127.0.0.1", 2049, 48000.0);
  }
};

class ConductorTests : public shim::UnitTest {
public:
  ConductorTests() : shim::UnitTest("Conductor", "bots") {}

  void runTest() override {
    beginTest("joins with no channel at all");
    {
      Rig rig;
      expect(rig.client->sawChannelCall, "it declared its channels");
      expect(rig.client->lastChannels.empty(),
             "and declared none -- a conductor is a name that talks, not a "
             "strip in anybody's mixer");
      expect(rig.client->connected);
      expect(rig.conductor->isActive());
    }

    beginTest("parting is idempotent");
    {
      Rig rig;
      rig.conductor->part();
      expect(!rig.conductor->isActive());
      rig.conductor->part();
      expect(!rig.conductor->isActive(), "a second part is a no-op, not a crash");
    }

    beginTest("knows its own name and its owner");
    {
      Rig rig;
      expect(rig.conductor->name() == "Vell[bot]");
      expect(rig.conductor->owner().empty(), "nobody is the owner by default");
      rig.conductor->setOwner("you");
      expect(rig.conductor->owner() == "you");
    }
  }
};

TEST_CASE("conductor") {
  ConductorTests t;
  t.runTest();
}

} // namespace
```

Add to `libs/jambot/test/CMakeLists.txt` beside `IntervalPumpTests.cpp`:

```cmake
    ConductorTests.cpp
```

- [ ] **Step 2: Run it and watch it fail to compile**

```bash
cd /home/programming/chalkwalk-jambot
cmake -B build && cmake --build build -j $(nproc)
```

Expected: FAIL -- `../src/Conductor.h: No such file or directory`.

- [ ] **Step 3: Write `Conductor.h`**

```cpp
#pragma once

#include "BotClient.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

// The band's leader, and the only bot that speaks for it.
//
// No instrument, no channel, no audio: it is a name in the room that talks,
// which is the shape `TutorBot` already proved and which costs a server almost
// nothing. What it buys is an AUTHORITY. Four bots agreeing by evaluating the
// same deterministic function is exact for music and wrong for speech, because
// two bots reaching the same conclusion both say it -- so the band had
// rank-stagger arbitration, and arbitration is what you build when nobody is in
// charge.
//
// One per band, always. An optional conductor would mean keeping both
// mechanisms alive and testing the fallback nobody exercises, which is how the
// roster race survived on Linux until macOS found it.
//
// Designed in `docs/superpowers/specs/2026-08-31-conductor-design.md` in
// Antiphon.
class Conductor : protected BotClient::Listener {
public:
  Conductor(std::string username, std::unique_ptr<BotClient::Client> client);
  ~Conductor() override;

  Conductor(const Conductor &) = delete;
  Conductor &operator=(const Conductor &) = delete;

  // Who the band is playing with. The conductor is the bot that cares whether
  // the owner is present, so this lives here rather than on the tutor.
  void setOwner(std::string ownerUsername);
  std::string owner() const;

  bool join(const std::string &host, int port, double rate);

  // Leaves. Idempotent -- a conductor must be as easy to get rid of as any
  // other bot.
  void part();

  bool isActive() const { return active.load(); }
  const std::string &name() const { return username; }

protected:
  BotClient::Client *client() { return netClient.get(); }
  double sampleRate() const;

private:
  std::string username;
  std::unique_ptr<BotClient::Client> netClient;

  mutable std::mutex stateMutex;
  std::string ownerName;
  double rate = 0.0;

  std::atomic<bool> active{false};
};
```

- [ ] **Step 4: Write `Conductor.cpp`**

```cpp
#include "Conductor.h"

Conductor::Conductor(std::string user,
                     std::unique_ptr<BotClient::Client> c)
    : username(std::move(user)), netClient(std::move(c)) {
  if (netClient)
    netClient->addListener(this);
}

Conductor::~Conductor() {
  part();
  if (netClient)
    netClient->removeListener(this);
}

void Conductor::setOwner(std::string ownerUsername) {
  std::lock_guard<std::mutex> sl(stateMutex);
  ownerName = std::move(ownerUsername);
}

std::string Conductor::owner() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return ownerName;
}

double Conductor::sampleRate() const {
  std::lock_guard<std::mutex> sl(stateMutex);
  return rate;
}

bool Conductor::join(const std::string &host, int port, double r) {
  if (!netClient)
    return false;

  netClient->setSampleRate(r);

  // No channel at all: the conductor sends nothing and is not a strip in
  // anybody's mixer.
  netClient->setChannels({});

  // Subscribed, because a conductor that cannot hear the room cannot lead it.
  netClient->setDefaultRecvEnabled(true);

  netClient->connect(host, port, username, "");
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    rate = r;
  }
  active = true;
  return true;
}

void Conductor::part() {
  if (!active.exchange(false))
    return;
  if (netClient)
    netClient->disconnect();
}
```

- [ ] **Step 5: Run the test**

```bash
cd /home/programming/chalkwalk-jambot
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure -R chalkwalk_jambot
```

Expected: PASS, all suites.

- [ ] **Step 6: Prove the channel assertion has teeth**

Temporarily change `netClient->setChannels({})` to `netClient->setChannels({"lead"})` and rerun. Expected: the "declared none" expectation FAILS. Revert.

This matters because the instrument-less property is the whole reason the conductor is cheap, and a test that passes either way would not defend it.

- [ ] **Step 7: Commit**

```bash
git add src/Conductor.h src/Conductor.cpp test/ConductorTests.cpp test/CMakeLists.txt
git commit -m "Give the band a conductor, which so far only arrives and leaves."
```

---

### Task 3: The conductor speaks

**Files:**
- Modify: `libs/jambot/src/Conductor.h`, `libs/jambot/src/Conductor.cpp`
- Modify: `libs/jambot/test/ConductorTests.cpp`

**Interfaces:**
- Consumes: Task 2's `Conductor`.
- Produces: `void Conductor::say(const std::string &text)` -- sends a room chat line, a no-op when not active.

- [ ] **Step 1: Write the failing test**

Add to `ConductorTests::runTest`:

```cpp
    beginTest("says what it is told to say");
    {
      Rig rig;
      rig.conductor->say("The Understudies: Mirn (kit), Vell (bass).");
      expect(rig.client->said.size() == 1);
      expect(rig.client->said.front() ==
             "The Understudies: Mirn (kit), Vell (bass).");
    }

    beginTest("a parted conductor says nothing");
    {
      Rig rig;
      rig.conductor->part();
      rig.conductor->say("anybody there?");
      expect(rig.client->said.empty(),
             "speech after parting is a line nobody can answer");
    }
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build -j $(nproc)
```

Expected: FAIL -- `'class Conductor' has no member named 'say'`.

- [ ] **Step 3: Implement**

In `Conductor.h`, public:

```cpp
  // Speaks for the band. A no-op once parted, so a queued line cannot arrive
  // after the conductor has gone.
  void say(const std::string &text);
```

In `Conductor.cpp`:

```cpp
void Conductor::say(const std::string &text) {
  if (!active.load() || !netClient)
    return;
  netClient->sendChat(text);
}
```

`sendChat` is verified against `src/BotClient.h:173`:
`virtual void sendChat(const std::string &text) = 0;`. There is also
`sendPrivate(to, text)` at :174, which this plan does not use -- the conductor
speaks to the room.

- [ ] **Step 4: Run the test**

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure -R chalkwalk_jambot
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/Conductor.h src/Conductor.cpp test/ConductorTests.cpp
git commit -m "Let the conductor speak, and go quiet once it has left."
```

---

### Task 4: TutorBot becomes a Conductor

The behaviour change the spec accepts: the tutor's teaching ends, but the bot does not leave. `part()` stays available -- a conductor can still be shut down -- but the teaching thread reaching `Done` must no longer call it.

**Files:**
- Modify: `libs/jambot/src/TutorBot.h`, `libs/jambot/src/TutorBot.cpp`
- Modify: `libs/jambot/test/TutorBotTests.cpp`

**Interfaces:**
- Consumes: `Conductor` (Tasks 2-3).
- Produces: `class TutorBot : public Conductor`, keeping `Step`, `step()`, `kIntervalsToAgree`, and losing `join`, `part`, `setOwner`, `isActive`, `name`, `username`, `client`, `owner`, `sampleRate` -- all now the base's.

- [ ] **Step 1: Change the failing expectation first**

`TutorBotTests.cpp` currently asserts the tutor leaves. Find it:

```bash
cd /home/programming/chalkwalk-jambot
grep -n 'isActive' test/TutorBotTests.cpp
```

The existing case `expect(!rig.tutor->isActive(), "the tutor never left")` inverts. Replace it with:

```cpp
    beginTest("teaching ends, but the conductor stays");
    {
      Rig rig;
      // ... drive the thread to Done exactly as the existing case does ...
      expect(rig.tutor->step() == TutorBot::Step::Done);
      expect(rig.tutor->isActive(),
             "a conductor cannot part -- the band always has one. Section 7's "
             "'six lines and it parts' is now 'its teaching ends'.");
    }
```

**Copy the drive-to-Done sequence from the existing case verbatim.** Do not re-derive it; the sequence is six specific interactions and getting one wrong makes the test assert nothing.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure -R chalkwalk_jambot
```

Expected: FAIL -- the tutor is inactive, because it still parts at `Done`.

- [ ] **Step 3: Stop the thread parting**

In `TutorBot.cpp`, find where `Done` is reached and remove the `part()` call. Leave the sign-off line: the tutorial still announces that it is finished.

- [ ] **Step 4: Run the test**

Expected: PASS.

- [ ] **Step 5: Commit the behaviour change on its own**

```bash
git add src/TutorBot.cpp test/TutorBotTests.cpp
git commit -m "Let the teaching end without the teacher leaving."
```

- [ ] **Step 6: Now reparent the class**

`TutorBot.h`: `class TutorBot : public Conductor`. Delete the members and methods the base owns (`username`, `client`, `owner`, `sampleRate`, `active`, `join`, `part`, `setOwner`, `isActive`, `name`). Keep `Step`, `step()`, `kIntervalsToAgree`, `advance`, `noteDiagnostic`, `lastReading`, `agreeingIntervals`, `saidDiagnostic`, `stateMutex` if still needed for those.

Constructor forwards:

```cpp
  TutorBot(std::string username, std::unique_ptr<BotClient::Client> client)
      : Conductor(std::move(username), std::move(client)) {}
```

Replace every `client->sendChat(...)` with `say(...)`, and every `client->` with `client()->`.

- [ ] **Step 7: Run the whole suite**

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Expected: PASS, all 5 suites. The existing tutor tests passing unchanged is the gate -- the reparent must not alter behaviour.

- [ ] **Step 8: Check the budget test still means something**

```bash
grep -n 'at most\|budget\|ten' test/TutorBotTests.cpp | head
```

The suite asserts a hundred events produce at most ten lines. The conductor now speaks too, so confirm whether that assertion is counting TEACHING lines or ALL lines. If all, it must be narrowed to teaching lines -- the conductor legitimately speaks for the band forever, and a budget that counts its speech will fail for the wrong reason once Task 5 lands. Fix it now and say so in the commit.

- [ ] **Step 9: Commit**

```bash
git add src/TutorBot.h src/TutorBot.cpp test/TutorBotTests.cpp
git commit -m "Make the tutor a conductor that teaches, not a bot that leaves."
```

---

### Task 5: The room always has one

**Files:**
- Modify: `src/PracticeRoom.h`, `src/PracticeRoom.cpp` (Antiphon)
- Modify: `test/PracticeRoomTests.cpp`

**Interfaces:**
- Consumes: `Conductor` from jambot.
- Produces: `PracticeRoom` holds `std::unique_ptr<Conductor> conductor;` created in `start()` and destroyed in `stop()`. `botCount()` and `botNames()` continue to report PLAYERS only.

- [ ] **Step 1: Point Antiphon at the jambot working tree**

```bash
cd /home/programming/antiphon
cmake -B build-conductor -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
```

Confirm configure prints `chalkwalk-jambot: OVERRIDE at ...`.

- [ ] **Step 2: Write the failing test**

In `test/PracticeRoomTests.cpp`, inside the existing room fixture:

```cpp
    beginTest("a room always has a conductor, and it is not a player");
    {
      PracticeRoom room;
      PracticeRoom::Config cfg;
      cfg.bandSize = 4;
      expect(room.start(cfg));

      expect(room.botCount() == 4,
             "the conductor is not counted as a player -- it has no voice, no "
             "pump slice and no share of the mix");
      expect(room.hasConductor(), "and yet it is there");

      room.stop();
    }
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build-conductor -j $(nproc) 2>&1 | grep -i 'error' | head
```

Expected: FAIL -- `'class PracticeRoom' has no member named 'hasConductor'`.

- [ ] **Step 4: Implement**

`PracticeRoom.h`, alongside the existing tutor member (look at how `withTutor` builds one and follow it exactly -- same client type, same naming path through `BotNames`):

```cpp
  // Outside `bots` on purpose, for the same reason the tutor is: it has no
  // voice, so it takes no pump slice and no share of the mix. It is not a
  // player and `botCount` must not say it is.
  bool hasConductor() const;

private:
  std::unique_ptr<Conductor> conductor;
```

In `start()`, create and join it after the server is listening and before or beside the players. In `stop()`, `conductor.reset()`.

**Name it through `BotNames` against the same `taken` list the players use**, so the conductor cannot share a name with a player or the owner.

- [ ] **Step 5: Run the test**

```bash
cmake --build build-conductor -j $(nproc) && \
  ctest --test-dir build-conductor --output-on-failure -R ninjam-unit-tests
```

Expected: PASS.

- [ ] **Step 6: Run the whole Antiphon suite**

```bash
ctest --test-dir build-conductor --output-on-failure
```

Expected: PASS, 8/8. Watch particularly for `PracticeRoomTests` cases that assert a client count or a room membership size -- a new name in the room changes those numbers, and any that fail are telling you something real rather than being noise.

- [ ] **Step 7: Bump the submodule and re-verify with NO override**

```bash
cd /home/programming/chalkwalk-jambot
git log --oneline -1                       # the SHA to pin
cd /home/programming/antiphon/libs/jambot
git fetch origin && git checkout <sha>
cd /home/programming/antiphon
rm -rf build-verify
cmake -B build-verify -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
cmake --build build-verify -j $(nproc)
ctest --test-dir build-verify --output-on-failure
```

Expected: configure prints NO `OVERRIDE` line, and 8/8 pass. **An override build does not count as done** -- the submodule SHA must describe what was verified.

Push jambot before bumping, or the SHA will not exist for anyone else:

```bash
cd /home/programming/chalkwalk-jambot
git push https://github.com/chalkwalk/chalkwalk-jambot.git main:main
```

- [ ] **Step 8: Commit**

```bash
cd /home/programming/antiphon
git add src/PracticeRoom.h src/PracticeRoom.cpp test/PracticeRoomTests.cpp libs/jambot
git commit -m "Put a conductor in every room, and keep it out of the player count."
```

---

## Not in this plan

Deliberately, so nobody starts them here:

- **Plural speech migration.** `PracticeBot::speakDelayMs` and `rankAmong` stay exactly as they are. Deleting them is the next plan, and it is the one that pays off the race.
- **Membership.** The conductor does not ask for players yet; `PracticeRoom::addPlayer` still has no caller outside tests.
- **Intent commands.** No `play from N`, no reject-if-past rule.
- **Voting.** Deferred pending measurement of the server's vote threshold -- see the spec. The existing position (`BotAnswer.h:105`, a bot never votes) is unchanged by this plan.
- **`BOT-CHAT.md` updates** (16.9's tutor-recruits, section 7's it-parts, 16.10's withdrawn CPU figures). They belong with the plan that makes them true.
