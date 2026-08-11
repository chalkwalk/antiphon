# Contributing to Antiphon

Antiphon is a NINJAM client shaped as an audio plugin. It works, it is small on
purpose, and it is licensed **GPLv3** -- contributions are accepted on those
terms.

Read [`PRINCIPLES.md`](PRINCIPLES.md) before proposing anything structural, and
[`NON-GOALS.md`](NON-GOALS.md) before proposing a feature. A surprising number of
reasonable-sounding requests already have a written answer there, and the answer
is usually "not that, but here is the thing we offer instead". If your idea
cannot be expressed within the principles, say so in the issue -- that is a
useful conversation, not a rejection.

## 1. What is most needed

In priority order. The first item is worth more than everything below it
combined.

1. **Running Antiphon somewhere that is not Linux.** Every line of this has been
   built and tested on Linux, in the CLAP format, in one DAW. Windows and macOS
   builds are the single biggest gap between "works" and "released". Compilation
   fixes, runtime reports, and "it crashed on load in Ableton" are all valuable.
2. **Screen-reader testing on Windows and macOS.** This matters more than the
   ordinary cross-platform case. JUCE has screen-reader backends on macOS
   (VoiceOver) and Windows (NVDA, Narrator) and **none on Linux**, so all of the
   accessibility work in this project is currently unexercised on the platforms
   where it would actually be used. If you use a screen reader, your report is
   the most useful thing this project can receive. See
   [`docs/ACCESSIBILITY.md`](docs/ACCESSIBILITY.md), which is deliberately honest
   about what is unproven.
3. **Testing in more DAWs and more formats.** VST3 in particular has had far
   less exercise than CLAP, and the macOS AU build has had none at all -- it is
   compiled only by CI and no host has ever loaded it. If you have Logic Pro or
   GarageBand, simply reporting whether Antiphon appears and passes audio is a
   real contribution. Note that AU is fixed at one stereo bus in and one out, by
   a limit of the format, so the per-player stem routing is expected to be
   unavailable there and its bus buttons are disabled on purpose.
4. **Interoperability findings.** If Antiphon sounds wrong to somebody running
   Jamtaba or ReaNINJAM in the same room, that is a bug of the highest severity
   here, and the report should say what they heard and what they were running.
5. **Ordinary bugs and UI work**, once the above are moving.

## 2. Issues before pull requests

- **Architectural changes, new features, large refactors:** open an issue first
  and describe the change against `PRINCIPLES.md`. Work that arrives as a
  surprise pull request may be turned down for reasons that would have taken one
  paragraph to establish beforehand.
- **Bug fixes, documentation, typos, build fixes:** just open the pull request.

Bug reports are most useful with the platform, DAW, plugin format, sample rate
and buffer size, plus what the server and the other players were.

## 3. Workflow

1. Fork, and branch from `main`.
2. Make the change, with tests (see §5).
3. Commit with a **one-line message in the imperative**, describing the effect
   rather than the mechanism -- `Sum a mono channel instead of discarding its
   right input`, not `fix ChannelMix`. Look at `git log` for the register.
4. Open a pull request against `main`.

## 4. Coding standards

- **C++17**, with `juce::` types preferred over `std::` where both exist.
- **`clang-format` is enforced, at a pinned version.** The configuration is
  `.clang-format` in the root; CI fails on unformatted code. Run
  `clang-format -i` on what you touched, not on files you did not.

  **Use version 20.1.8.** Major versions of clang-format disagree with each
  other -- 18 and 20 format `({})` differently -- so a mismatch produces a
  failing gate on lines you never edited. Whatever your distribution ships,
  the pinned one is a `pip install` away and is exactly what CI runs:

  ```bash
  pip install "clang-format==20.1.8"
  ```
- **`clang-tidy`** is configured in `.clang-tidy` and runs in CI as an advisory
  check while an existing backlog is worked down. Do not add new findings.
- **ASCII only in source files** -- no non-ASCII bytes anywhere, including
  comments and string literals. Use `--` for an em dash and `->` for an arrow.
  `juce::String(const char*, size_t)` asserts ASCII validity.
- **Comments explain why, not what.** The house style is few comments, each
  earning its place by recording a non-obvious invariant or a protocol
  workaround, often citing a line in a reference implementation.
- **Audio-thread code must not allocate, lock, do file I/O, or log**
  (`PRINCIPLES §7`). The audio thread currently takes no lock at all. Do not
  reach for one to share new state with it; publish it the way the existing
  paths do.
- Further conventions, and the traps that cost real time, are in
  [`AGENTS.md`](AGENTS.md). It is written for AI assistants working in this
  repository but is accurate for humans too, and it is the fastest way to
  understand the codebase's rules.

## 5. Testing

Antiphon has a real test suite and it is not optional. The bugs it has caught
were all invisible to listening: audio a few percent sharp, an interval a sample
short, a parser reading past a buffer. Assume your change has the same failure
mode.

```bash
cmake -B build
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
./build/test/AntiphonAudit_artefacts/AntiphonAudit   # exit code is the finding count
```

Both must pass. [`test/README.md`](test/README.md) explains each layer, and
`AGENTS.md` has a table mapping the area you changed to the suite that should
grow.

Three rules worth stating here:

- **Fix bugs test-first, and prove the test has teeth.** Write the failing test,
  then fix it. If the fix is a one-liner, reinstate the bug briefly and confirm
  the test goes red. A test that passes both ways is worthless.
- **Audio assertions must be statistical** -- RMS, and pitch by zero crossings.
  Vorbis is lossy and has codec delay; sample-by-sample comparison will never
  hold.
- **Parser changes get an ASan run.** Over-reads pass silently otherwise. The
  sanitiser baseline is **zero** warnings under both ASan and TSan, with no list
  of known-acceptable findings -- deliberately, because a suite with a remembered
  exception is one nobody reads carefully. The only output that is not ours is
  four UBSan lines from inside vendored libvorbis.

Anything touching the UI or the audio thread also wants a Standalone launch
before you call it done.

## 6. Accessibility is a build gate, not a nicety

Every control must carry a name a screen reader can announce. `AntiphonAudit`
walks the real component tree across every UI state and fails if one does not.
**If you add a control, name it; if you add a new surface or state, add it to the
audit** -- an unaudited state is how the connect dialog went unchecked for its
whole life.

> **CI does not run this check. You have to.**
>
> The audit is excluded on every CI platform, so a green tick on your pull
> request says nothing whatsoever about accessibility. On macOS and Windows
> runners it segfaults for want of a window session; on Linux it hangs in
> teardown, in a JUCE thread-destruction race that has nothing to do with what
> it audits. `ROADMAP.md` has the stack trace and the list of fixes already
> tried.
>
> So run it yourself before opening a pull request that touches the UI:
>
> ```bash
> ./build/test/AntiphonAudit_artefacts/AntiphonAudit   # exit code = findings
> ```
>
> It takes about two seconds and is reliable off a runner. This is a debt we
> intend to pay, not a standard we have dropped.

## 7. Documentation

Ship the documentation change with the code change. Seven files at the root each
own one job, and a fact belongs in exactly one of them -- the table at the bottom
of `AGENTS.md` says which. A shipped roadmap item moves to `docs/COMPLETED.md` in
the same commit that finishes it.

A number quoted anywhere needs a method behind it. `docs/PARITY.md` carries the
measurements and the calibration; cite it rather than restating figures.
