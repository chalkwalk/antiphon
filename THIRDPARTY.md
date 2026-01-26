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
| **JUCE** | `JUCE/` (submodule) | AGPLv3 or commercial | Patched at configure time from `patches/`; see `CLAUDE.md`. |
| **libogg / libvorbis** | `modules/ogg`, `modules/vorbis` (submodules) | BSD-style (Xiph) | Ogg/Vorbis encode and decode. |
| **clap-juce-extensions** | `modules/clap-juce-extensions` (submodule) | MIT | CLAP plugin format support. |
| **WDL** | `utils/` | zlib-style (Cockos) | Remaining vendored headers (`heapbuf`, `queue`, `wdlstring`). `sha1` and `vorbisencdec` were replaced by first-party code and are no longer used. |
| **NINJAM reference sources** | `references/` (submodules) | GPLv2 | Read-only protocol reference. Not linked into the product; `test/refclient/` links the reference client for interoperability testing only and is excised before release. |

The product itself is intended to be GPLv3.
