# Sync and tempo: synced, or not, and always told which

Date: 2026-11-06. Branch `sync-and-tempo`, cut from `origin/main` (c543b64)
plus the three accessibility commits from local `main` (chip button wording,
the audit grading what a reader says, the announcement volume knob).

## Why

Victor plays guitar and bass through Reaper, with a screen reader. Antiphon
replaced ReaNINJAM for him because ReaNINJAM has almost no accessibility.

Today, when he joins a room and Reaper's tempo differs from the room's, he hears
**silence** and is told nothing useful. Remote audio is gated on three things at
once -- tempo matches, Sync armed, DAW playing -- and the one message that would
explain the first is lost (it is spoken in the same instant as "Connected", inside
`Announcer`'s 400 ms gap, dropped, and never retried). What he does hear is the
chip offering to vote the room to his DAW tempo, which is the wrong advice. He
concludes nobody is playing. When the room votes a tempo change mid-jam, the same
thing happens: he drops out silently and thinks the band stopped. Today the
workaround is a human explaining in chat.

The sync model has problems for sighted users too:

- **Sync needs a ritual**: match the tempo, press Sync, then *start* the DAW
  transport. If the DAW is already playing you must stop and restart it.
- **A pause can leave you out of step with no way back.** Connected, the
  interval clock counts on its own while the DAW plays and freezes when it
  stops. Most DAWs resume from somewhere else (stop returns to where play
  began), so the jam carries on, but off the DAW's bars, and nothing says so.
  Recovering means pressing Sync and stopping and starting the DAW again.
- **Typing in chat can stop the DAW.** Keys leak to the host when the chat field
  is not really focused; the space bar stops the transport, which (today) also
  cuts you out of the room.

## The rule

**You are either perfectly synced, or not synced -- never something in between --
and Antiphon always tells you which, and why.**

"Perfectly synced" means: same tempo as the room, interval position derived from
the DAW playhead, so a tempo-synced synth or a loop in the DAW lands on the
room's bars. "Not synced" is a legitimate, clearly announced state in which you
still hear the room and the room still hears you.

Every in-between state of today disappears:

| Today | After |
|---|---|
| Tempo matches, Sync not pressed: silent | No Sync button; syncing is automatic once tempo matches and the DAW plays |
| Synced, DAW paused and resumed elsewhere: playing off the bars | Position comes from the playhead, so resuming anywhere lands correctly |
| Room changes tempo: silent | Reaper follows automatically; elsewhere you are told you are not synced and still hear the room |
| DAW stopped: silent | The room keeps playing and your live audio keeps going out on Antiphon's clock; you are told, repeatedly, until you act |

## Goals

1. A screen reader user always knows, without asking, whether they are synced,
   and if not, exactly what to do about it, in one sentence.
2. Joining a room, or the room changing tempo, never produces unexplained
   silence.
3. Getting synced is one action from anywhere (`Sync now`), and in Reaper it
   needs no action at all.
4. Pausing the DAW can never leave you silently out of step.
5. Typing in chat rarely reaches the DAW.

## Non-goals

- Supporting a DAW session view without the main transport running. In Live and
  Bitwig, launching clips needs the transport anyway; that is accepted.
- Pushing tempo or transport into DAWs other than Reaper. They get the plain
  announcement and instruction.
- Remembering servers, usernames and passwords. It is wanted, and it is the next
  design, deliberately not mixed into this one.

## Anti-goals

- **Complicating the common case for the rare one.** About 95% of players always
  want full sync. The unsynced mode is an escape hatch, not a peer.
- **A mode that is "mostly synced".** If a state cannot honestly be called synced,
  it is announced as not synced.
- **Announcements that nag.** Each change is said once, in one sentence. The
  repeating reminder is a short tone, not speech.

## The design

### Two modes, chosen explicitly

A radio group named **Clock**, replacing the Sync button in the plugin toolbar:

1. **Synced to DAW** -- the default. Selected every time you join a room; the
   choice is not saved with the project.
2. **Antiphon's clock** -- Antiphon keeps its own interval clock at the room's
   tempo and ignores the DAW's tempo and transport.

Beside it, a **Play** toggle for Antiphon's clock, as in the standalone. It is
always present, in the same place, so tabbing never finds controls appearing and
disappearing. While synced it is disabled but still reachable, with the
description "Follows the DAW transport while synced". Its name stays fixed: a
screen reader hears "Play, toggle button, pressed / not pressed". The label does
not swap between "Play" and "Stop", which is the standalone button's current
behaviour and a problem in its own right. The standalone's button gets the same
fix in this branch.

The standalone has no DAW, so it has no Clock group; it is always on its own
clock, as today.

### Synced to DAW

**The interval position is the DAW playhead, modulo the interval length.** This
is what `HostGrid` already does offline (DESIGN section 4.2), now used connected
as well. Play from anywhere and you land at the matching point in an interval.
Nothing is counted, so nothing drifts, and there is no Sync button.

**Synced means all of the following are true:**

- the DAW reports a playhead position;
- the DAW's tempo equals the room's **exactly**: within 0.001 BPM, which is
  about 3 samples per 16-beat interval at 110 BPM. Room tempos are integers,
  every DAW lets you type one exactly, and Reaper's `SetCurrentBPM` hits it. A
  DAW at 110.3 against a room at 110 is not synced, because the intervals would
  differ in length by a quarter of a percent and every boundary would glitch.
  Today's comparison rounds to the nearest whole BPM; that has to change;
- the DAW is playing.

If the tempo or position test fails, you are **not synced**. Antiphon plays the
room on its own clock at the room's tempo, sends your live audio, and says why
and what to do. As soon as the tests pass, you are synced -- no button.

**In Reaper**, Antiphon sets Reaper's tempo to the room's when you join and on
every tempo change the room votes, and says so. On a vote, the new tempo is set
when it **takes effect** at the interval boundary, not when the vote is
announced: the audio thread publishes "the grid changed tempo" and the message
thread calls `SetCurrentBPM`. The few milliseconds between them are not
announced as "not synced"; a short grace period (a quarter of a second, say)
after a room tempo change covers it. A setting (persisted with the
plugin state, default on) turns this off for anyone whose project must not be
touched. With it on, the tempo test never fails in Reaper. Optionally, Antiphon
can set Reaper's loop to one interval; that is offered as an action, not done
automatically.

**When the DAW stops while synced**, the room keeps playing and your live audio
keeps going out, on Antiphon's clock, continuing from the current position. You
are told: one spoken line, a visual state in the header, and a short **repeating
tone** (see Cues) until you act. You can keep playing live, press play in the DAW
to sync again, or choose Antiphon's clock.

**When the DAW resumes**, the interval position snaps to the playhead. This is a
**jump**, and jumps are handled as follows:

- **What you send: always a full-length interval.** A forward jump tears a hole
  in the interval being captured; fill the missing stretch with silence, so
  everything after it reaches the room at the right point in the bar. A backward
  jump ends your interval early; pad its tail with silence to full length, send
  it, and start the next one. Nobody has verified how the server and other
  clients treat a short upload, so we never send one. The plan includes a
  `FakeNinjamServer` test of the lengths, and a check against a real server with
  the reference client.
- **What you hear.** Play the room's current interval from the matching point
  (skip ahead), rather than restarting it, so you are on their beat at once. If
  that interval has not arrived (delivery is all-or-nothing), the result is
  silence until the next one; never the previous interval replayed. A backward
  jump starts your next interval early, and the room's next interval may not have
  arrived yet, so there can be a gap of silence. That is inherent to how Ninjam
  delivers audio. If `NinjamClient`'s playback structures cannot seek within the
  current interval today, changing them is in scope.
- **Say it.** "Synced to Reaper, beat 9 of 16."

**DAW loops.** A loop exactly one interval long (or a whole number of intervals)
just works. A loop shorter than the interval wraps the playhead before the
interval ends. Treat a wrap from the loop end to the loop start (JUCE reports the
loop points) as continuous time, so the interval keeps counting through it,
rather than as a backward jump. Any other backward move is a jump. *(Builder's
call, below.)*

**BPI changes while synced.** The position is measured from an **anchor**, a
playhead position where an interval began: `(playhead - anchor) mod BPI`. The
anchor starts at 0 (the DAW's bar 1). When the room's new BPI takes effect, at
the boundary of the old grid as it does today, the anchor moves to that boundary.
That makes a BPI change seamless: no jump, no odd-length interval, and the
intervals stay on the DAW's bars whenever the old boundary was on a bar. Every
other rule is unchanged, with "playhead mod BPI" read as "measured from the
anchor".

### The whole state, in one table

Connected, in the plugin. This is the pure, JUCE-free module that replaces
`SyncState` and `RunGate`.

| Clock | DAW | Tempo | State | Grid | In jam (send + hear) | Click and phase bar |
|---|---|---|---|---|---|---|
| Synced to DAW | playing, reports a position | matches | **Synced** | playhead | yes | run |
| Synced to DAW | playing | does not match | **Not synced: tempo** | Antiphon's clock, room tempo | yes | run |
| Synced to DAW | stopped | any | **Not synced: DAW stopped** | Antiphon's clock, continuing | yes | run |
| Synced to DAW | reports no position | any | **Not synced: no position from DAW** | Antiphon's clock | yes | run |
| Antiphon's clock, Play on | any | any | **Antiphon's clock** | Antiphon's clock | yes | run |
| Antiphon's clock, Play off | any | any | **Stopped** | stopped | no | stopped |

Moving into **Synced** from any other row is a jump to the playhead. Moving out
of it continues from the current position, with no jump. In the "Not synced"
rows the Play toggle is disabled and shows not pressed.

**Disconnected, nothing changes.** In a host, `HostGrid` follows the DAW as it
does today; the standalone has its own transport. The Clock group stays in place,
so the tab order never changes, but is disabled, with the description "Applies
while connected".

**If the Reaper bridge cannot be obtained** in some format, or a call fails,
Antiphon behaves as in any other DAW, and the announcements say so.

### Antiphon's clock

Selecting it, or pressing Play while it is selected, starts Antiphon's own
interval clock at the room's tempo.

- Switching from a playing, synced DAW continues from the current beat; no jump.
- Starting from stopped starts at beat 1.
- **Play off means you are out of the jam**: the room is silent and nothing is
  sent, said in one line ("Stopped. You are out of the jam.").
- Switching back to Synced to DAW snaps to the playhead (a jump, as above).

### Sync now: Ctrl+Alt+S

Ctrl+Alt+S keeps the key people already use for Sync. It works from anywhere,
does whatever comes next, and says what is left:

| Situation | Reaper | Other DAWs |
|---|---|---|
| On Antiphon's clock | select Synced to DAW, then continue down this table | same |
| Tempo does not match | set Reaper to the room's tempo | "Set your DAW to 110 BPM. Antiphon syncs as soon as it matches." |
| DAW stopped | start Reaper playing | "Press play in your DAW." |
| Already synced | "In sync, beat 5 of 16." | same |

### Status on demand: Ctrl+Alt+I

Speaks the full state in one sentence, whenever asked, for example:

- "Connected to ninbot.com, 5 players. Room 110 BPM, 16 beats. Synced to Reaper,
  beat 5 of 16."
- "... Not synced: your DAW is at 95 BPM. Hearing the room on Antiphon's clock."
- "... DAW stopped. The room is still playing on Antiphon's clock. Press play to
  sync."
- "... On Antiphon's clock, playing."

The existing `StatusReadout` carries the same text for anyone who navigates to it.

### What is said, and when

One sentence per event, composed as a single announcement, never two in the same
instant. The join announcement is the case that is broken today and must be
tested.

| Event | Reaper (auto tempo on) | Other DAWs |
|---|---|---|
| Join, DAW playing | "Connected to X. Room 110 BPM, 16 beats. Reaper set to 110. Synced." | Tempo matches: "Connected to X. Room 110 BPM, 16 beats. Synced." Otherwise: "Connected to X. Room 110 BPM, your DAW is at 95: not synced, but you can hear the room. Set your DAW to 110 to sync." |
| Join, DAW stopped | "Connected to X. Room 110 BPM, 16 beats. Reaper set to 110. Hearing the room on Antiphon's clock; press play to sync." | same, plus the tempo instruction if it does not match |
| Room tempo changes | "Room tempo changed to 120 BPM. Reaper set to match." | "Room tempo changed to 120 BPM. Your DAW is at 110: not synced. Set it to 120 to sync." |
| Tempo now matches | -- | "Synced to your DAW." |
| DAW stops while synced | "DAW stopped. The room is still playing on Antiphon's clock." | same |
| DAW resumes | "Synced to Reaper, beat 9 of 16." | "Synced to your DAW, beat 9 of 16." |
| Choose Antiphon's clock | "Antiphon's clock. Not following the DAW." | same |
| Play / stop on Antiphon's clock | "Playing on Antiphon's clock." / "Stopped. You are out of the jam." | same |

These are all at the Important tier of the announcement volume knob: each is
either the answer to a keystroke or a change to whether you are in the jam.

### Cues

A short, non-musical tone for "the DAW stopped and you are on Antiphon's clock",
repeating once per interval until you act. This is the first `CueVoice` the
roadmap's *Four channels* section describes, and it follows its rules: under
about 150 ms, a timbre the band cannot make, deliberately off the beat, a fixed
pitch unrelated to the key, never transmitted. It has its own enable and level.
Only this one cue is in scope; arrival, departure and message cues stay on the
roadmap.

The visual counterpart is the existing header status row, in its warning colour,
for as long as the state lasts.

### The tempo-vote chip

**Removed:** the "Your DAW is at N BPM -- Propose" chip, which offered to start a
vote for your DAW's tempo. It confuses screen reader users and nobody else uses
it.

**Kept:** the chip that appears when someone else has started a vote ("Vote: 120
BPM, 2/4"), which is how you join a vote in progress, and the "these chords look
like D minor -- Set key" chip.

### Typing in chat must rarely reach the DAW

Chat stays an inline field in the plugin window. Harden it:

1. After sending a message, focus stays in the chat field.
2. It is unmistakable, visually, whether keys are going to chat or to the DAW.
3. When the plugin window loses keyboard focus while the chat field had it, say
   so ("Keys now go to the DAW") or play a cue.
4. Document Reaper's per-plugin "Send all keyboard input to plugin" option in the
   user guide.

Because a DAW stop no longer cuts you out of the room, a leaked space bar costs
much less than it does today.

## Shortcuts

| Keys | Action | Change |
|---|---|---|
| Ctrl+Alt+S | Sync now | was Arm Sync |
| Ctrl+Alt+I | Say status | new |
| Ctrl+Alt+Enter | Play / stop Antiphon's clock (selects Antiphon's clock) | new |
| Ctrl+Alt+F | Toggle between Synced to DAW and Antiphon's clock | new |

None of them uses Space: it is the key most likely to leak to the DAW, and on
macOS Ctrl+Option+Space is VoiceOver's own "activate" key. Check every new chord
against the platform modifier (`Shortcuts::globalModifierName()`) and against
NVDA, JAWS, VoiceOver and Orca bindings before settling.

## Constraints

- **PRINCIPLES section 7.** The audio thread does not allocate, lock or block.
  Reaper API calls (set tempo, play) are made from the message thread, never from
  `processBlock`. New state shared with the audio thread is published the way
  `streamSlots` and `audioChannels` are.
- **PRINCIPLES section 9 still holds.** The local grid is still the sole
  authority for interval swaps; it is now derived from the playhead while synced,
  rather than counted. The network still never moves it.
- **Parity.** Connected and synced, interval lengths come from the playhead, not
  from `njclient.cpp:806`'s truncated count. With the tempo matched exactly, they
  differ from the reference client's by the truncation alone (at most one
  sample) plus the 0.001 BPM tolerance (a few samples). Measure that and record it
  in `docs/PARITY.md`. Uploads are always full length regardless (above).
- **Testable logic out of `PluginProcessor`.** The new mode/state logic replaces
  `SyncState` and `RunGate` with a pure, JUCE-free module. Jump handling for
  transmit and receive goes where it can be driven by `AudioLoopbackTests`.
- **The Reaper bridge is optional and contained.** In VST3, through JUCE's
  `VST3ClientExtensions::setIHostApplication` and Reaper's host-application
  interface. In CLAP, through `clap_host->get_extension("cockos.reaper_extension")`
  and its `GetFunc` (clap-juce-extensions ships an example:
  `modules/clap-juce-extensions/examples/HostSpecificExtensionsPlugin`). No other
  host is affected. Declare the few function signatures and interface IDs we use
  ourselves rather than vendoring the Reaper SDK, and record their source in
  `THIRDPARTY.md` / `docs/references/`. Check the licence before copying anything.
  Function names to verify against the SDK: `SetCurrentBPM`, `OnPlayButton`,
  `GetPlayState`, `GetSet_LoopTimeRange`.
- **AU** gets everything except the Reaper bridge.
- **ASCII only** in source.

## Documents that change with it

- `NON-GOALS.md` fence #6 (timeline integration) is reversed: we now follow the
  playhead. Fence #7 (pushing tempo into the DAW) is narrowed to "except Reaper,
  through its own plugin API". Fence #12's mention of REAPER integration needs a
  matching edit.
- `DESIGN.md` sections 4.1, 4.2 and 5 are rewritten for the new model.
- `README.md` and `website/docs/`: the player-facing sync story, shortcuts, and
  the Reaper keyboard-input tip.
- `docs/ACCESSIBILITY.md`: the Clock radio group, the new shortcuts and the cue.
- `ROADMAP.md`: this work area, and the first `CueVoice` item.
- `test/AuditMain.cpp`: new states for "not synced, tempo mismatch", "DAW stopped
  while synced" and "Antiphon's clock".

## Left to the builder

- Data structures, module names and how the jump is detected.
- The exact tempo tolerance.
- **The loop-wrap rule** above is a sensible default (my call, not agreed). If it
  proves fragile in a host, fall back to treating every backward move as a jump.
- **Whether the repeating cue starts when you join with the DAW already stopped**
  (my call: no -- it starts on a stop while synced; joining stopped is normal and
  is covered by the join sentence and the visual).
- How the radio group is exposed to JUCE's accessibility layer: JUCE has no
  radio-group role, so this needs care and an audit check.
- Whether the hosts we care about keep calling `processBlock` while stopped. If
  one does not, the "room keeps playing" behaviour cannot happen there; say so in
  the docs rather than working around it.
- The Announcer fix for the lost join message: composing one sentence is the
  primary fix; whether `Announcer` should also queue rather than drop is a
  separate call.

## Calls made without explicit agreement

- Reaper's tempo is changed automatically, with a setting to turn it off.
- Shortcut letters: I (status), Enter (play), F (mode).
- The repeating cue sounds once per interval.
- The loop-wrap rule, and the cue not starting on a stopped join (above).
- Uploads are always full length, padded with silence after any jump.
- A room BPI change moves the anchor, so it never causes a jump.
- Tempo tolerance 0.001 BPM; Reaper's tempo set when a change takes effect.
- The click and phase bar keep running whenever you are in the jam, synced or
  not.
- The Clock group is disabled, not hidden, while disconnected.
- The standalone Play button's fixed-name fix is in this branch.
