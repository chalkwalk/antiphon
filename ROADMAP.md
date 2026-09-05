# Antiphon -- Roadmap

The forward-looking plan. Work is organised into **named work areas**, grouped
under headings for readability. Sub-tasks are checkboxes, so the state of the
project is visible on every return to the repo.

**Work is referred to by name, not by number.** A decimal milestone id means
nothing when you come back to the repo in three weeks, and it invites scope drift
between huge items and tiny ones. "Audio-thread hygiene" is the reference; the
grouping headings are organisational only and are never cited.

For architecture see `DESIGN.md`. For the principles every piece of work must
satisfy, see `PRINCIPLES.md`, and for the standing refusals `NON-GOALS.md`.
**Before adding a work area here, confirm it clears both.**

**Shared code across the Chalkwalk plugins is planned separately**, in a
document kept outside this repository -- which libraries are extracted, which
third-party dependencies are taken, and the licence and JUCE-free rules. That
argument is not restated here, and nothing below depends on having read it.

---

## Active focus

*(2026-08-30)*

The client works, and it has now been used by somebody other than its author.
It connects to real servers, transmits and receives in time with other clients,
and is verified differentially against the reference client at two tempos
(`docs/PARITY.md`). A contributor has built the AU on macOS, joined a jam and
worked it with a screen reader, reporting that it compares favourably with the
official client -- the first evidence the accessibility pass works where it
counts. Bounded honestly under *Screen-reader verification*.

The three items this block listed on 2026-08-08 have all landed: audio-thread
hygiene is down to the one tracked `callAsync`, the GitHub move shipped, and all
three platforms build and pass the unit suite. The work since has been the
practice room and its band.

**Re-derived 2026-08-30**, which the previous ordering asked for in as many
words. *Bots that talk* stood at the top of it and was already largely built --
`BotChat` wires `BotLanguage` and `BotAnswer` into `PracticeBot` -- and the
cross-platform items below it are unchanged and unstarted. What follows is the
band's half, ordered; the platform work is not superseded, only set beside it.

Next, in order:

1. **The room takes a request for a fifth player from chat.** `addPlayer` has
   no caller outside the tests, so a band that can grow is reachable from code
   and not from a room. This was blocked on a design conflict rather than on
   typing, and the conflict is now decided -- see *A band of more than four*.
   The smallest unblocked piece, and it is what makes the band size a thing a
   player can touch.
2. **`performanceSeed`, and then phrases.** The band never repeats anything, so
   a long session meanders. The prerequisite is not the repeat: it is that the
   performance seeds are session-held today, so every interval is jittered
   identically and a returning phrase would be a loop rather than a take. The
   two land together, within-interval repetition before the form across
   intervals. See *Form: repetition, tension and release*.
3. **The naming tail.** `role: instrument` in the channel shipped, and what is
   left is two items rather than a work area: `BotChat` answering "what are you
   playing" from the channel rather than the username, and confirming the
   rename round-trips to a reference client. The second belongs with
   *Multi-channel differential test* and should travel with it.
4. **General MIDI, behind the ecosystem answer.** Decided, and smaller than it
   first looked: what is shared with `seq_play` is the patched FluidLite
   VENDORING -- the submodule pin, the SF3 loop-end patch and its writeup, the
   re-attack test, the trimmer -- and not a C++ engine, which turned out to be
   626 lines shaped around that project's track model. The shape of that
   repository lives in the ecosystem plan; what Antiphon does first is
   unchanged and independent of it -- load an SF2 from a path the player
   chooses, and hear one sampled voice next to the model it would replace. See
   *Sampled instruments, alongside the models*.

Still open and not reordered by the above: **turn the macOS report into
findings** -- a verdict cannot be acted on, and `auval` in CI is the cheap first
answer to what stops the AU regressing silently -- and **Windows in a host**,
the remaining platform where nothing has opened a window, opened a device or
joined a jam, and half of where the screen-reader work is reachable at all.

**What has landed since, 2026-08-18/19: the repository split, and it went the
other way round.** That plan is complete. This project consumes
[chalkwalk-music](https://github.com/chalkwalk/chalkwalk-music),
[chalkwalk-dsp](https://github.com/chalkwalk/chalkwalk-dsp) and
[chalkwalk-ninjam](https://github.com/chalkwalk/chalkwalk-ninjam) as submodules
under `libs/`, all MIT, all JUCE-free, each building and testing standalone.

The entry above argued against splitting *while the practice room is
unreachable*, and that argument stands and was not overruled -- what changed is
the unit. Nothing was restructured around the practice room: what left were
four pieces of general-purpose code (music theory, DSP primitives, the wire
protocol, the loudness meter) that this repository happened to hold, and the
plugin's own shape is untouched. The **client** stayed here for exactly the
reason this entry gives; only the **protocol** left. See *Split the client
out*.

Also adopted: **libebur128** for ITU-R BS.1770 loudness, replacing 107 correct
lines of K-weighting and gating in `AudioMeasure.h`. Not a bug fix -- the two
agree to under 0.001 LU on gated material, which is how the swap was checked.
The finding was about the tests: the five ffmpeg goldens are steady sines,
where every block holds equal energy and the gate never decides anything, so
they could not tell the two implementations apart and the relative gate had no
coverage at all. It has now.

---

## Maintenance

This file is forward-looking. When every checkbox in a work area is ticked, it
moves to `docs/COMPLETED.md`, compressed to a few sentences: the intent, what
actually shipped, and any load-bearing decision made along the way.

Rules:

- **Compress only when complete.** A work area with any unchecked box stays here
  verbatim. Compression never deletes an open item.
- **Keep the name.** The `### <Name>` heading is the stable reference; it must
  survive the move so cross-references and git archaeology still work.
- **Move in the same change as the work.** A shipped feature and its archive
  entry belong in one commit.
- **Withdrawn work is archived too**, with the reason. See the entry for the
  withdrawn capture-alignment item in `docs/COMPLETED.md` -- knowing what was
  measured and why it was wrong is worth more than a deleted line.

---

## Correctness and audio hygiene

### Audio-thread hygiene

Two paths violate `PRINCIPLES §7` (the audio thread does not allocate, lock,
block or log). Both are known, both are small, and both are the kind of defect
that appears as an unreproducible dropout in someone else's DAW.

- [x] `getDecodedAudio` takes `downloadMutex` on the audio thread. The RX path is
      otherwise lock-light -- the network thread publishes progress with an
      atomic `writePos` -- so the lock is protecting the *container*, not the
      audio. Replace with a structure the audio thread can traverse without
      blocking.
      - [x] **The dangerous half is gone.** The network thread used to hold this
            lock across the whole Vorbis decode and resample of a WRITE chunk --
            milliseconds -- while the audio thread blocked on it every block.
            The lock now covers the map lookup only. Safe because
            `guidToInterval` is mutated solely by the network thread and the
            audio thread never reads it.
      - [x] Removed the data race that narrowing exposed: the decode called
            `AudioBuffer::getWritePointer`, which writes `isClear`, while the
            audio thread's `addFrom` read it. The lock had been hiding it.
            Write pointers are now taken once before the interval is shared.
            Confirmed with TSan: the warning disappeared, and the only races
            left are the known shutdown one below.
      - [x] **The lock is gone.** `channelStreams` (a `std::map` walked under
            the lock) is now `streamSlots`, a fixed array of 64 slots that are
            never created or destroyed while the client lives, so the audio
            thread walks all of them without blocking and can never see a
            half-built or freed entry.

            `SpscRing` (`src/SpscRing.h`) carries ownership in a circle so that
            freeing never lands on the audio thread:

              ready:   network -> audio   "here is a decoded interval"
              retired: audio -> network   "done with this one, free it"

            The retire direction is the load-bearing half -- dropping the last
            reference to a `DecodedInterval` frees a multi-megabyte buffer, and
            that must not happen inside the callback. `retired` is sized above
            `ready.capacity() + 2` (the most the audio thread can hold, being
            `current` and `fadeOut`), so handing an interval back can never
            fail and the audio thread is never stuck holding one.

            State is split by owner. Playback cursors are audio-thread only;
            volume, pan, mute, solo and output bus are atomics the UI writes and
            the audio thread reads; `peakLevel` is an atomic the audio thread
            writes and the UI reads -- it had been a plain float shared between
            them, a race in every build that ever ran. `remoteUsers` stays a
            locked map for the UI, under `usersMutex`, which the audio thread
            never takes. The audio thread never touches a `juce::String`.

            Slot lifecycle is Free -> Live -> Draining -> Free. The network
            thread claims and marks draining; only the audio thread publishes
            kFree, because only it knows when it has let go. On disconnect the
            slots are marked draining rather than freed, since the audio thread
            may be mid-block inside them.

            Verified: full suite green, and TSan reports zero races in the RX
            path over 81 864 assertions -- the only warnings left are the known
            shutdown race below. `AudioLoopbackTests` cycles a channel out and
            back 80 times, more than there are slots, so it fails unless
            released slots are genuinely reclaimed; confirmed by breaking the
            hand-back and watching it go red.
- [x] `setSaveTx` / `setSaveRx` are called from `processBlock` on **every block**
      and do file I/O on the toggling call. Move the toggle handling off the
      audio thread entirely; the audio thread should only ever see a flag.
      Now applied from the message thread when the toggle changes
      (`applyDebugCaptureSettings`); `processBlock` no longer mentions them.
- [x] **Re-audit of `processBlock`, 2026-08-08.** Four findings; three fixed
      here, one deferred to a tracked work area. The result, so the list stays
      authoritative:

      - [x] **`localChannelMutex` was taken three times per block** -- the
            monitor mix, `captureInputRange` and `fireCaptureLambdas`. This was
            worse than the RX lock, because of what held the other end: the
            editor's 30 Hz timer holds it while *constructing channel strips*,
            `addLocalChannel` held it across a 30-second ring allocation (about
            11 MB at 96 kHz), and `setStateInformation` holds it while parsing
            XML. The audio thread could block on any of those.

            Fixed the same way as the RX path. `localChannels` stays a locked
            `std::vector` for the UI; the audio thread walks `audioChannels`, a
            fixed array of raw pointers published by `publishLocalChannels()`
            with a release store on the count. Safe without a lock because a
            `LocalChannel` is never destroyed while the processor lives:
            removing one moves it to `spareLocalChannels` and shrinks the count,
            and a later add takes it back out. A recycled channel never resizes
            its own ring -- only `prepareToPlay` does, which the host does not
            run concurrently with `processBlock`.
      - [x] **`getDecodedAudio` took `rxFileMutex` on every block**, even with
            Save Rx off, and `isSavingRx` was a plain `bool` written by the
            message thread while the audio thread read it. Now an atomic checked
            before the lock is considered, so the normal path takes no lock at
            all. The file write itself stays on the audio thread when the toggle
            is on: writing the mix as the audio thread sees it is the whole
            point of the toggle, and it is never on in ordinary use.
      - [x] **`clockEvents` and `captureSegments` cannot outgrow their
            reserves.** Checked rather than assumed: `clockEvents` reserves at
            least 64 events, and exceeding it needs 63 beats inside one block --
            about 1.5 million samples at 120 bpm and 48 kHz. `captureSegments`
            reserves 8 against one segment per interval boundary crossed. Both
            are safe by orders of magnitude, and no change was made.
      - [ ] **`MessageManager::callAsync` still fires from the audio thread** at
            each interval boundary in `fireCaptureLambdas`. It is a `new`, a
            lock, an array append and a `write()` syscall
            (`juce_Messaging_linux.cpp:72`). Once per interval rather than per
            block, and removing it means the redesign already tracked below as
            *Lock-free TX handoff* -- so it is recorded there, not fixed here.
            The lock around it is gone; the post is not.

### Transmit gating is per interval, not per sample

`xmitEnabled` is read once, at the interval boundary, while `captureInputRange`
fills the ring unconditionally. So the flag decides all-or-nothing for a whole
interval, and toggling within one has no effect except through its final state:

- ends **on** at the boundary: the *entire* interval is sent, including every
  stretch where TX was off -- audio you may never have meant to share;
- ends **off**: the whole interval is dropped, including the part recorded
  while TX was on.

Agreed behaviour: TX gates the audio, the interval boundary gates the
transmission. Anything TX was off for becomes silence, the interval keeps its
exact length, and an interval with TX off throughout sends nothing at all --
which is what happens today and is what the reference client does for a
non-broadcasting channel. The protocol has no objection: an interval is just an
Ogg stream of N samples, and Vorbis encodes silence almost for free.

- [x] `ChannelMix::write` takes a `transmitting` flag and writes silence rather
      than input when it is false. The gate went there, not into
      `captureInputRange`, because `PluginProcessor` cannot be compiled into the
      test target -- logic left in it is untestable by construction.
- [x] An interval is sent when transmit was on for *any* part of it, tracked by
      `LocalChannel::txActiveThisInterval`. Reading `xmitEnabled` at the
      boundary instead would discard an interval you played most of and then
      switched off during. An interval you were silent for throughout still
      sends nothing, which is what the reference client does for a channel that
      is not broadcasting.
- [x] Tested in `ChannelMixTests.cpp`, block by block with the flag changing --
      the same shape as `processBlock` across an interval. Proven to have teeth
      by ignoring the gate and watching 28 assertions go red.

### Clickless gain changes and retroactive transmit

Shipped. `GainRamp` ramps every gain change over 5 ms; `TransmitSpans` records
where transmit was on and applies it at the interval boundary with ramped edges;
solo became one global bus that overrides mute; Recv silences immediately.
Holding TX, or Ctrl+Alt+Shift+T, toggles transmit and applies it to the whole
interval so far. See `DESIGN.md` section 6.1.

- [ ] The gesture itself has no automated coverage -- it lives in
      `LocalChannelStrip`, which cannot be compiled into the test target. The
      logic under it (`TransmitSpans`, `GainRamp`) is tested; the press, the
      hold threshold, the flash and the announcement need hands.


### Underrun tail

When a decoded interval runs short, `getDecodedAudio` leaves the remainder of the
block silent. A hard cut to silence is the most audible possible failure mode.

- [ ] Hold the last sample or fade out over a few milliseconds instead of
      cutting.
- [ ] Decide and document what "short" means here -- a late interval and a
      genuinely short one are the same thing to the reader, but not to the user.
- [ ] Test in `AudioLoopbackTests.cpp` with a deliberately truncated interval.

### Client shutdown race

Shipped. The socket now has exactly one owner: the network thread creates it,
uses it and closes it, and `disconnectFromServer` signals rather than reaching
in.

- [x] `disconnectFromServer` no longer touches the socket. It used to call
      `close()` to interrupt a blocking read, while `run()` closed it as well on
      the way out -- both writing the socket's own members, since
      `StreamingSocket::close()` clears its `hostName`. A real race, not a false
      positive: the fd could in principle be reused between the two closes.
- [x] `readFull` waits with a timeout instead of blocking for a whole message.
      The blocking form only re-checked `threadShouldExit` between reads, so a
      peer that sent half a message and stalled held the thread until the socket
      gave up -- which is what made the external close look necessary in the
      first place. Signalling is now enough.
- [x] Confirmed under TSan: **0 warnings**, down from 29. Restoring the external
      close brings all 29 back, so the fix is the fix rather than a change of
      timing.

**The baseline is now zero, in both sanitisers.** Any TSan warning is a
regression, and so is any ASan error. The only remaining sanitiser output is
four UBSan lines from inside vendored libvorbis (`bitwise.c`, `floor1.c`,
`psy.c`), which is not ours and is documented as ignorable in `AGENTS.md`.

### Lock-free TX handoff

`processBlock` does a `callAsync` with a full buffer copy at each interval
boundary. A `juce::AbstractFifo` would remove the copy and the allocation and
reduce TX latency jitter. Much safer to attempt now the loopback tests exist.

This is now the **last remaining `PRINCIPLES 7` violation on the audio thread**,
and the re-audit above confirmed the cost precisely: `callAsync` is a `new`, a
`CriticalSection`, a `ReferenceCountedArray` append and a `write()` syscall
(`juce_Messaging_linux.cpp:72`). It fires once per interval rather than once per
block, which is why it outlived the rest.

- [ ] Replace the per-channel `callAsync` copy with a FIFO drained on the message
      thread.
- [ ] Verify transmitted interval length is unchanged against the reference
      client -- this touches the exact path `docs/PARITY.md` measures.

### The interval boundary is a compute spike

Everything this project does periodically, it does **all at once, at the
interval boundary**. That is the cheapest thing to write and the worst shape for
a live audio process: a long contiguous burst of compute is what starves an
audio callback on a busy or low-core machine, where the same total work spread
thinly would not.

**The 27% below was measured on a build with no optimiser in it.** `cmake -B
build`, the command this repository documents, left `CMAKE_BUILD_TYPE` empty,
and CMake's empty is not Debug -- it is no flags at all: no `-O`, no `-g`,
NDEBUG undefined. The tree defaults to RelWithDebInfo now -- with NDEBUG stripped back out of it, so assertions
survive the optimiser -- and the numbers
below are both builds so the correction is visible rather than quietly
substituted. `PRINCIPLES` §5 says suspect the measurement; the build the
measurement was taken on turns out to be part of the measurement.

Synthesis, per interval, at 100 bpm and bpi 16 -- the practice room's OWN
shape, which is the second thing that was wrong here: the old figure came from
the test fixture's 120/8, a 4 s interval the room never uses.

| voice | no optimiser | `-O2`, the default | share of the band |
|---|---|---|---|
| Kit | 1300 ms | **452 ms** | 62% |
| Bass | 78 ms | 25 ms | 3% |
| Keys | 459 ms | 219 ms | 30% |
| Lead | 52 ms | 31 ms | 4% |
| **band** | **1890 ms (19.7%)** | **728 ms (7.6%)** | |

The `-O2` column is with assertions LIVE, since that is what the default build
is. Stripping them as well gives 679 ms rather than 728 -- a 7% difference,
which is what `assert` and JUCE's debug checks cost here and is not a reason to
give either up. The whole ctest run is 179 s with them and 178 s without.

From `AntiphonVoiceLab bench`, which renders whole intervals and times them --
the unit the conductor actually renders. Across four seeds the optimised band
lands at 7.5-10.3% of an interval; the kit is 55-62% of it every time.

End to end, including the Vorbis encode, from the responsiveness test in
`PracticeRoomTests.cpp`: the longest single bot render is **162 ms optimised**
against 435 ms before, at 120/8. The most expensive single voice there is Keys
at 123 ms, so encode is about 39 ms -- a quarter of a bot's cost rather than
the third the old table claimed. The quartet costs roughly **10-11% of one
core** in a build anybody would ship or profile.

**Smearing does not make it cheaper, and the fan will not get quieter.** Worth
stating plainly because the two get conflated: the conductor is single-threaded,
so spreading the burst changes neither the total work nor the peak core in use.
What it buys is the length of the longest contiguous compute region, which is
what decides whether an audio callback misses its deadline. If the goal is less
CPU rather than smoother CPU, that is an optimisation problem and synthesis is
where two thirds of it is.

**Threading the bots is not the answer to either, and is worth refusing by
name.** It cannot reduce total work, which is the paragraph above. It would
shorten the burst -- but the burst is already staggered, and the freeze that
started this was a LOCK rather than the cost. What it would spend is the reason
`PracticeRoom` has one conductor thread: bots that share a clock stay tight
with each other for free, and one thread is one thing to reason about at
teardown. At 7% of a core for four and perhaps 14% for eight, there is no
capacity problem for parallelism to solve.

**The receive side is already smeared, and should be left alone.** Remote
intervals decode incrementally as each `SERVER_DOWNLOAD_INTERVAL_WRITE` arrives,
on the network thread, rather than in one pass at the boundary. Nothing to do
here; recorded so it is not "fixed" into a burst by someone tidying.

- [x] **Stagger the bots.** `jambot::IntervalPump` -- named `Conductor` when
      this shipped -- gained a slice count and calls back once per slice at
      even offsets through the interval; `PracticeRoom` renders one bot per
      slice. No change to the renderer at all, and the
      slack was already there -- Ninjam transmits interval N while N-1 plays,
      so a bot rendered three quarters of the way through still arrives in
      time.

      **Measured 1073 ms -> 435 ms**, by timing the worst `botNames()` latency
      while the band plays: that call takes `botsMutex`, so what it reports is
      the longest time the room spends inside one render. Asserted in
      `PracticeRoomTests.cpp`.

      The prediction here was ~270 ms, which is 1073/4 and was wrong: the four
      voices do not cost the same, so quartering the calls does not quarter the
      longest one. The kit and the keys are stereo and go through a room and a
      chorus respectively. Worth remembering before the next item's estimate --
      if the longest slice needs to come down further, it is one voice that
      has to get cheaper, not the schedule that has to get finer.
- [x] **Chunk the local encode.** `NinjamClient::processCapturedAudio` encodes a
      whole interval in one pass, and for a human player that pass runs on the
      MESSAGE thread, posted by the `callAsync` in *Lock-free TX handoff*. So
      every player, in every room, takes a UI hitch at each boundary -- this is
      not a practice-room problem. The loop is already in 1024-sample blocks
      (the `// We should send smaller chunks` comment is about the packets, not
      the pass); what is missing is spreading those blocks over the interval
      instead of running them back to back.

      **Done, and it did NOT need the FIFO redesign first**, which this entry
      claimed it would. The handoff splits cleanly in two: draining the FIFO
      and applying the transmit spans is a memcpy and a ramp and has to stay on
      the message thread, because the FIFO wants emptying promptly; the Vorbis
      pass is the expensive half and nothing about it is thread-bound.
      `enqueueCapturedAudio` hands that half to an `EncodeWorker`, which paces
      it across half the interval. `processCapturedAudio` is unchanged for its
      other caller: the band's bots pass no pacing and encode flat out on the
      conductor thread, because they are already a slice apart and pacing them
      again would run one bot's encode into the next bot's slice.

      Everything the encode touches was already thread-safe -- `writeFull` takes
      a lock and documents three calling threads, `SessionWriter` locks, and
      `getSystemRandom()` is `thread_local` -- which is why this was contained.
      The `callAsync` itself is untouched and *Lock-free TX handoff* still owns
      it.
- [ ] **Beat-sized synthesis, if staggering is not enough.** The deep version of
      this, and much the most expensive: `BotBand::renderInterval` renders a
      whole interval as one deterministic function of `(voice, settings,
      intervalIndex)`. Rendering a beat at a time means carrying voice state
      across chunk boundaries -- notes ring past the beat they start on -- which
      is a stateful renderer where there is now a pure one. Do not start it
      until the cheap staggering has been measured and found wanting.
- [x] **A pre-existing TSan race in the practice room, found on the way, and
      fixed.** `NinjamClient::removeListener` on the conductor thread --
      `reapPartedBots` destroys a bot, which destroys its `NinjamBotClient` --
      against `ListenerList::call` on the main thread.

      The cause was one template argument. `ListenerList::remove` and its
      callback loop BOTH take `listeners->getLock()`, so the locking was
      already written; the default array uses `DummyCriticalSection`, so both
      locks were no-ops and the two raced on the list's iterator bookkeeping.
      `juce::ThreadSafeListenerList` is the same class with a real
      `CriticalSection`. Safe to hold across callbacks here because every
      `listeners.call` is posted through `callAsyncIfAlive` and so runs on the
      message thread, and no listener touches the lock the reap path holds.

      Not introduced by the two changes above -- verified under TSan at the two
      commits preceding them -- so the baseline had been broken for longer than
      anyone had looked. **It is back to zero**: TSan clean across every
      threaded suite, ASan clean, and the only UBSan output is the libvorbis
      line `AGENTS.md` already documents as not ours.
- [x] **A command reaches the band together now, named by interval.** Chat is
      still delivered to each bot separately, but a band-wide play or stop no
      longer arrives that way: the CONDUCTOR takes it, names the interval it
      takes effect from, and the room applies it to everybody in one pass
      before the latch. So the dispatch window this describes is closed rather
      than narrowed -- a bot that learns of the command late still applies it
      in the same interval as the rest, and one naming an interval already gone
      is refused rather than applied late.

      The fix this entry proposed -- "bots record a REQUESTED transition and the
      room drains those atomically at latch time" -- is what shipped, arrived at
      from the other end: the request is recorded by the room rather than by
      each bot, because the conductor makes it once instead of four bots making
      it separately.

      Original entry follows.

- [ ] ~~**A command reaches the band one bot at a time.**~~ Chat is delivered to
      each bot through its own client's `callAsync`, so four bots learn of a
      `stop` in four separate dispatches. The band's phase latch is taken for
      all of them in one pass, which makes the render decision uniform -- but a
      latch landing INSIDE that dispatch window would still catch some bots
      before the command and some after, which is the same split the latch
      exists to prevent, narrowed from seconds to the width of four callbacks.
      Found by a test that watched the raw states and was flaky for exactly
      this reason; the test now watches the latch, which is the observable that
      decides what is heard.

      The fix, if it is worth one: bots record a REQUESTED transition and the
      room drains those atomically at latch time, so the command lands on the
      band rather than on four objects in turn. Not done -- it reaches into
      `BotChat`'s act handling, and the remaining window is orders of magnitude
      narrower than the bug that prompted it.
- [ ] **Then measure again, on the machine that complained.** The number that
      matters is not the duty cycle but whether an audio callback ever misses,
      and neither of the figures above is that measurement (`PRINCIPLES §5`).

---

## Interoperability

Everything here comes from the "Not covered" section of `docs/PARITY.md`. These
are honest gaps in what we have proven, not suspected bugs.

**These all need the live harness back.** It was GPLv2-linking and was removed
before publication, so any item below starts with rebuilding it out of tree --
against `justinfrankel/ninjam` at the revision in `docs/references/SOURCES.md`,
in a scratch directory, the way `scripts/testserver.sh` now fetches the server.
Whatever it proves must land as fixtures in `test/fixtures/reference/` and
numbers in `docs/PARITY.md` before it is torn down again.

### Can one machine mix a room in the gap?

A measurement, not a feature, and the precondition for something else entirely:
a server that lets people listen to a jam in a browser, designed in
`docs/superpowers/specs/2026-08-31-observer-server-design.md` and recorded in
the ecosystem plan. Nothing about that server is scheduled and this item does
not schedule it. What it does is answer the one question the whole design rests
on, cheaply, before anybody writes a line of it.

**The question.** A server producing one stream for listeners must decode every
player's channels for interval N, mix them, and encode the result. It cannot do
that as the uploads arrive: interval delivery is all-or-nothing, so a sparse
interval decodes to nothing until the end-of-stream flush. The whole job
therefore happens in the gap at the interval boundary -- and every millisecond
over budget is PERMANENT extra lag for every listener, not a one-off, because
the next interval starts no later for being late with this one.

**Why it belongs here.** This repository owns `VorbisCodec` and `ChannelMix`,
and it owns the discipline for this kind of claim: a number quoted anywhere
needs a method (`PRINCIPLES §5`), and this file already carries a withdrawn CPU
estimate taken on a build with no optimiser in it. The same trap is open here,
on a slower machine.

- [ ] Decode a realistic room's worth of channels for one interval, mix them,
      and encode one stereo stream. Time it as a whole, on an optimised build,
      at a real bpm/bpi rather than a convenient one.
- [ ] On the machine that would have to do it -- a Raspberry Pi -- and not only
      on a development machine, since that is the number that decides whether a
      Pi is the right host at all.
- [ ] Report it as a fraction of the interval, which is the unit that matters:
      the budget is the gap at the boundary, and what is left over is the margin
      before listeners slip to two intervals behind.
- [ ] Say what the answer means either way. A comfortable number makes the
      server worth planning; a bad one changes the design rather than the
      schedule -- mixing would have to move off the boundary, or off the Pi.

### Multi-channel differential test

Every parity run so far used a **single channel**. Multi-channel transmit is a
headline feature (a guitarist sending guitar and vocals as separate channels
others can mix) and it is the least-tested one.

- [ ] Drive the reference client harness with two local channels and confirm both
      arrive, correctly labelled and correctly indexed.
- [ ] Confirm `CLIENT_SET_CHANNEL_INFO` names round-trip to a reference client's
      display.
- [ ] Check behaviour at the server's `maxchan` boundary (see `docs/PROTOCOL.md`).

### Long-run drift measurement

Whether our interval grid drifts against the reference client's over a long
session is **unmeasured, not clean**. A rounding error would accumulate ~0.87
samples per interval at 137/11 -- roughly 8.7 samples over the 10 intervals
observed, which is below the 16-sample resolution of the current instrument. A
least-squares fit reported +0.48 samples/interval, but that is reproducible as an
artifact of readings alternating between two quantisation bins.

- [ ] Longer run (100+ intervals) or a finer envelope hop, or both.
- [ ] Record the result in `docs/PARITY.md` either way -- "no drift, measured to
      X over Y intervals" is the useful outcome, not just a bug hunt.

### Sample rates and hosts beyond the tested set

Our own suite sweeps 44.1 / 48 / 96 kHz. The reference client has only been run
against us live at 48 kHz, in one host, on one platform, in one format.

- [ ] 44.1 kHz and 96 kHz against the live reference client.
- [ ] VST3 as well as CLAP.
- [ ] A second DAW.

---

## User experience

### Standalone as a first-class use case

The standalone was promoted from a development convenience to a supported
secondary use case (`PRINCIPLES §2`, `DESIGN.md` §16). Startup now shows the
window before touching a device, opens it under a budget on a worker thread, and
offers a device picker when that fails -- but the rest of the standalone story
has not had the same attention.

- [x] Window before device; failures reported rather than swallowed.
- [x] Remember the working device in `audioSetup`.
- [ ] Asynchronous device *picking*. `AudioDeviceSelectorComponent` opens devices
      synchronously on the message thread, so choosing a wedging device still
      freezes the window (`DESIGN.md` §16.3).
- [ ] Assess the picker with a screen reader -- it is JUCE stock and was already
      a known gap in `docs/ACCESSIBILITY.md`.
- [ ] Decide what the standalone does about the one-bus limit: today
      `disableNonMainBuses()` silently drops the extra buses, so a user who adds
      a channel in standalone gets no way to feed it.


### Accessibility regression gate  *[shipped]*

The audit rules were unit-tested, but the tree that ships is the one that rots.
`AntiphonAudit` links the plugin's own library, drives the real editor through
four states, and exits with the finding count, so ctest fails the build when a
control arrives unnamed.

- [x] Console target linking the plugin library, running headless.
- [x] Four states: default, extra channels and buses, connect dialog, and a
      connected session driven by `FakeNinjamServer`.
- [x] Reports state their own coverage, and a state that examines exactly what
      the previous one did is a failure -- a state never reached passes
      vacuously.
- [x] Registered as `ctest -R accessibility-audit`.
- [x] Audit the standalone's audio-trouble view, which lives in its own window.
- [x] JUCE's stock device picker: audited rather than waived. Its Output and
      Input dropdowns were unnamed, and now adopt their attached labels.
- [ ] The rest of the device panel -- sample rate, buffer size, channel lists --
      only appears once a device is open, so the audit never reaches it.

### Screen-reader verification

`docs/ACCESSIBILITY.md` is explicit that no claim is made about verification with
an actual screen reader. The mechanical audit cannot judge whether a description
is helpful, whether the tab order feels sane, or whether announcements land at
useful moments.

**First external result, macOS, from a screen reader user.** A contributor built
the AU on their own Mac, loaded it in a host, joined a jam and worked the plugin
with a screen reader. Their assessment was that it compares *favourably* with the
official client, and that they intend to recommend it to blind musicians they
play with. This is the first evidence the accessibility pass works where it
counts rather than where it is measured, and the audit alone could never have
produced it.

Hold it at what it is, though: **one session, one platform, one person, recorded
from a verbal report rather than from notes.** It is strong evidence the approach
is right and weak evidence about any specific control. Nothing here is a
substitute for the checkboxes below, and the standing rule (`PRINCIPLES §5`)
applies -- a favourable result gets the same scrutiny as an unfavourable one.

- [x] Test with VoiceOver on macOS. Done once, by hand, as above.
- [ ] **Capture what was actually found.** The report was a verdict, not a list.
      Ask for the specifics while they are still fresh: which controls read
      badly, where the tab order surprised them, whether announcements arrived
      at useful moments, and what they reached for that was not there. A verdict
      cannot be turned into a fix; a list can.
- [ ] **Four specific questions**, which *Four channels, and we have built one*
      designs an answer to without having asked anybody. Every item there is a
      defensible default rather than a validated one, and one sentence back
      could reorder the section:
      - Did you want chat SPOKEN -- all of it, mentions only, or none?
      - Would a tone for arrivals and departures help, or get in the way of
        listening to the band?
      - Do you use OSARA in Reaper? ReaNINJAM ships with it and a blind NINJAM
        player is likely arriving from there, so its idioms probably beat ours.
      - What did you do when you wanted to re-read something that had scrolled
        past?
- [ ] Test with NVDA on Windows -- the other platform where JUCE has a backend,
      and still wholly unexercised.
- [ ] Repeat sessions rather than one. The questions the audit cannot answer are
      not answered once either, and the people best placed to answer them are
      now reachable.
- [ ] Assess the standalone's audio-device picker (stock
      `AudioDeviceSelectorComponent`, never looked at). Not covered by the
      session above, which came in through the AU.

### Four channels, and we have built one

The organising idea the accessibility work has been missing, and the reason the
three items below it read as unrelated when they are one design.

A screen reader is SERIAL. There is no peripheral vision to put something in
the corner of, so every piece of information is either an interruption or it is
invisible -- and the user is holding an instrument, where speech over your own
playing is worse than missing the line that caused it. Accessible realtime
applications converge on four carriers rather than one, and which carrier a
fact gets is the whole design:

| Layer | Carrier | For |
|---|---|---|
| **Ambient** | a non-speech cue | joins, parts, "a message arrived" |
| **Directed** | speech, interrupts | private messages and mentions -- somebody is waiting on you |
| **On demand** | speech, because you asked | review the last message, the levels, the key |
| **Structural** | navigation | move through history item by item |

Antiphon has a partial layer 2 and nothing else. *Chat history structure* is
layer 4 and *Level check gesture* is layer 3; they are below because they were
written before the model was, not because they are separate work.

**The reference implementation is Reaper with OSARA**, and the overlap is not a
coincidence worth ignoring: Reaper is Justin Frankel's, ReaNINJAM ships with
it, and a blind NINJAM player today is very likely arriving from exactly there.
Matching their idioms beats inventing ours, and finding out whether our macOS
contributor is an OSARA user is one question.

#### Ambient: earcons

**Established technique, not an invention.** Earcons -- structured abstract
tones -- and auditory icons are a literature going back to Blattner and
Brewster, and every screen reader ships sound schemes. Users routinely turn
SPEECH off for a class of event and leave the TONE on, for the reason above: a
cue is parsed in under a tenth of a second and costs nothing from the speech
channel.

A musical application is the best and the worst place for this at once. Best,
because the user is already listening closely with a trained ear. Worst,
because the whole output is sound, so a cue can be misheard as a part and it
lands in the mix. That tension gives the design rules, and none of them is
arbitrary:

- **Short**, under about 150 ms, so it fits between notes.
- **A timbre the band cannot produce.** The most important one. Against drums,
  bass, keys and lead, a bare sine pip or a filtered-noise tick is instantly
  not-music; anything with an instrumental envelope will be heard as a part.
- **Deliberately off the grid.** On a beat it is music, between beats it is an
  event. Counterintuitive, and it is what sells the distinction.
- **A direction metaphor rather than a code book.** Rising for arrival, falling
  for departure -- nothing to memorise. Reserve arbitrary timbres for events
  with no natural direction.
- **A fixed pitch unrelated to the room key.** Tempting to put it in key so it
  blends; that is the wrong goal. It should be legible as not the band.
- **Rate limited**, exactly as `Announcer` already does for speech. Six players
  arriving is one event.

- [ ] **`CueVoice`, beside `MetronomeVoice`.** The same shape and the same test
      layer -- one-shot, JUCE-free, pitch asserted by measurement rather than
      by formula. Three to start: arrival, departure, message.
- [ ] **Its own enable and its own level**, like the metronome's. Defaulting on
      in the standalone.
- [ ] **Panned to the player it is about**, once the rest works. It answers
      "who left" with no speech at all, and the pan is already in the strip.
      Second iteration; start mono and prove the idea first.

**Two things about the routing are already settled by the code**, and both are
favourable. Cues would never be TRANSMITTED: `processBlock` snapshots every
input before anything overwrites it and capture works from that snapshot, which
is why the metronome click does not reach the room, and a cue mixed at the same
point inherits it. And cues would land in the plugin's OUTPUT, so a DAW
recording the master bus records them -- which is not novel, it is the deal the
metronome already has, and nobody tracks with the click on. In the standalone,
where a blind player most likely is and where the practice room lives, the
question does not arise.

#### Directed: what speech is actually for

**Chat announcements land across message categories.** `onChatMessage` formats
and announces incoming user messages, private messages, actions, topics, votes,
server notices, and harmony changes to assistive technologies.

- [x] **Auto-speak incoming chat messages.** Formats and speaks public messages,
      private messages, actions, topics, server notices, and votes.
- [x] **Make `Announcer`'s comment and its behaviour agree.** Header comment updated
      to reflect that chat is included in discrete announcements.

#### The rename question, decided

*The band's two names* asks what a channel rename costs a screen reader, and it
became urgent when the re-send shipped ahead of it. **Decided: silent.** A
remote rename is ambient information and nobody is waiting on it; announcing it
is precisely the "a control's label moved under the reader" problem
`PRINCIPLES §11` exists to prevent. It stays discoverable by navigating to the
strip, where the name is already the accessible label, and through the review
gesture if a bot said something about it.

- [ ] Carry that decision back to *The band's two names* and close its
      checkbox, once anything here ships.

#### The caveat that outranks all of the above

This is reasoned from how the technology works, not from playing a jam with a
screen reader, and it is a defensible default rather than a validated one. **We
have a real user**: the contributor who built the AU on macOS, joined a jam and
worked it with a reader. One sentence from them could reorder this whole
section.

- [ ] Ask, with the specifics, and add it to *Screen-reader verification*'s
      list: **when you were in that jam, did you want chat spoken -- all of it,
      mentions only, or none? Would a tone for arrivals and departures help or
      get in the way? Do you use OSARA?**

### Chat history structure

**Layer 4 of *Four channels* above.** Chat history is a read-only text editor.
It is navigable, but there is no per-message structure a reader can jump
between, so finding "what did they say three messages ago" means scanning
character by character.

- [ ] Give messages individual structure a reader can navigate.
- [ ] **Room events reviewable as their own thread.** Joins and parts render
      into the chat display as `JoinPart` and are then buried in whatever was
      said around them. "Who left?" is a question with an exact answer, and it
      should not require reading the conversation to find it. Nearly free once
      messages have structure, and it is the half of the earcon idea that says
      what the tone was about.

### Level check gesture

**Layer 3 of *Four channels* above**, and the pattern generalises further than
levels: anything that changes too often to announce wants a gesture that says
it once, on demand.

VU meters expose a value, but there is no way to hear levels without a reader
announcing them continuously -- which `PRINCIPLES §11` explicitly refuses. A
deliberate "read me the levels now" gesture is the missing half of that decision.

- [ ] A shortcut that speaks the current levels once, on demand.
- [ ] **The same gesture for the last chat message**, and again for the one
      before it. The counterpart to not auto-speaking chat: an earcon says
      something arrived and this is how you find out what. Cheap, and it is the
      layer that makes the ambient one usable rather than merely quiet.
- [ ] The same gesture, or one beside it, for the harmony: the key, the chart,
      and the chord sounding now. The chord changes several times a bar, so it
      can never be announced on a timer -- which is exactly the argument above,
      and the reason it is the same work area.

### Melodic shaping: the two terms held back

The lead now prices the interval it moves by (`chalkwalk::music::chooseNote`),
which is what stopped it leaping oddly. Two further terms were designed at the
same time and deliberately **not** shipped with it, so each can be heard on its
own rather than as part of one large change to how the melody sounds.

**Direction memory.** After moving up, moving down again should cost a little,
and vice versa -- so a run reads as intentional rather than as a sequence of
independent decisions. The classical rule is the opposite (reverse after a
leap, to fill the gap), and both are right at different sizes. One term
captures both, with the sign set by how big the previous move was:

```
directionCost = same direction as the last move ? 0 : reversalCost
  reversalCost = +2  when |lastMove| <= 4   -- continuing a run reads as intent
               = -3  when |lastMove| >= 7   -- a leap wants filling in
```

It must stay small relative to the contour weight, because the contour
(Rise/Fall/Arch/Walk) is already doing directional work and a strong direction
term will fight it. `leadstats` reports "direction kept", which is the number
to watch: it sits near 58% today.

**Rest and duration by strength.** Duration in `renderLead` is *emergent*, not
chosen -- a note is held until the next sounding step -- so "spend longer on
strong notes" and "rest after strong notes" are the same lever, not two. The
place to pull it is the existing dropout rule:

```cpp
// was: if (strength == 0 && rng.range(0, 2) == 0) continue;
if (strength == 0 && rng.range(0, 5) < 3 - tierOfPrevious / 2) continue;
```

Rest more readily after a strong note, and the strong note is held longer for
free.

**The coupling question, answered: one way only.** Note choice may inform the
rhythm; the rhythm must never depend on it. The onset grid is the Euclidean
figure the rest of the band shares, and a lead whose figure moved with its note
choice would stop playing the same groove as everyone else. That invariant is
asserted -- `LeadLineTests` checks that every sounding step is an onset of the
lead's own figure -- and it must survive this change.

There is a second, independent duration lever that touches no rhythm at all:
the colour-note cap in `renderLead` (`held = min(length, eighth)` for tier 2)
generalised to `capForTier()`. It shortens weak notes with a note-off rather
than by moving a note-on, so it composes with the rest bias instead of
competing with it.

- [ ] Direction memory, measured with `leadstats --repeats 40`, heard before
      and after.
- [ ] Strength-biased rests, with the figure invariant still asserted.
- [ ] `capForTier()` in `renderLead`, replacing the hard-coded tier-2 cap.

### Voicing by register, not by pitch class

The band's lead avoids a semitone above a sounding chord tone
(`noteTier` in `BotBand.cpp`), and that rule is **register-blind**: it
compares pitch classes, so `B4`/`C5` and `B6`/`C7` are the same question to
it. They are not the same answer. A semitone that is unusable in a close
mid-register voicing is playable two octaves up.

That matters here more than anywhere else in the ecosystem, because the band
puts its chords below its lead by design ("an octave above the keys, so it is
heard as a melody over the chords rather than as part of them"). The chords sit
in the muddy register and the lead in the clean one, so the same pitch class is
a mistake in one octave and fine in another -- and the current model can only
veto it, never move it.

The theory, the measurements and the interface change belong to
chalkwalk-music and are written up in its
[ROADMAP](https://github.com/chalkwalk/chalkwalk-music/blob/main/ROADMAP.md).
The short version: roughness depends on how many CRITICAL BANDS an interval
spans, pitch is logarithmic and the critical band is not, so the same interval
is a different amount of rough depending where it is played. Thirds and wider
clean up monotonically as they rise; the semitone does not, and is roughest
around C4-C5 -- which is exactly the register the band's keys occupy.

- [ ] Wait for chalkwalk-music to grow a register-aware rank. This is not
      antiphon's to solve; it is one model and it should have one home.
- [ ] When it lands, the lead's clash rule becomes a VOICING decision rather
      than a veto: a colour note that clashes below can be taken an octave up
      instead of being dropped.
- [ ] Re-check the band's register split afterwards. The lead sits at 72 and
      the keys below it because of a rule that will have changed.

### The band's harmony

The practice room's band plays over a chart, and a chart is also the one thing
a room can say about its music that Ninjam has no field for. Both halves live in
`src/Harmony.{h,cpp}`; see `DESIGN.md` section 6.3.

- [x] Chord vocabulary players actually write, and a name for every chord read.
- [x] Bars survive parsing, so a bar holding two chords is half the time each.
- [x] One layout table per interval, shared by every voice.
- [x] Voice leading for the keys bot, solved around the loop.
- [x] A key inferred from a chart, offered on a chip and never applied by itself.
- [x] Degrees and roman numerals, resolved locally so nothing new goes on the wire.
- [x] The chart drawn along the phase bar, where position carries the timing.
- [ ] **Chart repetition.** `| ii | V | I |` might be a three-bar loop or the
      same loop three times over a long interval. Today a chart always fills
      exactly one interval. A repeat count -- explicit, or inferred when the bars
      divide the interval evenly -- is its own decision.
- [ ] **A key change keeps the chart, and the chart says what it is relative
      to.** Designed in `DESIGN.md` section 6.4; that section is the
      specification and this is the checklist. The bug underneath it -- a key
      announcement calling `Harmony::defaultChart` and throwing away a
      progression somebody typed -- is fixed; what is left is one UI affordance.
      - [x] A relative chord: interval from the tonic, explicit tones, and a
            binding of delegated or overridden. `Harmony::RelativeChord`.
      - [x] Decide the binding when the chart is read: diatonic with the mode's
            quality is delegated, anything else is an override. `toRelative`
            and `resolve`, with round-tripping in one key asserted lossless
            over seven charts and five keys.
      - [x] A minor-mode realisation table, so a delegated `V` stays major.
            `Harmony::modeChordOn`. A slash bass is never delegated: an
            inversion is a voicing decision the key has no opinion on.
      - [x] Spelling derived per chord: `Harmony::spellNote`, and `chartText`
            and `chordName` overloads that take the key rather than a flag. In
            the scale the key has already decided; out of it, a lowered degree
            from above and a sharp at the tritone, by the same rule
            `romanName` uses so a chart and its numerals cannot disagree.
      - [x] Accidentals measured against the parallel major in
            `RelativeChord::semitones`; display reads from the mode's own
            scale, so `bIII` in a minor key echoes back as `III`.
      - [x] `parseDegreeChart` reachable from the practice room, read against
            the key the room is already in -- and from the CLIENT too, via
            `src/RoomHarmony.h`, which is the one place a chat line's effect on
            the key and the chart is decided. It was two places and they
            drifted: the band followed `| ii | V | I |` and carried a chart
            through a key change while the chord row above the phase bar did
            neither, so the display went stale with nothing to say so.
      - [x] **A key change no longer bins the chart.** `PracticeBot` moves a
            chart somebody wrote through `toRelative`/`resolve`, and rebuilds
            only a chart the key itself implied. This was the bug underneath
            the whole section.
      - [x] "Use the default chords for this key" as something a player can ask
            for: `RESET_CHART`, 24 corpus lines, answered by
            `BotAnswer::answerResetChart`. Offers the line to paste rather than
            acting -- a bot that reverted its own chart would be playing
            something nobody else in the room could see.
      - [x] The fixture table: `(chart, from-key, to-key, expected chart)`,
            with the arguments from section 6.4 as its rows, in
            `HarmonyTests`.
      - [ ] Letters never rewritten; a key change with one up offers the
            transpose on the chip instead, the way an inferred key is offered.
            The only piece left, and it is UI: the editor renumbers and
            respells on a key change but has never re-derived, so nothing is
            wrong today -- there is just no way to accept the move.
- [ ] **Harmony beyond diatonic.** `Harmony::realise` is the named seam:
      secondary and altered dominants, tritone substitution, borrowing from
      adjacent modes. Functional roman naming (`V7/vi`) belongs with it, since
      it is the same knowledge and today's naming is deliberately mechanical.
- [ ] **Fuller voicings.** Ninths and thirteenths voiced rather than named only,
      and dropping the root from the pad when the bass is already on it.
- [x] The practice room is wired into the processor now, so the timeline's
      "show the band's own chart in practice" rule is reachable.

### Bots that talk

Practice is the best introduction to Antiphon and nothing says so. Beyond
teaching, the bots could feel like present players rather than pattern
generators -- answering when asked what they are playing, noticing a chart they
cannot read -- without a language model and without becoming a novelty.

**Designed in `libs/jambot/docs/BOT-CHAT.md`; that document is the proposal and this is the
checklist.** Chat only: the bots do not listen, and musical interaction is
separate future work. What makes a bot feel alive here is precision and
restraint rather than conversation.

- [x] `src/BotLanguage.{h,cpp}`: a cascaded finite-state recogniser -- segment
      clauses, fuse idioms, decide word class from context, map to concepts,
      repair what is not a real word, read the clause's force, score with a
      margin. Indirect phrasing has to work or the bots feel like a vending
      machine.
- [x] A corpus of phrasings and their intents, including the ones that must be
      clarified rather than guessed and the ones that must not be answered at
      all: `test/fixtures/bot-phrases.txt`, 607 lines, **a quarter of them held
      out from tuning**. The three miss rates over the holdout are the numbers
      to quote and drive down.
- [x] **They have been driven down, and this axis is finished.** Measured
      2026-08-15 by `NinjamTests BotLanguage`: tune 467/469 correct (99.6%),
      **holdout 147/148 (99.3%)** -- fallback 0.0%, clarify 0.0%, wrong 0.7%,
      which is a single held-out case. `BotAddress` is 143/143 over its own
      corpus and `BotAnswer` passes 74 assertions. Further tuning would be
      fitting to noise; the remaining work on this feature is *connection*, not
      accuracy. Re-run the suites rather than citing these numbers second-hand
      (`PRINCIPLES §5`).
- [ ] **Measure the server's vote threshold.** The RULE is settled and is in
      `libs/jambot/docs/BOT-CHAT.md` section 8; what is open is the
      measurement. Its arithmetic was read from the reference server rather
      than observed, and a number quoted anywhere needs a method
      (`PRINCIPLES §5`).

      One correction carried back from the conductor work, because it was
      recorded the other way for a day: **abstaining is a vote against.** The
      denominator is everyone connected, bots included, so a band that stays
      out of a vote does not stay neutral -- it makes the vote unwinnable. The
      conductor design briefly said the band should cast ONE vote with the
      others abstaining, which would leave a room of one human and four bots
      unable to change tempo at all.

      What the conductor does change is how the band votes once the gate trips,
      and it is now specified: `needed = M - N` off the vote line, the conductor
      votes, and it commands `needed - 1` members to vote with it. That replaces
      section 8 point 4, which had each bot wait its own delay and check on
      waking whether the motion had carried -- the delay-and-watch mechanism
      deleted everywhere else, and the last place it was still assumed.

      A second correction landed 2026-09-03, from reading the server rather than
      reasoning about it. **`M` is a required vote count, not a head count** --
      the threshold formula is substituted straight into the line -- so nothing
      has to be inferred from it, and the gate is not a majority rule at all but
      the server's own arithmetic on the human population. Both fixed gates
      previously written down were wrong, in both directions. `docs/PROTOCOL.md`
      records the server facts; `libs/jambot/docs/BOT-CHAT.md` section 8 records
      what the band does with them.

      What is left to measure is the live behaviour: connect a varying number of
      clients to `scripts/testserver.sh`, with `SetVotingThreshold` actually set,
      and check the line against the formula (`PRINCIPLES` §5). Reading a server
      is not observing one.
- [x] **Voting in the practice server.** A stored vote and timestamp per
      client, a recount, the server's four lines verbatim, and a config change
      on carry. The arithmetic is `chalkwalk::ninjam::voting` rather than a
      third copy of a rounding rule, so a server tallying votes and a bot
      deciding whether to back one now agree by construction (`PRINCIPLES §8`).
- [x] **Turn practice-room voting ON.** At 50%, once the band could cast the
      shortfall. A room of one player and five bots needs three votes and the
      player has one; the band supplies the other two, so one player can now
      change the tempo -- and two who disagree still cannot, which is the
      property that makes it worth practising.
- [x] **The band's vote policy.** Bots are ordinary clients, so they count
      toward the threshold, and abstaining is a vote against: four of them
      would take tempo control away from a room of three humans entirely. The
      conductor reads `N/M` off the vote line, tops the vote up once the humans
      have cast what a room of just them would have needed, casts its own vote
      and commands `needed - 1` members for the rest. It never proposes a value,
      it latches so it cannot answer its own vote, and a change of candidate
      resets it. Designed in `libs/jambot/docs/BOT-CHAT.md` section 8.
- [x] `src/BotAnswer.{h,cpp}`: what a bot says when asked about the room, as
      pure functions over a `Room` struct, with key and chart provenance
      (defaulted / topic / chat). Every reply is asserted not to parse as a key
      announcement or a chart, because saying either performs it.
- [x] A second key form, `/key D minor`, matched only at the start of a line --
      so the key can be explained without being set, and so any client can set
      it. `MusicalKey::parseAnnouncement`.
- [ ] **Sync the practice room's topic to the key.** The room owns its server,
      so the topic can be derived state and therefore never stale -- but
      `PracticeServer` has no chat hook to notice a key change through, and that
      plumbing wants designing rather than bolting on. Never on a server we do
      not own.
- [x] Answering `SET_KEY`, `SET_TEMPO` and `SET_CHART` honestly. All three are
      recognised; none is a thing a bot may decide, and saying so is the point
      of recognising them. `BotAnswer::answerSetKey/answerSetTempo/answerSetChart`. Three parts, designed in `libs/jambot/docs/BOT-CHAT.md`: that the
      room decides, what it currently is, and how to change it in any client
      (`!vote bpm N`, a `| Am | F |` line, a `[key: ...]` tag). Two special
      cases, both about not implying a decision was made: a key that was
      defaulted rather than chosen, and no chart at all.
- [x] **The key tag is self-triggering, and reply text must respect it.**
      `MusicalKey::parseTagged` matches `[key:` anywhere in a line, so a bot
      explaining the syntax would set the key by explaining it. The answer is
      that the bot puts the tag up itself rather than teaching it -- a
      translator, not an authority, since any player in any client can type the
      tag and `/key` is only a shortcut for it. Whatever renders bot chat needs
      a test that no reply text parses as a key.
- [x] **One bot answers a common question.** Addressing decides who was asked,
      not how many should speak, and `REPORT_*`/`SET_*` are one fact rather than
      four. Acting stays collective -- `band, shake` rerolls all four -- and only
      the line about it is rationed.
- [ ] **One arbitration primitive, four uses**: the arrival roster, the tempo
      vote, the key-change acknowledgement and common answers. Staggered delay,
      then check whether the job is already done. It should replace the fixed
      "lowest instrument first" order the key-change cue was designed with,
      which picks a bot that may have been told `quiet` and then never speaks.
      **Two of the four are built** -- the roster and common answers -- and they
      share `PracticeBot::speakDelayMs`. The vote and the key-change
      acknowledgement do not exist to arbitrate yet.
      - [x] The stagger is RANK in the room's sorted bot list times 400ms, not
            a hash modulo. A hash has no minimum separation: it put two of four
            bots 32ms apart, both timers fired in one scheduling wake on macOS,
            and the roster was posted twice. It had never held -- Linux passed
            on a margin nobody had measured. Found by CI on 2026-08-22, the
            first run on a non-Linux compiler since the bots were written.
      - [x] **DONE, and the primitive is gone rather than shared.** The
            heading above asks for one arbitration primitive with four uses.
            There is none, and there are no uses: the arrival roster and the
            command confirmation are both the conductor's, the tempo vote and
            the key acknowledgement never got built, and a single speaker needs
            no tiebreak. `speakDelayMs`, `rankAmong`, `botsPresent`,
            `heardAnotherBot`, `pendingBandReply`, `bandReplyTimer`,
            `kSpeakStaggerMs` and `kIdleSpeakerPenaltyMs` are all deleted.

            The idle penalty is the piece worth remembering, because it was
            RIGHT: with the band half stopped, an idle bot answering "already
            stopped" would tell the room nothing was happening while three bots
            ended the tune. A delay was the only way four peers could express
            that. The conductor states it instead, because it issued the stop
            and can see the phases -- which is the same fact, held rather than
            raced for.

      - [x] **The ARRIVAL race is gone, and by removal rather than tuning.**
            The roster and the joining instructions are the conductor's now,
            and a conductor is alone, so there is nothing left to arbitrate on
            that path. Verified by ten consecutive runs of the suite that
            failed one in seven -- a single green run would not have been
            evidence.

            **The stagger itself survives, for commands only**, and that is
            not an oversight. `band stop` is still four peers who might each
            answer, so `speakDelayMs` still ranks them. Proved the hard way
            during this work: flattening that delay made every acting bot wake
            at once, and the band confirmed one `band stop` three times. The
            check `heardAnotherBot` only works if somebody is heard FIRST.

            It goes when the conductor owns commands and can state the fact
            itself -- it will know whether the band wrapped up or was already
            stopped because it issued the stop. Until then this path keeps the
            mechanism, race and all.

            Original entry follows.

      - [ ] ~~**The rank stagger STILL races, about one run in seven**~~, and
            this is where the fix lives rather than in another round of tuning.
            Observed 2026-09-01 on Linux, in `PracticeRoomTests`' latecomer
            case:

            ```
            !!! Test 8 failed: an already-announced bot spoke again:
            MSG|Pemo[lead-bot]|say "band play" to start us and "band stop" ...
            ```

            A bot whose arrival window had already closed posted the joining
            instructions a second time. Not reproducible on demand -- one
            failure in seven runs of the same binary -- which is the shape of
            the 2026-08-22 bug rather than a new one: the margin was narrowed
            and never made safe.

            **Deliberately not being fixed by tuning the stagger.** The
            conductor design removes this mechanism entirely: plural speech
            becomes the conductor's, singular answers need no arbitration
            because exactly one bot was addressed, and `speakDelayMs` and
            `rankAmong` are deleted rather than repaired. Widening the delay
            again would buy margin nobody can measure, on a mechanism with a
            scheduled end. See
            `docs/superpowers/specs/2026-08-31-conductor-design.md`.

            **What that costs meanwhile:** an intermittent red build. It is a
            test failure and not a player-visible fault -- the worst a listener
            gets is the joining instructions twice -- so it is being carried
            rather than papered over with a longer timeout, which would hide
            the evidence that the mechanism is unsound.
- [x] `BotChat.{h,cpp}` as pure functions over what a bot knows: a snapshot in
      and an intention out, so `PracticeBot` decides nothing and the join is
      testable without a room.
- [x] A fifth, instrument-less tutor bot that teaches six lines and then stops.
      It was designed to part here and does not: it is a conductor as well as a
      tutor, so the teaching ends and the bot stays to lead.
      The players play the changes; they do not teach. `TutorBot` in
      chalkwalk-jambot, hosted here behind `PracticeRoom::Config::withTutor`,
      which is OFF by default -- a practice room is exactly where somebody
      meeting the interval model first arrives, so it should end up on, but
      flipping it changes the membership of every room the tests start and that
      is a change to make deliberately rather than alongside the bot.

      It needed an interface change first, and that is the part worth
      remembering: `BotClient::Listener` had no audio callback at all, so a bot
      could send audio and never receive any. Two of the six lines are gated on
      hearing the player, so they were not merely unbuilt but unobservable.
      `onIntervalReceived` is generic rather than a favour to the tutor -- *A
      responsive jamming partner* needs exactly the same thing.
- [x] The tutor's one piece of listening. `InputCheck` in chalkwalk-jambot:
      an interval in, one of five readings out, and the tutor uses it to decide
      whether "that interval just went out" is a true thing to say.

      **A click is not a duty cycle, and that was found by building it.**
      Section 7's table says "tiny duty cycle"; measured as a fraction of an
      interval, ONE short percussive note -- which that same section names by
      hand as a part that must never be called silence -- is indistinguishable
      from a buffer underrun, and the same click would be caught at bpi 8 and
      missed at bpi 32. It is judged in seconds now. The test fails under the
      fraction rule, which is how the correction was checked.

      Two smaller departures, both in section 7 now: the `Playing` row is the
      FALLBACK rather than a positive test, since the uncertainty rule sends
      anything unclear there anyway -- which retires the transient count the
      table asked for, as it appears only in the row never evaluated. And the
      check outlives step 2, because read strictly it would make the quiet and
      clipping rows unreachable: both let the thread through, so it is past
      step 2 within two intervals and a row needing three in a row could never
      fire.

      **The instruments are reachable now.** chalkwalk-dsp 16e6486 splits
      `Loudness.h` out of `Measure.h`, which had `<ebur128.h>` at the top of it
      and so could not be included by anything linking the header-only
      `chalkwalk::dsp` -- which is exactly what chalkwalk-jambot links. Peak,
      rms and crest were reachable only from that library's TEST target, the
      wrong half of a repository to be able to measure a signal. The table
      needs those three plus a duty cycle and a transient count, and NO
      integrated loudness, so nothing about this wants libebur128 back.
      `test/MeasureIsolation.cpp` upstream is what keeps the wall standing: it
      builds `Measure.h` against `chalkwalk::dsp` alone and has no test cases,
      because the compile is the assertion.

      Original text follows: subscribed to the owner alone, using
      `AudioMeasure` plus a duty cycle and a transient count to tell silence,
      a faint signal, clicks and clipping from somebody playing -- so it can say
      "that went out" rather than hope. It gates which encouraging line is said
      and never becomes a judgement.
- [x] The budget, and a test that asserts a hundred events produce at most N
      lines. N is TEN -- five lessons, a sign-off, and four diagnostics that
      fire once each -- and there is no budget mechanism because section 7 is
      right that the tutor is finite by construction. The test is what holds
      the construction to it: a hundred events of every kind, including audio
      of every reading, and the tutor must not find an eleventh thing to say.
      It goes red if a diagnostic is allowed to repeat.

      What the test does NOT cover, said plainly rather than left to look
      covered: `advance` now takes the step it expects and derives its own
      line, so a caller cannot pair one step's line with another's. Every
      caller tests `step()` and then acts, which is a check and an act with the
      lock released between them. The hardening is reasoned, not proved -- a
      threaded test was written, passed against the old shape as well as the
      new one, and was deleted rather than kept as decoration.
- [x] `quiet`, per bot: `SET_QUIET`/`SET_LOUD` reach
      `BotChat::Act::SetChatMuted`. The gate is applied once, after the
      decision, so a new intent cannot forget it; only two things still speak,
      and both confirm an action rather than commenting on one -- coming back,
      without which there is no way out of the mute, and leaving.
- [x] Unprompted speech off outside the practice room, **for the tutor**. It
      is opt-in by construction: nothing but `PracticeRoom` ever builds one,
      and it is behind `Config::withTutor`. There is no flag to add because
      creating the bot IS the switch.

      The premise of this item was wrong, though, and the correction belongs
      somewhere: "nothing speaks unprompted yet" was already untrue when it was
      written. The **arrival roster** is unprompted speech -- a bot joins and
      names the band without being asked -- and it predates the tutor by some
      way. Whether that should be conditional on the room is a real question
      and a separate one; it is about the players, and the tutor cannot answer
      it. It is the item below.
- [ ] **Decide whether the arrival roster belongs on a public server.** A bot
      joins, waits its rank in the stagger, and posts "The Understudies: Mirn
      (kit), ..." without being asked. In a practice room that is the band
      introducing itself and is most of what makes it read as a band rather
      than four processes starting. On somebody else's server it is four
      clients typing into a room that did not ask, and the case for it is much
      weaker.
      - [ ] Decide: always, never, or only where the room is ours. "Only where
            the room is ours" needs the bot to KNOW, which it currently has no
            way to -- a practice room is a Ninjam server like any other, which
            is the property the whole design rests on and is not worth giving
            up for this.
      - [ ] Whatever is chosen, a bot that says nothing on arrival still has to
            be identifiable. The channel name carries the instrument, so the
            roster is not the only thing that says who is playing what -- check
            that before assuming the roster is load-bearing.

- [x] **The room is reachable from the app.** It was a separate command-line
      tool and nothing in `src/` started one. It is a button in the connect
      dialog now -- a destination beside the server list, not a mode -- and the
      processor owns the room so a closed window does not take the band with
      it. Leaving stops it.
- [ ] **Being present without playing.** Built, bar two things: **the endings
      have never been listened to** -- they want ears and a `--seconds` render
      rather than another assertion -- and nothing outside the practice room can
      reach the states. **Designed in `libs/jambot/docs/BOT-CHAT.md` section 15; that section is
      the specification and this is the checklist.**
      - [x] Four states -- Silent, Playing, Wrapping, Resolving -- sampled ONCE
            per interval at the top of the render and held for it. `Wrapping`
            and `Resolving` advance on their own, one interval each; `start`
            during `Wrapping` cancels the ending, and nothing escapes
            `Resolving`. `src/BandPlayState.h`, pure and driven directly by
            `test/BandPlayStateTests.cpp` -- through a room the timing is only
            observable as several seconds of audio.
      - [x] `Silent` transmits NOTHING, rather than an interval of zeroes, and
            a silent bot still follows the key and the chart: that is most of
            what anybody does between tunes. Band membership (`inBand`) and
            audibility are separate questions now.
      - [x] `START_PLAYING`/`STOP_PLAYING` reach `BotChat::Act`, and the reply
            depends on what the bot is already doing -- four states, four
            different truths. Reading again part-way
            tears an interval across two states, and delivery is
            all-or-nothing. `PracticeBot::playing` already exists for this and
            is dead weight today: never cleared, and `BotChat::Self::playing`
            is passed in and never read.
      - [x] `stop` means stop PLAYING, not leave. It was a part command in
            `kPartCommands`, in `BotAddress::isPartCommand` and in the `[LEAVE]`
            corpus, which contains `stop playing` in as many words -- the `part`
            footgun again, with the least destructive phrase wired to the most
            destructive act. Takes `halt`, `enough`, `thats enough` and
            `were done` with it; leaving keeps words that can only mean leaving.
      - [x] `START_PLAYING` / `STOP_PLAYING` intents, corpus lines first, and
            the acts to carry them. Individual and whole-band come free:
            `BotAddress::Address::Collective` already sits beside `Named`.
      - [x] The ending is TWO intervals, as `BotBand::Phase` through
            `renderInterval` rather than a second code path. A complete wrap-up
            interval -- same chart, lead laying out at the halfway point, keys
            thinning behind it, kit filling through the last bar -- a taper
            rather than a switch, since nobody winds down all at once. The BASS
            is deliberately unchanged: the rhythm section carries the time into
            the final downbeat. Then a resolving interval that opens on the
            chord the loop resolves to, rings two beats, and is quiet for the
            remainder. A downbeat chord with nothing leading into it is a
            dropout with a note on the front; the wrap-up is what makes the
            ending sound intended, and it is where the fill lives.
      - [x] The wrap-up invents NO harmony -- no turnaround, nothing the room
            did not write. The chart is the room's; the signal is arrangement.
      - [x] The resolve lands on `Harmony::resolutionChord`: the room's own
            tonic chord if the chart contains one, otherwise the mode's tonic
            triad. NOT the chart's last chord, which is often the V precisely
            so the loop loops. One rule covers blues, modal vamps and plain
            diatonic, and it only invents when the chart never said what the
            tonic sounds like here.
      - [x] Do NOT reach for `inferKey` when the ending sounds wrong in an
            unannounced key. Held to: nothing in the ending path consults it. A key guess is offered, never acted on; the wrong
            ending is a symptom of an unset key and the fix is to set it.
      - [x] It costs nothing extra: the band renders an interval every slot
            regardless, so this is two ordinary intervals of CPU and bandwidth.
            What is spent is time -- about three intervals from typing to
            silence, 12 s at 120/8, which is roughly how long a real band takes
            and scales sensibly with bpi.
      - [ ] Tune how the two intervals SOUND by ear, in `AntiphonBandLab` or
            `AntiphonVoiceLab`. The SHAPE is asserted -- energy on the downbeat
            and quiet after, the lead out by the last quarter, a fill present,
            each phase distinguishable -- but the numbers behind it have never
            been listened to: how long the chord rings, how far the keys thin,
            and whether the kit's landing wants more than an open hat over the
            kick, which is standing in for a crash the kit does not have.
      - [x] The reply says what is about to happen rather than implying it
            stops now: "wrapping it up -- ending on the downbeat after this
            one."
      - [ ] Nothing outside chat can start or stop the band: there is no
            transport control for it in the UI. Reachable from the app now, but
            only by typing.
      - [x] Arrive Silent. The band connects before the player does, so playing
            on connect played to an empty room; the roster line already re-arms
            for the first human and is where start/stop is taught -- the way IN
            first, because a room where nothing happens looks broken. Disposes
            of the wait-forever COST as a side effect: a band nobody joins now
            encodes nothing.
      - [x] **One authority tier: any human, every command.** Eviction is
            already open to everyone deliberately, so gating anything less
            destructive behind ownership would be incoherent. The owner is not
            a permission -- it is who the cleanup rule watches. Bots still take
            no orders from bots.
      - [x] Owner departure stops being fatal. A PART used to call `part()` at
            once, `onDisconnected` refuses to reconnect by design and
            `reapPartedBots` deletes the objects -- so a 30 s blip destroyed the
            band and the room ran on empty. Now, on the other-humans predicate
            the roster already computed: others present -> keep playing and
            start no clock, since the band plays for the room and anyone present
            can dismiss it; room empty -> silence plus three minutes; nobody
            arrived yet -> six. Silencing CUTS rather than ending, because an
            ending played to nobody is encoding for its own sake, and the
            departure rule is `BandPlayState::silence`'s only caller.
            `PracticeRoom::Config` carries both durations, so the countdown is
            testable in seconds rather than minutes.
      - [x] Returning inside the window does not restart them, and needs no line
            of its own: the arrival roster already re-arms for the first human in
            a room, which on a reconnect is the returning player, and says
            exactly what a welcome back would. Where it does not re-arm, others
            were present and the band never stopped -- so both cases are covered
            without a line, which beats having one.
- [x] **One bot speaks for the band.** A collectively addressed message whose
      answer would be the same from everyone gets exactly one reply, phrased
      for the band ("we're wrapping it up"); one whose answer differs -- what
      each is playing, sounds like, is -- gets all four. Delay-and-watch, with
      a bot that ACTED speaking ahead of one that had nothing to do, so a
      half-stopped band does not have a silent bot answer for it.
- [ ] Addressing: at most one bot ever answers, cold silence is the default,
      first contact must be explicit, and a message aimed at a human is
      answered by nobody. Four bots replying to one question is the annoyance
      the whole feature has to avoid. Corpus at
      `test/fixtures/bot-addressing.txt`, 143 cases, many of them "nobody".
- [x] `tools/PracticeRoomMain.cpp` (`antiphon-practice`): hosts a room and waits,
      so the band can be heard and talked to before any of it is reachable from
      the plugin. Cheap because the room was designed as a destination rather
      than a mode -- there was nothing to integrate, only something to start.
- [ ] **Chat entry affordances**: cursor up/down through sent-message history,
      and tab completion of usernames. Addressing a bot means typing its name,
      so completion is not a convenience here -- it is most of the friction.
- [x] **Wire the addressing half.** `BotAddress::classify` is called from
      `PracticeBot::handleAddressed` (`src/PracticeBot.cpp:637`), so *who was
      asked* is decided by the measured recogniser. `withoutAddress` strips the
      name before command matching, which is what stopped "Ravo: shake"
      defeating every command.
- [x] **Wire the other half.** `src/BotChat.{h,cpp}` is the join: a pure
      function from (room, music, self, message) to *what to say and what to
      do*, with `PracticeBot` reduced to a snapshot in and an intention out.
      The ad-hoc exact matching it replaced (`handlePrivateCommand`,
      `handleBandCommand`) is gone. Covered by `test/BotChatTests.cpp`, which
      is where the words are asserted without a socket.
- [x] **The trap in that wiring.** A reply quoting `[key:` would set the key by
      explaining it. `BotAnswer` asserts it over its own replies, and the sweep
      runs over every provenance combination -- both `keySource` and
      `chartSource`, since varying only one leaves `describeChart` unable to
      return the bare chart text that is the actual hazard.
- [x] **`PracticeBot` has a test file of its own**, which the client interface
      is what made possible: a thirty-line fake client, no socket, no room, and
      the answers arrive synchronously. It found a real gap immediately -- a
      parted bot went on answering, because the guard had always been the
      transport's rather than the bot's.

### A legal BPI can exhaust memory

`NinjamClient` reserves one decoded interval per remote channel at
`sampleRate * 60 / bpm * bpi * 1.5`. That is 2.3 MB per channel at the usual
120/8, and the server will happily go far past it -- **1000 BPI and 39 BPM are
both legal and both were set on a live server by accident** (measured; see
`docs/references/ninjam.md`).

| BPI | BPM | Reserved per remote channel, per interval |
|---|---|---|
| 8 | 120 | 2.3 MB |
| 64 | 120 | 18.4 MB |
| 1000 | 120 | **288 MB** |
| 1024 | 40 | **885 MB** |

A room at 1024/40 with four remote players asks for three and a half gigabytes,
allocated on the network thread, with no guard anywhere.

- [ ] Decide what a client should DO about an interval it cannot afford. The
      options are all unpleasant -- refuse to connect, connect muted with an
      explanation, or cap and accept that playback is wrong -- and the honest
      one is probably to say so in the UI rather than to fail silently.
- [ ] Whatever is chosen, **do not clamp the tempo we display**. JamTaba drops
      out-of-range config with no `else` and shows a stale tempo instead
      (`ServerInfo.cpp:112-134`); at 1000 BPI it desyncs outright, showing 8 in
      its selector and 32 on its metronome while the server is at 1000. Being
      wrong quietly is worse than being unable to play.
- [ ] Consider warning before `/bpi` sets something the room cannot follow. The
      server allows it, but no other client in the room will survive it.

### Form: repetition, tension and release

The parts are generated fresh every interval and never return to anything, so a
long session meanders: nothing recurs, nothing builds, nothing resolves. The
lead is the clearest case -- `leadLine` rerolls its contour from
`saltedSeed + 7919 * intervalIndex`, which is a rule that says "never repeat".

The cheap fix is that **every bot already knows `intervalIndex`**, so every bot
can evaluate the same function of it and arrive at the same structure with no
listening and no coordination. That is the third use of this trick -- one bot
acknowledges a key change, one bot answers a question, and now the whole band
follows one arc -- and it is worth recognising as the pattern it is: identical
inputs, identical deterministic function, agreement for free.

**The enabling change is to split the seed in two.** Today one seed plus the
interval index decides everything, which is exactly why repetition and
staleness cannot be separated: repeating a phrase means reusing the seed, and
reusing the seed reproduces the interval sample for sample. So:

- `figureSeed = f(roomSeed, voice, section)` decides **what** is played, and is
  the same for every interval of the same section;
- `performanceSeed = f(roomSeed, voice, intervalIndex)` decides **how** it is
  played, and is different every time.

A phrase that returns is then the same music and a different take, which is
what the interlock at the bottom of this section asks for -- reached by
construction rather than by hoping the jitter is enough.

**Two axes of repetition, and they are independent.** Both are missing and the
first is probably the larger win per unit of work:

- *Within* an interval -- a phrase shorter than the interval, repeated. Every
  figure currently spans the whole interval, so nothing recurs inside one. A
  two-bar riff played four times is the difference between a riff and eight
  bars of through-composed line.
- *Across* intervals -- the form. AABA.

**The hard constraint, from the interval delay: form varies TEXTURE, never
HARMONY.** You hear the band a whole interval late, so the form you hear is
rotated against the form they are playing. That is harmless while every section
shares the chart -- a rotation of the same chords is the same chords. Give the
sections different chords and it becomes fatal: you would be soloing over a
progression you cannot hear. The chart stays one chart.

**And the form is illegible unless something marks it.** A listener a whole
interval behind cannot infer where the phrase begins from the notes alone. The
turnaround is what makes the structure perceptible, which promotes it from
decoration to the thing that makes the rest of this audible at all.

- [x] **Split the seed**, per above -- landed as `BotBand::Hold` and
      `figureSeed`, naming the three answers to "how long is this decision
      held": Session, Section, Interval. Every call site kept its exact value,
      so the band plays identically; proved by rendering three intervals before
      and after and comparing byte for byte, not by the suite passing.

      **The model above was too simple, and classifying the sites is what
      showed it.** Decisions are not held per VOICE, they are held per
      DECISION, and no voice is internally consistent: the lead's RHYTHM is
      session-held while only its note contour rerolls, so "the lead never
      repeats" is true of half of it. That also means there is no default form
      that is a no-op for everything -- a one-section form would freeze the
      lead's contour, and a section-per-interval form would unfreeze
      everything else. Hence a per-decision hold rather than a per-voice one.

      **Nothing is section-held yet**, which is the gap rather than an
      oversight: there are no sections until there is a form, so
      `Hold::Section` folds to `Hold::Session` and that is the true statement.

- [x] **The performance seeds are session-held too, and nobody noticed.** Found
      while classifying: the per-hit jitter, the per-note drift and the
      velocity variation all seed from `saltedSeed(voice, seed) + step`, with
      no interval term at all -- so **every interval is jittered identically**
      today. What makes two consecutive drum intervals differ is the hat
      rotation and nothing else.

      That matters because the interlock at the bottom of this section assumes
      the performance already varies and only the figure needs freeing. It does
      not. `performanceSeed` is the other half of the split and it lands WITH
      the repeat rather than before it, because on its own it changes the audio
      for no benefit -- a fresh jitter per interval is only worth having once
      there is a figure returning for it to differentiate.

      **The repeat it lands with is the one INSIDE the interval**, not the
      form. That is the smallest thing that gives the fresh jitter something to
      differentiate, and it keeps the two changes separable when something
      sounds wrong: a figure recurring four times in one interval either
      breathes or it does not, and the answer is audible in one render rather
      than across a section. The three items are one branch in this order --
      `performanceSeed`, phrase length inside the interval, then the form.
- [x] **The foundation stratum.** The kit and the bass select from one shared
      part rather than the bass deriving its figure from the kick's by
      doubling. `BotBand::Foundation::part` and `::select`. Byte-identical,
      proved by `AntiphonVoiceLab parity` -- 288 renders hashed before and
      after -- rather than by the suite passing.

      **This was not on the list, and the item below cannot be built without
      it.** `figureFor(Bass)` nudged its pulse count coprime with its steps SO
      THAT the figure would not repeat inside the interval, because a repeating
      scaled copy of the kick is the kick again. A phrase period is exactly a
      repeat, so it fought that rule -- and the rule was right. What was wrong
      was deriving one figure from another: defined against the kick rather
      than from it, the collapse stops being possible rather than being avoided
      by coprimality.

      Two corrections to `libs/jambot/docs/BOT-CHAT.md` 16.2 fell out of
      checking it against the code. It had the bass as the complement of the
      kick and the kit as taking the whole figure; the bass takes the whole and
      the kit takes a subset, and the second of those is forced by the phrase
      rather than chosen. Designed in
      `docs/superpowers/specs/2026-09-03-foundation-stratum-and-phrases.md`.
- [x] **Phrase length inside the interval.** A figure whose period is a half or
      a quarter of the interval, repeated, rather than one that spans it. Seed
      chosen per voice, since a bass riff and a lead line do not want the same
      answer -- and the bass figure is already nudged AWAY from repeating
      inside the interval on purpose, so that rule becomes a choice rather than
      a constant.

      **This comes before the form, and lands in the same branch as
      `performanceSeed`.** Two reasons, and neither is preference. It is the
      larger win per unit of work, per the two-axes note above -- a riff played
      four times against eight bars of through-composed line is a bigger
      audible change than which interval recurs. And it does not touch the
      interlock at the bottom of this section: repetition inside one interval
      puts no two identical intervals next to each other, so
      `test/BotBandTests.cpp` stays green on its own terms while
      `performanceSeed` is proved out on something smaller than AABA.

      **Landed 2026-09-04, with `performanceSeed`, as one commit.**
      `Foundation::phraseSteps` takes the period from the chart's own repeat
      where it has one and from a seed-chosen divisor where it does not -- which
      with the default `I V vi IV` is every room, since that progression does
      not repeat inside itself. Designed in
      `docs/superpowers/specs/2026-09-03-foundation-stratum-and-phrases.md`.

      **Two numbers here came from measuring rather than deciding, and both
      changed the code.**

      *The floor is a bar, not a beat.* Written as a beat first, which reads as
      reasonable: over 200 seeds at bpi 32, one seed in six then chose a phrase
      of ONE BEAT repeated thirty-two times, which is a metronome. With the bar
      floor the choices are the whole interval, a half and a quarter -- evenly
      spread at 68/63/69 over 200 seeds, and identical at bpi 8, 16 and 32.

      *A phrase need not span whole bars.* Forcing that is free on an even
      chart -- no metre loses a phrase at 2, 4, 6 or 8 bars -- and ruinous on an
      odd one: it leaves nothing but the whole interval in **68%** of metres at
      three bars, **81%** at five, **87%** at seven, swept over bpi 2..32. So a
      player who typed a three-bar progression would silently lose the feature
      entirely. The unaligned case is not a fault either: a bar-and-a-half
      phrase over three bars repeats twice per cycle and lands right at the
      cycle boundary, and the bass takes its pitch from whatever chord is under
      it. That answers the last of the spec's three open questions, and it did
      not need ears.

      **The interlock now rests on the performance rather than on the hats.**
      Two consecutive drum intervals differ because `performanceSeed` carries
      an interval term, not because the hat pattern rotates -- which is the
      precondition *Phrases that return* was waiting for.
- [x] **Phrases that return.** A form table -- AABA, ABAC, AAAB -- indexed by
      interval, so a phrase is a thing the listener can recognise coming back
      rather than a fresh roll each time. The table and the section length come
      from the room seed, so `shake` changes the shape of the music and not just
      its notes.

      This is the item the interlock is about, and it is third rather than
      first for that reason: it is the first thing that can put two A intervals
      adjacent, and it is only safe once the performance actually varies
      between them. It is also what finally makes `Hold::Section` mean
      something -- today it folds to `Hold::Session` because there are no
      sections.

      **A RETURN IS ONLY A RETURN IF THE HARMONY STAYED.** Raised 2026-09-04,
      before any of this is built, because it is cheap to write down now and
      expensive to discover later.

      The constraint above says the FORM must not vary the harmony. That is
      about the sections; it says nothing about the harmony changing underneath
      them from outside, and it can: `settings.key` and `settings.chart` are
      mutated in place whenever somebody types a key or a chart
      (`PracticeBot.cpp`, `applyKey`/`applyChart`). So an A at interval 4 and
      an A at interval 8, with a `/chords` between them, would be the same
      figure over different harmony -- which is not a phrase coming back, it is
      a phrase being reused. A listener has nothing to recognise.

      So **a key or chart change resets the form origin**, exactly as
      `BandPlayState` going Silent -> Playing does in the item below. New
      harmony, new statement, starting from A. That also keeps `Hold::Section`
      honest: a section is a stretch of one harmony, and a section seed that
      outlived a chart change would be holding a decision about music that no
      longer exists.

      **This does not touch the phrase INSIDE an interval**, which reads the
      chart live at every step and therefore adapts by construction -- that is
      why a within-interval repeat may cross a chord change and an
      across-interval one may not.

      **LANDED 2026-09-05 for the kit and the bass, and WITHDRAWN for the
      lead.** `Form` decides the letter; a letter is a density transformation
      of the session's own figures, so B is this tune at a different weight
      rather than a different tune. Section length is a duration converted by
      the metre, never one interval -- at one, the boundary a listener hears is
      a whole section out of step with the one they are playing over.

      **The lead is the honest failure here, and its three measurements are
      worth more than the feature would have been.** Section-holding its
      contour was tried three times and withdrawn:

      1. A whole section rendered as ONE LINE REPEATED -- nothing in
         `leadLineFrom` depends on the interval index except the contour, and
         `carryIn` is derived by re-rendering the previous interval.
      2. Displacing the rhythm fixed that and the seams leapt: a held shape
         restarting every interval means Rise into Rise ends high and begins
         low. Mean seam motion 3.29 -> 4.25 semitones, against 4.00 for
         disabling the note carry entirely.
      3. Spanning the arc across the section fixed the seam and flattened the
         line: each interval covers 1/n of the arc, and 15.5% of moves in Bb
         Lydian repeated the note.

      What a solo needs is an arc over the section AND motion inside the
      interval, at once. That is melodic design rather than wiring, and it is
      its own item now rather than a loose end.
- [ ] **A form for the LEAD.** The three failures above are the specification:
      whatever holds the shape across a section must not repeat a line, must
      not restart the shape at every seam, and must not flatten the motion
      inside an interval. `test/BotBandTests.cpp` keeps the test that caught
      the first, still pointed at the section, so the next attempt fails there
      rather than by ear.

      **One overlap is inherent and no reset fixes it.** The room hears the
      band an interval late, so a chart typed during interval N is one the
      listener is playing over while still hearing the band's interval N-1,
      built on the old chart. That is the interval delay rather than the form,
      it lasts exactly one interval, and it is true today with no form at all.
- [x] **Starting a tune starts the form**, and so do a key change, a chart
      change and a **tempo or metre change** -- four triggers, one mechanism,
      `Settings::formOrigin`. The fourth is a consequence of the section length
      being a duration: a carried `!vote bpm` would otherwise strand the band
      part way through a section of a length that no longer exists.

      None of the four has an interval index of its own -- they all arrive off
      the pump -- so the bot remembers the last interval rendered and resets to
      the NEXT one. The interval being rendered already has its letter.
- [ ] **The conductor is the ear.** The band is deaf by construction -- an
      unsubscribed client never causes the server to send it an interval, so it
      never allocates one -- and that is why a room of bots costs one client's
      worth of buffers rather than five. Subscribing all of them to hear the
      room would give that up five times over.

      **So exactly one bot listens, and it is the one with no instrument.** The
      conductor has no channel and no audio; it is the member with the capacity
      to spare, and it is already the one that holds facts about the room and
      speaks for the band. It subscribes to the HUMAN channels only --
      `BotNames::looksLikeBot` already knows which -- because a band listening
      to itself is a feedback loop with extra steps.

      What it produces is a handful of plain numbers, per
      `libs/jambot/docs/PRINCIPLES.md` `JAMBOT §11`: density, register centre
      and spread, how active, how syncopated, where the energy sits. Those
      reach the band through `BandControl`, the way a vote does -- the
      conductor decides, the members act. They **bias** existing decisions and
      never replace them: a pulse count, a register, whether to rest, the
      ceiling on prominence.

      **One tension to resolve deliberately, not silently.** `JAMBOT §6` says
      the same seed plays the same music, and a band that reacts to you breaks
      that as stated. The principle wants scoping rather than dropping:
      deterministic *given the room's inputs*, where a reading joins the key and
      the chart as an input. Same seed and same reading, same music -- which is
      still testable, because a reading can be handed to the band by a test.

      The reading is a whole interval old and that is fine: it describes
      interval N and biases N+1, which is heard in N+2. `JAMBOT §4` only
      forbids needing to perceive a boundary sooner than an interval, and this
      needs to perceive nothing at all.
- [ ] **General MIDI instruments, as the development sound.** `BotVoice.h` is
      1595 lines of hand-rolled synthesis and `BotDsp.h` another 615 -- a THIRD
      implementation, parallel to `chalkwalk-physical`, which exists with
      waveguides, modal resonators and exciters and which `chalkwalk-jambot`
      does not depend on. Only the primitives ever moved to `chalkwalk-dsp`.

      **The argument is measurement hygiene rather than taste.** With
      hand-rolled voices you cannot tell whether a part sounds wrong or its
      timbre does. On a known Acoustic Bass patch, a bass line that sounds
      wrong IS wrong -- which is what makes the interesting work (themes and
      their variations, drifting prominence) judgeable at all. It also unblocks
      genre as a continuum, which wants far more timbres than anyone will
      hand-roll.

      **The bank is bundled in `chalkwalk-soundfont`, which Antiphon already
      has as a submodule via that library** -- `CHALKWALK_SOUNDFONT_BANK` is
      the path, and there is nothing to fetch or install. GeneralUser GS
      v2.0.3, decided for Lockstep rather than here -- 261 presets, 13 drum kits, shipped as SF3 at q0.8,
      about 10 MB, chosen by ear over a converted ladder. Its caveats belong in
      the shipped licence file: permissive but not OSI/DFSG-free, and the
      author cannot fully vouch for every sample's origin, the samples having
      been freely available before formal CC0 assignment existed. Bundled data,
      not linked code. It wants FluidLite specifically, because it leans on
      modulators that TSF gets wrong. See `libs/jambot/docs/SOURCES.md`, and
      `seq_play`'s roadmap for the measurements.

      **The whole bank, not a trimmed one.** Trimming served a consumer with a
      fixed instrument list; a band whose style is a point in a parameter space
      wants all of it -- and 261 presets is the raw material genre-as-continuum
      needs. `chalkwalk-soundfont` keeps the trim script for consumers that do
      need one.

      **Deprecating the synthesis is NOT decided by this.** Landing GM first
      means the choice can be made by listening to both, which is the only way
      it should be made.
- [ ] **A shared intensity curve.** One deterministic arc over a section, read
      by every voice and mapped to its own parameters: hats thicken, the bass
      gets busier, the keys add extensions, the lead climbs. Tension and release
      without anybody hearing anybody.
- [ ] **Staggered rests.** A voice drops out for a bar at low intensity, with a
      per-voice threshold from its salted seed so the drop-outs never coincide,
      and a floor that guarantees somebody is always playing. Sparse stretches
      and dense ones, rather than everyone stopping at once.
- [x] **Turnarounds mark the form.** The fill lands on the last interval of a
      section rather than every fourth. Four was a fixed guess at the phrase
      length a listener hears whether or not anyone intended one; it is the
      actual one now.
- [ ] Deviation, so the form does not become its own kind of stale: an
      occasional departure whose likelihood grows the longer a phrase has
      repeated. With the seed split this has a natural home -- the departure is
      a figure decision, so it belongs to the section seed and the repeat
      count, not to the performance.
- [ ] **The keys should be able to comp.** Today `renderKeys` holds one
      sustained chord per chord-span: it is a pad, and a pad is the only thing
      the keyboard player ever does. A Euclidean figure of stabs with a short
      hold, re-striking the current chord while the chart still decides which
      chord it is, would give the band a rhythmic middle it does not have.

      **Seed-chosen**, like every other timbre decision here: some sessions
      pad, some comp, some sit between. That keeps `shake` meaningful and means
      the question "which is right" does not have to be answered.

      The envelope is the actual work and it wants ears rather than a rule.
      `PadPatch` was shaped for chords that ring into each other -- its release
      is two seconds -- and a stab is a different instrument's gesture. This is
      an `AntiphonVoiceLab` job (`libs/jambot/docs/BOT-CHAT.md` has no opinion on it).
- [ ] **Being told a form.** `band, play ABACBA` as a chat intent: parse a
      letter string, bound its length, store it in `Settings`. Cheap once the
      mechanism exists and worth having last rather than first -- the default
      form has to be good before choosing one is interesting.

**One interlock to get right.** `test/BotBandTests.cpp` asserts that two
consecutive drum intervals are not bit-identical -- today the hat rotation
carries that -- and genuine repetition is exactly what would break it: AABA
puts two A intervals next to each other, and under one seed they would be the
same samples. The answer is not to weaken the test. It is the seed split above:
repetition is identical in its *figure* and never in its *performance* -- but
note the item above, because the performance does NOT currently vary between
intervals at all. The swing and per-hit jitter are session-held, so they
produce the same deviation every time; `performanceSeed` is what has to make
them differ, and it is a prerequisite of the repeat rather than a companion. A phrase that returns played
exactly the same way twice is a loop; played fractionally differently, it is a
band. The two pieces of work want doing in that order.

- [ ] **A seed should not change the volume.** The kit's integrated loudness
      varies by 3.7 LU across seeds, purely because a busy Euclidean figure has
      more hits in it than a sparse one -- so `shake` currently changes how loud
      the band is as well as what it plays, and it bounds how precisely the
      band can be balanced at all. Normalising each voice to a loudness target
      at render time would fix both. `AudioMeasure::integratedLufs` is the
      instrument; the cost is one extra pass over the interval.

      Half of this is already done for the keys, and the half that is done is
      the half a constant can fix. A brass patch is a driven near-square through
      a filter that opens on every note and a strings patch is two saws barely
      driven, so the seed's choice of patch was worth 6.4 LU on its own;
      `PadPatch::level` is a measured per-character correction and takes the
      spread across fourteen seeds to 2.4 LU. What is left is the same thing
      the kit has -- how many notes the voicing put where -- and no constant
      touches it.
- [ ] **The unit suite takes two minutes, and that is now an iteration cost.**
      It grew honestly -- most of it is rendering audio and measuring it, which
      is what the band tests are for -- but the loop between an edit and an
      answer is long enough to discourage running it. Worth an hour with a
      profile: shorter renders where a defect shows in the first note, fewer
      redundant seeds, and possibly a `--quick` subset for the edit loop with
      the full sweep left to CI.

      **Most of it was the missing optimiser**, and defaulting the tree to
      RelWithDebInfo took the whole ctest run from 261 s to 178 s -- jambot's
      own suite from 81 s to 27 s. What is left is a different problem from the
      one described above: `ninjam-unit-tests` barely moved, 149 s to 141 s, so
      it is not compute-bound at all. It is waiting -- sleeps, sockets and
      timeouts -- and no amount of shorter rendering will touch it. Profile
      before shortening anything.
- [ ] **Tab completion in the chat field.** Complete `/` commands from the
      command list, and usernames after `/msg` and `/kick` from the room's user
      list -- and a name at the start of a line, which is how a bot is addressed
      (`libs/jambot/docs/BOT-CHAT.md` section 5). Common prefix first, then cycling.
      Accessibility is half the point: the completion and the candidate list
      both want announcing, and a name nobody can spell is a name nobody can
      reach.
- [ ] **Resolve `/msg` and `/kick` against the user list, not whitespace.**
      Both split on the first space, so neither can reach a username containing
      one. Longest match against the names actually in the room fixes it, and
      is what makes tab completion and hand-typing agree.

### A band of more than four

Four is a good practice room and a bad ceiling, and the reason it is four is
that `BotBand::Voice` has four values and each of them IS its synthesis.

**Designed in `libs/jambot/docs/BOT-CHAT.md` section 16; that section is the
specification and this is the checklist for the half that lives here.** The
band's own half -- strata, roles, the full part each role selects from, the
per-stratum mix budget, instrument family tags -- belongs to the bots and is
not restated.

The shape, in one paragraph: four rhythmic STRATA (foundation, pad, lead,
accent) with any number of players attached to one; a ROLE is a stratum plus a
register plus how it relates to that stratum's figure; the stratum computes a
full part and each role selects from it, so nobody invents rhythm at the leaf
and the aggregate coheres without coordination. Roles accumulate in a fixed
order -- rhythm, bass, chords, lead, accent, chord double, perc, backup melody
-- so the Nth player plays the Nth role.

**At N=4 that is exactly the band that exists**, which is how it gets verified:
it can land without changing what the practice room sounds like, so it is
checkable against a mix already tuned by ear rather than being a rewrite whose
output nobody can judge.

- [x] **`PracticeRoom::Config` gains a band size**, and the room enforces the
      cap: eight on loopback, four elsewhere. The cap lives here rather than in
      whatever asks for a bot, so no chat path can exceed it. Four elsewhere is
      etiquette encoded as a default -- eight uploads into a stranger's room is
      not something to do by accident.

      `voiceableBandSize` takes the lower of the cap and what the band can
      actually voice, which is four until section 16's roles land -- so the cap
      is not yet the binding limit anywhere, and that is the point of putting
      it in before anything can exceed it. Downward it already does something:
      `--players 3` is a trio, and it is the RIGHT three, because players are
      added in arranging order rather than enumeration order. Zero gives one
      player; a room with no band in it is a server, and there is a class for
      that.
- [x] **Channel names carry `role: instrument`** and are re-sent when either
      changes. Supersedes *The band's two names* below, which put the role in
      the username -- see section 16.7 for why that was wrong.

      `BotBand::roleName` and `instrumentName` compose it; `PracticeBot`
      publishes it at join, at `playAs`, after a shake, and when the lead is
      asked for by name, guarded on having actually changed. The room no longer
      decides what a part is called -- it does not know, and its answer would
      be stale the first time somebody said "shake".

      The role word is not the voice's name: `voiceName` says how a part is
      MADE and `roleName` says what it DOES, so the keyboard is `chords:
      strings` rather than `keys: strings`. One-to-one only while there are
      four of each.

- [x] **A bare "shake" does not reach the bots, and that is correct.** Settled:
      the CODE was right and the manual was wrong. `BotAddress` classifies an
      unaddressed line as `Ignore` -- nobody is addressed by default, because
      these are ordinary clients that can join anybody's server and a band that
      reacted to every line typed between two people would be unusable in a
      room with people in it.

      `README.md` and `website/docs/your-first-jam.md` listed `shake` and `what
      are you playing?` bare, beside `band, start`. Both now carry the address,
      and both state the rule rather than leaving it to be inferred from a
      table -- including the exception that made it confusing: having spoken to
      a bot, its attention window keeps it listening for a few turns, so a bare
      `shake` works mid-conversation and not out of the blue.

      The room was never broken -- `band, shake` reaches all four over chat,
      which is now asserted by typing it rather than by calling `shake()`. That
      test has teeth: sending the bare form fails four assertions. The reason
      this went unnoticed is the reason it is worth writing down -- a feature
      documented as something you type needs a test that types it, and the old
      one called the method.
- [x] **Addressing resolves against live channel names.** `Participant` carries
      `role` and `sound` split off the live channel name, ALONGSIDE the
      `instrument` it takes off the username, and a word reaches a bot if it
      matches any of them. The keyboard answers to `keys`, to `chords` and to
      `strings`.

      Keeping both is deliberate: the username does not change, so neither
      should what a player who learned it last week types. The live words get a
      narrower rule than the static vocabulary -- matched only where a name
      could go, never mid-sentence -- because that list earned the benefit of
      the doubt by being closed and curated, and a channel name is whatever a
      bot is calling itself. The 150-case corpus is untouched, which is what
      makes this additive rather than a rewrite.

      Also fixed on the way: a bot built its own room entry from the channel
      name passed at CONSTRUCTION, a placeholder the room never sees, so it was
      the only participant unable to recognise its own role.
- [x] **The room creates a player, and owns the cap.** `PracticeRoom::addPlayer`
      brings one more in, in arranging order, and returns false when the band
      is already as large as it can be -- a rule speaking, not a failure. The
      check lives there and nowhere else, so a tutor, a chat command or
      anything later cannot become the path that exceeds it by not knowing
      about it (section 16.9).

- [ ] **The TUTOR brings one in, and section 7 says it will have left --
      DECIDED, it does not.** The other half of 16.9, and it was blocked on a
      conflict between two sections rather than on code.

      Section 7 makes finishing one of the tutor's three defining properties:
      six lines and it parts, and "a tutorial that leaves when you have got it
      is a rare and good thing". Section 16.9 gives the tutor the job of
      recruiting **in session**, on the good argument that it is the bot whose
      job is the session rather than the music. Both cannot hold: a bot that
      has gone cannot recruit.

      **SHIPPED DIFFERENTLY, and better: the CONDUCTOR takes the request.**
      What follows was the resolution recorded on 2026-08-31, and it was
      overtaken the same week by the conductor design -- there IS a bot in the
      middle, and it is the one whose whole job is the session rather than the
      music. That recovers 16.9's argument in full rather than spending it, and
      section 7 keeps its ending too, because the tutor is a conductor that
      also teaches rather than a separate bot. Both properties survive, which
      the original resolution could not manage.

      Corrected here rather than left standing, because a file with two answers
      in it is worse than a file with the wrong one. See
      `docs/superpowers/specs/2026-08-31-conductor-design.md`.

      *Superseded reasoning follows.*

      **Section 7 wins, and the room takes the request directly from chat with
      no bot in the middle.** Of the two properties in conflict, one is
      load-bearing and one is an argument about where a capability is best
      housed. A tutorial that leaves is a thing the project has and few others
      do; recruiting-as-an-arranging-act is a preference about attribution that
      changes nothing a player can hear. So the tutor keeps its ending intact
      and the room grows on request.

      What that spends, said plainly rather than left to be discovered: 16.9's
      argument that recruiting is an arranging ACT rather than a command. It is
      a real loss -- "we could use a bass player" reads as a musician speaking
      and `band, add a player` reads as a command line -- and it is bounded,
      because nothing stops a bot acquiring the intent later once the room can
      honour it. The path is what is being decided here, not the phrasing.

      The three alternatives and why each lost, so this is not re-argued:
      - The tutor stays but silent, waking only to recruit -- spends exactly
        the property that just won.
      - A sixth bot whose job is arranging -- another name in the room doing
        nothing most of the time, in a room whose whole case is that it reads
        as a band rather than as processes.
      - The tutor recruits only during its six lines -- keeps both properties
        by narrowing the window to when a newcomer is least likely to want a
        fifth player, which is a way of keeping a feature by making it
        worthless.

- [x] **A player brought in mid-session is announced by the conductor.** The
      band used to handle this itself: a bot that had never been announced said
      so and named the room it could see. That rule went with the roster, and
      for one plan nothing replaced it -- harmlessly, because nothing could
      bring a player in. Now something can, so the conductor names whoever
      arrived, reading the room before and after rather than being handed a
      name. The roster is not re-posted: everybody has met the others.

      Original entry follows.

- [ ] ~~**A player brought in mid-session is not announced.**~~ The band used to
      handle this itself: a bot that had never been announced said so and named
      the room it could see, so the introduction landed when the band was
      complete rather than being lost because the moment passed. That rule was
      "announce unless somebody announced ME", and it went with the roster when
      the conductor took it -- deliberately, because it is the same rule that
      raced.

      Nothing has replaced it, so today a fifth player simply appears. The room
      is not blind -- Ninjam sends a JOIN and the mixer gains a strip -- but
      nobody says who arrived. It belongs with the item below rather than with
      the roster, because the moment worth announcing is the moment somebody is
      ADDED, and that is what the caller below does.
- [x] **Somebody can ask for a player, and get one.** `addPlayer` had no caller
      outside the tests, so growth was reachable from code and not from a room.
      The caller is the CONDUCTOR, reading a chat intent -- not the room
      directly, which is what the entry above corrects.

      The cap did not move: it is checked in `addPlayer` and nowhere else,
      which is the whole reason it lives there (section 16.9), and the callback
      returns only whether it worked so that no second place can get it wrong.
      A refusal is a rule speaking, so it says "that is as many of us as this
      room takes" rather than returning a silent `false`.
- [ ] **A role change has to land on a boundary.** Switching a bot from chords
      to bass mid-phrase changes the arrangement under a line somebody is
      playing over. Probably the same treatment as the ending -- take effect at
      an interval head, possibly only at a phrase boundary -- and it is
      unexamined in section 16.10.
- [x] **Make a voice cheaper first -- WITHDRAWN, the number was wrong.** This
      said the quartet was 27% of a core and eight would be ~54%, and made
      cheapness a prerequisite for six-plus players. Both figures came from a
      build with no optimiser in it, at a tempo the practice room does not use.
      Optimised, at 100/16, the quartet is 10-11% of a core including encode,
      so eight is somewhere near 20% and there is nothing to clear first. See
      *The interval boundary is a compute spike* for the measurements and the
      method.

      What survives is the ranking, which is worth keeping because it is the
      answer if this ever does bind: the **kit is 55-62% of the band's
      synthesis**, more than the other three together, and keys is most of the
      rest. Bass and lead are under 10% between them and are not worth looking
      at. Nothing should be made cheaper until there is a number saying it
      needs to be -- this entry is what happens otherwise.

### The band's two names

**The mechanism shipped; two items remain and neither is a work area.** Read
the checklist at the bottom before the argument above it -- `role: instrument`
is live in the channel name, re-sent on change, and addressing resolves against
it. What is left is `BotChat` answering "what are you playing" from the channel
rather than the username, and confirming the rename round-trips to a reference
client, which belongs with *Multi-channel differential test* and should travel
with it rather than be done twice.

Everything below is the reasoning that produced that, kept because two of its
conclusions were superseded and knowing which is worth more than a tidy
section.

A player in a Ninjam room has two names, and Antiphon's band originally put the
same word in both. `PracticeRoom.cpp` derives one string from
`BotBand::voiceName` and spends it twice: once inside the username, as
`Delvo[bass-bot]`, and once as the bot's only channel name, as `bass`. So the
mixer reads `bass` under a player called `...[bass-bot]`, which is a strip of
screen saying nothing the line above it did not.

**The two fields are not the same kind of fact, and the protocol already says
which is which.** A username is fixed at `CLIENT_AUTH_USER` and cannot change
without reconnecting. A channel name is re-sent with `CLIENT_SET_CHANNEL_INFO`
whenever it changes and the server broadcasts `USER_INFO_CHANGE` to everyone
(`docs/PROTOCOL.md`), which is how a rename reaches other clients at all --
Antiphon already does this for local channels from `LocalChannelStrip`. So the
immutable field should carry the immutable fact and the mutable one the
changeable fact:

- the **username carries the ROLE** -- kit, bass, keys, lead -- which is what
  the bot is in the band for the whole session, and what it must be addressed
  by;
- the **channel name carries the INSTRUMENT** -- what it is holding right now,
  which can change mid-session and should be free to.

**The first of those is SUPERSEDED, and by its own argument.** It rests on the
role being fixed for the session, and once a band can be more than four that is
false: a role changes when a player leaves and the band closes ranks, and when
somebody asks a bot to switch. A role in the username means a rejoin to change
one -- the band visibly leaving and coming back to rearrange itself.

So the username carries identity alone (`Name[bot]`) and the channel name
carries `role: instrument`, both halves mutable. See *A band of more than four*
above and `libs/jambot/docs/BOT-CHAT.md` section 16.7. What survives from this
entry is everything below: that the two fields are different KINDS of fact, that
the mutable one is the channel name, that `CLIENT_SET_CHANNEL_INFO` is the
mechanism and re-sending on change is the part worth having, and the screen
reader question a mid-tune rename raises.

**The instrument names already exist and are already human.** `BotVoice`
carries `LeadInstrument` with `leadInstrumentName` returning "electric piano",
"guitar", "lead synth", and `bassPatch` / `keysPatch` / `leadPatch` are
seed-chosen with player overrides. The band knows what it is holding and does
not say so. This ships on the current synthesis, with no dependency on
*Sampled instruments, alongside the models* -- that section makes the
vocabulary bigger, not the mechanism different.

It also disposes of a constraint the shared field forced. A username must be
one token so `/msg` can reach it; a channel name has no such rule, which is why
"electric piano" can be a channel name and can never be a handle.

**SUPERSEDED by *A band of more than four* above, and reconciled here rather
than left to contradict it.** That section put the role in the USERNAME; 16.7
moved it to the channel name instead, because roles change and a username
cannot without a rejoin. What shipped is `role: instrument` in the channel,
`Name[instrument-bot]` unchanged in the username.

Two of the items below shipped under that design, and one was abandoned with
it. What is left is genuinely still open and is kept:

- [ ] ~~Split the word so `usernameFor` takes the role~~ -- **abandoned**. The
      username keeps the instrument, and addressing reads the role off the live
      channel instead. Nothing about `looksLikeBot` or `handleOf` changed.
- [x] Name the channel from the patch the seed chose, via
      `CLIENT_SET_CHANNEL_INFO`. Shipped as `role: instrument`.
- [x] Re-send on change. Shipped: at join, at `playAs`, after a shake, and when
      the lead is asked for by name, guarded on having actually changed.
- [ ] **Decide what a rename costs a screen reader -- DECIDED, silent; this
      closes when something ships.** A remote rename is ambient and nobody is
      waiting on it, so announcing it is exactly the "a control's label moved
      under the reader" problem `PRINCIPLES §11` exists to prevent. It stays
      discoverable by navigating to the strip, where the name is already the
      accessible label. Nothing announces it today, so the behaviour is already
      right; what was missing was the decision. See *Four channels, and we have
      built one*.
- [ ] Answer "what are you playing" with the channel name rather than the role,
      in `BotChat`. The question already has a corpus entry and currently
      returns the word the username also says -- and now that the channel says
      something different and truer, the gap is wider than when this was
      written. The corpus is in chalkwalk-jambot, so this is an override-and-bump
      job (`CHALKWALK_JAMBOT_DIR`) rather than a change here.
- [ ] Confirm a reference client and Jamtaba both show the rename. It is
      ordinary protocol usage, so this is a check rather than a risk, and it
      belongs with the *Multi-channel differential test* which already has to
      confirm names round-trip. **Do it there**, in one session against one
      client, rather than standing a second manual check up beside it.

### Sampled instruments, alongside the models

Not scheduled, and deliberately not started while the synthesis plan has three
steps left -- two half-finished engines would be worse than one finished one.
Recorded because the analysis is done, and because checking it changed the
answer twice.

**The player has to be FluidSynth, and that is now practical.** GeneralUser GS
makes heavy use of SoundFont modulators, and its own documentation names the
synths that render it correctly: FluidSynth 1.0.9 or later, BASSMIDI, MuseScore
2.0.3+, SynthFont2, VSTSynthFont. TinySoundFont is not among them, so the
one-MIT-header option is out for this bank.

FluidSynth was previously unusable here for one reason -- it dragged in glib,
which is exactly the framework `PRINCIPLES §6` refuses. **That is fixed
upstream.** Since 2.5.0 it builds with `-Dosal=cpp11 -Denable-libinstpatch=0`
and no glib at all, and the glib path is deprecated for removal in 2.6.0. With
drivers, libsndfile and libinstpatch all disabled it is a small static library
with no dependencies we do not already have.

Ardour vendors a trimmed FluidSynth in `libs/fluidsynth`, which is a worked
precedent for a GPL audio project doing exactly this. A submodule is preferable
to a fork we would then own.

**The other compatible players were surveyed, and only one is a real
alternative -- which turns out to be a lighter fork of the same engine.**

| Player | Library form? | Verdict |
|---|---|---|
| BASSMIDI | Yes, cross-platform | **Out on licence.** BASS is proprietary and closed, free only for non-commercial use. GPLv3 cannot link against it and be distributed, whatever its quality. |
| MuseScore | Not separable | **It is FluidSynth.** MuseScore's SF2 engine is a modified FluidSynth; its own Zerberus synth is SFZ-only and was removed in MuseScore 4. A second vendoring precedent rather than a second option. |
| SynthFont2 / VSTSynthFont | No | Closed source, Windows only. Out twice over. |
| **FluidLite** | Yes | **The real alternative, and possibly the better one.** |

FluidLite is a stripped fork of FluidSynth built to have no external
dependencies at all -- standard C only -- and to keep just the settings and
synth. It deliberately omits MIDI file reading, realtime MIDI and audio output,
which is precisely the surface we do not want, because the conductor drives the
notes and JUCE takes the audio. LGPL-2-or-later, so the licence reasoning below
is unchanged. There is no glib question because there was never a glib.

Two things to establish before preferring it. It is derived from FluidSynth
**1.x**, and GeneralUser GS wants 1.0.9 or later, so it is nominally in range --
but whether the fork kept full modulator support is a question to answer by
RENDERING something and listening, not by reading a README. And it is less
actively maintained than mainline, across several forks (divideconcept, katyo,
batlogic), which is a real cost against a build that is otherwise much simpler.

So: FluidLite first if it renders the bank correctly, mainline FluidSynth as the
known-good fallback. Both are the same licence and the same reasoning.

**The listening test above has already been run, in another Chalkwalk project,
and FluidLite won.** `seq_play` ships `src/tonecore/ToneEngine`: one
multi-timbral FluidLite instance against this same GeneralUser GS bank,
JUCE-free, no external dependencies, voices pre-allocated and program changes
resolved through a preset cache so nothing allocates once it is running. It
renders the modulator-dependent presets correctly, which is the question the
first checkbox below was written to answer. That does not close the checkbox
here -- what a bank sounds like under a drum machine is not what it sounds like
under this band, and the decision still wants one voice heard next to the model
it would replace -- but it removes the risk that the fork is unusable, and it
means the SF3 loop-end patch and the re-attack scan are carried rather than
rediscovered.

**The ecosystem question is DECIDED, and the answer is smaller than the
question assumed: what is shared is the VENDORING, not an engine.** The shape
of it -- which repository, under whose name, and on what schedule -- lives in
the ecosystem plan kept outside this repository, not in this file and not in
`seq_play`'s. What is recorded here is the reasoning, because it corrects an
earlier entry in this section, and what it means for Antiphon.

The first answer was "extract `ToneEngine` as a shared library", by analogy
with `libs/dsp` and `libs/music`. Reading the code rather than the description
of it says otherwise, on three counts:

- **FluidLite is nearly all of it.** `ToneEngine` is 626 lines -- 370 in the
  `.cpp`, 141 in the header, 115 in `tone_preset_swap` -- against a vendored
  synth with libvorbis inside it. The interesting part was never the wrapper.
- **The wrapper is Lockstep-shaped, and says so.** Its `kNumChannels = 16` is
  documented as load-bearing: it is that project's track count, its MIDI
  channel count and its audio-group count at once, and "the track index IS the
  channel is the whole design". Antiphon has bots and not tracks, and no
  16-way identity to spend.
- **A fifth of it solves a problem this project does not have.**
  `tone_preset_swap` exists so a program change does not allocate on the audio
  thread. The band renders on the conductor thread against a four-second
  deadline, so `PRINCIPLES §7` is not engaged here at all -- which is stated
  further down this section as the reason the whole feature is cheap.

**`PRINCIPLES §8` is satisfied by the vendoring rather than by the library**, and
the principle's own wording is what distinguishes them: a module named for a
RULE rather than a component. The rule with two consumers is *how the SF3 loop
bound is read, and against which FluidLite revision* -- and today it has two
homes, or will the moment a second project applies the patch. The wrapper is a
component, shaped by each consumer's threading model, and two wrappers over one
correctly-patched synth have nothing to drift about because the only thing that
could drift is the patch. `ChannelMix` exists because "mono" had quietly
acquired three meanings; there is no equivalent here.

**It exists now**, as
[chalkwalk-soundfont](https://github.com/chalkwalk/chalkwalk-soundfont),
LGPL-2.1-or-later, green on Linux, macOS and Windows, and Lockstep is ported
onto it. Consume it with `add_subdirectory` and link `chalkwalk::soundfont`;
`CHALKWALK_SOUNDFONT_DIR` overrides the submodule the same way the other
libraries do. It holds four things and no C++ adapter:

- the **FluidLite submodule pin** (`divideconcept/FluidLite`, `ignore = dirty`,
  patched at configure time), so both consumers build the same revision rather
  than drifting apart;
- **`fluidlite-sf3-loop-offbyone.patch`**, one character of diff and sixty
  lines of writeup, of which the writeup is the valuable half -- every symptom
  of this bug points away from the loader, which is why it is worth exactly one
  home;
- the **re-attack regression test** -- a held C4 on program 0, rendered long
  and scanned for a rise in a decaying envelope. It is the only thing that
  catches this class of fault and it must not be written twice. It runs in the
  CONSUMER's ctest as well as its own, so a project verifies the patch in the
  build it actually made; and that library's CI reverses the patch and requires
  the test to fail, because an assertion that would hold either way is not one;
- **`trim_soundfont.py`**, which is bank-shaped rather than engine-shaped and
  depends on neither consumer's architecture. It has LEFT `scripts/` here.

Antiphon then writes its own thin engine over it -- load, note on and off,
program select, render -- which is a smaller job than seq_play's for exactly
the reasons above.

**Three things recorded here were wrong, and porting Lockstep found them.**
Corrected rather than quietly fixed, because each was believed on the strength
of this file:

- **SF3 is built against the vendored `stb_vorbis`, not Xiph.** The argument
  below -- that the feature is nearly free because this repository already
  vendors libogg and libvorbis for the Ninjam codec -- is true of an option
  nobody takes. stb is one vendored header, and the real reason is not the
  dependency count: FluidLite's Xiph path in `fluid_defsfont.c` decodes through
  a file-static `vorbisData` struct and is **not reentrant**, while
  `stb_vorbis_decode_memory` is stateless. So the hazard does not exist on this
  path rather than being avoided by care.
- **The re-attack count is five, not four.** Re-measured against both the full
  bank and the trimmed fixture, on this machine and on a clean CI runner: five
  rises, worst +1.65 dB. The amplitude matches what was recorded; the count does
  not. Nothing depends on it -- the assertion is `== 0` -- but a number in this
  file should be the number that was measured.
- **`sf3convert` needs `-z`.** The invocation quoted below and in the trimmer's
  own docstring omits it, exits 2, and compresses nothing.

**The bank does not go with the vendoring either, and that is unchanged by the
reframing above.** Three things already measured below decide it. The trim is provably LOSSLESS -- bit-identical FluidSynth output, not
"sounds the same" -- so a per-consumer bank costs nothing in fidelity. The size
is essentially all acoustic multisampling, so dropping the synths and effects
saves 11% and "everything" is not a small delta over "the useful part": 10.07 MB
against 6.16 for the band set. And the two consumers want DIFFERENT sets --
Antiphon drops the electronic and 808/909 kits because the modelled kit does
that job better, and drops Room because `BotDsp::Room` already puts the kit in a
room, which is close to the opposite of what a drum machine wants. A union bank
would be the largest of the three and would serve neither.

So the shared repository ships the TRIMMER and not the bytes -- the same move
that took `make_wordlist.py` into chalkwalk-jambot, for the same reason: the
tool belongs with the thing it feeds. Each consumer declares its own preset set
and the hash of the result, and its CI builds that bank at package time from
the upstream SF2. One patched synth, one trimmer, N banks, nothing committed.
Antiphon's set is **band + 5 acoustic kits**, for the reasons argued below.

That also keeps the provenance caveat where it belongs: a disclosure each
consumer weighs for itself, rather than one baked into a library every later
project links.

**The libogg/libvorbis split is MOOT under this shape, and the note is kept
only so it is not re-raised.** Against a shared library it was a real question:
SF3 needs the codecs, this repository vendors both for the Ninjam codec, and
another consumer might not -- which is the `Loudness.h` situation exactly. With
no shared C++ target there is nothing to split. FluidLite carries its own
vendored libvorbis and each consumer resolves the guard its own way, the same
way `libs/ninjam` already handles whichever project adds ogg and vorbis first.

**None of this blocks anything here.** The first checkbox below is loading an
SF2 from a path the player chooses, with no bundled bank and so no packaging or
provenance question at all, and the listening test that decides whether a
sampled voice earns its place comes before any of the above matters.

**It also gives the channel name something to say.** *The band's two names*
puts the instrument in the channel name and can change it mid-session; a
General MIDI program is a vocabulary of instrument names far larger than the
three the lead currently has, and the two features are each other's payoff. The
naming work does not wait for this -- it ships on the models -- but a bank is
what makes a rename worth doing more than once.

**FluidLite has a known SF3 loop-point bug, and the fix is a `+ 1`.**
Already found and patched against this same GeneralUser GS bank
(`fluidlite-sf3-loop-offbyone.patch`). Written down here so nobody
rediscovers it, because every symptom points away from the loader.

*Symptom.* Sustained piano notes repeat every ~2 s, quietly, like a delay with
very low feedback: the whole sample loops instead of its sustain loop. It hits
some patches and not others -- Grand Piano and Bright yes, E.Grand and E.Piano
no -- so it reads as a bad patch, or as a bad SF2 -> SF3 conversion. It is
neither, and both were ruled out by controls: mainline fluidsynth 2.4.8 renders
the same SF3 clean, and the source SF2 clean.

*Cause*, read at `divideconcept/FluidLite` 4a01cf1, which is the revision every
line cited here was checked against. In `fluid_defsfont_get_sample`, in the SF3
branch only, an Ogg sample is decoded and then `sample->end = sampleframes - 1`
-- the LAST VALID INDEX.
But `loopend` per the SoundFont spec is the first sample AFTER the loop, an
EXCLUSIVE bound. FluidLite knows that; `fluid_voice.c:1795` says so in as many
words (*"'end' is last valid sample, loopend can be + 1"*). The validity check
three lines below the decode compares the exclusive bound against the inclusive
index:

```c
if (sample->loopend > sample->end || ...)
```

so every sample whose loop runs to the very end -- `loopend == end + 1`, which
is legal and common -- is judged "fowled" and repaired to `loopstart = start +
8; loopend = end - 8`, which loops the entire sample. Most of GeneralUser's
Grand Piano samples loop to the end; the E.Piano samples loop well short of it,
which is exactly the split observed. **SF2 never reaches this code**, so the bug
is confined to the format the size table below otherwise argues for.

*Why it survived.* The SF2 path in the same file was rewritten, carefully, and
the SF3 branch was not. `fixup_sample` now derives three named predicates and
its comment says what the thinking was -- *"hours of thinking through this have
concluded, that it would be best practice to mangle with loops as little as
necessary by only making sure loopend is within sdtachunk_size"* -- so on that
path `loopend > end` merely WARNS and uses the value anyway, and only a loop
outside the sample data chunk is repaired at all. The SF3 branch is the old
mangle-it-into-range code, still reachable because compressed samples are
explicitly skipped by the careful path (*"compressed samples get fixed up after
decompression"*) and then fixed up by the branch nobody revisited. That is the
real shape of this: not an off-by-one somebody typed, but two loop checks in
one file that no longer agree.

*Fix.* `sample->loopend > sample->end + 1`, applied as a patch at configure time
rather than a fork -- the same mechanism `patches/` already uses here.

*Measured*, by holding a C4 on program 0, rendering 14 s and scanning the
decaying envelope for re-attacks (a monotonic decay has none): **5 re-attacks at
+1.65 dB before, 0 after**, against 0 for both controls. (Recorded here as four
until it was re-measured for the shared repository's test, on this machine and
on a clean CI runner. The amplitude was right; the count was not.)

One thing deliberately left unverified, and the comparison above says more
about it than the first reading did: the third clause of the same check,
`loopstart <= sample->start`. By then `loopstart` has been rebased to an offset
from `start` and `start` is 0, so a loop beginning at frame 0 would also be
"repaired". `fixup_sample` tests the same thing as `loopstart < start` --
strictly less than -- so this is not a suspicion about which bound is right, it
is the same two-checks-disagreeing split as the loopend clause, and the answer
is already in the file. It stays out of the patch because nothing in this bank
appears to loop from frame 0, so it was never measured, and a fix nobody can
demonstrate is not a fix (`PRINCIPLES §5`).

**Licensing is a non-issue, which is not obvious.** FluidSynth is
LGPL-2.1-or-later, and LGPL's static-linking condition is that the user must be
able to relink against a modified library. Antiphon is GPLv3, so the entire
source is published anyway and the condition is satisfied by construction.
Nothing extra to do beyond a `THIRDPARTY.md` entry.

**Real-time safety is a non-issue too, and only for this use.** The band renders
on the conductor thread, one interval at a time -- about half a second of work
against a four-second deadline -- so FluidSynth may allocate and lock as much as
it likes. `PRINCIPLES §7` is not engaged at all. This would be a completely
different proposition for a sampled instrument on the audio thread, and that
difference is the whole reason this is cheap.

**One synth, not four.** Each `fluid_synth_t` loads its own copy of the sample
data, so a synth per bot is four copies of a thirty-megabyte bank in memory. One
synth with a MIDI channel per voice, rendered a voice at a time, keeps it to
one -- and the bots already render serially on a single conductor thread, so the
sharing costs no synchronisation.

**On bundling: an earlier note in this file called the provenance caveat
"decisive", and that was overstated.** The facts: the GeneralUser GS v2.0
licence explicitly permits use and modification in software projects; the
caveat is a DISCLOSURE by the author that he cannot account for every sample's
origin, aimed at people shipping commercial products; and several Linux
distributions package and redistribute it regardless. For a GPLv3 project this
is a judgement rather than a bar, and the honest reading is that bundling is
defensible with a residual risk that is disclosed, accepted by others, and
cheap to remedy.

**SF3 changes the weight question, and costs us nothing to support.** SoundFont
3 is the same format with the samples Ogg Vorbis compressed -- an extension
Werner Schweer created for MuseScore for exactly this reason. The decompression
is free to us -- though not by the route this paragraph originally claimed.
FluidLite CAN build SF3 against Xiph's libogg and libvorbis, which this
repository already vendors for the Ninjam codec, and that is what made the
feature look cheap. The path actually taken is the vendored `stb_vorbis`: one
header, no external dependency at all, and reentrant where the Xiph path is not
(see the corrections above). Either way the whole feature adds no new
third-party code.

**Measured, by converting the bank at every quality setting:**

| quality | size | of SF2 | marginal cost per 0.1 step |
|---|---|---|---|
| 0.1 | 5.85 MB | 19.0% | -- |
| 0.3 | 6.74 MB | 21.9% | +436 KB |
| 0.5 | 8.00 MB | 26.0% | +760 KB |
| **0.8** | **10.07 MB** | **32.7%** | +856 KB |
| 0.9 | 11.34 MB | 36.8% | +1304 KB |
| 1.0 | 13.38 MB | 43.4% | +2092 KB |
| SF2 | 30.82 MB | 100% | -- |

Two things fall out of that curve. **The knee is at 0.8**, which is also where
the conversion guidance sits for quality reasons -- below it each step costs
about 550 KB and above it about 1400, nearly twice as steep, so the last fifth
of the quality range buys the least and costs the most. And **even the top
setting is 2.3x smaller than the SF2**, so there is no configuration in which
shipping the uncompressed bank makes sense.

**At 10 MB the weight objection largely dissolves**, which is a change from the
position recorded above against 30. It has to be a data file rather than JUCE
binary data -- embedded it would be 40 MB across four plugin formats, and in git
it would be permanent -- but 10 MB fetched at package time and verified by hash
is unremarkable.

**The better question these numbers raise is why ship 128 instruments at all,
and the answer has now been measured rather than guessed.**
`scripts/trim_soundfont.py` keeps a chosen set of presets and drops the rest,
following the preset-bag-generator-instrument-sample chains outward and
renumbering every one of them.

The obvious guess about what that saves is WRONG, and worth recording. Dropping
264 of GeneralUser GS's 287 presets -- 92% of them -- removes only 59% of the
bytes. The sound effects are cheap, a fraction of a second each; the expensive
presets are exactly the ones worth keeping, because a convincing piano or string
section is many megabytes of multisampling. A quarter of the presets gives about
40% of the size, not 25%.

It compounds with SF3 though, and that is where it pays:

| set | presets | SF2 | SF3 at q0.8 |
|---|---|---|---|
| minimal | 8 | 7.16 MB | 1.90 MB |
| core | 23 | 12.42 MB | 3.55 MB |
| core + 8 kits | 31 | 16.53 MB | 4.82 MB |
| **band + 5 acoustic kits** | **43** | **20.33 MB** | **6.16 MB** |
| everything but synths and effects | 99 | 27.63 MB | 8.57 MB |
| the whole bank | 287 | 30.82 MB | 10.07 MB |

`core` is what a physical model will never do well: piano, vibes and marimba,
two organs, nylon and steel guitar, violin, cello, pizzicato, string ensemble,
choir, four brass, three saxes, oboe, clarinet, flute. Everything the band
already plays is left out, because modelling those is better.

**The drum kits are the bargain, and the arithmetic is not obvious.** Each is
about 2.7 MB alone, but they share almost everything -- the GS kits are largely
one set of samples remapped with a few kit-specific pieces -- so the first costs
2.69 MB and the other seven cost 1.38 MB between them.

Worth taking for a musical reason too. The modelled kit has three pieces; each
sampled kit has 65 samples, including five toms, ride, ride bell, crash, splash,
china, cowbell, tambourine, claves, congas, bongos, timbales, agogo, guiro,
cabasa, shaker and woodblock. None of that is a physical model anybody here is
going to write, and `ROADMAP` already carries "multi-tap clap, cowbell, rimshot
and toms" as deferred work. Percussion is also the best case for Ogg, since a
one-shot is never looped and loop artifacts are the whole risk.

The five kept are the acoustic ones. **Electronic and 808/909 are dropped
because the modelled kit already is a synthesised one**, and does that job
better: it varies continuously with velocity and never repeats, which is exactly
what a drum-machine sample cannot do. **Room is dropped because the kit is
already put in a room of our own** (`BotDsp::Room`), and baking a second one
into the samples would be two rooms.

That last point generalises: the sampled kits are a palette to extend the
modelled kit with -- toms, cymbals, hand percussion, colour -- not a replacement
for its kick, snare and hat. A machine-gunned snare is the classic sampler
failure and it is most audible on the thing you hear every bar.

**And the intuition about dropping the synthesisers is the wrong one, which is
worth knowing before anybody acts on it.** Cutting the synths and the sound
effects -- the obvious first move -- saves 11% of the bytes, because they are
short and thin. Every megabyte is in acoustic multisampling, which is precisely
what any of these sets is keeping. So the choice is not "what do we throw away"
but "how much acoustic material do we want", and the honest range is 6 MB for
the band's own palette against 8.6 MB for everything acoustic in the bank.

**The trim is provably lossless.** Rendering the same MIDI through the full bank
and through each trimmed one gives BIT-IDENTICAL output from FluidSynth -- not
"sounds the same" or "measures the same", but byte for byte. The only lossy step
is the Ogg conversion afterwards, whose error at q0.8 measures 27.8 dB below the
signal.

At 3.55 MB the bundling argument is over: that is a tenth of the original, it is
smaller than the fonts already embedded in the plugin, and it makes the
committed-versus-fetched question uninteresting. What remains is only whether a
sampled voice earns its place at all, which is a listening question and still
first in the order below.

The catch is quality rather than size, and it lands unevenly across exactly the
instruments we want. Lossy compression shows on short LOOPED samples, so a
sustained string or organ tone is the risk and a piano -- one-shot, long, never
looped -- is not. Since the wanted set includes both, the setting cannot be
chosen from the size table alone.

It can be chosen by measurement, with what is already here: render the same part
through the SF2 and through each SF3, and compare with `AudioMeasure` and by
ear, which is the loop the voice lab exists for. `antiphon-voicelab file a.wav
b.wav --lufs` already does the level-matched A/B.

So the bundling decision is worth reopening once a voice exists to judge, rather
than settled now. What follows is the argument as it stands against the
uncompressed bank; halve or quarter every number for SF3.

What actually argues against bundling is weight, not licence:

- Thirty megabytes as JUCE binary data, in four plugin formats, is roughly a
  hundred and twenty megabytes installed and a generated source file nobody
  wants to compile.
- In git it is permanent: every clone pays for it forever, in a project whose
  stated ambition is to fit in your head.

So if it is ever bundled, it is as a **data file fetched at package time by CI
and verified by hash**, installed once and found at runtime -- never committed
and never embedded. An in-app opt-in download is the third option and the most
expensive: HTTPS in a plugin that currently speaks only Ninjam, a progress and
error surface that has to be announced for a screen reader, an integrity check,
and a hosting commitment that outlives our interest in it.

**The order below defers every one of those questions.** Nothing about bundling
has to be decided until a single sampled voice has been heard next to the model
it would replace, at which point we will know whether it is worth paying for.

The musical caveat from the first draft stands unchanged: a sample is the same
recording every time, repetition is this band's specific enemy, and a General
MIDI bank has one or two velocity layers, so velocity moves volume and a filter
rather than articulation. Samples lose for everything the band currently plays
and win for what we will never model -- an acoustic piano, a brass section,
bowed strings, reeds.

- [x] **Decide between FluidLite and mainline FluidSynth -- FluidLite, and it is
      already carried.** Settled by `chalkwalk-soundfont`, which pins it,
      patches it and renders the modulator-dependent presets correctly, with
      Lockstep running on it. Submodule and not a fork, as this asked; SF3
      builds against the vendored `stb_vorbis` rather than the Xiph path named
      here originally, which is not reentrant (see the corrections above).

      This settles the ENGINE only. Whether a sampled voice earns its place in
      THIS band is a different question and is still open, two boxes down --
      what a bank sounds like under a drum machine is not what it sounds like
      here. `THIRDPARTY.md` still wants an entry, and it wants one for
      chalkwalk-soundfont rather than for FluidLite directly.
- [ ] **If FluidLite wins: take the patch from the shared vendoring, not a copy
      of it** -- `fluidlite-sf3-loop-offbyone.patch`, written up above, and one
      character. That repository is the one home for it and for the re-attack
      scan, which comes as a test rather than a listening note: a held C4
      rendered long and scanned for a rise in a decaying envelope is a cheap
      assertion, and it is the only thing that catches this class of fault.
      Check upstream FluidLite first, in case the fix has landed and the patch
      can be dropped on both sides at once.
- [ ] Load an SF2 from a path the player chooses. No bundled bank, so no
      packaging or provenance question yet.
- [ ] One shared synth, a channel per voice, driven from the conductor thread.
- [ ] One voice at a time, selectable like the lead's instruments, so the
      comparison against the model is direct, and measured with `AudioMeasure`
      like everything else.
- [ ] Through the existing per-note tone, drift and saturation chain rather than
      straight out -- which is also what a real sampler does to stop notes
      machine-gunning.
- [ ] Layering -- a sampled attack over a modelled body -- once a single sampled
      voice has been lived with.
- [ ] Compare an SF3 conversion against the SF2 on the same part, measured, to
      see whether the compression is audible on looped samples.
- [ ] Only then, and only if it earned its place: whether to ship a bank, in
      which format, and fetched at package time rather than committed.

### Breaking the repository up  [done]

*(2026-08-18/19.)* Done, and it went the other way round from the analysis that
used to sit here: nothing was restructured around the practice room. What left
were four pieces of general-purpose code this repository happened to hold --
music theory, DSP primitives, the wire protocol and the loudness meter -- each
now an MIT, strictly JUCE-free library that builds and tests standalone, and
each consumed here as a submodule under `libs/`.

The long analysis that reached that decision covered more than this project, so
it is not kept here; the reasoning and the standing argument live in the
ecosystem plan. Two things that analysis had wrong are worth recording, since
they are what changed the answer: the shared libraries are strictly JUCE-free,
so the `juce::String` dependency in the music layer went away and with it the
objection that every layer needs JUCE; and the Scala tuning parser it missed
entirely is a first-class part of `chalkwalk-music`.

- [ ] Correct the line count in `AGENTS.md`, which is out by a factor of three.

### A responsive jamming partner

Sketched in `libs/jambot/docs/BOT-CHAT.md` section 14, and not scheduled. A bot receives a
whole interval at once and composes a whole interval at once, so it holds your
complete phrase -- ending and all -- at the moment a human listener has heard
only its first beat, and it answers into the same slot they would. It can
therefore be more responsive than a player in the room, while staying entirely
inside the form.

- [ ] Decide whether this is wanted at all before building any of it.
- [ ] Analysis as a bias on the existing generator rather than a replacement, so
      that with no analysis the band plays exactly as it does now.
- [ ] Rhythm and density before pitch: much cheaper, and most of the effect.
      Key detection from audio is its own project and is not this.

### Split the client out

> **Done, 2026-08-19, as the protocol rather than the client.**
> [`chalkwalk-ninjam`](https://github.com/chalkwalk/chalkwalk-ninjam) is a
> submodule at `libs/ninjam` and carries `NinjamProtocol`, `VorbisCodec`,
> `Sha1`, `IntervalClock`, `SpscRing` and `ChannelMix` under MIT, with the
> provenance note `PRINCIPLES §6` required.
>
> The survey changed the unit. This entry proposed moving the *client*, but
> `NinjamClient` carries `juce::File`, `juce::AudioBuffer` and forty-odd locks
> -- host concerns a protocol library has no business owning. The protocol
> underneath it was already JUCE-free in five files of six, and is the part
> nothing else on the shelf provides. So the client stayed and the wire format
> left.
>
> Five of the six moved as a using-declaration each: their APIs did not change,
> so not one call site here did either. `NinjamProtocol` did change --
> `juce::MemoryBlock` became `ByteBuffer` and `juce::String` became
> `std::string` -- and cost about a hundred small conversions across
> `NinjamClient`, `PracticeServer`, `FakeNinjamServer` and two test files.
> `juce::String` constructs implicitly from `std::string`, so parsed fields
> still flow into the UI untouched; the other direction is an explicit
> `.toStdString()` at each site, which is where the boundary now shows.
>
> The extraction paid for itself immediately: linking against the library
> aborted the handshake, and the cause was three `juce::jlimit(lo, hi, value)`
> calls transcribed as `std::clamp(lo, hi, value)` during the port. Fixed and
> covered in the library, where neither builder had had a test at all.


`NinjamClient`, `NinjamProtocol`, `VorbisCodec`, `Harmony` and the bots have no
dependency on the plugin -- `tools/StemsMain.cpp` and the wanted
`tools/BotMain.cpp` already prove it. Making them their own repository, consumed
here as a submodule with the bots a submodule of that, would let a bot travel
with the client rather than with the plugin.

It is a packaging decision rather than a code one, and it costs a repository
boundary in exchange for reuse nobody has asked for yet. Written down because
the thought recurs, not because it is scheduled.

**Superseded in detail by *Breaking the repository up*,** which measured the
dependency direction rather than assuming it and found four layers where this
entry assumes one boundary. Kept as the shorter statement of the same recurring
thought; decide both together or not at all.

- [ ] Decide, and if the answer is no, move this to `NON-GOALS.md` with the
      reason.

---

## Release engineering

### The GitHub move

The project is going public as an open-source repository.

- [x] `LICENSE` -- GPLv3. `THIRDPARTY.md` records the component licences that
      constrain this, and why GPLv3 is the only real option under JUCE.
- [x] `CONTRIBUTING.md`.
- [x] Issue templates (bug, accessibility, feature) and a PR template.
- [x] Remove the reference sources from the published history entirely, rather
      than only from the tip. See `docs/COMPLETED.md`.
- [x] Confirm the README quick start works from a genuinely fresh clone,
      including submodule init.
- [x] A documentation site at `antiphon.chalkwalkmusic.com`, and a wiki synced
      from it.
- [x] Repo description, homepage and topics -- set on GitHub, not in the tree.
- [x] `antiphon.chalkwalkmusic.com` pointed at GitHub Pages, deploying from
      Actions. Live and serving over HTTPS.
- [x] Wiki created and syncing. GitHub does not create the backing `.wiki.git`
      repository until a first page is saved by hand in the web UI, so that had
      to happen once before `wiki-sync.yml` had anywhere to push.
      `wiki-sync.yml` detects the missing-wiki case and prints the instruction
      rather than failing, so if this is ever set up again the symptom is an
      empty wiki rather than a red run.
- [x] A logo, with the favicon, navbar mark, README mark and social card built
      from it. **The logo is a placeholder** -- good enough to ship, not final.
      Every derivative is produced by `scripts/make_logo_assets.py <source.png>`,
      so replacing the logo is one command rather than a hunt through the tree.
- [ ] Replace the placeholder logo with the final mark. Check the 16px and 32px
      cuts specifically: the current one is a three-lobed knot and it gets busy
      at favicon size.
- [ ] Fresh screenshots for the guide. Every page currently leans on the one
      README screenshot.

### Cross-platform builds

Development has been on Linux. This was assumed to be the single biggest gap
between "works" and "released"; the first CI run on all three platforms says
otherwise, and the measured result is below.

It remains disproportionately important for accessibility: JUCE has
screen-reader backends on macOS and Windows and **none** on Linux, so that work
is still unexercised where it counts.

- [x] CI that attempts all three platforms. macOS and Windows are marked
      `continue-on-error` so the gap is visible without blocking; that comes off
      as each one goes green.

**First measured result, 2026-08-10 -- and it is much better than expected.**
The paragraph above was written when none of this had ever been compiled
elsewhere. What CI actually found:

- **All three platforms build, and all three pass the entire unit suite** --
  protocol, codec, interval clock, mixing, loopback, the lot. macOS and Windows
  needed **no source changes whatsoever**. Whatever cross-platform work remains,
  "does it compile and is the audio logic correct" is not it.
- **The one failure everywhere is `accessibility-audit`**, and it is an
  environment limit rather than a finding about the code. It drives a real JUCE
  component tree: on macOS and Windows runners there is no window session for
  the peer to attach to and it segfaults. Excluded on those two in CI.
- **Windows initially failed to configure at all**, which was a defect in the
  workflow, not the project: `-G Ninja` made CMake take MinGW g++ off the
  runner's PATH, and JUCE rejects MinGW outright. Dropping the generator flag on
  Windows gets Visual Studio and MSVC, which is the configuration that avoids
  it. MSVC 19.51 then compiled the tree without complaint.

- [ ] Confirm what the plugin does once *loaded* on macOS and Windows. Building
      and passing headless tests is a long way from a host instantiating it.
      **macOS: done once, by a contributor, via the AU -- see below. Windows is
      still untouched**: nothing there has opened a window, opened a device, or
      joined a jam.
- [x] macOS: decide whether AU is in scope. **It is, and it is built** --
      `FORMATS` gains AU under `if(APPLE)`. Logic Pro and GarageBand load no
      other format, so without it macOS support means "every DAW except the two
      most common ones".
- [x] Confirm the AU actually loads. **It does.** A contributor built it on
      their own Mac, loaded it in a host, joined a jam and used it with a screen
      reader -- the whole path, not just instantiation. That retires the "the
      format is a claim, not a fact" caveat this line used to carry.
- [ ] **Keep it that way: the AU is not regularly tested.** One report from one
      machine, at one point in the history, by hand. Development is on Linux, CI
      only compiles the AU, and nothing automated instantiates it anywhere -- so
      the next AU regression will be found by a person or not at all. What would
      change that, cheapest first: `auval` in the macOS CI job, which validates
      an AU without a DAW and needs no window session; then a named macOS
      smoke-test pass before each release. Until one of those exists, treat
      "the AU works" as true-as-of-a-date rather than as a standing guarantee,
      and re-check it by hand after anything touching buses, the editor or
      startup.
- [ ] AU is one stereo bus in, one stereo out, deliberately. JUCE's AU wrapper
      drops the `busLayoutChanged` notification our patch adds
      (`DESIGN.md` §"AU is one bus in, one bus out"), so the bus controls are
      disabled there and say why. If a route to per-player stems under AU is
      ever wanted it means patching the AU wrapper to raise
      `kAudioUnitProperty_ElementCount`, and then finding out whether Logic
      honours it for an effect -- which it may well not. Not planned.
- [ ] The audit does not cover the AU-disabled bus state. `AntiphonAudit` builds
      its own editor, where `wrapperType` is never `wrapperType_AudioUnit`, so
      the branch that disables the four bus buttons is unreachable from the
      gate. Every other disabled-control state is audited; this one is not.
- [ ] Run the accessibility audit on macOS and Windows. It needs a runner with a
      real window session, or an audit path that does not realise peers. This
      matters more than it looks: those are the two platforms where the
      screen-reader work is reachable at all, so leaving them unaudited defeats
      the point of the gate.
- [ ] Drop `continue-on-error` per platform once each is trusted, so the badge
      stops claiming more than it knows.
- [ ] Packaged artefacts are uploaded per platform by CI; check that a VST3 and
      a CLAP built this way actually load in a host on each.

### Packaging

- [ ] Installers or archives per platform, with the plugin landing where hosts
      look for it.
- [ ] Install instructions in the README for people who do not build from source.
- [ ] Decide on a release/versioning scheme.

### AntiphonAudit hangs in teardown, on CI only

**Root cause found, not yet fixed. `main` is red on Linux because of it.** The
accessibility audit -- the gate that fails when a control arrives unnamed --
wedges in teardown on GitHub's ubuntu runners. It has never once hung on the
development machine: 30 consecutive runs clean, plus deliberate attempts with
`ALSA_CONFIG_PATH=/dev/null` and with no `XDG_RUNTIME_DIR`, `HOME` or `DISPLAY`.

How often it hangs depends on how it is invoked, which is itself a clue:

| Invocation | Result |
|---|---|
| Under `ctest`, on a runner | Hung every time -- five separate runs, including three consecutive retries |
| Standalone, on a runner | Hung once, completed once |
| Anything, locally | Never hung, in hundreds of runs |

The stack, from a runner caught mid-hang:

```
Thread 1 (main):
#0  pthread_cond_destroy ()
#1  juce::WaitableEvent::~WaitableEvent (juce_WaitableEvent.h:47)
#2  juce::Thread::~Thread (juce_Thread.cpp:58)
#3  FakeNinjamServer::~FakeNinjamServer (FakeNinjamServer.cpp)
#4  main (AuditMain.cpp)
```

`juce::Thread` clears its handle from *inside* the exiting thread
(`threadEntryPoint` calls `closeThreadHandle()` on its way out), so
`stopThread()` can report success while that thread is still unwinding.
`~Thread()` then destroys its `WaitableEvent` members underneath it, and glibc
2.39 -- the runner's -- blocks in `pthread_cond_destroy`. The development
machine runs glibc 2.43, which is why it never reproduced locally.

The audit's own work is unaffected: when it completes it reports `0 finding(s)
across 6 state(s)`. This is purely a teardown race.

**Two fixes were tried and both reverted, recorded so they are not retried
blind:**

- *Interruptible accept.* `run()` polled the listener instead of blocking in
  `waitForNextConnection()`. A genuine improvement and it was kept, but it is
  not this bug: the "thread did not exit within 2000ms" warning added alongside
  it never fired, proving the thread had already exited cleanly.
- *`std::thread` instead of `juce::Thread`,* so that `join()` gives a real
  join. Correct in principle -- it removes the mechanism entirely -- but it
  segfaulted the audit 12 times out of 12 locally, and chasing that was a
  bigger job than the symptom warranted. Reverted.
- *Leaking the server* to skip the destructor. Segfaulted about a third of the
  time locally. Reverted.

**A retry mitigation was tried and removed.** `ctest --repeat after-timeout:3`
retries a timeout and nothing else, so it would not have softened the gate -- an
unnamed control exits with the finding count, a failure rather than a timeout,
and still fails first time. That part was verified against a purpose-built ctest
project. It was removed anyway because it does not work: all three attempts
timed out, and six minutes of CI bought nothing. Retrying only helps something
that is actually intermittent, and under ctest this is not.

- [ ] Fix the race properly. The `std::thread` route is still the right shape;
      it needs the segfault it exposed understood first, which is a real bug in
      its own right and probably a latent one.
- [ ] Establish whether production is exposed. `NinjamClient` is also a
      `juce::Thread`, and it is destroyed when a host unloads the plugin --
      the same pattern, on a machine whose glibc we do not choose. Nothing has
      been observed, and nothing has been ruled out.
- [x] Ruled out the display. Running the whole suite under `xvfb-run`, so the
      audit had a real X server instead of no `DISPLAY` at all, produced a
      byte-identical hang at the same line. The environment difference that
      looked most promising is not the cause; glibc version remains the only
      one that correlates.
- [x] Ruled out ALSA, which was the original suspicion and the reason the 120s
      timeout exists. The `snd_seq_hw_open` failure in the log is the MIDI
      sequencer, is non-fatal, and the trace continues through two more states
      after it. At the hang only two threads exist -- main, and JUCE's timer
      thread -- so ALSA has left nothing running.
- [ ] Work out why `ctest` makes it near-certain when a bare run does not.
      Whatever differs -- pipes rather than a terminal, process group,
      environment -- is likely the same thing that decides the race.
- [x] Decided what `main` does meanwhile: **the audit is excluded from CI on
      every platform**, and `CONTRIBUTING.md` says so in terms nobody can miss,
      because a green tick now says nothing about accessibility. It remains a
      hard gate locally, where it is reliable and takes two seconds. Chosen over
      leaving `main` permanently red, which teaches people to ignore CI --
      but it is a debt, and the exclusion comes off with the fix.

### Clear the clang-tidy backlog

`clang-format` is enforced. `clang-tidy` is configured but **advisory**: it runs
in CI without failing the build, because adopting it on an existing codebase
produced a backlog, and a gate with a remembered exception is one nobody reads
carefully (the same argument as the sanitiser baseline).

Measured at adoption, over the 33 first-party translation units: **142
findings.** That is after switching off six checks that are stylistic preference
fighting JUCE and audio idiom -- the raw number was 756, of which 261 were
`modernize-use-nodiscard` and 122 `modernize-avoid-c-arrays`. The reasoning is
in `.clang-tidy`; the point of disabling them was to leave a backlog worth
clearing rather than a number worth flattering.

The three most alarming-sounding findings were checked at adoption and are all
false positives, recorded here so they are not re-investigated: the
`bugprone-signed-char-misuse` in `NinjamProtocol.cpp` is a `juce::int8` field,
which is exactly the right type for a signed protocol byte; the
`bugprone-incorrect-roundings` in `HostGrid.h` cannot see a negative value
because the loop starts at `ceil(ppqStart)`; the `dangling-else` in
`RemoteChannelRow.cpp` binds as the indentation says it does.

- [ ] Work down the 142, largest categories first: 21 `bugprone-narrowing-conversions`
      (worth real attention -- this is sample arithmetic), 18 `readability-use-std-min-max`,
      16 `readability-isolate-declaration`, 12 `readability-avoid-const-params-in-decls`.
- [ ] Brace the `dangling-else` anyway. It is not a bug and it is one line.
- [ ] When the count reaches zero, set `WarningsAsErrors: '*'` in `.clang-tidy`
      and drop `continue-on-error` from the CI step. Both halves in one commit.

### Documentation upkeep

- [x] A current screenshot for the README (`docs/images/antiphon.png`), also
      used as the site's hero image.
- [ ] Keep `test/README.md`'s suite list in step with `test/` -- it drifted once
      already.

---

## Parked

Not planned. Recorded here so the decision is visible rather than rediscovered;
the reasoning lives in `NON-GOALS.md`.

### Video

Jamtaba-proprietary `JTBv` extension. Would need `juce_video` for capture and
FFmpeg as a hard dependency, for a feature exactly one other client implements.
We already filter the fourCC, so a Jamtaba user in the room costs us nothing.
-> `NON-GOALS.md` fence #1, `DESIGN.md` §14.

### OSC tempo sync

Send `/tempo/raw {bpm}` to localhost when the server tempo changes, so the DAW
can follow. Works for one DAW in one configuration; a support burden disguised as
a feature. Reference implementation: `abNinjam`. -> `NON-GOALS.md` fence #7.

**Parked, not condemned.** This one is a fence about *mechanism*: if a
host-agnostic route to setting the DAW tempo appears, the fence comes down.
