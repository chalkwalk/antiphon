# Third-party components

## Fonts

| Component | Path | Licence | Notes |
|---|---|---|---|
| **Inter** | `assets/fonts/Inter-{Regular,Bold}.ttf` | **OFL-1.1** (`assets/fonts/OFL.txt`) | The product typeface. The two static cuts the surface asks for, embedded via `juce_add_binary_data(antiphon_fonts ...)` and installed as the default LookAndFeel's typeface by `installProductLookAndFeel()`. The variable font is deliberately not vendored -- a face JUCE must instance per size reintroduces the per-machine variability the embed exists to remove. |

**OFL-1.1 obligation.** The licence permits bundling and redistribution with
software, including selling that software, provided the font is not sold on its
own and the licence text travels with it (`assets/fonts/OFL.txt`; embedded
copies excepted). The Reserved Font Name is *Inter*: a **modified** version may
not be distributed under that name. We ship the cuts unmodified, so no rename is
required -- but that constraint binds anyone who re-generates or subsets them.

## Code

| Component | Path | Licence | Notes |
|---|---|---|---|
| **JUCE** | `JUCE/` (submodule) | AGPLv3 or commercial | Patched at configure time from `patches/`; see `AGENTS.md`. |
| **libogg / libvorbis** | `modules/ogg`, `modules/vorbis` (submodules) | BSD-style (Xiph) | Ogg/Vorbis encode and decode. |
| **clap-juce-extensions** | `modules/clap-juce-extensions` (submodule) | MIT | CLAP plugin format support. |

## Data

| Component | Path | Licence | Notes |
|---|---|---|---|
| **SCOWL** word list | `src/BotDictionary.h` (generated) | Permissive, attribution required | The real-word gate for the practice room's chat parsing: a word that is ordinary English is not a mistyped one. Not the whole list -- `scripts/make_wordlist.py` keeps only the words within the typo-repair budget of a `BotLanguage` lexicon entry, which is the only place a dictionary can change a decision. |

**SCOWL obligation.** Spell Checker Oriented Word Lists, Copyright 2000-2011
Kevin Atkinson, taken from the Debian `wbritish` package. Use, copy, modify,
distribute and sell are all granted without fee, provided the copyright notice
and permission notice appear in copies and in supporting documentation --
which this section is, and which the generated header repeats in its own
comment so the notice travels with the file. The word lists come with no
warranty. Constituent lists include the public-domain Moby Words II. GPLv3
imposes nothing further here: the terms are strictly more permissive.

That table is the whole list. In particular:

- **No WDL.** Antiphon began by vendoring two Cockos WDL headers, `sha1` and
  `vorbisencdec`. Both were replaced by first-party code -- `src/Sha1.{h,cpp}`
  and `src/VorbisCodec.{h,cpp}` -- and the rest of WDL was never used. Nothing
  from it remains in the tree or in the build.
- **No NINJAM reference source.** The GPLv2 reference client, the reference
  server and the other clients were **read** as protocol documentation and were
  never vendored; the harness that once linked the reference client for
  differential testing has been removed, along with any trace of it in the
  published history. `docs/references/SOURCES.md` records what was read and at
  which revision; `docs/PARITY.md` records what was measured.

## Licence

Antiphon is **GPLv3** (`LICENSE`).

This is not a free choice. JUCE is offered under AGPLv3 or a commercial licence,
and the open-source route obliges any work built on it to be released under a
compatible copyleft licence. GPLv3 satisfies that and is compatible with the
BSD-style Xiph libraries and the MIT CLAP extensions. Anyone shipping a binary
must therefore also make the corresponding source available -- which for this
project is the intent anyway, not a constraint being tolerated.
