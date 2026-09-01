# Membership From Chat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let somebody ask for another player in chat, and have the band actually grow -- closing the item that has read "nothing asks for a player yet" since `addPlayer` was written.

**Architecture:** A new `Intent::AddPlayer` in `BotLanguage`, which stays the one home for what words mean. The conductor gets a thin response layer of its own rather than going through `BotChat`, which is shaped entirely around band members. The conductor asks the room through a callback, so chalkwalk-jambot never learns what a `PracticeRoom` is; the cap stays inside `addPlayer`, and a refusal is a line rather than a silent false.

**Tech Stack:** C++17, chalkwalk-jambot (JUCE-free, Catch2 via `test/JuceUnitShim.h`), Antiphon (`PracticeRoom`).

**Spec:** `docs/superpowers/specs/2026-08-31-conductor-design.md`

## Global Constraints

- **ASCII only in source files.** `--` for an em dash, `->` for arrows.
- **2-space indent, braces on same line, members lowerCamelCase.** C++17.
- **chalkwalk-jambot is JUCE-free** and must not learn about Antiphon types.
- **No comments except for non-obvious invariants.** A comment explains *why*.
- **The corpus is the specification.** A new phrasing goes in `test/fixtures/bot-phrases.txt` FIRST, the test goes red, then the lexicon widens. If widening takes more than a word or two, that is the signal the design over-reached.
- **clang-format 20.1.8 is CI-enforced in Antiphon only** (jambot has no `.clang-format`; do not run it there). Use `/tmp/claude-1002/-home-programming-antiphon/67ee3feb-9a95-4950-bebd-fde1300ad5ab/scratchpad/cfvenv/bin/clang-format`.
- **Iterate with `-DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot`; bump and re-verify with NO override before done.** Antiphon needs `-DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE`.

## What is NOT in this plan

- **Asking for a specific instrument or role.** `add a bass player` is refused as a phrasing, not implemented: the band can only voice four roles until *A band of more than four*, so a specific request would mostly say no.
- **Removing a player.** `BOT-CHAT.md` 16.10 lists what a removal does to the roles below it as unexamined, and this plan must not settle that by accident.
- **Command confirmations** and their surviving rank stagger. Untouched.

## File Structure

| File | Responsibility |
|---|---|
| `libs/jambot/test/fixtures/bot-phrases.txt` | The new phrasings, as the specification. |
| `libs/jambot/src/BotLanguage.h` / `.cpp` | `Intent::AddPlayer` and the words that reach it. |
| `libs/jambot/src/Conductor.h` / `.cpp` | Hears chat, asks for a player, says what happened. |
| `libs/jambot/test/ConductorTests.cpp` | That it asks, and what it says when refused. |
| `src/PracticeRoom.cpp` (Antiphon) | Supplies the callback; `addPlayer` unchanged. |
| `test/PracticeRoomTests.cpp` (Antiphon) | End to end: type it, and the band is five. |

---

### Task 1: The words

**Files:**
- Modify: `libs/jambot/test/fixtures/bot-phrases.txt`
- Modify: `libs/jambot/src/BotLanguage.h`, `libs/jambot/src/BotLanguage.cpp`

**Interfaces:**
- Produces: `BotLanguage::Intent::AddPlayer`.

- [ ] **Step 1: Add the phrasings to the corpus, which is the specification**

Append to `test/fixtures/bot-phrases.txt`:

```
[ADD_PLAYER]
add a player
add another player
add another
bring in another player
bring another player in
bring someone else in
we need another player
can we have another player
one more player
one more
another player please
get another player
make it a five piece
```

- [ ] **Step 2: Run the corpus test and watch the miss rate rise**

```bash
cd /home/programming/chalkwalk-jambot
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j $(nproc)
ctest --test-dir build -R chalkwalk_jambot --output-on-failure 2>&1 | grep -i 'miss\|ADD_PLAYER\|FAILED'
```

Expected: FAIL. The tag `ADD_PLAYER` has no `Intent`, so the fixture parser will
reject it or every line will miss. **Read which**, because it changes Step 3: a
rejected tag means the enum mapping needs the entry first, a miss means only the
lexicon does.

- [ ] **Step 3: Add the intent**

In `BotLanguage.h`, in `enum class Intent`, after `StartPlaying`:

```cpp
  // Asked for the band to GROW. Not a request to any one bot -- it is one
  // answer about the room, so the conductor owns it and the players ignore it.
  AddPlayer,
```

In `BotLanguage.cpp`, find the tag-to-intent table the fixture parser uses
(`grep -n 'START_PLAYING' src/BotLanguage.cpp`) and add `ADD_PLAYER` beside it,
mapping to `Intent::AddPlayer`.

- [ ] **Step 4: Widen the lexicon until the corpus passes**

The concepts likely needed are a "more" idea (`another`, `one more`, `else`)
against the existing `Instrument`/`Identity` vocabulary for "player". Add the
smallest set of words that resolves the corpus lines.

**If this takes more than a word or two, stop and report it** -- the corpus
header says that is the signal the design over-reached, not that the corpus is
short.

- [ ] **Step 5: Run the corpus test**

```bash
ctest --test-dir build -R chalkwalk_jambot --output-on-failure 2>&1 | tail -5
```

Expected: PASS, and the reported miss rate no higher than before this task.
**Quote the rate in the commit message** -- it is the number this file exists to
produce.

- [ ] **Step 6: Prove it does not over-match**

Add to `test/BotLanguageTests.cpp`, in the existing negative-case group (find it
with `grep -n 'not a' test/BotLanguageTests.cpp`):

```cpp
    // "another" is not always a recruit. These are about the MUSIC.
    expect(read("play another one").intent != Intent::AddPlayer);
    expect(read("give me another chord").intent != Intent::AddPlayer);
    expect(read("another key please").intent != Intent::AddPlayer);
```

If any of these now resolve to `AddPlayer`, the lexicon is too wide -- narrow it
rather than deleting the assertion.

- [ ] **Step 7: Commit**

```bash
git add test/fixtures/bot-phrases.txt src/BotLanguage.h src/BotLanguage.cpp test/BotLanguageTests.cpp
git commit -m "Understand a request for another player."
```

---

### Task 2: The conductor asks, and says what happened

**Files:**
- Modify: `libs/jambot/src/Conductor.h`, `libs/jambot/src/Conductor.cpp`
- Modify: `libs/jambot/test/ConductorTests.cpp`

**Interfaces:**
- Consumes: `BotLanguage::Intent::AddPlayer` (Task 1); `BotAddress::classify`; `Conductor::say`.
- Produces:
  - `using Recruit = std::function<bool()>;`
  - `void Conductor::setRecruit(Recruit r);`
  - `void Conductor::onChatMessage(const std::string &type, const std::string &username, const std::string &text) override;`

  `Recruit` returns **true if a player was added** and false if the room refused. The conductor does not know why; the room owns the cap.

- [ ] **Step 1: Write the failing test**

Add to `ConductorTests.cpp`:

```cpp
    beginTest("asked for another player, it asks the room");
    {
      Rig rig;
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->say("you", "band, add a player");

      expect(asked == 1, "the conductor did not ask the room");
      expect(!rig.client->said.empty(), "it said nothing about it");
      expect(rig.client->said.back().find("bring") != std::string::npos ||
                 rig.client->said.back().find("in") != std::string::npos,
             rig.client->said.back());
    }

    beginTest("a refusal is a rule speaking, not silence");
    {
      Rig rig;
      rig.conductor->setRecruit([&] { return false; });
      const auto before = rig.client->said.size();
      rig.client->say("you", "band, add a player");

      expect(rig.client->said.size() == before + 1,
             "the room refused and nobody said so");
      const auto &line = rig.client->said.back();
      expect(line.find("full") != std::string::npos ||
                 line.find("as many") != std::string::npos,
             "the refusal does not say why: " + line);
    }

    beginTest("an unaddressed request is not one");
    {
      Rig rig;
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->say("you", "add a player");

      expect(asked == 0,
             "nobody is addressed by default, and a conductor is not an "
             "exception to that");
    }

    beginTest("it does not recruit on its own say-so");
    {
      Rig rig;
      int asked = 0;
      rig.conductor->setRecruit([&] { ++asked; return true; });
      rig.client->say("Quado[kit-bot]", "band, add a player");

      expect(asked == 0, "a bot asked for a player and the conductor obliged");
    }
```

- [ ] **Step 2: Run it and watch it fail**

Expected: FAIL -- `'class Conductor' has no member named 'setRecruit'`.

- [ ] **Step 3: Implement**

`Conductor.h`, public:

```cpp
  // Asks for one more player. Returns true if one was added.
  //
  // A callback rather than an interface, because the only thing this library
  // needs to know is whether it worked -- the cap, the naming and the ordering
  // all belong to whoever hosts the room, and a conductor that knew about them
  // would be a second place they could be got wrong.
  using Recruit = std::function<bool()>;
  void setRecruit(Recruit r);
```

private:

```cpp
  void onChatMessage(const std::string &type, const std::string &username,
                     const std::string &text) override;

  Recruit recruit;
  BotAddress::Attention attention;
```

`Conductor.cpp`:

```cpp
void Conductor::setRecruit(Recruit r) {
  std::lock_guard<std::mutex> sl(stateMutex);
  recruit = std::move(r);
}

void Conductor::onChatMessage(const std::string &type,
                              const std::string &username,
                              const std::string &text) {
  if (!isActive() || type != "MSG")
    return;

  // A bot asking for a bot is a band that grows on its own say-so, which is a
  // cap nobody set. Only a person may ask.
  if (BotNames::looksLikeBot(username))
    return;

  // The room as this conductor sees it. Only enough to be addressed: the
  // conductor is not matched by role or instrument, having neither.
  BotAddress::Room room;
  for (const auto &m : netClient->members()) {
    BotAddress::Participant p;
    p.username = m.username;
    p.handle = BotNames::handleOf(p.username);
    p.isBot = BotNames::looksLikeBot(p.username);
    room.participants.push_back(p);
  }
  room.resolveHandles();

  BotAddress::Incoming in;
  in.sender = username;
  in.text = text;
  in.isPrivate = false;
  using namespace std::chrono;
  in.at = duration<double>(steady_clock::now().time_since_epoch()).count();

  // `Collective` is the one that matters -- "band, add a player" is addressed
  // to all of us, and growing the band is one answer about the room. `Named`
  // and `Continuation` count too, so `conductor: add a player` and a follow-up
  // inside the attention window both work.
  const auto who = BotAddress::classify(room, botName, in, attention);
  if (who != BotAddress::Address::Collective &&
      who != BotAddress::Address::Named &&
      who != BotAddress::Address::Continuation)
    return;

  const auto rest = BotAddress::withoutAddress(room, botName, text);
  if (BotLanguage::read(rest).intent != BotLanguage::Intent::AddPlayer)
    return;

  Recruit ask;
  {
    std::lock_guard<std::mutex> sl(stateMutex);
    ask = recruit;
  }
  if (!ask)
    return;

  if (ask())
    say("bringing somebody else in.");
  else
    say("that is as many of us as this room takes.");
}
```

The signatures above are verified against `src/BotAddress.h`:
`Address classify(const Room&, const std::string &me, const Incoming&, Attention&)`
returns an **enum**, not a struct with an `addressed` flag, and the address is
stripped separately by `withoutAddress(room, self, text)`. `Room` holds
`participants` and wants `resolveHandles()` called after filling it -- there is
no `me`/`myHandle` on it. `BotLanguage::read` is spelled as `PracticeBot.cpp`
spells it; check that one call there before writing it.

`Conductor.cpp` needs `#include <chrono>` and `#include "BotAddress.h"` and
`#include "BotLanguage.h"` for this.

- [ ] **Step 4: Run the tests**

```bash
cmake --build build -j $(nproc) && ctest --test-dir build --output-on-failure
```

Expected: PASS, all suites.

- [ ] **Step 5: Prove the bot guard has teeth**

Remove the `looksLikeBot(username)` early return and rerun. Expected: "a bot
asked for a player and the conductor obliged" FAILS. Restore it.

This matters more than it looks: the band's own chat is the loudest thing in a
practice room, and a conductor that recruited on a bot's line would grow the
band without anybody asking.

- [ ] **Step 6: Commit**

```bash
git add src/Conductor.h src/Conductor.cpp test/ConductorTests.cpp
git commit -m "Let the conductor ask the room for another player."
```

---

### Task 3: The room answers, and the band grows

**Files:**
- Modify: `src/PracticeRoom.cpp` (Antiphon)
- Modify: `test/PracticeRoomTests.cpp` (Antiphon)

**Interfaces:**
- Consumes: `Conductor::setRecruit(std::function<bool()>)` (Task 2); `PracticeRoom::addPlayer()`, unchanged.
- Produces: a room where typing `band, add a player` makes `botCount()` grow.

- [ ] **Step 1: Wire the callback**

In `PracticeRoom::start`, beside `conductor->setBandName(...)`:

```cpp
  // The cap is NOT checked here. `addPlayer` owns it, and owning it in one
  // place is the whole reason it lives there -- a second check would be a
  // second thing to get wrong (PracticeRoom.h, `maxBandSize`).
  conductor->setRecruit([this] { return addPlayer(); });
```

**Check the lifetime.** `conductor` is destroyed in `stop()` before `bots`, and
the lambda captures `this`, so it is valid for as long as the conductor is. If
`stop()` ever changes order, this is the thing that breaks -- say so in a
comment there rather than relying on the reader noticing.

- [ ] **Step 2: Write the end-to-end test**

In `test/PracticeRoomTests.cpp`:

```cpp
    beginTest("asking in chat brings a player in");
    {
      // The whole point of the item this closes: growth was reachable from
      // code and not from a room.
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bandSize = 3;
      expect(room.start(cfg));
      expectEquals(room.botCount(), 3);

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you), "the band never introduced itself");

      you.client.sendChatMessage("band, add a player");
      expect(waitUntil([&] { return room.botCount() == 4; }, 8000),
             "asking for a player did not bring one in");
    }

    beginTest("the room says no when it is full, and says why");
    {
      PracticeRoom room;
      auto cfg = testConfig("you");
      cfg.bandSize = PracticeRoom::maxBandSize(true);
      expect(room.start(cfg));
      const int full = room.botCount();

      Joiner you;
      expect(you.join(room, "you"));
      expect(waitForRoster(you), "the band never introduced itself");

      const int before = you.snapshot().size();
      you.client.sendChatMessage("band, add a player");
      juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);

      expectEquals(room.botCount(), full,
                   "a full room grew anyway");

      bool refused = false;
      const auto lines = you.snapshot();
      for (int i = before; i < lines.size(); ++i)
        if (lines[i].containsIgnoreCase("as many") ||
            lines[i].containsIgnoreCase("full"))
          refused = true;
      expect(refused,
             "the room refused silently, which reads as a broken command");
    }
```

- [ ] **Step 3: Build and run**

```bash
cd /home/programming/antiphon
cmake -B build-membership -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JAMBOT_DIR=/home/programming/chalkwalk-jambot \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
cmake --build build-membership -j $(nproc)
./build-membership/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests PracticeRoom 2>&1 | grep '!!! Test'
```

Expected: no output.

- [ ] **Step 4: Announce the newcomer, or decide not to**

`ROADMAP.md` carries *A player brought in mid-session is not announced* against
*A band of more than four*. This plan is where somebody is added, so it is where
that becomes visible: type the command and nothing says who arrived.

Decide and do one of two things, and say which in the commit:

- the conductor names the newcomer after a successful recruit -- one line, and
  the roadmap item is ticked; or
- it does not, and the roadmap item stays open with a note that the command
  landed without it.

**Do not leave it undecided.** The whole item exists because the announcement
went missing when the roster moved.

- [ ] **Step 5: Run the full suite, ten times for the room**

```bash
ctest --test-dir build-membership --output-on-failure
for i in $(seq 1 10); do
  n=$(./build-membership/test/NinjamTests_artefacts/RelWithDebInfo/NinjamTests PracticeRoom 2>&1 | grep -c '!!! Test')
  echo "run $i: $n failures"
done
```

Expected: 8/8, and ten clean runs. **Run these on a quiet machine** -- a
concurrent build has already produced one spurious failure in this suite, and a
result taken under load is not evidence either way.

- [ ] **Step 6: Format, bump, and re-verify with NO override**

```bash
CF=/tmp/claude-1002/-home-programming-antiphon/67ee3feb-9a95-4950-bebd-fde1300ad5ab/scratchpad/cfvenv/bin/clang-format
$CF --dry-run -Werror src/*.cpp src/*.h test/*.cpp test/*.h tools/*.cpp

cd /home/programming/chalkwalk-jambot && git push origin main && git rev-parse --short HEAD
cd /home/programming/antiphon/libs/jambot && git fetch origin && git checkout <sha>
cd /home/programming/antiphon
rm -rf build-verify && cmake -B build-verify -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCHALKWALK_JUCE_DIR=/home/programming/.juce/JUCE
cmake --build build-verify -j $(nproc) && ctest --test-dir build-verify --output-on-failure
```

Expected: no `OVERRIDE` line, 8/8.

- [ ] **Step 7: Close the roadmap items**

Two entries under *A band of more than four* are settled by this task and must
be updated together:

- *Nothing asks for a player yet* -- tick it, and name the caller: a person, in
  chat, through the conductor.
- *The TUTOR brings one in* -- its recorded resolution was "the room takes the
  request directly from chat, with no bot in the middle", and that is **not what
  shipped**: the conductor is the bot in the middle, which is what recovers
  16.9's argument that recruiting is an arranging act. Correct it rather than
  leaving two answers in the file.

Also update `libs/jambot/docs/BOT-CHAT.md` 16.9, which still says the tutor
recruits.

```bash
git add src/PracticeRoom.cpp test/PracticeRoomTests.cpp libs/jambot ROADMAP.md
git commit -m "Let somebody ask for another player, and get one."
```

---

## Self-review notes

- Task 1 covers the spec's "the conductor understands the request"; Task 2 the "conductor asks, room enforces" split; Task 3 the end-to-end.
- The cap is checked in exactly one place throughout, which is the property `PracticeRoom.h` argues for.
- The `Recruit` callback returns `bool` in Task 2 and is consumed as `bool` in Task 3.
- Step 4 of Task 3 is a decision point rather than an instruction, deliberately: the announcement question was opened by plan 2 and this is the first plan where it is visible.
