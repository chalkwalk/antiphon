# Accessibility in Antiphon

Accessibility is a goal of this project, not a feature of it. Antiphon is
deliberately narrow -- connect, set levels, transmit, chat, stay in time -- and
that makes it a realistic candidate for being genuinely usable with a screen
reader rather than nominally compatible with one.

## Platform support, honestly

JUCE implements accessibility on some platforms and not others. This is not
something annotation can work around.

| Platform | Screen reader | Status |
|---|---|---|
| macOS | VoiceOver | **Works.** JUCE has a native backend. |
| Windows | NVDA, JAWS, Narrator | **Works.** JUCE has a UIA backend. |
| Linux | Orca | **Does nothing.** JUCE has no AT-SPI backend. |

On Linux, `JUCE_NATIVE_ACCESSIBILITY_INCLUDED` is never defined and
`AccessibilityHandler::AccessibilityNativeImpl` is an empty stub
(`JUCE/modules/juce_gui_basics/native/accessibility/juce_Accessibility.cpp`).
Every `setTitle`, role and announcement in this codebase compiles and is
discarded; Orca sees an opaque window. We build the annotations anyway, because
the work is identical on every platform and effective on two of the three.

A Linux self-voicing mode -- Antiphon speaking through `speech-dispatcher`
itself, rather than through an accessibility bridge -- is the realistic route if
Linux support is ever needed. It is not planned.

## What is annotated

- Every control that takes keyboard focus has a **name** and a **description**.
  Single-glyph buttons are the ones that mattered most: `M`, `S`, `TX`, `R`, `+`
  and `-` previously announced as those literal characters.
- **Faders and pans speak their values with units** -- "-12.0 dB", "left 20",
  "centre" -- rather than a bare number.
- **Channel strips and remote players are focus containers** with their own
  names, so a reader navigates player by player and announces "Instrument,
  Mute" instead of presenting forty flat controls whose names repeat.
- **The header is a `StatusReadout` component.** Connection state, tempo,
  interval length and sync state are painted as graphics and would otherwise not
  exist for a reader. It is the first thing keyboard focus lands on, because
  "where am I and is it working" is the first question.

## Why connecting is a separate dialog

A modal dialog is usually *better* for a screen reader than fields embedded in
the main window, and that is why the server browser is one:

- Focus is trapped inside it. Tab cycles the six fields that matter instead of
  wandering into a mixer with forty controls.
- It announces itself on open, with a role and a title, so the user knows a new
  context has appeared and roughly what it wants.
- It has a clear beginning and end, which matches the task: this is a discrete
  thing you do once, not part of playing.

Embedded connect fields would sit in the middle of a large surface with no
boundary marking them out, and nothing to say whether they are currently
relevant.

Three things have to be true for that to hold, and all three are, but only the
first came free:

1. **Genuinely modal.** `DialogWindow::LaunchOptions::launchAsync()` calls
   `enterModalState (true, nullptr, true)`, so input is blocked behind it and it
   takes keyboard focus. A non-modal overlay would announce nothing and leak
   focus to the mixer behind it.
2. **Escape closes it** (`escapeKeyTriggersCloseButton`), so there is always a
   way out that does not require finding a button.
3. **Focus returns to where it came from.** JUCE does *not* do this -- there is
   no focus restoration in `ModalComponentManager` -- so closing the dialog
   would otherwise strand the user somewhere arbitrary. Antiphon records the
   previously focused component and restores it.

There are **four** ways out of the dialog -- Connect, Cancel, the X drawn in the
UI, and the window manager's own close button -- and all four must land in the
same place. They now share one teardown, attached as a modal callback, so focus
restoration happens whichever route the user takes. Before that, closing from
the title bar skipped our code entirely and left the dialog unable to reopen.

## Announcements

Discrete events and chat are spoken: connecting and disconnecting, sync state changes,
server tempo and BPI changes, votes, players joining or leaving, and incoming chat messages
(user messages, private messages, actions, topics, and harmony updates).

Continuous values are deliberately **not** spoken. Meter levels and interval
position change tens of times a second; announcing them would interrupt the
reader constantly and make the app less usable, not more. They are available on
demand by focusing the control.

Verbosity has three settings -- **Off**, **Important only** (the default) and
**All**. Repeated identical messages are suppressed, and a minimum gap is enforced
so a burst of six joins does not become six interruptions.

## Keyboard

Everything is reachable by Tab and operable from the keyboard. Tab order is:
session status, local channels, remote players, chat, toolbar.

A few shortcuts exist for things worth doing quickly mid-performance. They use
`Cmd+Option` on macOS (avoiding VoiceOver's `Control+Option` VO key collisions) and
`Ctrl+Alt` on Windows/Linux to stay clear of what a DAW claims, and none of them is the only
route to its action -- each control can also be tabbed to and pressed.

| Shortcut (macOS / Win+Linux) | Action |
|---|---|
| `Cmd+Option+S` / `Ctrl+Alt+S` | Arm Sync (then start the DAW transport) |
| `Cmd+Option+C` / `Ctrl+Alt+C` | Focus the chat message box |
| `Cmd+Option+O` / `Ctrl+Alt+O` | Open Server Browser / Connect dialog |
| `Cmd+Option+P` / `Ctrl+Alt+P` | Start a practice room and join it |
| `Cmd+Option+M` / `Ctrl+Alt+M` | Toggle Mute All local channels |
| `Cmd+Option+H` / `Ctrl+Alt+H` or `?` | Open Keyboard Shortcuts Help dialog |
| `Cmd+Option+A` / `Ctrl+Alt+A` | Write an accessibility audit report to the desktop |
| `Cmd+Option+Shift+T` / `Ctrl+Alt+Shift+T` | Toggle Transmit retroactively for current interval |
| `Cmd+Option+L` / `Ctrl+Alt+L` | Focus Local Channels section |
| `Cmd+Option+R` / `Ctrl+Alt+R` | Focus Remote Users section |
| `Alt+Right` / `Cmd+Option+Right` / `Ctrl+Alt+Right` | Select Next Local Channel |
| `Alt+Left` / `Cmd+Option+Left` / `Ctrl+Alt+Left` | Select Previous Local Channel |
| `Alt+Down` / `Cmd+Option+Down` / `Ctrl+Alt+Down` | Select Next Remote Player |
| `Alt+Up` / `Cmd+Option+Up` / `Ctrl+Alt+Up` | Select Previous Remote Player |
| `Cmd+Option+1`..`9` / `Ctrl+Alt+1`..`9` | Jump directly to Local Channel 1..9 |
| `Cmd+Option+Shift+1`..`9` / `Ctrl+Alt+Shift+1`..`9` | Jump directly to Remote Player 1..9 |
| `M` (when channel active) | Toggle Mute for selected channel |
| `S` (when channel active) | Toggle Solo for selected channel |
| `X` (when local channel active) | Toggle Transmit for selected local channel |
| `Up` / `Down` / `+` / `-` | Nudge volume for selected channel (+/- 1 dB) |
| `Left` / `Right` | Adjust pan for selected channel |

The shortcuts match the key **code**, not the text character. With Ctrl held,
X11 hands JUCE a control character -- Ctrl+Alt+A arrives with a text character
of `0x01`, not `'A'` -- so an earlier version compared against `'A'` and none of
these ever fired, silently. `src/Shortcuts.h` owns the mapping and is
unit-tested, because a shortcut that does nothing looks exactly like a shortcut
that is not pressed.

`Ctrl+Alt+A` now names the file it wrote in the status readout and in the
announcement, and says so when the write fails. It is the one action with no
other visible effect, so "it worked" and "nothing happened" were
indistinguishable.

## The audit

Nobody working on this can run a screen reader against every build, and on Linux
a reader-based check would pass vacuously while the annotations rotted. So the
coverage is checked mechanically instead.

`AccessibilityAudit` walks the component tree and reports controls that a reader
would announce as nothing, or as the same thing as their neighbour:

- **MISSING NAME** -- unnamed, or named something useless when spoken alone
  (a bare `M` or `+`).
- **DUPLICATE NAME** -- two controls in the same container that cannot be told
  apart. The same name in *different* containers is fine: every strip has a
  Mute, and the strip is the context.
- **NO DESCRIPTION** -- reachable and named, but nothing explains what it does.

`Ctrl+Alt+A` writes a report to the desktop, covering the editor and the connect
dialog when it is open. The report states what it examined -- roots, components,
how many are reachable by keyboard -- because a verdict of "no issues found"
cannot otherwise be told apart from having examined nothing, which is the state
the dialog was in.

Two layers check it. The rules are unit-tested headlessly
(`test/AccessibilityAuditTests.cpp`) against a synthetic tree. The **real
component tree** is checked by the `AntiphonAudit` target, which links the
plugin's own library, drives the genuine editor through five states, and exits
with the finding count -- so `ctest` fails the build when a control arrives
without a name. See `test/README.md`.

**What the audit does not tell you** is whether the result is pleasant to use.
It cannot judge whether a description is helpful, whether the tab order feels
sane, or whether announcements land at useful moments. That needs a real screen
reader user.

**It has now been used by one.** A contributor built the AU on macOS, loaded it
in a host, joined a jam and worked the plugin with a screen reader, and reported
that it compares favourably with the official client. That is the first evidence
any of this works where it counts, and it is worth stating plainly because the
rest of this document is deliberately pessimistic.

It is also worth bounding just as plainly: **one person, one platform, one
session, reported verbally rather than as a list of findings.** It says the
approach is sound. It does not say any particular control reads well, it says
nothing at all about Windows or NVDA, and it is not a substitute for the gaps
below -- most of which it never touched.

## Known gaps

- Verified with a screen reader once, on macOS, as above. Not on Windows, not
  repeatedly, and not in a way that produced actionable detail.
- Not verified with an actual screen reader by the authors, as distinct from by
  a contributor.
- JUCE's stock `AudioDeviceSelectorComponent`, which the standalone's recovery
  screen embeds, labels its Output and Input dropdowns for sighted users only:
  the labels are attached with `Label::attachToComponent` and no accessible
  title is set. The audit found this. `AccessibleNaming::adoptLabelNames` makes
  a control adopt the label already attached to it, so the dropdowns now
  announce "Output" and "Input". The rest of that panel -- sample rate, buffer
  size, channel lists -- is only reached when a device is open, and has not been
  assessed.
- VU meters expose a value but there is no way to hear levels continuously
  without a reader announcing them constantly. A periodic "level check" gesture
  may be worth adding.
- Chat history is an accessible read-only multi-line text area with caret
  navigation for line-by-line reading. Providing structured per-message navigation
  gestures remains a future enhancement.
