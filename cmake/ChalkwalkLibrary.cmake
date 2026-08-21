# ---------------------------------------------------------------------------
# Where the shared libraries come from.
#
# Unset, nothing changes: this repository's submodules are used and a fresh
# clone builds with no extra steps. The submodule stays the source of truth for
# WHICH commit this project wants.
#
# CHALKWALK_MUSIC_DIR, CHALKWALK_DSP_DIR, CHALKWALK_NINJAM_DIR -- cache
# variables or environment variables -- point at working checkouts instead,
# which is what makes a change to a library testable HERE without a commit and
# without a push:
#
#     cmake -B build -DCHALKWALK_MUSIC_DIR=$HOME/Programming/chalkwalk-music
#
# Antiphon then compiles that working tree directly. Edit there, rebuild here,
# run the suite; no round trip through GitHub. That matters most for the
# library that is still growing: a change to `Harmony` wants Antiphon's 3,000
# assertions run against it before it is committed anywhere.
#
# THE SUBMODULE SHA NO LONGER DESCRIBES WHAT YOU BUILT while one of these is
# set, which is the whole cost of it. CI must not use them, and neither should
# anything whose result is meant to be attributable -- `docs/PARITY.md`'s
# measurements above all, since a number that cannot name the commit that
# produced it is not a measurement. Use an override to iterate; bump the
# submodule and re-verify before calling anything done.
#
# Same shape as Anvil's CHALKWALK_PHYSICAL_DIR, deliberately: one pattern
# across the ecosystem is worth more than a better one used in one place.
# ---------------------------------------------------------------------------

function(chalkwalk_add_library name submodule_path)
    string(TOUPPER "${name}" upper)
    set(var "CHALKWALK_${upper}_DIR")

    if(NOT ${var} AND DEFINED ENV{${var}})
        set(${var} "$ENV{${var}}")
    endif()
    set(${var} "${${var}}" CACHE PATH
        "Working checkout of chalkwalk-${name}; empty means use this repository's own submodule")

    if(${var})
        if(NOT EXISTS "${${var}}/CMakeLists.txt")
            message(FATAL_ERROR
                "${var} is set to '${${var}}' but there is no chalkwalk-${name} "
                "there. Point it at a checkout, or unset it to use the submodule.")
        endif()
        set(root "${${var}}")
        message(STATUS
            "chalkwalk-${name}: OVERRIDE at ${root} "
            "(the submodule SHA does not describe this build)")
    else()
        set(root "${CMAKE_CURRENT_SOURCE_DIR}/${submodule_path}")
        if(NOT EXISTS "${root}/CMakeLists.txt")
            message(FATAL_ERROR
                "No chalkwalk-${name}.\n"
                "  This repository's ${submodule_path} submodule is not checked "
                "out, and ${var} is not set. Either:\n"
                "    git submodule update --init --recursive\n"
                "  or point at a working checkout:\n"
                "    cmake -B build -D${var}=/path/to/chalkwalk-${name}")
        endif()
    endif()

    # Each library's own suite runs inside this project's ctest, so Antiphon
    # verifies its dependencies rather than assuming them.
    set(CHALKWALK_${upper}_TESTS ON CACHE BOOL "" FORCE)
    add_subdirectory("${root}" "${CMAKE_BINARY_DIR}/libs/${name}")
endfunction()
