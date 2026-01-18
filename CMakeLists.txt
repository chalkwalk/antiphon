# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.22)

# Set the project name
project(Ninjam VERSION 0.1.0)

# enable_testing() must be called at the top level for ctest to work
enable_testing()

# JUCE patches, applied to the submodule at configure time.
#
# Each is idempotent: "git apply --reverse --check" succeeds only when the
# patch is already in the working tree, so a second configure is a no-op and a
# fresh clone gets it applied once.
set(NINJAM_JUCE_PATCHES
    "${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-linux-plugin-keyboard-focus.patch")

find_package(Git QUIET)
if(GIT_FOUND)
    foreach(juce_patch IN LISTS NINJAM_JUCE_PATCHES)
        if(NOT EXISTS "${juce_patch}")
            continue()
        endif()
        get_filename_component(juce_patch_name "${juce_patch}" NAME)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${juce_patch}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/JUCE"
            RESULT_VARIABLE _juce_patch_present
            ERROR_QUIET OUTPUT_QUIET)
        if(NOT _juce_patch_present EQUAL 0)
            message(STATUS "Applying JUCE patch: ${juce_patch_name}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" apply "${juce_patch}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/JUCE"
                RESULT_VARIABLE _juce_patch_result)
            if(NOT _juce_patch_result EQUAL 0)
                message(FATAL_ERROR
                    "Failed to apply ${juce_patch_name} to the JUCE submodule. "
                    "Ensure submodules are initialised (git submodule update "
                    "--init --recursive) and the JUCE working tree is clean.")
            endif()
        else()
            message(STATUS "JUCE patch already applied: ${juce_patch_name}")
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
add_subdirectory(src)

# Unit and loopback tests (console app, headless)
add_subdirectory(test)
