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
- [ ] The same gesture, or one beside it, for the harmony: the key, the chart,
      and the chord sounding now. The chord changes several times a bar, so it
      can never be announced on a timer -- which is exactly the argument above,
      and the reason it is the same work area.

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
- [ ] **Harmony beyond diatonic.** `Harmony::realise` is the named seam:
      secondary and altered dominants, tritone substitution, borrowing from
      adjacent modes. Functional roman naming (`V7/vi`) belongs with it, since
      it is the same knowledge and today's naming is deliberately mechanical.
- [ ] **Fuller voicings.** Ninths and thirteenths voiced rather than named only,
      and dropping the root from the pad when the bass is already on it.
- [ ] The practice room is not wired into the processor at all yet, so the
      timeline's "show the band's own chart in practice" rule is written but
      unreachable. It lands with the room.

### Bots that talk

Practice is the best introduction to Antiphon and nothing says so. Beyond
teaching, the bots could feel like present players rather than pattern
generators -- answering when asked what they are playing, noticing a chart they
cannot read -- without a language model and without becoming a novelty.

**Designed in `docs/BOT-CHAT.md`; that document is the proposal and this is the
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
- [ ] **Measure the server's vote threshold.** `docs/BOT-CHAT.md` proposes how
      the band votes, and the whole proposal rests on `M` as a function of the
      number of clients -- which nothing here records. Connect a varying number
      of clients to `scripts/testserver.sh` and read it off the vote line before
      building any of it (`PRINCIPLES` §5).
- [ ] **The band's vote policy.** Bots are ordinary clients, so they count
      toward the threshold, and abstaining is a vote against: four of them take
      tempo control away from a room of three humans entirely. The rule -- vote
      only for a candidate a majority of humans already back, never propose one,
      staggered like the arrival roster -- is designed in `docs/BOT-CHAT.md` and needs
      no coordination between the bots: they queue behind staggered delays the
      way they announce themselves, and each checks on waking whether the motion
      already carried, so the band casts exactly the shortfall and stops.
      Nothing casts a vote today.
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
- [ ] Answering `SET_KEY`, `SET_TEMPO` and `SET_CHART` honestly. All three are
      recognised; none is a thing a bot may decide, and saying so is the point
      of recognising them. Three parts, designed in `docs/BOT-CHAT.md`: that the
      room decides, what it currently is, and how to change it in any client
      (`!vote bpm N`, a `| Am | F |` line, a `[key: ...]` tag). Two special
      cases, both about not implying a decision was made: a key that was
      defaulted rather than chosen, and no chart at all.
- [ ] **The key tag is self-triggering, and reply text must respect it.**
      `MusicalKey::parseTagged` matches `[key:` anywhere in a line, so a bot
      explaining the syntax would set the key by explaining it. The answer is
      that the bot puts the tag up itself rather than teaching it -- a
      translator, not an authority, since any player in any client can type the
      tag and `/key` is only a shortcut for it. Whatever renders bot chat needs
      a test that no reply text parses as a key.
- [ ] **One bot answers a common question.** Addressing decides who was asked,
      not how many should speak, and `REPORT_*`/`SET_*` are one fact rather than
      four. Acting stays collective -- `band, shake` rerolls all four -- and only
      the line about it is rationed.
- [ ] **One arbitration primitive, four uses**: the arrival roster, the tempo
      vote, the key-change acknowledgement and common answers. Staggered delay,
      then check whether the job is already done. It should replace the fixed
      "lowest instrument first" order the key-change cue was designed with,
      which picks a bot that may have been told `quiet` and then never speaks.
- [ ] `src/BotChat.{h,cpp}` as pure functions over what a bot knows, so a seed
      and a script of events give a byte-identical transcript.
- [ ] A fifth, instrument-less tutor bot that teaches six lines and then parts.
      The players play the changes; they do not teach.
- [ ] The tutor's one piece of listening: subscribed to the owner alone, using
      `AudioMeasure` plus a duty cycle and a transient count to tell silence,
      a faint signal, clicks and clipping from somebody playing -- so it can say
      "that went out" rather than hope. It gates which encouraging line is said
      and never becomes a judgement.
- [ ] The budget, and a test that asserts a hundred events produce at most N
      lines. The test that keeps it from becoming annoying.
- [ ] `quiet`, and unprompted speech off outside the practice room.
- [ ] Addressing: at most one bot ever answers, cold silence is the default,
      first contact must be explicit, and a message aimed at a human is
      answered by nobody. Four bots replying to one question is the annoyance
      the whole feature has to avoid. Corpus at
      `test/fixtures/bot-addressing.txt`, 143 cases, many of them "nobody".
- [x] `tools/PracticeRoomMain.cpp` (`antiphon-practice`): hosts a room and waits,
      so the band can be heard and talked to before any of it is reachable from
      the plugin. Cheap because the room was designed as a destination rather
      than a mode -- there was nothing to integrate, only something to start.
- [ ] **Wire any of it to a bot.** `BotLanguage` and `BotAddress` both pass
      their corpora and neither has a caller: nothing in `PracticeBot` reaches
      them, so none of the measured accuracy is reachable by a player yet.

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

- [ ] **Phrases that return.** A form table -- AABA, ABAC, AAAB -- indexed by
      interval, so a phrase is a thing the listener can recognise coming back
      rather than a fresh roll each time. The table and the section length come
      from the room seed, so `shake` changes the shape of the music and not just
      its notes.
- [ ] **A shared intensity curve.** One deterministic arc over a section, read
      by every voice and mapped to its own parameters: hats thicken, the bass
      gets busier, the keys add extensions, the lead climbs. Tension and release
      without anybody hearing anybody.
- [ ] **Staggered rests.** A voice drops out for a bar at low intensity, with a
      per-voice threshold from its salted seed so the drop-outs never coincide,
      and a floor that guarantees somebody is always playing. Sparse stretches
      and dense ones, rather than everyone stopping at once.
- [ ] **Turnarounds mark the form.** The drums already fill every fourth
      interval; make that the section boundary rather than a fixed count.
- [ ] Deviation, so the form does not become its own kind of stale: an
      occasional departure whose likelihood grows the longer a phrase has
      repeated.
- [ ] **The unit suite takes two minutes, and that is now an iteration cost.**
      It grew honestly -- most of it is rendering audio and measuring it, which
      is what the band tests are for -- but BotBand alone is 57 seconds and the
      loop between an edit and an answer is long enough to discourage running it.
      Worth an hour with a profile: shorter renders where a defect shows in the
      first note, fewer redundant seeds, and possibly a `--quick` subset for the
      edit loop with the full sweep left to CI.
- [ ] **Tab completion in the chat field.** Complete `/` commands from the
      command list, and usernames after `/msg` and `/kick` from the room's user
      list -- and a name at the start of a line, which is how a bot is addressed
      (`docs/BOT-CHAT.md` section 5). Common prefix first, then cycling.
      Accessibility is half the point: the completion and the candidate list
      both want announcing, and a name nobody can spell is a name nobody can
      reach.
- [ ] **Resolve `/msg` and `/kick` against the user list, not whitespace.**
      Both split on the first space, so neither can reach a username containing
      one. Longest match against the names actually in the room fixes it, and
      is what makes tab completion and hand-typing agree.

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
is free to us: FluidLite builds SF3 support against Xiph's libogg and libvorbis,
**which this repository already vendors as submodules** because the Ninjam codec
needs them. So the whole feature adds one small library and no new third-party
code at all.

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

- [ ] Decide between FluidLite and mainline FluidSynth by rendering the bank
      through both and listening for the modulator-dependent presets. Submodule,
      not a fork; `THIRDPARTY.md` entry either way. Build SF3 support against the
      libogg and libvorbis already vendored here.
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

### Breaking the repository up

Wanted, planned here, and **not next** -- see the ordering argument at the end.

#### It is four layers, not three

The dependency direction was checked rather than assumed, and the good news is
that it is already clean: nothing in the client layer includes anything above
it, and nothing in the bots includes a plugin header. The boundary exists in
practice; it is simply not enforced.

The surprise is that there is a fourth thing hiding in the middle. `MusicalKey`
and `Harmony` are used by the bots AND by the plugin's chat UI -- announcing a
key and reading a chord chart are room features that exist with no band in the
room at all -- so they belong to neither. Putting them in the bots would make
the plugin depend on the band in order to parse `| Am | F |`, which is exactly
backwards.

```
music  (MusicalKey, Harmony, Euclidean)      no dependencies, JUCE-light
  ^
  |     njclient  (protocol, codec, Sha1, IntervalClock, ChannelMix, SpscRing)
  |        ^
  +--- bots  (band, synthesis, PracticeBot, PracticeRoom)
           ^
        antiphon  (processor, editor, UI, standalone)
```

Two other placements the split forces a decision on, both currently ambiguous:
`IntervalClock` is client (it reproduces `njclient.cpp:806` and both layers
above use it), and `AudioMeasure` is included by **no production file at all** --
it exists for the tests and the tools, which is worth knowing before deciding
where it lives.

#### JUCE in three repositories: a real problem, and a solved one

Every layer needs JUCE. Even `music` does, for `juce::String`.

The cost is not build time -- JUCE compiles its modules into each consuming
target regardless of how many checkouts exist -- it is **disk and clone time**:
94 MB per copy, so three submodules is 280 MB and three fetches for one
developer. Worse, `add_subdirectory(JUCE)` three times collides on target names,
so the naive arrangement does not even configure.

The standard answer is that a leaf repository *requires* JUCE rather than
*vendoring* it:

```cmake
if(NOT TARGET juce::juce_core)
  # Built on its own. Fetch a copy; when nested, the parent already provided one.
  FetchContent_MakeAvailable(JUCE)
endif()
```

`FETCHCONTENT_SOURCE_DIR_JUCE` then points every repository at one checkout for
anybody working across them. One copy, and each repository still builds and
tests alone.

**The patches are a non-issue, which is worth checking rather than assuming.**
Both `patches/*.patch` are plugin concerns -- embedded-window keyboard focus, and
bus-layout change notification -- so they stay with `antiphon`, and the two lower
layers want unpatched JUCE. `clap-juce-extensions` is plugin-only for the same
reason.

#### Extract with history, not by copying

`git filter-repo --path` per layer, which keeps every commit that touched those
files and therefore keeps blame and the reasoning. That matters more here than
in most projects: the commit messages are where the *why* lives, and a fresh
"initial import" would throw away the part of this repository that is hardest to
reconstruct. `NinjamClient.cpp` alone has 41 commits behind it.

The counter-proposal -- build the deepest repository fresh, then port -- is
worse on both counts: it loses that history, and it means maintaining two copies
of the client while the port is in flight.

#### The real cost is the documentation

`PRINCIPLES.md`, `NON-GOALS.md` and `DESIGN.md` are one argument about one
program, and they are the most valuable artefacts here after the code. Three
repositories means either duplicating them, which guarantees drift, or leaving
them in `antiphon`, which leaves the other two under-documented and cites
`PRINCIPLES §N` across a repository boundary.

I do not have a good answer to this and it should not be waved past. The least
bad option is probably that the principles stay in `antiphon` and are cited by
URL from the others, with each leaf carrying only what is true of it alone --
but "the docs get worse" is a genuine cost of the split and belongs in the
decision.

#### Phases, and why the first one is the one to do

1. **Separate CMake libraries inside this repository**, with the dependency
   direction declared and enforced by the build. Half a day, no risk, entirely
   reversible.
2. Move the shared modules to the layer that owns them; record the choices in
   `AGENTS.md`, whose line count is also out by a factor of three.
3. Split `test/` the same way, which is the part likely to bite -- the test
   target deliberately re-lists production sources, and that arrangement needs
   rethinking per layer rather than copying.
4. Live with it. Anything that has to reach across a boundary is the boundary
   being wrong, and finding that out costs one commit here and a cross-repository
   migration later.
5. Only then `git filter-repo`, three repositories, submodules, three CI
   configurations.

**Phase 1 is worth doing on its own merits even if the repositories never
happen.** It is most of the benefit -- the layering becomes real, the bots'
future dependencies cannot leak into the client -- for a fraction of the cost,
and it makes the eventual split mechanical because the hard part of a split is
discovering the boundary.

#### A shared library across the Chalkwalk projects

Wider than this repository and not scheduled, but the split is the moment to
plan it, because extracting a layer here and extracting it for everybody are
nearly the same work.

**The argument is not theoretical, and reading the other repositories made it
stronger than the version written from memory.**

`polyBlep` was ported here from seq_play; porting it meant testing it, and
testing it found the correction being ADDED where it should have been
subtracted -- so seq_play's oscillators aliased 82% worse than no correction at
all, for its whole life, with one of its own tests passing *because* of the bug.
`Svf` and `hermite4` came the other way. (An earlier draft here guessed that
arps-euclidya carried the same oscillator bug. It does not: it has no
oscillators. It is a MIDI generator.)

**Euclidean rhythm is implemented three times, and the three do not agree.**

| | Where | Formulation |
|---|---|---|
| Antiphon | `src/Euclidean.h` | `(i * pulses) % length < pulses` |
| seq_play | `src/core/Euclidean.h` | the same, under the name `bjorklund` -- which it is not; Bjorklund's is the recursive one |
| arps-euclidya | `src/EuclideanMath.cpp` | Bresenham with the error term seeded at `steps / 2` |

That seeding is not cosmetic. It rotates the pattern:

```
E(3,8)   x..x..x.   vs   .x..x.x.
E(5,16)  x...x..x..x..x..   vs   .x..x...x..x..x.
E(5,8)   x.x.xx.x   vs   x.x.xx.x        (agree)
```

Same necklace, different phase -- so a figure that lands on the downbeat in two
of these projects lands off it in the third, and Antiphon's kick relies on
exactly that ("the kick lands on the downbeat; everything else moves"). Three
implementations, three names, two behaviours, one author. **The rule of three is
met today, without waiting for `mpe_phys`.**

##### The rule for taking a dependency

"Use third-party as much as possible" is the right instinct and the wrong rule,
because it does not discriminate. This one does:

> **Take a dependency when the thing has a SPECIFICATION you could fail to
> meet. Write it yourself when it is small enough to test exhaustively.**

Loudness has a specification (ITU-R BS.1770) and a reference implementation, and
being subtly wrong about K-weighting is invisible. SoundFont has a specification
and GeneralUser GS leans on the obscure parts of it. An FFT has a correctness
proof and a hundred person-years of optimisation. Those are dependencies.

A state-variable filter is forty lines with a magnitude response you can assert
at DC and Nyquist. `hermite4` is eight lines and exact on a straight line.
Those are not dependencies, and taking one buys nothing but a version to track.

| Thing | Verdict | Why |
|---|---|---|
| Loudness (BS.1770) | **Adopt `libebur128`** (MIT) | A spec we reimplemented and validated against ffmpeg. Correct today; one refactor from being subtly wrong forever |
| SoundFont 2/3 | **Adopt FluidLite** (LGPL) | Already decided above |
| FFT, if ever needed | **Adopt** PFFFT or KISS | `brightnessHz` measures spectral slope precisely to avoid needing one; if that stops being enough, do not write one |
| Gather resampling | **Adopt** soxr / zita / libsamplerate | Well served, and the quality differences are measurable rather than matters of taste |
| `Svf`, `hermite4`, `DelayLine`, `polyBlep`, `softClip` | **Keep** | ~250 lines, already written, already tested. Replacing them rewrites call sites for no functional gain |
| The voices -- `PluckedString`, `ModalBank`, `Cabinet`, `Room`, `Chorus` | **Keep** | These are instruments, not primitives. Nothing third-party is trying to be them |
| Music theory | **Keep, and share** | See below -- this is the thinnest ground of all |
| The scatter resampler | **Keep, and it is the crown jewel** | See below |

##### The scatter write has no third-party equivalent

seq_play's `deckcore/Resampler.h` is a 16-tap polyphase windowed-sinc resampler
whose cutoff falls to Nyquist/rate above unity, so the source is band-limited
before it can alias. That much is ordinary. What is not ordinary is `scatter`:
the same kernel *deposited* into the destination at a fractional position, with
a 1/rate density compensation, so a write head moving at a variable rate lays
its samples down without imaging.

That is the adjoint of interpolation -- gather read, scatter write -- and it is
the piece nobody ships. Every resampling library that exists is a gather
resampler: soxr, libsamplerate, zita-resampler, libfresample, libswresample,
Signalsmith. You feed input and pull output. None of them exposes the transpose,
because the common use case is playback and playback only ever gathers. Writing
at a variable rate is what a tape machine does, and it needs the other half.

So this is the one piece of DSP across these projects with genuinely nothing to
adopt, and the strongest candidate for a shared library on the merits rather
than on convenience.

##### Music theory: seq_play's model is the general one

Antiphon has `MusicalKey{tonic, Mode}` -- a tonic and one of seven named modes.
seq_play has `Scale.h`, and it is strictly more general:

```
KeySig { root, brightness, modifiers[], scaleType }  ->  uint16_t pitch-class mask
```

`brightness` is a signed axis centred on Dorian, which is the right centre
because Dorian is symmetric -- the modes fan out bright and dark either side of
it, one accidental per step, which IS the circle of fifths without asking anyone
to remember mode names. `modifiers` then alter individual degrees, producing
scales with no name at all, and everything collapses to a twelve-bit mask.
There is even a `modifierApplies` notion for whether a modifier is currently
doing anything, which is a genuinely good UI idea.

**Antiphon's model is a special case of it**: diatonic, no modifiers, with mode
and brightness in bijection. So the shared library takes seq_play's
representation as the primary one and keeps named modes as a naming and parsing
convenience, because a player types "D Dorian" and should not have to type an
integer.

The cost is concrete and worth knowing before agreeing: Antiphon indexes scales
by degree (`degreeToMidi(key, degree, octave)`, `kScaleDegrees = 7`), and a
pitch-class mask has `popcount(mask)` degrees rather than always seven. Every
call site that assumes seven has to become "the nth set bit". Bounded, entirely
mechanical, and invisible until you try it.

##### What else is worth sharing, and is not served elsewhere

C++ music theory is poorly covered: what exists is framework-tied
(`ofxMusicTheory` needs openFrameworks), narrow (`Septima` does seventh-chord
voice leading), or a MIDI scoring environment (`CFugue`). Nothing is a
dependency-free, tested library of the following, which between these projects
already exists and is retyped rather than shared:

- **Pitch** -- keys, scales as pitch-class masks, brightness, modifiers, modes
  as presets, spelling (which sharp, which flat).
- **Harmony** -- chords, charts with bar timing, roman numerals, key inference
  from a progression, voice leading by cyclic dynamic programming.
- **Rhythm** -- Euclidean patterns, accent placement, metric strength.
- **Melody and dynamics** -- note strength against a chord, contour shapes, and
  the COUPLING between them: strong beats take strong notes, a colour note may
  pass but not sit. That rule is the reason the lead stopped sounding wrong in
  minor keys, and it is the least obvious thing any of these projects knows.
- **Velocity as articulation** -- the idea that a technique is a RANGE velocity
  moves along rather than a switch between samples, which is what makes the
  bass and the electric piano sound played.
- **Measurement** -- `AudioMeasure`, wrapping `libebur128` for loudness rather
  than reimplementing it, but keeping the combined interface.

##### Melody generation: the two versions diverged usefully

Antiphon's `leadLine` was ported from seq_play's `MelodyGen.h`, so they share a
spine -- metric strength drives note choice, four contour shapes, a seeded RNG,
nearest-candidate-to-target with jitter. What is interesting is what each gained
afterwards, because they went in complementary directions and neither is simply
better.

**What seq_play does better, and Antiphon should take:**

- **Onset placement by strength class.** This is the standout. Antiphon draws
  the lead's onsets from a Euclidean figure, which is even but metrically blind:
  it will happily put a note on the third sixteenth and leave the downbeat
  empty. seq_play sorts every step by metric strength and fills class by class --
  all the downbeats, then all the half-bars, then the quarters -- and only when
  the density budget runs out MID-CLASS does it Euclidean-spread the remainder
  within that class. So density becomes a musical dial: turn it up and the line
  fills in progressively weaker subdivisions, which is what a player does.
- **Metric strength as a trailing-zero count.** `pos == 0` is the downbeat;
  otherwise the strength is how many times the position divides by two. It
  generalises to any length for free, where Antiphon's is a hand-written ladder
  keyed to eighths and BPI.
- **Strength-scaled sustain, capped to the next onset.** A weak note is short,
  so what is left over becomes a rest that bridges into the next stronger onset.
  Antiphon holds every note until the next one and gets its rests from an
  explicit one-in-three dice roll on weak beats -- cruder, and less connected to
  the metre.
- **`stepLeap` and `coreBias` as dials** -- how far the line may leap, and how
  wide the note pool is (triad, pentatonic arc, everything). Antiphon hardcodes
  both.
- **`snapToRank`**: search outward from the contour target for the nearest
  candidate the beat allows, ties resolving flatter. Cleaner than building an
  allowed-set and linear-scanning it, and the tie-break is defined rather than
  incidental.

**What Antiphon does better, and seq_play should take:**

- **Chord awareness, which is the big one.** seq_play ranks notes by
  fifths-distance from the KEY's root -- an elegant continuous ranking, and
  chord-blind. Antiphon ranks them against the CHORD SOUNDING AT THAT STEP, so
  the line follows a progression rather than a key. Over `| Dm | Bb | F | C |`
  seq_play would play D-minor-ish material throughout; Antiphon lands on chord
  tones as the chart moves.
- **The avoid-note rule, derived rather than listed.** A scale tone a semitone
  above a chord tone is the one that clashes. That gives the flat sixth in
  Aeolian, the fourth in Ionian, the flat second in Phrygian -- and correctly
  leaves Lydian's sharp fourth alone, because it is a whole tone above the third
  and is the characteristic note of the mode. Porting beat strength without this
  is what made an early Antiphon lead sound wrong in minor keys.
- **Colour notes pass rather than sit.** A tier-2 note is capped to one eighth
  whatever its beat would allow. seq_play scales sustain by strength alone, so a
  dissonance on a weak beat can still be held into the next chord.
- **A contour rerolled per interval**, so the line develops across a phrase
  instead of repeating. seq_play's contour is a fixed parameter.

**The synthesis** is a generator that ranks candidates on BOTH axes: fifths
distance from the key, which always exists, and relation to the current chord,
which exists when there is a chart. The chord relation dominates where it
applies and the fifths rank carries the rest -- so the same generator serves a
sequencer track with no harmony and a bot following a progression, which is
exactly the pair of cases these two projects have.

Two of these are worth taking into Antiphon **independently of any sharing**,
because they are improvements here on their own terms: onset placement by
strength class, and strength-scaled sustain. Both are contained inside
`leadLine`.

##### Note strength: one model, two contexts

Lockstep (the sequencer, still called seq_play on disk) does have a more
intelligent note-strength model than Antiphon, and it is worth taking whole.

```
fifthsOffsetOf(root, pc):  ((pc - root) * 7) mod 12, folded to [-5, 6]
noteStrengthRank        :  2 * |offset|, minus 1 when the note sits on the
                           side the scale's brightness leans towards
```

Multiplying the semitone distance by 7 inverts the "a fifth is seven semitones"
map, so a pitch class becomes its position on the circle of fifths in one line.
Distance from the root is then the strength axis, and "further in fifths is
weaker" falls straight out of the geometry -- the root, then the
dominant/subdominant pair, then outward, with modifier and out-of-scale notes
furthest. The doubling exists so the lean tie-break can never cross a distance
boundary.

The **lean** is the part with no equivalent here at all: at equal fifths
distance, a bright scale favours the sharp-side note and a dark scale the
flat-side one. That is a real musical fact -- the ♯4 belongs to Lydian and the
♭2 to Phrygian -- expressed as one signed comparison.

**Antiphon's model is chord-relative and Lockstep's is scale-relative**, and
that is the whole difference: Lockstep builds melody against a scale because a
sequencer track has no chart, while Antiphon infers or is told a progression and
can therefore ask a sharper question. Neither can do the other's job.

The unified model decomposes into **three independent axes**, which is what
makes it worth building once rather than twice:

1. **Membership.** Is the pitch class in the scale mask at all? Out-of-scale is
   weakest regardless of everything below.
2. **Tonal distance**, in fifths, with the lean tie-break. The insight is that
   this needs no new mechanism to become chord-aware -- only a second centre.
   Rank against the CHORD root and against the SCALE root and add them:

   ```
   rank = a * |fifths from chord root| + b * |fifths from scale root|
   ```

   The chord root is strongest, the scale root nearly so, a note far from both
   is weak, and with no chart the first term drops out and it degrades exactly
   to Lockstep's model. One function, both contexts.
3. **Clash.** A semitone above a note the chord is actually SOUNDING, which is
   Antiphon's avoid-note rule. This is orthogonal to the other two -- it is
   about simultaneity rather than tonality, which is why it correctly spares
   Lydian's ♯4 (a whole tone above the third) while condemning Ionian's fourth.
   A demotion applied after the distance ranking, not part of it.

##### The rest of Lockstep's generative core

Checked so the catalogue is complete rather than the parts that happened to come
up. `src/core/` also holds:

- **`HarmonyGen.h`** -- a progression printer that deliberately carries NO chord
  theory: no qualities, no templates, no auto-voicer, just up to four voices as
  indices into the diatonic ladder so they are always in key, moved by ear. That
  is the opposite choice to Antiphon's `Harmony`, which parses named chords,
  infers keys and voice-leads by dynamic programming. Both are defensible and
  they do not merge: one is a hand-editing tool, the other reads what a human
  typed in chat. Worth recording as a deliberate divergence rather than a gap.
- **`AccentVel.h`** -- metric weight to velocity as a curve with centre and
  depth. The velocity half of the beat-strength idea, which Antiphon does
  ad hoc per voice.
- **`Density.h`** and **`MetricSelect.h`** -- a subtractive thinning overlay that
  can only silence trigs, never add them, selecting deterministically by
  tier-plus-Euclid rather than a per-step hash. This is a better-formed version
  of what Antiphon's "staggered rests" roadmap item is reaching for, and it is
  already written.

##### Shims: only where they clean something up

Preference is to port call sites to third-party interfaces directly. The one
exception worth defending is `AudioMeasure`, and it earns it: its value is not
any single measurement but that peak, rms, crest, brightness, pitch and loudness
come from ONE interface, so tuning by ear and asserting a threshold cannot use
different numbers. That is a real interface improvement over five libraries.
FluidLite gets driven directly. A resampler would be used directly.

##### Ordering

##### What the other repositories actually contain

Read rather than assumed, because the plan above was drafted from memory and two
of its guesses were wrong:

| Project | `src/` lines | What it is | Overlap |
|---|---|---|---|
| seq_play | 74 000 | Sequencer, tape machine, drum and analog machines, Push 1 surface | The largest by far, and the source of `Svf`, `hermite4`, `polyBlep`, the scale model and the scatter resampler |
| Antiphon | 19 600 | This | Harmony, measurement, the band's voices |
| arps-euclidya | 19 000 | A MIDI generator -- no audio DSP at all | Euclidean, and nothing else |
| mpe_phys | 2 500 | Physical modelling: `BowedExciter`, `WaveguideResonator` | The third consumer for STRINGS AND RESONATORS, not for theory |

Two consequences the earlier draft got wrong. `mpe_phys` is not a future
consumer of the music-theory layer; it is a present one of the physical-modelling
layer, which is where `PluckedString` and `ModalBank` live. And a grep for
shared concepts has to be read carefully: `brightness` appears in all four and
means three different things -- scale brightness in seq_play, spectral centroid
in Antiphon and mpe_phys, and UI colour in arps-euclidya.

##### Ordering

Not now, and not before the in-repository separation above -- the shared library
is the same boundary discovery repeated across four codebases, and doing it here
first is the cheap rehearsal.

The first extraction should be **Euclidean**, because it is the smallest, it has
three real consumers today, and the three disagree -- so it is the one where
sharing fixes a live defect rather than merely preventing a future one. Deciding
which phase is correct is a musical decision somebody has to make once, which is
precisely the argument for one implementation.

#### Why this is not the next thing

The benefits are all anticipated: independent reuse by somebody who is not us,
and keeping a soundfont dependency out of the client. Neither exists yet.

The costs are immediate: three CI configurations, a submodule dance on every
clone, worse documentation, and cross-repository refactoring in a project that
has touched two layers in most of its recent sessions.

And the ordering argument that settles it: **the practice room is not wired to
the plugin UI at all.** No user can currently reach a bot. Restructuring the
repository around a feature nobody can run yet is optimising the wrong axis
while two synthesis steps, the entire chat implementation and the owner-identity
gap are all unbuilt and user-visible.

- [ ] **The unit suite takes two minutes, and that is now an iteration cost.**
      It grew honestly -- most of it is rendering audio and measuring it, which
      is what the band tests are for -- but BotBand alone is 57 seconds and the
      loop between an edit and an answer is long enough to discourage running it.
      Worth an hour with a profile: shorter renders where a defect shows in the
      first note, fewer redundant seeds, and possibly a `--quick` subset for the
      edit loop with the full sweep left to CI.
- [ ] **Tab completion in the chat field.** Complete `/` commands from the
      command list, and usernames after `/msg` and `/kick` from the room's user
      list -- and a name at the start of a line, which is how a bot is addressed
      (`docs/BOT-CHAT.md` section 5). Common prefix first, then cycling.
      Accessibility is half the point: the completion and the candidate list
      both want announcing, and a name nobody can spell is a name nobody can
      reach.
- [ ] **Resolve `/msg` and `/kick` against the user list, not whitespace.**
      Both split on the first space, so neither can reach a username containing
      one. Longest match against the names actually in the room fixes it, and
      is what makes tab completion and hand-typing agree.

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
is free to us: FluidLite builds SF3 support against Xiph's libogg and libvorbis,
**which this repository already vendors as submodules** because the Ninjam codec
needs them. So the whole feature adds one small library and no new third-party
code at all.

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

| | SF2 | SF3 at q0.8 |
|---|---|---|
| full bank, 287 presets | 30.82 MB | 10.07 MB |
| core + eight drum kits, 31 presets | 16.53 MB | **4.82 MB** |
| core, 23 presets | 12.42 MB | 3.55 MB |
| minimal, 8 presets | 7.16 MB | 1.90 MB |

**The drum kits are the bargain, and the arithmetic is not obvious.** Each is
about 2.7 MB alone, but they share almost everything -- the GS kits are largely
one set of samples remapped with a few kit-specific pieces -- so the first costs
2.69 MB and the other seven cost 1.38 MB between them. Eight kits, 1.27 MB
compressed.

Worth taking whole for a musical reason as well. The modelled kit has three
pieces; each sampled kit has 65 samples, including five toms, ride, ride bell,
crash, splash, china, cowbell, tambourine, claves, congas, bongos, timbales,
agogo, guiro, cabasa, shaker and woodblock. None of that is a physical model
anybody here is going to write, and `ROADMAP` already carries "multi-tap clap,
cowbell, rimshot and toms" as deferred work. Percussion is also the best case
for Ogg compression, since a one-shot is never looped and the loop artifacts are
the whole risk.

That does NOT make the modelled kick, snare and hat redundant: they vary
continuously with velocity and never repeat, which is exactly what a sample
cannot do and exactly what a backing band needs most from its drummer. The
sampled kits are a palette to extend it, not a replacement for it.

The `core` set is what a physical model will never do well: piano, vibes and
marimba, two organs, nylon and steel guitar, violin, cello, pizzicato, string
ensemble, choir, four brass, three saxes, oboe, clarinet, flute. Everything the
band already plays is left out, because modelling those is better.

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

- [ ] Decide between FluidLite and mainline FluidSynth by rendering the bank
      through both and listening for the modulator-dependent presets. Submodule,
      not a fork; `THIRDPARTY.md` entry either way. Build SF3 support against the
      libogg and libvorbis already vendored here.
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

### Three layers, one repository -- for now

Not scheduled. Prompted by the soundfont question, which is the first thing that
would put a dependency on one part of this program that the other parts have no
use for.

`src/` is 19 600 lines and has grown three distinct concerns, which is worth
saying plainly because `AGENTS.md` still describes it as "about 6 000 lines" and
that stopped being true somewhere in the band work:

| Layer | Lines | What it is |
|---|---|---|
| The Ninjam client | 3 700 | Protocol, codec, SHA1, the interval clock. Genuinely reusable, and depends on nothing else here |
| The bots | 7 100 | The band, the synthesis, the harmony, the chat. The largest of the three and the newest |
| The plugin | 5 000 | The processor, the editor, the UI, the standalone shell |

The dependency direction is already one-way in practice -- bots and plugin both
use the client; the client uses neither -- but nothing enforces it, so it holds
by habit rather than by construction.

**The case for splitting** is that these have different audiences and are
acquiring different dependencies. A Ninjam client library is useful to somebody
who does not want a practice band; a practice band that grows FluidLite and a
soundfont should not force either on a plugin user who only wants to jam.

**The case against doing it as three repositories now** is that this project
refactors across all three layers constantly -- most sessions have touched two
of them -- and cross-repository refactoring with submodules is where that
velocity goes to die. It is also three CI configurations, three release
cadences, and a submodule dance on every clone, in a repository that already
patches two submodules at configure time.

**So: separate CMake libraries inside one repository first,** with the
dependency direction enforced by the build rather than remembered. That gets the
layering, keeps the bots' dependencies out of the client, and makes an eventual
repository split mechanical rather than exploratory -- the hard part of a split
is discovering the boundary, and this discovers it while the cost of being wrong
is one commit.

The repository split earns itself when somebody outside this project wants the
client library, or when the bots' dependency footprint would otherwise be
imposed on plugin users who do not want a band. Neither is true yet.

- [ ] Three CMake targets with the dependency direction declared and enforced.
- [ ] Move the shared JUCE-free modules to whichever layer owns them, and say
      which in `AGENTS.md`. `MusicalKey`, `AudioMeasure` and `IntervalClock` are
      each used by more than one and want deciding rather than assuming.
- [ ] Correct the line count in `AGENTS.md`, which is out by a factor of three.
- [ ] Only then, and only on evidence: separate repositories.

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

**One interlock to get right.** `test/BotBandTests.cpp` asserts that two
consecutive drum intervals are not bit-identical -- today the hat rotation
carries that -- and genuine repetition is exactly what would break it. The
answer is not to weaken the test: it is that repetition should be identical in
its *figure* and never in its *performance*, which is what the swing and
per-hit jitter in the synthesis work provide. A phrase that returns played
exactly the same way twice is a loop; played fractionally differently, it is a
band. The two pieces of work want doing in that order.

### A responsive jamming partner

Sketched in `docs/BOT-CHAT.md` section 14, and not scheduled. A bot receives a
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

`NinjamClient`, `NinjamProtocol`, `VorbisCodec`, `Harmony` and the bots have no
dependency on the plugin -- `tools/StemsMain.cpp` and the wanted
`tools/BotMain.cpp` already prove it. Making them their own repository, consumed
here as a submodule with the bots a submodule of that, would let a bot travel
with the client rather than with the plugin.

It is a packaging decision rather than a code one, and it costs a repository
boundary in exchange for reuse nobody has asked for yet. Written down because
the thought recurs, not because it is scheduled.

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
