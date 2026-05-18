# antiphon-stems: offline session archive -> WAV stems.
#
# Deliberately the lightest target in the repo. It follows the NinjamTests
# recipe rather than the plugin's: juce_audio_formats for the WAV writer,
# juce_events for the message manager, and nothing else. juce_audio_utils is NOT
# linked -- it drags in juce_audio_processors/juce_audio_devices and thus
# X11/ALSA, which would stop this running on a headless box, which is exactly
# where you would want to batch-convert an archive.
#
# VorbisCodec.cpp is re-listed here rather than shared, matching the convention
# explained at the top of test/CMakeLists.txt. It is the one source under src/
# that includes no JUCE at all, so it compiles into any target.

juce_add_console_app(AntiphonStems
    COMPANY_NAME "Chalkwalk"
    PRODUCT_NAME "antiphon-stems")

juce_generate_juce_header(AntiphonStems)

target_sources(AntiphonStems PRIVATE
        StemsMain.cpp
        ${CMAKE_SOURCE_DIR}/src/ClipsortLog.cpp
        ${CMAKE_SOURCE_DIR}/src/VorbisCodec.cpp)

target_compile_definitions(AntiphonStems PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(AntiphonStems
    PRIVATE
        juce::juce_audio_formats
        juce::juce_events
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

target_include_directories(AntiphonStems PRIVATE ${CMAKE_SOURCE_DIR}/src)
