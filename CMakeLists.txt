# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.22)

# Set the project name
project(Antiphon VERSION 0.1.0)

# enable_testing() must be called at the top level for ctest to work
enable_testing()

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
        set(patch_dir "${CMAKE_CURRENT_SOURCE_DIR}/${patch_submodule}")

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

# Configure JUCE
add_subdirectory(JUCE)

# Configure CLAP JUCE extension
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Add Ogg and Vorbis. BUILD_TESTING is forced off so their test suites do not
# register with ctest -- they are EXCLUDE_FROM_ALL here and so never built.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/ogg EXCLUDE_FROM_ALL)
add_subdirectory(modules/vorbis EXCLUDE_FROM_ALL)

# Add the sources subdirectory
# ---------------------------------------------------------------------------
# chalkwalk-music -- shared, JUCE-free music theory (../ECOSYSTEM.md).
# Submodule: https://github.com/chalkwalk/chalkwalk-music (MIT).
#
# It builds and tests standalone, with no JUCE and no parent, which is the test
# of the boundary. Its own Catch2 suite is turned on here so this project
# verifies its dependency rather than assuming it.
# ---------------------------------------------------------------------------
set(CHALKWALK_MUSIC_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(libs/music)

add_subdirectory(src)

# Offline tools. Kept out of src/ because nothing here is part of the plugin --
# antiphon-stems is a headless converter that links neither JUCE's GUI modules
# nor the plugin client.
add_subdirectory(tools)

# Unit and loopback tests (console app, headless)
add_subdirectory(test)
