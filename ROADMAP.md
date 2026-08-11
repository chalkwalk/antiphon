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

---

## Active focus

*(2026-08-08)*

The client works: it connects to real servers, transmits and receives audio in
time with other clients, and has been verified differentially against the
reference client at two tempos (`docs/PARITY.md`). The accessibility pass has
just landed -- every control is named, the header is readable, and the audit runs
headlessly.

Next, in order:

1. **Audio-thread hygiene** -- two known real-time violations, both easy, both
   the kind of thing that becomes a dropout report from a user we cannot debug
   for.
2. **The GitHub move** -- `LICENSE`, `CONTRIBUTING.md` and a README that stands
   on its own, since the project is going public.
3. **Cross-platform builds** -- everything so far is Linux and CLAP. The
   accessibility work in particular is *only* effective on the two platforms we
   have never built for.

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

### Practice echo

Shipped. See `DESIGN.md` section 6.2.

- [ ] v1 echoes one local channel. Summing several needs to know when the last
      channel's post for a boundary has arrived, which is fragile; deferred
      rather than guessed at.
- [ ] The echo history is rebuilt on a tempo or BPI change, since every stored
      interval becomes the wrong length. Currently that means switching practice
      off and on; doing it automatically, with an announcement, would be
      friendlier.

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

- [ ] Test with VoiceOver on macOS and NVDA on Windows -- the two platforms where
      JUCE actually has a backend.
- [ ] Get a session with a screen reader user, which is the only thing that
      answers the questions the audit cannot.
- [ ] Assess the standalone's audio-device picker (stock
      `AudioDeviceSelectorComponent`, never looked at).

### Chat history structure

Chat history is a read-only text editor. It is navigable, but there is no
per-message structure a reader can jump between, so finding "what did they say
three messages ago" means scanning character by character.

- [ ] Give messages individual structure a reader can navigate.

### Level check gesture

VU meters expose a value, but there is no way to hear levels without a reader
announcing them continuously -- which `PRINCIPLES §11` explicitly refuses. A
deliberate "read me the levels now" gesture is the missing half of that decision.

- [ ] A shortcut that speaks the current levels once, on demand.

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
  Windows gets Visual Studio and MSVC, which is what arps-euclidya does and why
  it never hit this. MSVC 19.51 then compiled the tree without complaint.

- [ ] Confirm what the plugin does once *loaded* on macOS and Windows. Building
      and passing headless tests is a long way from a host instantiating it:
      nothing has yet opened a window, opened a device, or joined a jam there.
- [x] macOS: decide whether AU is in scope. **It is, and it is built** --
      `FORMATS` gains AU under `if(APPLE)`. Logic Pro and GarageBand load no
      other format, so without it macOS support means "every DAW except the two
      most common ones".
- [ ] Confirm the AU actually loads. It has never been compiled: development is
      on Linux, so CI is the first machine to build it and no host has
      instantiated it. Until then the format is a claim, not a fact.
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
