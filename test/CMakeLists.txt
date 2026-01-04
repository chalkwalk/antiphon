# Headless console app running the juce::UnitTest suite.
#
# Production sources are re-listed here rather than shared via an object
# library: juce_generate_juce_header() only works on targets created by a
# juce_add_* function, and every src/*.h opens with #include <JuceHeader.h>.
# Each target generates its own JuceHeader.h into its own private include
# directory, so the same .cpp compiles correctly in both.
#
# PluginProcessor.cpp and all UI sources are deliberately absent -- they need
# the JucePlugin_* defines that only juce_add_plugin supplies.

juce_add_console_app(NinjamTests
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "NinjamTests")

juce_generate_juce_header(NinjamTests)

target_sources(NinjamTests
    PRIVATE
        TestMain.cpp
        Sha1Tests.cpp
        VorbisCodecTests.cpp
        NinjamProtocolTests.cpp
        IntervalClockTests.cpp
        MetronomeVoiceTests.cpp
        GainUtilsTests.cpp
        FakeNinjamServer.cpp
        LoopbackTests.cpp
        AudioLoopbackTests.cpp
        RealServerTests.cpp
        ReferenceFixtureTests.cpp
        ${CMAKE_SOURCE_DIR}/src/NinjamClient.cpp
        ${CMAKE_SOURCE_DIR}/src/IntervalClock.cpp
        ${CMAKE_SOURCE_DIR}/src/MetronomeVoice.cpp
        ${CMAKE_SOURCE_DIR}/src/Sha1.cpp
        ${CMAKE_SOURCE_DIR}/src/VorbisCodec.cpp
        ${CMAKE_SOURCE_DIR}/src/NinjamProtocol.cpp
)

target_compile_definitions(NinjamTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # runDispatchLoopUntil is only declared when modal loops are permitted;
        # the loopback tests need it to drain callAsync.
        JUCE_MODAL_LOOPS_PERMITTED=1)

# juce_audio_formats pulls in juce_audio_basics -> juce_core.
# juce_audio_utils is deliberately NOT linked: it depends on
# juce_audio_processors/juce_audio_devices and thus X11/freetype/ALSA, which
# would break headless runs.
target_link_libraries(NinjamTests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(NinjamTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ninjam-unit-tests COMMAND NinjamTests)
set_tests_properties(ninjam-unit-tests PROPERTIES TIMEOUT 120)

# Temporary interop harness around the official reference client, plus the
# interop tests that drive it. Everything to do with it lives in refclient/ so
# that deleting that one directory removes it completely -- the guard below is
# what makes the rest of the build survive its absence. See test/README.md.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/refclient/CMakeLists.txt)
  add_subdirectory(refclient)
endif()
