## What this changes

<!-- The effect, not the mechanism. One or two sentences. -->

## Why

<!-- The problem it solves. Link the issue if there is one. -->

## Checks

- [ ] `ctest --test-dir build --output-on-failure` passes
- [ ] `./build/test/AntiphonAudit_artefacts/AntiphonAudit` exits 0
- [ ] `clang-format` clean on the files I touched
- [ ] Tests added or extended for the behaviour I changed -- and I confirmed
      they fail without the fix
- [ ] Documentation updated in the same commit, if this changed behaviour

If any box is unticked, say why here rather than removing it.

## Environment tested

<!-- Platform, plugin format, DAW, sample rate. If you tested on Windows or
     macOS, say so prominently -- that is the gap this project most needs
     closed. -->

## Anything touching the audio thread?

<!-- Delete if not. Otherwise: confirm no allocation, no lock, no file I/O and
     no logging on that path (PRINCIPLES §7), and say whether you ran the ASan
     and TSan builds. The baseline is zero warnings from src/. -->
