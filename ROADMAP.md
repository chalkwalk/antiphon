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

- [ ] `getDecodedAudio` takes `downloadMutex` on the audio thread. The RX path is
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
      - [ ] The audio thread still takes the lock to traverse `channelStreams`
            and `remoteUsers`. Worst-case wait is now bounded and short, but a
            lock on the audio thread is still a lock. Needs an audio-thread-owned
            structure fed by a command queue, with deferred destruction so the
            audio thread never frees a buffer.
- [x] `setSaveTx` / `setSaveRx` are called from `processBlock` on **every block**
      and do file I/O on the toggling call. Move the toggle handling off the
      audio thread entirely; the audio thread should only ever see a flag.
      Now applied from the message thread when the toggle changes
      (`applyDebugCaptureSettings`); `processBlock` no longer mentions them.
- [ ] Re-audit the rest of `processBlock` for the same class of problem once
      those two are gone, and record the result so the list stays authoritative.

### Idle CPU

Profiled with `gprofng` on an idle, disconnected standalone (30 s, Xvfb, so pure
software rendering as on any Linux X11 build). CPU fell from **16.5 s to 4.2 s**
over the same window -- roughly 55% of a core down to 14% -- and painting fell
from 66% of all CPU to 19%.

Three causes, all the same shape: redrawing things that had not changed.

- [x] The 30 Hz tick called `repaint()` on the whole editor. Only the header
      animates; the strips already repaint their own meters. Now repaints the
      header band only.
- [x] The tick called `relayoutChannelArea()` every frame, and every `setBounds`
      it performed dirtied a component and bought another repaint. Now only when
      the set of strips changes; `resized()` still always relayouts.
- [x] Meter updates repainted the whole strip, redrawing the static dB scale --
      seven pieces of text per strip -- for a bar a few pixels wide. Now repaints
      the meter gutter only.
- [x] The header repainted even when nothing moved. Disconnected, the phase bar
      is frozen and the text fixed, so the tick now repaints only when connected,
      a flash is decaying, or the status text changed.
- [x] Meters repaint only when the drawn bar would land on different pixels
      (2% of its length; reaching silence always redraws, or a decaying bar
      settles short of empty). Tested in `GainUtilsTests`.
- [x] The phase bar steps in **sixteenth notes** rather than every frame --
      6.7-9.1 repaints/s against 30, and what other clients do. Beat flashes are
      sampled at those steps rather than driving repaints of their own, since
      they decay over ~0.4 s while beats arrive every 0.44-0.6 s and would
      otherwise keep the header redrawing most of the time.
- [ ] **Re-profile connected.** Every measurement so far is of an idle,
      disconnected window, where the phase bar is frozen and no meter moves --
      so the two changes above are largely unexercised by the 3.95 s figure.
      Connected is the case that matters and the one still unmeasured.
- [ ] Chat needed no work: its repaints are already event-driven, in
      `setChatConnectedState`, and a TextEditor redraws only when its content
      changes. Recorded so nobody re-investigates it.

### Underrun tail

When a decoded interval runs short, `getDecodedAudio` leaves the remainder of the
block silent. A hard cut to silence is the most audible possible failure mode.

- [ ] Hold the last sample or fade out over a few milliseconds instead of
      cutting.
- [ ] Decide and document what "short" means here -- a late interval and a
      genuinely short one are the same thing to the reader, but not to the user.
- [ ] Test in `AudioLoopbackTests.cpp` with a deliberately truncated interval.

### Client shutdown race

`~NinjamClient` closes the socket while `run()` may be blocked in `read()`.
Currently only visible as test-teardown flakiness, which is exactly how this
class of bug presents until it does not.

- [ ] Signal the thread and wait for it before touching the socket.
- [ ] Confirm the fix under TSan, which is the tool that can actually see this
      (`-fsanitize=thread`; it cannot be combined with ASan).

### Lock-free TX handoff

`processBlock` does a `callAsync` with a full buffer copy at each interval
boundary. A `juce::AbstractFifo` would remove the copy and the allocation and
reduce TX latency jitter. Much safer to attempt now the loopback tests exist.

- [ ] Replace the per-channel `callAsync` copy with a FIFO drained on the message
      thread.
- [ ] Verify transmitted interval length is unchanged against the reference
      client -- this touches the exact path `docs/PARITY.md` measures.

---

## Interoperability

Everything here comes from the "Not covered" section of `docs/PARITY.md`. These
are honest gaps in what we have proven, not suspected bugs.

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

- [ ] `LICENSE` -- GPLv3. (The product is intended to be GPLv3; `THIRDPARTY.md`
      records the component licences that constrain this.)
- [ ] `CONTRIBUTING.md`.
- [ ] Confirm the README quick start works from a genuinely fresh clone,
      including submodule init.
- [ ] Repo description, topics, issue templates.

### Cross-platform builds

Everything so far is Linux. This is the single biggest gap between "works" and
"released", and it is disproportionately important for accessibility: JUCE has
screen-reader backends on macOS and Windows and **none** on Linux, so all of that
work is currently unexercised.

- [ ] Windows build (VST3, CLAP, Standalone).
- [ ] macOS build (VST3, CLAP, AU?, Standalone) -- decide whether AU is in scope.
- [ ] CI that builds all three platforms.
- [ ] Run the test suite on each.

### Packaging

- [ ] Installers or archives per platform, with the plugin landing where hosts
      look for it.
- [ ] Install instructions in the README for people who do not build from source.
- [ ] Decide on a release/versioning scheme.

### Excise the reference-client harness

`test/refclient/` links GPLv2 reference sources and is temporary by design. Its
findings are captured in `docs/PARITY.md`, which is why that file exists.

- [ ] Capture anything still only provable by the live harness as golden
      fixtures (`test/fixtures/`).
- [ ] `rm -rf test/refclient` -- the build guards `add_subdirectory` with an
      `EXISTS` check, so nothing else changes.
- [ ] Decide whether to scrub it from published history
      (`git filter-repo --path test/refclient --invert-paths`).

### Documentation upkeep

- [ ] A current screenshot for the README.
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
