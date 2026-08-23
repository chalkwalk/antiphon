---
id: getting-started
title: Getting started
sidebar_position: 2
---

# Getting started

There is no installer yet, but there is a download.

## Take a build

Every push to `main` publishes VST3, CLAP and standalone builds for Linux,
macOS and Windows as CI artefacts. Open the newest green run on the
[Build workflow](https://github.com/chalkwalk/antiphon/actions/workflows/build.yml)
and take the one for your platform.

You need to be signed in to GitHub to download them, and they expire after a
while. The macOS and Windows builds compile and pass the full test suite, but
**neither has ever been loaded in a host** -- and the macOS one is unsigned, so
Gatekeeper will object to it.

## Or build it

You need CMake, a C++17 compiler, and git.

```bash
git clone --recurse-submodules https://github.com/chalkwalk/antiphon.git
cd antiphon
cmake -B build
cmake --build build -j $(nproc)
```

If you cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` before configuring.

That produces:

- **Standalone** -- `build/src/Antiphon_artefacts/Standalone/Antiphon`
- **VST3** -- `build/src/Antiphon_artefacts/VST3/`
- **CLAP** -- `build/src/Antiphon_artefacts/CLAP/`
- **AU** -- `build/src/Antiphon_artefacts/AU/`, built on macOS only

Copy the VST3 or CLAP into wherever your DAW looks for plugins. On Linux that is
usually `~/.vst3/` and `~/.clap/`. On macOS the AU goes in
`~/Library/Audio/Plug-Ins/Components/`, and is only worth using if your DAW is
Logic Pro or GarageBand -- it cannot do per-player stem routing.

:::note Which platform?
Everything so far has been built and tested on **Linux**, in the **CLAP**
format. Windows and macOS are in the build matrix so the gap is visible, and
they are expected to fail today. If you get it building elsewhere, that is the
single most useful contribution the project can receive.
:::

## Where to put it in your DAW

Antiphon is an **effect**, and it belongs on the **master bus** in most setups.
It passes your audio through untouched and adds the remote players to the mix.

If you want to record each player separately, it needs to be somewhere its extra
output buses can reach the tracks you want them on -- see
[Recording the jam as stems](./routing-stems.md).

## Your first connection

1. Open Antiphon (standalone, or on the master bus in your DAW).
2. Click **Connect**.
3. Either pick a room from the **server browser** -- a live list from
   ninbot.com -- or type a host and port directly.
4. Connect as **anonymous** unless you have an account on that server.

`ninbot.com:2049` is the usual busy public server, and a reasonable place to
find out whether any of this works.

## What happens next

**Nothing, for one interval.** The first interval after you connect is silent,
because nobody's audio has arrived yet. Then the room fades in.

This is the point at which people conclude the plugin is broken. It is not.
Read [Your first jam](./your-first-jam.md).

## Running standalone instead

The standalone build goes straight into an audio interface, no DAW required, and
is a supported way to use Antiphon rather than a development convenience.

It has one limitation worth knowing up front: the standalone host gives it a
**single input bus and a single output bus**. So multi-channel transmit and
per-player stem recording -- the features that motivate the plugin -- are not
available there. Plug a guitar in and jam; reach for the plugin when you want to
record the result apart.
