# Reference sources

Antiphon is a clean-room implementation in the sense that matters: no reference
source is compiled into the product, and none is vendored in this repository.
What we did do is *read* other NINJAM clients and the reference server, because
the wire format is under-documented and its edge cases are only discoverable
from working code.

Comments in `src/` cite those readings by repository and file, for example:

```
// (justinfrankel/ninjam njclient.cpp:794-810)
```

This file says what each of those repositories is and which revision was read.
The revisions are the ones that were pinned as submodules while the work was
done; they are recorded so a citation can be checked against exactly the code it
was taken from, rather than against a moving branch head.

| Citation prefix | Repository | Revision read |
|---|---|---|
| `justinfrankel/ninjam` | <https://github.com/justinfrankel/ninjam> | `f4c0eff3a1d8d5f3ead470d0c7405c3ee37da24e` |
| `libninjam/libninjam` | <https://github.com/libninjam/libninjam> | `e216a8f081206182ff45d6467bcdeb1b14c53856` |
| `elieserdejesus/JamTaba` | <https://github.com/elieserdejesus/JamTaba> | `4bdbe69cfd9bb5a7e5e5842421c9ddf7fba6dc90` |
| `antanasbruzas/abNinjam` | <https://github.com/antanasbruzas/abNinjam> | `947ba6579261d5d090d093d0b4384a832aea75bb` |
| `nykwil/ninjam-next-plugin` | <https://github.com/nykwil/ninjam-next-plugin> | `7aa654590d06102369c08efac0d2675eb901f5e1` |
| `mkschulze/JamWide` | <https://github.com/mkschulze/JamWide> | `bbd9b1b4b444650acfcc6c30e1ac05839d3ad93d` |

To follow a citation, clone the repository at the pinned revision:

```bash
git clone https://github.com/justinfrankel/ninjam /tmp/ninjam
git -C /tmp/ninjam checkout f4c0eff3a1d8d5f3ead470d0c7405c3ee37da24e
```

## Licensing

The NINJAM reference sources are GPLv2. They were read, not copied, and nothing
derived from them is linked into Antiphon. `docs/PROTOCOL.md` records the wire
format as observed; `docs/PARITY.md` records what was measured against a live
reference client before the harness that did the measuring was removed.

`THIRDPARTY.md` lists what Antiphon actually ships and under what terms.

## The notes in this directory

The other files here are reading notes, written early, while deciding what
Antiphon should be. They are kept because the comparisons in them explain
choices the codebase still reflects -- particularly why there is no `NJClient`
wrapper and no Qt. They are notes from a point in time, not maintained
documentation: where one contradicts `DESIGN.md`, `DESIGN.md` is right.
