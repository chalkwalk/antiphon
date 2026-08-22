# AU is macOS-only, and is the only way into Logic Pro and GarageBand, which
# do not load VST3 or CLAP at all. It is deliberately less capable than the
# other two formats: the AU wrapper drops our busLayoutChanged notification
# (see AntiphonAudioProcessor::hasFixedBusLayout), so AU is one stereo bus in,
# one stereo out, and per-remote-channel stems need VST3 or CLAP.
set(antiphon_formats VST3 Standalone)
if(APPLE)
    list(APPEND antiphon_formats AU)
endif()

juce_add_plugin(Antiphon
    COMPANY_NAME "Chalkwalk"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    # These two four-character codes are the plugin's identity to an AU host,
    # and they must be pinned. Left unset, JUCE defaults the manufacturer to
    # 'Manu' -- shared with every other JUCE plugin whose author never set one --
    # and generates the plugin code with string(RANDOM) on every configure
    # (JUCE/extras/Build/CMake/JUCEUtils.cmake:1763). A fresh `cmake -B build`
    # would then mint a new Audio Unit, and every Logic session referring to the
    # old one would break. VST3 and CLAP identify by name and CLAP_ID, which is
    # why this went unnoticed until AU was added.
    PLUGIN_MANUFACTURER_CODE Chlk
    PLUGIN_CODE Antp
    FORMATS ${antiphon_formats}
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
        PracticeServer.cpp
        PracticeRoom.cpp
        MetronomeVoice.cpp
        RemoteUserStrip.cpp
        RemoteChannelRow.cpp
        LocalChannelStrip.cpp
        AntiphonLookAndFeel.cpp
        ChatFormat.cpp
        ClipsortLog.cpp
        SessionWriter.cpp
        AccessibilityAudit.cpp
        ServerBrowserDialog.cpp
        ShortcutsDialog.cpp
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
        chalkwalk::music
        chalkwalk::dsp
        chalkwalk::ninjam
        antiphon_fonts
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        ogg
        vorbis
        vorbisenc
    PUBLIC
        # PUBLIC because PluginProcessor.h owns a PracticeRoom, so the band's
        # headers are part of this target's interface rather than an
        # implementation detail -- anything that includes the processor needs
        # them on its include path too.
        chalkwalk::jambot
        juce::juce_recommended_config_flags)

# Make sure JUCE finds headers
target_include_directories(Antiphon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
