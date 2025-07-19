juce_add_plugin(Ninjam
    COMPANY_NAME "Chalkwalk"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    FORMATS VST3 Standalone
    PRODUCT_NAME "Ninjam")

juce_generate_juce_header(Ninjam)

target_sources(Ninjam
    PRIVATE
        PluginProcessor.cpp
        PluginEditor.cpp
        NinjamClient.cpp
        utils/sha1.cpp
)

clap_juce_extensions_plugin(TARGET Ninjam CLAP_ID "com.chalkwalk.ninjam" CLAP_FEATURES "audio-effect" "tool")

target_compile_definitions(Ninjam
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Ninjam
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        ogg
        vorbis
        vorbisenc
        vorbisfile
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Make sure JUCE finds headers
target_include_directories(Ninjam PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
