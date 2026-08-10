---
id: developer-guide
title: Developer guide
sidebar_position: 9
---

# Developer guide

Antiphon is about 6 000 lines of modern C++ against JUCE, and it is meant to fit
in your head. There is no `NJClient` wrapper, no Qt, and no heavy dependencies:
the protocol, the SHA1, the Ogg/Vorbis layer and the interval clock are all
first-party.

## Build and test

```bash
git clone --recurse-submodules https://github.com/chalkwalk/antiphon.git
cd antiphon
cmake -B build
cmake --build build -j $(nproc)
ctest --test-dir build --output-on-failure
./build/test/AntiphonAudit_artefacts/AntiphonAudit
```

Both must pass. The audit's exit code is the number of accessibility findings.

## How it is shaped

The load-bearing decision is that **testable logic lives outside
`PluginProcessor`**, which cannot be compiled into the test target at all -- it
needs the `JucePlugin_*` defines that only `juce_add_plugin` supplies. That is
why `IntervalClock`, `MetronomeVoice`, `SyncState`, `ChannelMix`, `GainUtils`
and the accessibility audit rules exist as separate JUCE-light modules, with
`processBlock` as a thin caller.

New audio-thread logic belongs in a module like those.

The audio thread takes **no lock at all**. Remote playback and local channels
both walk fixed arrays of never-destroyed objects, published with a release
store on a count. Do not reach for a lock to share new state with it.

## Where the documentation lives

Each file at the root owns exactly one job:

| File | Owns |
|---|---|
| `PRINCIPLES.md` | Why. Twelve principles with stable anchors, and a proposal gate. |
| `NON-GOALS.md` | What the project refuses, and what it offers instead. |
| `DESIGN.md` | What the software is -- the interval model, threads, sync, mixing, routing. |
| `ROADMAP.md` | What it is becoming. |
| `AGENTS.md` | How to work in the repository: the map, the rules, and the traps. |
| `docs/PROTOCOL.md` | The NINJAM wire format, and the traps in it. |
| `docs/PARITY.md` | What has been verified against the reference client, with the numbers. |
| `test/README.md` | How to run every test layer. |

Read them in the order **PRINCIPLES -> DESIGN -> ROADMAP**. If a proposal cannot
be expressed within the principles, it is not ready for the roadmap.

`AGENTS.md` is written for AI assistants working in this repository -- the
project is developed in collaboration with Claude Opus 5, and that file is the
honest record of how. It is equally accurate for humans, and it is the fastest
way to learn the codebase's actual rules.

## Things that will bite you

A few invariants that look like bugs and are not:

- **`IntervalClock::samplesPerInterval()` truncates on purpose.** It reproduces
  the reference client's arithmetic verbatim so interval boundaries line up with
  every other client on the server. Rounding it "correctly" would silently
  desync you from Jamtaba and ReaNINJAM.
- **The local metronome is the sole authority for interval swaps.** Network
  jitter must never move the playback clock.
- **Interval delivery is all-or-nothing.** Ogg emits a page only every ~4 kB, so
  a quiet interval decodes to nothing until the end-of-stream flush. Nothing may
  assume a partially received interval is playable.
- **Never branch on `JucePlugin_Build_Standalone`.** It is true in every format,
  and it once compiled the whole DAW sync flow out of the plugin. A test fails
  if it comes back.

## The reference sources

The NINJAM wire format is under-documented, so other clients and the reference
server were read as protocol documentation. **None of them is vendored here and
none is linked into the product** -- they are GPLv2, and Antiphon is a
first-party implementation.

Source comments cite them by upstream repository and file, like
`(justinfrankel/ninjam njclient.cpp:806)`. `docs/references/SOURCES.md` maps each
prefix to a repository and the exact revision that was read, so a citation can
still be followed.

## Contributing

See
[`CONTRIBUTING.md`](https://github.com/chalkwalk/antiphon/blob/main/CONTRIBUTING.md).
The most useful thing anyone can do right now is build and run Antiphon on
Windows or macOS -- and, especially, test it with a screen reader there.
