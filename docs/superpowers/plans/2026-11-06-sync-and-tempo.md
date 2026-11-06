# Sync and Tempo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Sync-button flow with "synced to the DAW playhead, or not synced -- and always told which", including Reaper tempo/transport control, jump-safe transmit and playback, one-sentence announcements, a Clock radio group, and the first earcon.

**Architecture:** Two pure, JUCE-free modules carry the logic: `ClockModel.h` (the spec's state table, replacing `SyncState.h` and `RunGate.h`) and `PlayheadGrid.h` (anchor-based playhead position, loop-wrap and jump detection, and the silence-padding arithmetic). Wording lives in a pure `SyncAnnouncements.h`. `PluginProcessor` and `PluginEditor` become thin callers. Reaper access sits behind a small `HostControl` interface. Only the plugin target implements it (`ReaperBridge.cpp`), so the tests use a fake.

**Tech Stack:** C++20 tree / C++17 libraries, JUCE (`juce::UnitTest`), VST3 + CLAP (clap-juce-extensions), CMake.

**Spec:** `docs/superpowers/specs/2026-11-06-sync-and-tempo-design.md`. Read it first. Section names below refer to it.

## Global Constraints

- ASCII only in every source file. Use `--` for an em dash.
- No allocation, lock, file I/O or logging on the audio thread (PRINCIPLES section 7). Reaper calls are made from the message thread only.
- Audio-thread/UI shared state is published through `std::atomic`, the way `streamSlots` and `audioChannels` are.
- A new `src/*.cpp` goes in `src/CMakeLists.txt` AND `test/CMakeLists.txt` (and `tools/CMakeLists.txt` if a tool uses it). `ReaperBridge.cpp` is the exception: plugin target only.
- Never branch on `JucePlugin_Build_Standalone`; use `isStandaloneApp()`.
- Tempo tolerance: `kTempoTolerance = 0.001` BPM.
- Reaper grace period after a room tempo change: 250 ms.
- Uploads are always full interval length.
- Each join starts in Synced to DAW; the Clock choice is not persisted. "Follow room tempo in Reaper" (default on) and the cue's enable and level are persisted in plugin state.
- Shortcuts: Ctrl+Alt+S Sync now, Ctrl+Alt+I Say status, Ctrl+Alt+Enter Antiphon play, Ctrl+Alt+F toggle Clock. None uses Space.
- `libs/ninjam` is a submodule. Change it in `/home/programming/chalkwalk-ninjam` with `-DCHALKWALK_NINJAM_DIR=...`, then bump the submodule. **Pushing chalkwalk-ninjam to GitHub needs the user's OK.**
- Build in `build-$USER/` with `-j4`, and check `/home/programming/tools/machine-load -q` before heavy work.
- Before claiming done: `cmake --build build-$USER -j4 && ctest --test-dir build-$USER --output-on-failure`, quoting the output.

## Review Focus

1. **Tempo automation or a tempo ramp in the DAW** while synced. The spec says "exact" tempo, but a ramp passes through the room tempo for an instant. Expect: synced only while the tempo is within tolerance, with no announcement flapping. Test in Task 1 (hysteresis: the state change is announced only after it has held 500 ms).
2. **The host reports a position but not a tempo, or a tempo of 0.** Expect: Not synced: no position from DAW, never a divide-by-zero. Test in Task 1.
3. **The DAW plays while a jump lands exactly on an interval boundary** (resume at bar 1). Expect: one interval close, not two, and no zero-length interval. Test in Task 2.
4. **A sample-rate or block-size change mid-jam** (an `prepareToPlay` re-call) while on Antiphon's clock. Expect: the position survives in beats, not in samples. Test in Task 4 (seek by phase in beats after prepare).
5. **Reaper with "follow room tempo" off and a mismatched tempo.** Expect: the other-DAW sentences, and Sync now gives the instruction rather than setting the tempo. Test in Task 6.

---

### Task 1: ClockModel -- the state table

**Files:**
- Create: `src/ClockModel.h`
- Create: `test/ClockModelTests.cpp`
- Modify: `test/CMakeLists.txt` (add the test file)

**Interfaces:**
- Produces:
  ```cpp
  namespace ClockModel {
  inline constexpr double kTempoTolerance = 0.001;
  inline constexpr int kAnnounceHoldMs = 500;
  enum class Mode { SyncedToDaw, AntiphonClock };
  enum class State { Disconnected, Synced, NotSyncedTempo, NotSyncedDawStopped,
                     NotSyncedNoPosition, AntiphonClock, Stopped };
  enum class GridSource { None, Playhead, OwnClock, HostOffline, StandaloneOwn };
  struct Inputs {
    bool connected = false; bool standalone = false; Mode mode = Mode::SyncedToDaw;
    bool antiphonPlaying = false; bool dawPlaying = false; bool dawHasPosition = false;
    double dawBpm = 0.0; int roomBpm = 0; bool inTempoGrace = false;
  };
  struct Outputs { State state; GridSource grid; bool inJam; bool jumpToPlayhead; };
  bool tempoMatches(double dawBpm, int roomBpm);
  class Machine { public: Outputs update(const Inputs &); State get() const; void reset(); };
  const char *stateName(State);   // for logs and tests only, not user copy
  }
  ```
- `jumpToPlayhead` is true on exactly the update that enters `Synced` from any other state.

- [ ] **Step 1: Write the failing tests** in `test/ClockModelTests.cpp`, as `juce::UnitTest("ClockModel", "ClockModel")`, one `beginTest` for each row of the spec's "whole state" table. Assert `state`, `grid`, `inJam`:
  - Synced row: `{connected, SyncedToDaw, dawPlaying, dawHasPosition, dawBpm=110.0, roomBpm=110}` gives `Synced, Playhead, inJam=true`.
  - `dawBpm=110.3` gives `NotSyncedTempo, OwnClock, inJam=true`.
  - `dawPlaying=false` gives `NotSyncedDawStopped, OwnClock, true`.
  - `dawHasPosition=false` gives `NotSyncedNoPosition, OwnClock, true`.
  - `dawBpm=0.0` gives `NotSyncedNoPosition` (Review Focus 2).
  - `AntiphonClock`, play on, any DAW: `AntiphonClock, OwnClock, true`.
  - `AntiphonClock`, play off, DAW playing at a matching tempo: `Stopped, None, false`.
  - Disconnected in a host: `Disconnected`, with grid `HostOffline` when the DAW is playing with a position, `None` otherwise; `inJam=false`.
  - Standalone: grid `StandaloneOwn` when `antiphonPlaying`; `inJam = connected && antiphonPlaying`.
  - `tempoMatches(110.0009, 110)` is true; `tempoMatches(110.002, 110)` is false.
  - `inTempoGrace=true` with a mismatched tempo keeps `Synced` if the previous state was Synced.
  - `jumpToPlayhead` is true once on entering Synced (from NotSyncedDawStopped, NotSyncedTempo, AntiphonClock), false on the next identical update, and false on leaving Synced.
  - Invariant sweep over every input combination: `inJam` implies `connected`, and `grid == None` implies `!inJam`.
- [ ] **Step 2: Run, and verify it fails to compile** (no `ClockModel.h`): `cmake --build build-$USER -j4 --target NinjamTests`.
- [ ] **Step 3: Implement `src/ClockModel.h`.** Header-only and JUCE-free. Open it with a comment carrying the spec's rule sentence and its table. `kAnnounceHoldMs` is used by the editor (Task 6), not here.
- [ ] **Step 4: Run** `./build-$USER/test/NinjamTests_artefacts/NinjamTests ClockModel`. Expected: all pass.
- [ ] **Step 5: Commit**: "Model the clock as synced, or not, and nothing between".

### Task 2: PlayheadGrid -- anchor, loop wrap, jumps, padding

**Files:**
- Create: `src/PlayheadGrid.h`
- Create: `test/PlayheadGridTests.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `HostGrid::phaseBeatsAt`, `HostGrid::advance` (unchanged).
- Produces:
  ```cpp
  namespace PlayheadGrid {
  enum class Move { Continuous, LoopWrap, Jump };
  // prevEndPpq: where the previous block ended; ppqStart: where this one starts.
  Move classify(double prevEndPpq, double ppqStart, bool looping,
                double loopStartPpq, double loopEndPpq);
  class Position {
  public:
    void reset();                                  // anchor 0, loop offset 0
    void onBpiChangeAt(double ppqBoundary);        // anchor := boundary - loopOffset
    Move observe(double ppqStart, double prevEndPpq, bool looping, double ls, double le);
    double musicalPpq(double ppq) const;           // ppq - anchor + loopOffset
  };
  struct JumpFill { int padBeforeClose; bool closeInterval; int padAfter; };
  // captured: samples already captured in this interval; target: the phase, in
  // samples, the playhead now says; len: interval length in samples.
  JumpFill fillForJump(int captured, int target, int len);
  }
  ```
- `observe` applies `LoopWrap` by adding `(loopEnd - loopStart)` to the loop offset. It applies `Jump` by resetting the loop offset to 0. `musicalPpq` is what `HostGrid::phaseBeatsAt`/`advance` are then fed.

- [ ] **Step 1: Write the failing tests:**
  - `classify(4.0, 4.0, ...)` gives `Continuous`; tolerance `1e-6` beats.
  - Looping 0..8 with prevEnd 8.0 and start 0.0 gives `LoopWrap`.
  - Looping, with start 3.0 from prevEnd 6.0, gives `Jump`.
  - Not looping, any backward move or a forward move over 1e-6, gives `Jump`.
  - Four wraps of an 8-beat loop with BPI 16: `phaseBeatsAt(musicalPpq(ppq), 16)` runs 0..16 continuously (8 after the first wrap, 0 after the second).
  - A jump after wraps lands at `ppq mod 16` again (offset reset).
  - `onBpiChangeAt(32.0)` then BPI 12: position at ppq 32 is 0, and at 44 is 0 again.
  - `fillForJump(1000, 5000, 10000)` gives `{4000, false, 0}` (forward).
  - `fillForJump(7000, 2000, 10000)` gives `{3000, true, 2000}` (backward).
  - `fillForJump(10000, 0, 10000)` gives `{0, true, 0}`. `fillForJump(0, 0, 10000)` gives `{0, false, 0}`: landing exactly on a boundary closes once, not twice (Review Focus 3).
- [ ] **Step 2: Run, and verify it fails.**
- [ ] **Step 3: Implement `src/PlayheadGrid.h`.** Header-only and JUCE-free. Its comment says why a loop wrap is continuous time and every other backward move is a jump (the spec's "DAW loops" section).
- [ ] **Step 4: Run** `NinjamTests PlayheadGrid`. Expected: pass.
- [ ] **Step 5: Commit**: "Derive the position from the playhead, through loops and BPI changes".

### Task 3: IntervalClock can resume mid-interval (chalkwalk-ninjam)

**Files (in `/home/programming/chalkwalk-ninjam`):**
- Modify: `include/chalkwalk/ninjam/IntervalClock.h`, `src/IntervalClock.cpp`
- Test: that library's IntervalClock test file (find it with `grep -rl IntervalClock test/`)

**Interfaces:**
- Produces: `void IntervalClock::seekToBeats(double phaseBeats);` This is the position within the current interval. The next `advance` emits events only for beats strictly after it, and does not emit `IntervalStart` unless `phaseBeats == 0`. Pending tempo/BPI changes still apply at the next interval start.

- [ ] **Step 1: Write the failing tests.** Run them at 44.1 and 48 kHz, and at block sizes 61 and 512:
  - After `seekToBeats(4.5)` at BPI 16, the next events are beats 5, 6, ... at sample offsets equal to `beatStartSample(b) - (position)`. Then `IntervalStart` arrives at `samplesPerInterval() - position` cumulative.
  - `phaseBeats()` immediately after the seek is 4.5 within one sample.
  - `seekToBeats(0.0)` behaves like `reset()`.
- [ ] **Step 2: Run its suite, and verify it fails.**
- [ ] **Step 3: Implement.** Convert beats to a sample position with the same rounding `beatStartSample` uses, then set the cursor and `nextBeat`. It stays C++17.
- [ ] **Step 4: Run** its suite. Then reconfigure Antiphon with `-DCHALKWALK_NINJAM_DIR=/home/programming/chalkwalk-ninjam` and run Antiphon's ctest. Expected: pass.
- [ ] **Step 5: Commit** in chalkwalk-ninjam: "Let the clock resume mid-interval". **Ask the user before pushing.** After the push, bump `libs/ninjam` in Antiphon, reconfigure without the override, and commit: "Bump chalkwalk-ninjam: the clock can resume mid-interval".

### Task 4: Processor -- drive the grid from ClockModel, with jumps handled

**Files:**
- Modify: `src/PluginProcessor.h`, `src/PluginProcessor.cpp` (processBlock sync section ~585-745, `captureInputRange` ~272)
- Modify: `src/NinjamClient.h`, `src/NinjamClient.cpp`
- Delete: `src/SyncState.h`, `src/RunGate.h`, `test/SyncStateTests.cpp`, `test/RunGateTests.cpp`. Move their surviving safety invariants (`!(inJam && echoOn)`) into `ClockModelTests`.
- Modify: `test/CMakeLists.txt`, `test/AudioLoopbackTests.cpp`, `test/LoopbackTests.cpp`

**Interfaces:**
- Consumes: Task 1 `ClockModel::Machine`, Task 2 `PlayheadGrid::Position`/`fillForJump`, Task 3 `seekToBeats`.
- Produces (processor, public, for the editor):
  ```cpp
  std::atomic<int> clockMode{0};              // ClockModel::Mode
  std::atomic<int> publishedClockState{0};    // ClockModel::State
  std::atomic<bool> syncNowRequested{false};  // editor -> message-thread handler (Task 6)
  std::atomic<int> gridTempoChangeCount{0};   // bumped when the grid applies a new BPM
  std::atomic<juce::uint32> roomTempoChangedMs{0};
  double hostBpm; bool hostHasPpq; bool hostIsPlaying;   // existing
  bool hostLooping; double hostLoopStartPpq, hostLoopEndPpq; // new, from PositionInfo
  void captureSilence(int count);             // audio thread; zeros into every channel fifo
  ```
  `localTransportPlaying` is reused as "Antiphon play" in the plugin too.
- Produces (NinjamClient): `void seekPlayback(int samplePos);`. Audio thread only. It sets `readPos` of every live slot's `current` to `min(samplePos, current length)`; a slot with no `current` stays silent.

- [ ] **Step 1: Write the failing loopback tests:**
  - `AudioLoopbackTests` "seekPlayback lands mid-interval": enqueue a probe interval with a step at the halfway point, call `seekPlayback(len/2)`, render one block, and assert the RMS is that of the step's second half.
  - `AudioLoopbackTests` "a seek into an interval not yet arrived is silent": RMS below -90 dBFS.
  - `LoopbackTests` "a jump-padded interval still uploads at full length": drive capture as the processor will (captured 7000, backward jump to 2000, len 10000 at 48 kHz, using `fillForJump`), decode what `FakeNinjamServer` received, and assert the decoded length is within the codec delay of 10000 and the first 2000 samples of the next interval are silent.
- [ ] **Step 2: Run, and verify they fail.**
- [ ] **Step 3: Implement `NinjamClient::seekPlayback` and `captureSilence`.** Then rewrite the processBlock sync section as a thin caller:
  - Fill `ClockModel::Inputs`, using `inTempoGrace = now - roomTempoChangedMs < 250`. `roomTempoChangedMs` stays 0 until Task 5 stamps it, so before then there is no grace period.
  - Choose the grid from `GridSource`. `Playhead` feeds `Position::musicalPpq(hostPpq)` to `HostGrid`. `OwnClock` and `StandaloneOwn` use `IntervalClock`. `HostOffline` is today's `useHostGrid` path, unchanged.
  - On leaving `Playhead`, call `intervalClock.seekToBeats(current phase)`, so the own clock continues with no jump.
  - On `jumpToPlayhead`, or a `PlayheadGrid::Move::Jump` while synced: apply `fillForJump`, which means `captureSilence(padBeforeClose)`, then close the interval through the existing `fireCaptureLambdas` + `swapIntervalBuffers` path if `closeInterval`, then `captureSilence(padAfter)`. Then call `ninjamClient.seekPlayback(target)`.
  - On a room BPI change taking effect while synced: `Position::onBpiChangeAt(boundary ppq)`.
  - Compare tempo with `ClockModel::tempoMatches`, not `lround`.
  - Remove `requestSync`/`syncRequested`.
  - Keep `phaseResetPending` and the "joining discards the transmit ring" block.
- [ ] **Step 4: Run the full suite**: `cmake --build build-$USER -j4 && ctest --test-dir build-$USER --output-on-failure`. Expected: all pass, including `no-build-standalone-macro`. Then build the TSan tree and run `NinjamTests`. Expected: zero warnings.
- [ ] **Step 5: Commit**: "Follow the playhead when synced, and keep the jam when not".

### Task 5: The Reaper bridge

**Files:**
- Create: `src/HostControl.h` (pure interface plus policy; in both targets)
- Create: `src/ReaperBridge.h`, `src/ReaperBridge.cpp` (plugin target only)
- Create: `test/HostControlTests.cpp`
- Modify: `src/PluginProcessor.h/.cpp`: inherit `clap_juce_extensions::clap_juce_audio_processor_capabilities`, and override `getVST3ClientExtensions()`
- Modify: `src/CMakeLists.txt`: add `ReaperBridge.cpp`, and link `clap_juce_extensions` to the shared target
- Modify: `THIRDPARTY.md`, `docs/references/SOURCES.md` (where the Reaper signatures and IIDs came from, at which SDK revision)

**Interfaces:**
- Produces:
  ```cpp
  struct HostControl {                    // message thread only
    virtual ~HostControl() = default;
    virtual bool available() const = 0;  // true only inside Reaper with the API found
    virtual bool setTempo(double bpm) = 0;
    virtual bool play() = 0;
    virtual bool isPlaying() const = 0;
    virtual bool setLoopBeats(double startPpq, double endPpq) = 0;
  };
  // Decides what to do; the fake in tests records the calls.
  struct TempoFollow {
    bool enabled = true;                  // persisted as "followRoomTempo"
    // Returns true when it called setTempo.
    bool onJoin(HostControl &, int roomBpm, double dawBpm);
    bool onGridTempoApplied(HostControl &, int roomBpm, double dawBpm);
  };
  std::unique_ptr<HostControl> makeReaperBridge(/* host pointers, see Step 3 */);
  ```
- The processor owns `std::unique_ptr<HostControl> hostControl` (null outside Reaper) and `TempoFollow tempoFollow`. A message-thread timer in the processor watches `gridTempoChangeCount`. It calls `onGridTempoApplied` and stamps `roomTempoChangedMs`.

- [ ] **Step 1: Write the failing tests** with a `FakeHost : HostControl`:
  - `onJoin` with the tempo already matching makes no call.
  - `onJoin` mismatched calls `setTempo(110.0)` once.
  - `enabled=false` makes no call, ever (Review Focus 5).
  - `available()=false` makes no call.
  - `onGridTempoApplied` calls `setTempo` with the new room tempo.
  - A fake whose `setTempo` returns false makes `onJoin` return false (the editor then uses other-DAW wording).
- [ ] **Step 2: Run, and verify it fails.**
- [ ] **Step 3: Implement.**
  - `HostControl.h` holds the policy.
  - `ReaperBridge.cpp` handles CLAP: `getExtension("cockos.reaper_extension")` gives a `reaper_plugin_info_t`-shaped struct, and its `GetFunc`.
  - It handles VST3: a `juce::VST3ClientExtensions` subclass whose `setIHostApplication` keeps the `FUnknown*` and `queryInterface`s Reaper's `IReaperHostApplication` for `getReaperApi`.
  - Resolve `SetCurrentBPM(ReaProject*, double, bool)`, `OnPlayButton()`, `GetPlayState()` and `GetSet_LoopTimeRange(...)`.
  - **Declare the struct layout, the IID and the signatures ourselves**, copied from Cockos's published SDK (`reaper_plugin.h`, `reaper_vst3_interfaces.h`, the `reaper_plugin_functions.h` docs), after checking that SDK's licence. Record the source and licence in `THIRDPARTY.md`.
  - Only create the bridge when `juce::PluginHostType().isReaper()`.
- [ ] **Step 4: Run** `NinjamTests HostControl`. Expected: pass. Build `Antiphon_VST3` and the CLAP target, and confirm both link.
- [ ] **Step 5: Manual check, in Reaper** (`/usr/local/bin/reaper`), with both VST3 and CLAP: load the plugin on the master track, join a local `scripts/testserver.sh` room at 110 BPM with Reaper at 95. Expected: Reaper's tempo becomes 110. Vote `!vote bpm 120`; when it lands, Reaper becomes 120. Record the result in the commit message.
- [ ] **Step 6: Commit**: "Let Reaper follow the room's tempo, and nobody else's DAW be touched".

### Task 6: What is said -- SyncAnnouncements and the editor

**Files:**
- Create: `src/SyncAnnouncements.h`, `test/SyncAnnouncementsTests.cpp`
- Modify: `src/PluginEditor.h/.cpp`: `updateStatusReadout` (replace the three `say` calls), `paint` header row 3, and the Sync now handler
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ClockModel::State`, `TempoFollow`/`HostControl::available()`.
- Produces:
  ```cpp
  namespace SyncAnnouncements {
  struct Context {
    juce::String place;            // "ninbot.com", or "the practice room"
    int players = 0; int roomBpm = 0; int bpi = 0; double dawBpm = 0.0;
    int beat = 0;                  // 1-based beat in interval
    bool reaper = false;           // bridge available AND follow enabled
    bool reaperSetTempo = false;   // TempoFollow just set it
  };
  juce::String onJoin(ClockModel::State, const Context &);
  juce::String onRoomTempoChange(ClockModel::State, const Context &);
  juce::String onTransition(ClockModel::State from, ClockModel::State to, const Context &);
  juce::String status(ClockModel::State, const Context &);     // Ctrl+Alt+I
  enum class SyncNowAction { None, SelectSynced, SetTempo, PressPlay };
  struct SyncNowPlan { SyncNowAction action; juce::String say; };
  SyncNowPlan syncNow(ClockModel::Mode, ClockModel::State, const Context &);
  juce::String headerText(ClockModel::State, const Context &); // drawn row 3
  }
  ```

- [ ] **Step 1: Write the failing tests.** Assert the spec's "What is said" table **verbatim**, with X = `ninbot.com` and the spec's numbers. For example:
  - `onJoin(NotSyncedTempo, {..roomBpm 110, dawBpm 95, reaper false})` returns "Connected to ninbot.com. Room 110 BPM, your DAW is at 95: not synced, but you can hear the room. Set your DAW to 110 to sync."
  - `onRoomTempoChange(Synced, {reaper true, reaperSetTempo true, roomBpm 120})` returns "Room tempo changed to 120 BPM. Reaper set to match."
  - The `syncNow` rows follow the spec table:
    - Antiphon's clock gives `SelectSynced`.
    - A mismatch in Reaper gives `SetTempo`.
    - A mismatch elsewhere gives `None`, saying "Set your DAW to 110 BPM. Antiphon syncs as soon as it matches."
    - A stopped Reaper gives `PressPlay`.
    - Synced gives "In sync, beat 5 of 16."
  - `status()` gives the four example sentences from the spec's "Status on demand" section.
  - A fractional DAW tempo is spoken with one decimal place ("110.3"), and an integer one with none.
  - No returned string contains a non-ASCII byte.
- [ ] **Step 2: Run, and verify it fails.**
- [ ] **Step 3: Implement `SyncAnnouncements.h`.** Then wire the editor:
  - **Join** produces ONE `say` with `onJoin`, replacing the separate "Connected" and tempo lines.
  - **Changes of state** go through `onTransition`, said only after the new state has held `ClockModel::kAnnounceHoldMs` (Review Focus 1), except `jumpToPlayhead` resyncs, which are said at once.
  - **Room tempo changes** go through `onRoomTempoChange`.
  - **Sync now:** `syncNowRequested` is handled on the editor timer. It executes the `SyncNowPlan` (`clockMode` store, `hostControl->setTempo`, `hostControl->play`) and then says its text.
  - **Status readout:** `StatusReadout::setStatus(status(...))`.
  - **Header:** row 3 draws `headerText`, with orange for NotSynced*, yellow for Stopped and green for Synced/AntiphonClock.
  - All of these say calls are `important = true`.
- [ ] **Step 4: Run** `NinjamTests SyncAnnouncements`, then the full ctest. Expected: pass.
- [ ] **Step 5: Commit**: "Say whether you are synced, once, in one sentence".

### Task 7: Controls and shortcuts -- the Clock group, Play, and the chip

**Files:**
- Modify: `src/PluginEditor.h/.cpp` (toolbar ~line 988, `syncButton` ~125-137, `transportButton`/`refreshTransportButton` ~139-147/448-460, `updateTempoChip` ~1612-1690, shortcut handling ~1328)
- Modify: `src/Shortcuts.h`, `src/ShortcutsDialog.cpp`, `test/ShortcutsTests.cpp`, `test/AccessibleNamingTests.cpp`
- Modify: `test/AuditMain.cpp`

**Interfaces:**
- Consumes: `clockMode`, `localTransportPlaying`, `syncNowRequested` (Task 4); `SyncAnnouncements` (Task 6).
- Produces: `Shortcuts::Action::SyncNow` (renamed from `ArmSync`, 'S'), `SayStatus` ('I'), `ToggleAntiphonPlay` (Enter), `ToggleClockMode` ('F').

- [ ] **Step 1: Write the failing tests:**
  - `ShortcutsTests`:
    - Ctrl+Alt+S gives `SyncNow`.
    - Ctrl+Alt+I gives `SayStatus`.
    - Ctrl+Alt+Return gives `ToggleAntiphonPlay`.
    - Ctrl+Alt+F gives `ToggleClockMode`.
    - Ctrl+Alt+Space gives `None`.
  - `AccessibleNamingTests`: the Play toggle's title is "Play" in both states; only its toggle state changes.
- [ ] **Step 2: Run, and verify it fails.**
- [ ] **Step 3: Implement:**
  - **Remove `syncButton`.** Add a "Clock" group: two `juce::ToggleButton`s with one radio group id, titled "Synced to DAW" and "Antiphon's clock", inside a container component titled "Clock".
    - Give the container a custom `AccessibilityHandler` with role `group`.
    - Each button's handler reports `isToggled` and the position in the set ("1 of 2") in its description.
    - The arrow keys move the selection.
    - While disconnected, the group is disabled, with the description "Applies while connected".
  - **Play toggle:** the fixed title "Play", shared with the standalone's `transportButton`, which drops its "Play"/"Stop" text swap and uses a glyph plus toggle state.
    - In the plugin, it is disabled while `clockMode == SyncedToDaw`, with the description "Follows the DAW transport while synced".
    - Pressing it (or Ctrl+Alt+Enter) selects Antiphon's clock and toggles `localTransportPlaying`.
  - **Reaper-only controls,** created only when `hostControl` is non-null. The host never changes within a session, so nothing appears or disappears mid-use. They sit right after the Clock group:
    - **"Reaper follows room tempo":** a toggle bound to `tempoFollow.enabled`, persisted as "followRoomTempo" in `getStateInformation`/`setStateInformation`.
    - **"Loop one interval":** a button that calls `hostControl->setLoopBeats(a, a + bpi)` with `a` = the start of the current interval in playhead beats, and says "Reaper loop set to one interval, bars N to M".
  - **`updateTempoChip`:** delete the `worthProposing` branch and `chipDawBpm`/`dismissedDawBpm`. Keep the vote and key branches.
  - **Shortcuts dialog:** add rows for the four shortcuts.
- [ ] **Step 4: Add audit states** to `AuditMain`: "not synced, tempo mismatch", "DAW stopped while synced", "Antiphon's clock". Run `./build-$USER/test/AntiphonAudit_artefacts/AntiphonAudit`. Expected: exit 0, and the report lists the three new states. Run the full ctest. Expected: pass.
- [ ] **Step 5: Commit**: "Choose the clock, play it, and stop offering a vote for your DAW".

### Task 8: The DAW-stopped cue

**Files:**
- Create: `src/CueVoice.h`, `src/CueVoice.cpp`, `test/CueVoiceTests.cpp`
- Modify: `src/CMakeLists.txt`, `test/CMakeLists.txt`, `src/PluginProcessor.h/.cpp` (mix at the metronome's point, after the input snapshot), `src/PluginEditor.cpp` (enable toggle and level, next to the metronome's)

**Interfaces:**
- Produces: `class CueVoice { void prepare(double sr); void trigger(); void render(float *out, int n, float gain); bool isActive() const; static constexpr double kFrequencyHz = 1568.0; static constexpr double kDurationMs = 120.0; };`. Processor atomics `cueEnabled{true}`, `cueVolume{0.5f}`, persisted as "cue" / "cueVolume".

- [ ] **Step 1: Write the failing tests** (MetronomeVoiceTests style):
  - The measured pitch is 1568 Hz within 1% (zero crossings, `test/TestSignal.h`).
  - The duration to -60 dB is at most 150 ms.
  - The render is silent after it ends.
  - It never allocates in `render` (no buffer growth).
- [ ] **Step 2: Run, and verify it fails.**
- [ ] **Step 3: Implement.** A sine pip with a fast attack and an exponential decay; fixed pitch.
  - **Processor:** while `publishedClockState == NotSyncedDawStopped`, and only when it was entered from `Synced` (the spec's call), trigger once per interval at beat 0.5 of the own clock, which is deliberately off the beat.
  - **Never transmitted:** mix after the input snapshot, as the metronome is.
- [ ] **Step 4: Run** `NinjamTests CueVoice` and the full ctest. Expected: pass.
- [ ] **Step 5: Commit**: "A tone for the one state you must not miss".

### Task 9: Chat keeps its keys

**Files:**
- Modify: `src/PluginEditor.cpp` (`chatInput` ~332-430), `src/PluginEditor.h`
- Modify: `README.md`, `website/docs/` (the page covering chat): the Reaper tip

**Interfaces:** none new.

- [ ] **Step 1: Implement:**
  - After `onReturnKey` sends, call `chatInput.grabKeyboardFocus()`.
  - A visible focus state: the outline in the accent colour plus a small "Typing to chat" label while the field has focus. The label is decorative to a reader (not focusable), since the field's own focus is announced.
  - In `focusLost`, if the cause is `focusChangedByMouseClick` or `focusChangedDirectly` while the editor window is no longer the foreground peer, say "Keys now go to the DAW" (important).
- [ ] **Step 2: Docs:** Reaper's "Send all keyboard input to plugin" (in the FX window's menu) in the README chat section and the matching website page.
- [ ] **Step 3: Manual check**, in Reaper and Bitwig on this machine: type a message, press Return, and type again. Expected: the second message goes to chat, and the transport does not move. Click Reaper's arrange view. Expected: "Keys now go to the DAW" is announced (check with Orca, or with a debug log of `Announcer`).
- [ ] **Step 4: Run** the full ctest and the audit. Expected: pass, audit exit 0.
- [ ] **Step 5: Commit**: "Keep the keys you type in chat".

### Task 10: Documents, parity, and the whole-branch check

**Files:**
- Modify:
  - `NON-GOALS.md`: fence #6 reversed, #7 narrowed to Reaper, and #12's REAPER sentence.
  - `DESIGN.md`: sections 4.1, 4.2 and 5 rewritten around ClockModel, PlayheadGrid and HostControl.
  - `README.md` and `website/docs/`: the sync story and the shortcuts.
  - `docs/ACCESSIBILITY.md`: the Clock group, the shortcuts and the cue.
  - `ROADMAP.md`: tick the first `CueVoice` item and add this area's closing.
  - `docs/COMPLETED.md`: move the finished area here.
  - `docs/PARITY.md`: a new measurement.
  - `AGENTS.md`: the layout map entries for the new files, with SyncState and RunGate removed.
  - `CONTRIBUTING.md`: if a rule changed.

- [ ] **Step 1: Parity measurement.** Run a real `ninjamsrv` (`scripts/testserver.sh`) with the reference client and Antiphon synced at 110/16, 44.1 and 48 kHz, and measure the transmitted interval lengths with `scripts/analyze_archive.py`. Expected: within (1 + the tolerance's few) samples of the reference client, and full length after a forced pause/resume jump. Record the method and the numbers in `docs/PARITY.md`.
- [ ] **Step 2: Write the docs.** Cite the spec rather than copying it. Then grep for any stale reference across docs, `src/` and `test/`: `grep -rn "SyncState\|RunGate\|ArmSync\|Press Sync\|fence #6" --include=*.md --include=*.h --include=*.cpp .`. Expected: only historical mentions remain in `docs/COMPLETED.md`.
- [ ] **Step 3: Full verification**, on a quiet machine (`machine-load -q`):
  - `cmake --build build-$USER -j4 && ctest --test-dir build-$USER --output-on-failure`.
  - The ASan and TSan trees: zero warnings outside the known libvorbis lines.
  - The audit exits 0.
  - The website build (`onBrokenLinks: 'throw'`) passes.
  - Quote all of the output.
- [ ] **Step 4: Manual smoke**, in Reaper, plus Bitwig as an "other DAW", against a local server. Walk each row of the spec's "What is said" table and confirm the sentence is spoken:
  - join with the DAW stopped;
  - a room tempo vote;
  - stop the DAW mid-jam: the cue repeats, and the room keeps playing;
  - resume from an earlier bar: synced on the playhead;
  - switch to Antiphon's clock, play and stop;
  - Ctrl+Alt+S from each state;
  - Ctrl+Alt+I.
- [ ] **Step 5: Commit**: "Write down synced-or-not, and retire the Sync button".
