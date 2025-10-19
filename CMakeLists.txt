# Set the minimum version of CMake that can be used
cmake_minimum_required(VERSION 3.22)

# Set the project name
project(Ninjam VERSION 0.1.0)

# enable_testing() must be called at the top level for ctest to work
enable_testing()

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
