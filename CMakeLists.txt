# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.22)

# Set the project name
project(Antiphon VERSION 0.1.0)

# enable_testing() must be called at the top level for ctest to work
enable_testing()

# A build type, when the caller did not name one.
#
# CMake's default is the EMPTY string, which is not "Debug" -- it is no flags
# at all: no -O, no -g, and NDEBUG left undefined. That is the worst of both,
# and it is what `cmake -B build` had been producing since the beginning.
#
# It was not a theoretical cost. The band's synthesis was measured at 27% of a
# core and that number went into ROADMAP.md as the reason to make a voice
# cheaper before growing the band past four. Rebuilt with -O2 it is closer to
# 10%, and the prerequisite it justified was an artefact of the flags. A
# measurement is only as good as the build it was taken on (`PRINCIPLES.md`
# section 5), and the build nobody chose is the easiest one to forget you are
# on.
#
# RelWithDebInfo rather than Release because this default is for people working
# on the tree: -O2 and -g, so a profile means something and gdb has symbols.
# CI names Release explicitly and is unaffected, as are the sanitiser builds,
# which name Debug.
#
# The cost, stated because it is real: RelWithDebInfo defines NDEBUG, so
# `jassert` compiles out. Configure with -DCMAKE_BUILD_TYPE=Debug when that is
# what you want.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Build type; RelWithDebInfo when the caller names none" FORCE)
    message(STATUS "No CMAKE_BUILD_TYPE given; using ${CMAKE_BUILD_TYPE}")
endif()

# C++20 -- applies to every target in the tree, including the format-specific
# plugin wrappers (Antiphon_Standalone / _VST3 / _CLAP) that juce_add_plugin
# creates. A per-target PUBLIC cxx_std_20 does NOT reach those wrappers,
# because JUCE compiles their sources independently with its own cxx_std_17
# floor (see JUCEUtils.cmake).
#
# It is a single standard across the build rather than a preference: JUCE has
# inlines gated on __cpp_char8_t, so a tree that compiles some translation
# units at 17 and some at 20 is an ODR hazard, not merely an inconsistent one.
# Same reasoning and same lines as Anvil and the sequencer.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(cmake/JuceSource.cmake)

# Submodule patches, applied at configure time.
#
# Each entry is "<submodule dir>|<patch file>". Each patch is idempotent:
# "git apply --reverse --check" succeeds only when the patch is already in the
# working tree, so a second configure is a no-op and a fresh clone gets it
# applied once.
#
# Do not hand-edit a submodule. Change the .patch, revert the submodule, and
# reconfigure.
set(ANTIPHON_SUBMODULE_PATCHES
    "JUCE|patches/juce-linux-plugin-keyboard-focus.patch"
    "JUCE|patches/juce-bus-layout-change-notification.patch"
    "modules/clap-juce-extensions|patches/clap-bus-layout-rescan.patch")

find_package(Git QUIET)
if(GIT_FOUND)
    foreach(patch_entry IN LISTS ANTIPHON_SUBMODULE_PATCHES)
        string(REPLACE "|" ";" patch_parts "${patch_entry}")
        list(GET patch_parts 0 patch_submodule)
        list(GET patch_parts 1 patch_relative)
        set(patch_file "${CMAKE_CURRENT_SOURCE_DIR}/${patch_relative}")
        # JUCE may be this repository's submodule or the shared checkout that
        # CHALKWALK_JUCE_DIR names; everything else is always local.
        if(patch_submodule STREQUAL "JUCE")
            set(patch_dir "${CHALKWALK_JUCE_ROOT}")
        else()
            set(patch_dir "${CMAKE_CURRENT_SOURCE_DIR}/${patch_submodule}")
        endif()

        if(NOT EXISTS "${patch_file}")
            continue()
        endif()
        get_filename_component(patch_name "${patch_file}" NAME)

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch_file}"
            WORKING_DIRECTORY "${patch_dir}"
            RESULT_VARIABLE _patch_present
            ERROR_QUIET OUTPUT_QUIET)
        if(NOT _patch_present EQUAL 0)
            message(STATUS "Applying ${patch_submodule} patch: ${patch_name}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" apply "${patch_file}"
                WORKING_DIRECTORY "${patch_dir}"
                RESULT_VARIABLE _patch_result)
            if(NOT _patch_result EQUAL 0)
                message(FATAL_ERROR
                    "Failed to apply ${patch_name} to ${patch_submodule}. "
                    "Ensure submodules are initialised (git submodule update "
                    "--init --recursive) and its working tree is clean.")
            endif()
        else()
            message(STATUS "${patch_submodule} patch already applied: ${patch_name}")
        endif()
    endforeach()
endif()

add_subdirectory("${CHALKWALK_JUCE_ROOT}" "${CMAKE_BINARY_DIR}/juce-build")

# Configure CLAP JUCE extension
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add Ogg and Vorbis. BUILD_TESTING is forced off so their test suites do not
# register with ctest -- they are EXCLUDE_FROM_ALL here and so never built.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/ogg EXCLUDE_FROM_ALL)
add_subdirectory(modules/vorbis EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# chalkwalk-music -- shared, JUCE-free music theory.
# Submodule: https://github.com/chalkwalk/chalkwalk-music (MIT).
#
# It builds and tests standalone, with no JUCE and no parent, which is the test
# of the boundary. Its own Catch2 suite is turned on here so this project
# verifies its dependency rather than assuming it.
# ---------------------------------------------------------------------------
include(cmake/ChalkwalkLibrary.cmake)
chalkwalk_add_library(music libs/music)

# ---------------------------------------------------------------------------
# chalkwalk-dsp -- shared, JUCE-free DSP primitives.
# Submodule: https://github.com/chalkwalk/chalkwalk-dsp (MIT).
#
# Same arrangement and the same reasoning as chalkwalk-music above. The filter,
# the polyBLEP oscillators, the soft clipper and the Hermite reader lived here
# and in a sibling project, and the two copies had diverged; the shared
# versions take both halves.
#
# It also owns MEASUREMENT now -- `chalkwalk::dsp::measure`, what `AudioMeasure`
# used to be -- and with it the libebur128 dependency that used to be vendored
# in this repository. That is a second target rather than part of the first, so
# the plugin links the primitives without linking a loudness meter; only the
# test and tool targets ask for `measure`. The move was for the usual reason:
# `peak` and `rms` existed three times over across these repositories and
# `fundamentalHz` twice, and a detector nobody has calibrated is how a
# measurement error gets mistaken for a bug (`PRINCIPLES §5`).
# ---------------------------------------------------------------------------
chalkwalk_add_library(dsp libs/dsp)

# ---------------------------------------------------------------------------
# chalkwalk-ninjam -- the NINJAM wire protocol, JUCE-free.
# Submodule: https://github.com/chalkwalk/chalkwalk-ninjam (MIT).
#
# Added after modules/ogg and modules/vorbis above, and not by accident: this
# library vendors its own copies of both, guarded by `if(NOT TARGET ogg)`, so
# whichever project adds them first wins and the second reuses them. Adding
# them twice is not a version conflict -- it is a duplicate CMake target name,
# which fails the configure outright.
#
# The protocol left this repository under MIT while antiphon stays GPLv3. The
# provenance note in its README is the record of why that is defensible: the
# GPLv2 reference sources were read, never vendored, and never entered any
# published history. See PRINCIPLES.md section 6.
# ---------------------------------------------------------------------------
chalkwalk_add_library(ninjam libs/ninjam)

# ---------------------------------------------------------------------------
# chalkwalk-jambot -- the practice room's band, and the chat they answer.
# Submodule: https://github.com/chalkwalk/chalkwalk-jambot (MIT).
#
# These were `src/jambot/` here until they earned their own repository. What
# stays is the HOSTING a practice room needs and a command-line bot does not:
# the loopback server, the room that puts a server and a band together, and
# `NinjamBotClient`, which is the whole of what ties the band to this plugin's
# transport.
#
# The direction of the dependency is the point. Antiphon uses the bots; the
# bots know nothing about Antiphon, and their suite runs without it -- which is
# what the `jambot-boundary` check was guarding towards and why that check is
# gone. A boundary a build enforces does not need a test to describe it.
# ---------------------------------------------------------------------------
chalkwalk_add_library(jambot libs/jambot)

add_subdirectory(src)

# Offline tools. Kept out of src/ because nothing here is part of the plugin --
# antiphon-stems is a headless converter that links neither JUCE's GUI modules
# nor the plugin client.
add_subdirectory(tools)

# Unit and loopback tests (console app, headless)
add_subdirectory(test)
