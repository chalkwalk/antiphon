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
        MetronomeVoiceTests.cpp
        GainUtilsTests.cpp
        SyncStateTests.cpp
        AudioDeviceStartupTests.cpp
        ShortcutsTests.cpp
        AccessibilityAuditTests.cpp
        ChatFormatTests.cpp
        BotAnswerTests.cpp
        BandPlayStateTests.cpp
        RoomHarmonyTests.cpp
        BotChatTests.cpp
        KeyTagTests.cpp
        SharedContractTests.cpp
        LeadLineTests.cpp
        AudioMeasureTests.cpp
        BotDspTests.cpp
        BandPatchTests.cpp
        BotBandTests.cpp
        BotAddressTests.cpp
        BotLanguageTests.cpp
        BotNamesTests.cpp
        ClipsortLogTests.cpp
        StemRenderTests.cpp
        RunGateTests.cpp
        EchoScheduleTests.cpp
        HostGridTests.cpp
        EchoBankTests.cpp
        GainRampTests.cpp
        TransmitSpansTests.cpp
        FakeNinjamServer.cpp
        LoopbackTests.cpp
        PracticeServerTests.cpp
        PracticeRoomTests.cpp
        AudioLoopbackTests.cpp
        RealServerTests.cpp
        ReferenceFixtureTests.cpp
        ${CMAKE_SOURCE_DIR}/src/NinjamClient.cpp
        ${CMAKE_SOURCE_DIR}/src/MetronomeVoice.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotBand.cpp
        ${CMAKE_SOURCE_DIR}/src/BandPatch.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotAddress.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotLanguage.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotNames.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeServer.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeBot.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeRoom.cpp
        ${CMAKE_SOURCE_DIR}/src/ChatFormat.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotAnswer.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotChat.cpp
        ${CMAKE_SOURCE_DIR}/src/ClipsortLog.cpp
        ${CMAKE_SOURCE_DIR}/src/SessionWriter.cpp
        ${CMAKE_SOURCE_DIR}/src/MusicalKey.cpp
        ${CMAKE_SOURCE_DIR}/src/AccessibilityAudit.cpp
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
        chalkwalk::music
        chalkwalk::dsp
        chalkwalk::ninjam
        ebur128
        juce::juce_audio_formats
        juce::juce_events
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(NinjamTests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ninjam-unit-tests COMMAND NinjamTests)
# Raised from 120, which the suite outgrew rather than regressed into.
#
# Roughly: BotBand 57 s, Loopback 22 s, PracticeRoom 17 s, BotDsp 17 s,
# AudioLoopback 13 s. Most of that is rendering audio and measuring it, which is
# the work these tests exist to do -- the band is now the largest part of the
# codebase and it is tested by listening to it with instruments rather than by
# checking flags.
#
# A timeout is a backstop against a hang, not a performance budget, so it is set
# well clear of the real figure. The real figure is still worth watching: two
# minutes is a long time to wait between edits, and `ROADMAP.md` tracks
# shortening it.
set_tests_properties(ninjam-unit-tests PROPERTIES TIMEOUT 300)

# Source-level guard; see the script for why this one is worth a test of its
# own. The bug it catches is invisible in the standalone, which is where we do
# most of our manual testing.
add_test(NAME no-build-standalone-macro
         COMMAND ${CMAKE_COMMAND}
                 -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/cmake/CheckNoStandaloneMacro.cmake)

# The same shape, guarding the boundary the music layer is being moved across.
# A `juce::String` added in passing still builds and still passes; it is only
# discovered when somebody tries to move the file.
# The bots are being extracted; this fails when the set of things that would
# have to come with them changes. See the script.
add_test(NAME jambot-boundary
         COMMAND ${CMAKE_COMMAND}
                 -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/cmake/CheckJambotBoundary.cmake)

add_test(NAME music-layer-is-juce-free
         COMMAND ${CMAKE_COMMAND}
                 -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/cmake/CheckMusicLayerIsJuceFree.cmake)

# ---------------------------------------------------------------------------
# Accessibility audit over the real component tree.
#
# NinjamTests cannot hold the UI: PluginProcessor and the editor need the
# JucePlugin_* defines that only juce_add_plugin supplies, and linking
# juce_gui_basics there would break the headless run. So this is a separate
# console app that links the plugin's own shared-code library and drives the
# genuine editor. Exit code is the finding count.
# ---------------------------------------------------------------------------

juce_add_console_app(AntiphonAudit
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "AntiphonAudit")

target_sources(AntiphonAudit PRIVATE AuditMain.cpp FakeNinjamServer.cpp
                                 AccessibleNamingTests.cpp)

# Reuse the plugin's generated JuceHeader.h and its JucePlugin_* defines rather
# than generating a second, subtly different set.
target_include_directories(AntiphonAudit
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/src/Antiphon_artefacts/JuceLibraryCode)

# Same as the plugin: without these juce_gui_extra pulls in GTK for a web
# browser this app has no use for.
target_compile_definitions(AntiphonAudit
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(AntiphonAudit
    PRIVATE chalkwalk::music chalkwalk::dsp chalkwalk::ninjam
    PRIVATE
        Antiphon
        antiphon_fonts
        juce::juce_audio_utils
        ogg vorbis vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

# Fails the build when a control arrives without a name a reader can announce.
# The exit code is the finding count; the report goes to stdout.
add_test(NAME accessibility-audit COMMAND AntiphonAudit)
# It builds a device picker, which enumerates audio devices. Enumerating is not
# opening, so it should not wedge -- but a build gate must fail rather than hang.
set_tests_properties(accessibility-audit PROPERTIES TIMEOUT 120)
