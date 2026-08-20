# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.22)

# Set the project name
project(Antiphon VERSION 0.1.0)

# enable_testing() must be called at the top level for ctest to work
enable_testing()

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
# libebur128 (MIT) -- ITU-R BS.1770 loudness.
#
# Adopted rather than maintained, on a standing rule: take the
# dependency when the thing has a SPECIFICATION you could fail to meet. Our own
# K-weighting and gating agreed with ffmpeg to inside 0.05 LU, so this is not a
# bug fix -- it is refusing to own a standard whose next revision, or whose
# short-term and range measures, we would have to track by hand.
#
# The target is declared here rather than by add_subdirectory: the vendored
# CMakeLists declares cmake_minimum_required(VERSION 2.8.12), which CMake 4
# refuses outright, and it also builds a shared library, tests and pkg-config
# files that a static consumer has no use for. The library itself is one C
# file.
#
# The bundled queue/ is used on every platform rather than only where
# sys/queue.h is missing, which is what upstream's try_compile decides. One
# copy on all three platforms is one behaviour to reason about, and Windows
# has no system sys/queue.h at all -- so the conditional could only ever
# produce a difference nobody wanted.
# ---------------------------------------------------------------------------
add_library(ebur128 STATIC modules/libebur128/ebur128/ebur128.c)
target_include_directories(ebur128
    PUBLIC  modules/libebur128/ebur128
    PRIVATE modules/libebur128/ebur128/queue)
set_target_properties(ebur128 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_definitions(ebur128 PRIVATE _USE_MATH_DEFINES)
endif()
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
    target_link_libraries(ebur128 PRIVATE ${MATH_LIBRARY})
endif()

# Add the sources subdirectory
# ---------------------------------------------------------------------------
# chalkwalk-music -- shared, JUCE-free music theory.
# Submodule: https://github.com/chalkwalk/chalkwalk-music (MIT).
#
# It builds and tests standalone, with no JUCE and no parent, which is the test
# of the boundary. Its own Catch2 suite is turned on here so this project
# verifies its dependency rather than assuming it.
# ---------------------------------------------------------------------------
set(CHALKWALK_MUSIC_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(libs/music)

# ---------------------------------------------------------------------------
# chalkwalk-dsp -- shared, JUCE-free DSP primitives.
# Submodule: https://github.com/chalkwalk/chalkwalk-dsp (MIT).
#
# Same arrangement and the same reasoning as chalkwalk-music above. The filter,
# the polyBLEP oscillators, the soft clipper and the Hermite reader lived here
# and in Lockstep, and the two copies had diverged; the shared versions take
# both halves.
# ---------------------------------------------------------------------------
set(CHALKWALK_DSP_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(libs/dsp)

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
set(CHALKWALK_NINJAM_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(libs/ninjam)

add_subdirectory(src)

# Offline tools. Kept out of src/ because nothing here is part of the plugin --
# antiphon-stems is a headless converter that links neither JUCE's GUI modules
# nor the plugin client.
add_subdirectory(tools)

# Unit and loopback tests (console app, headless)
add_subdirectory(test)
