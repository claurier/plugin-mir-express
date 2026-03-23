cmake_minimum_required(VERSION 3.22)

project(MirExpress VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── JUCE (git submodule at libs/JUCE) ────────────────────────────
add_subdirectory(libs/JUCE)

# ── Plugin target ─────────────────────────────────────────────────
# AU is macOS-only; JUCE will only build it when targeting Apple.
# VST3 and Standalone are cross-platform (macOS, Windows, Linux).
juce_add_plugin(MirExpress
    COMPANY_NAME                "MirExpress"
    BUNDLE_ID                   "com.mirexpress.plugin"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    MrEx
    PLUGIN_CODE                 Mexs
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Mir Express"
)

# Generates JuceHeader.h which re-exports all requested module headers.
juce_generate_juce_header(MirExpress)

target_sources(MirExpress
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
)

target_compile_definitions(MirExpress
    PUBLIC
        # Disable built-in web/curl to reduce dependencies.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # Do not attempt to replace a VST2 with the same ID.
        JUCE_VST3_CAN_REPLACE_VST2=0
        # Uncomment the line below if you hold a JUCE Personal/Pro license:
        # JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(MirExpress
    PRIVATE
        juce::juce_audio_utils   # AudioProcessor, AudioProcessorEditor, StandaloneWrapper…
        juce::juce_gui_extra     # Extra GUI utilities
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
