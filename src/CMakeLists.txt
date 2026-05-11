juce_add_plugin(Antiphon
    COMPANY_NAME "Chalkwalk"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    FORMATS VST3 Standalone
    # Tells the host the editor needs key events. Defaults to FALSE, which
    # leaves hosts free to keep the keyboard for their own shortcuts and is one
    # half of why no text field could be typed into inside a DAW.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PRODUCT_NAME "Antiphon")

juce_generate_juce_header(Antiphon)

# Embedded product typeface (Inter, OFL-1.1 -- see assets/fonts/OFL.txt).
#
# The surface renders with the same glyphs on every platform: a platform-default
# sans varies by distro and machine, which is a look the product never chose.
# Only the two static cuts we ask for are embedded (every Font in src/ is either
# plain or bold); the variable font is deliberately not vendored, since a face
# JUCE must instance per size reintroduces exactly that variability.
juce_add_binary_data(antiphon_fonts
    NAMESPACE AntiphonFonts
    HEADER_NAME AntiphonFonts.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf)

target_sources(Antiphon
    PRIVATE
        PluginProcessor.cpp
        StandaloneApp.cpp
        PluginEditor.cpp
        NinjamClient.cpp
        NinjamProtocol.cpp
        IntervalClock.cpp
        MetronomeVoice.cpp
        RemoteUserStrip.cpp
        RemoteChannelRow.cpp
        LocalChannelStrip.cpp
        AntiphonLookAndFeel.cpp
        ChatFormat.cpp
        MusicalKey.cpp
        AccessibilityAudit.cpp
        ServerBrowserDialog.cpp
        Sha1.cpp
        VorbisCodec.cpp
)

clap_juce_extensions_plugin(TARGET Antiphon CLAP_ID "com.chalkwalk.antiphon" CLAP_FEATURES "audio-effect" "tool")

target_compile_definitions(Antiphon
    PUBLIC
        # We supply our own JUCEApplication (src/StandaloneApp.cpp) so the window
        # exists before the audio device is opened. See DESIGN.md section 16.
        JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Antiphon
    PRIVATE
        antiphon_fonts
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        ogg
        vorbis
        vorbisenc
    PUBLIC
        juce::juce_recommended_config_flags)

# Make sure JUCE finds headers
target_include_directories(Antiphon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
