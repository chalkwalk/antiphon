# antiphon-stems: offline session archive -> WAV stems.
#
# Deliberately the lightest target in the repo. It follows the NinjamTests
# recipe rather than the plugin's: juce_audio_formats for the WAV writer,
# juce_events for the message manager, and nothing else. juce_audio_utils is NOT
# linked -- it drags in juce_audio_processors/juce_audio_devices and thus
# X11/ALSA, which would stop this running on a headless box, which is exactly
# where you would want to batch-convert an archive.
#
# The Ogg decoder is no longer re-listed here: it moved to chalkwalk-ninjam and
# arrives as a library target, which is the whole point of the extraction. What
# is still re-listed from src/ follows the convention explained at the top of
# test/CMakeLists.txt.

juce_add_console_app(AntiphonStems
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "antiphon-stems")

juce_generate_juce_header(AntiphonStems)

target_sources(AntiphonStems PRIVATE
        StemsMain.cpp
        ${CMAKE_SOURCE_DIR}/src/ClipsortLog.cpp)

target_compile_definitions(AntiphonStems PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(AntiphonStems
    PRIVATE chalkwalk::music chalkwalk::dsp chalkwalk::ninjam
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(AntiphonStems PRIVATE ${CMAKE_SOURCE_DIR}/src)

# antiphon-voicelab: render one bot voice to a WAV and measure it.
#
# A development instrument for tuning the synthesis by ear, not something a
# player ever runs -- so it is built but not installed. It links the band's own
# sources rather than copies of them, which is the point: what it renders is
# what the room hears, and the numbers it prints come from the same
# src/AudioMeasure.h the unit tests assert against.
#
# Same light recipe as antiphon-stems: no juce_audio_utils, so it runs on a
# headless box.

juce_add_console_app(AntiphonVoiceLab
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "antiphon-voicelab")

juce_generate_juce_header(AntiphonVoiceLab)

target_sources(AntiphonVoiceLab PRIVATE
        VoiceLabMain.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotBand.cpp
        ${CMAKE_SOURCE_DIR}/src/MusicalKey.cpp)

target_compile_definitions(AntiphonVoiceLab PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(AntiphonVoiceLab
    PRIVATE chalkwalk::music chalkwalk::dsp ebur128
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(AntiphonVoiceLab PRIVATE ${CMAKE_SOURCE_DIR}/src)

# antiphon-bandlab: a control surface for the band's synthesis.
#
# The one target here that is a GUI, and the one that links juce_audio_devices
# and juce_audio_utils -- it has to open an output and loop what it renders, so
# the headless rule the other two follow does not apply and cannot. It will not
# start on a machine with no display or no audio device, which is fine: it is a
# tuning instrument, and tuning is done at a desk with speakers.
#
# Like antiphon-voicelab it links the band's own sources rather than copies, so
# what it plays is what the room plays.

juce_add_gui_app(AntiphonBandLab
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "antiphon-bandlab")

juce_generate_juce_header(AntiphonBandLab)

target_sources(AntiphonBandLab PRIVATE
        BandLabMain.cpp
        ${CMAKE_SOURCE_DIR}/src/BandPatch.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotBand.cpp
        ${CMAKE_SOURCE_DIR}/src/MusicalKey.cpp)

target_compile_definitions(AntiphonBandLab PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(AntiphonBandLab
    PRIVATE chalkwalk::music chalkwalk::dsp ebur128
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(AntiphonBandLab PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# antiphon-practice: host a practice room and wait for you to join it.
#
# The room is a destination rather than a mode, so hosting it and playing in it
# are separate jobs: this process is the far side of the connection and renders
# nothing locally. Console app, no audio device, no GUI -- juce_audio_utils is
# NOT linked, for the same reason as AntiphonStems.
#
# Exists because nothing in src/ constructs a PracticeRoom yet; see ROADMAP.md.
# ---------------------------------------------------------------------------

juce_add_console_app(AntiphonPractice
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "antiphon-practice")

juce_generate_juce_header(AntiphonPractice)

target_sources(AntiphonPractice PRIVATE
        PracticeRoomMain.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeRoom.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeServer.cpp
        ${CMAKE_SOURCE_DIR}/src/PracticeBot.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotBand.cpp
        ${CMAKE_SOURCE_DIR}/src/BandPatch.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotAddress.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotLanguage.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotAnswer.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotChat.cpp
        ${CMAKE_SOURCE_DIR}/src/jambot/BotNames.cpp
        ${CMAKE_SOURCE_DIR}/src/ChatFormat.cpp
        ${CMAKE_SOURCE_DIR}/src/MusicalKey.cpp
        ${CMAKE_SOURCE_DIR}/src/NinjamClient.cpp
        ${CMAKE_SOURCE_DIR}/src/MetronomeVoice.cpp
        ${CMAKE_SOURCE_DIR}/src/ClipsortLog.cpp
        ${CMAKE_SOURCE_DIR}/src/SessionWriter.cpp)

target_compile_definitions(AntiphonPractice PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # runDispatchLoopUntil is gated behind this. The bots' chat handling and
        # their arrival timer both run on the message thread, so this process
        # has to pump it or the band is deaf.
        JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(AntiphonPractice
    PRIVATE chalkwalk::music chalkwalk::dsp chalkwalk::ninjam
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(AntiphonPractice PRIVATE ${CMAKE_SOURCE_DIR}/src)
