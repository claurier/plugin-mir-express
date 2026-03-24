cmake_minimum_required(VERSION 3.22)

project(MirExpress VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Auto-initialize git submodules (JUCE, MIRLib) ─────────────────
# Attempts to pull submodules automatically during configure.
# Falls back to a clear error message if git is unavailable or fails
# (e.g. no SSH agent, no network) so the developer knows exactly what
# to run manually.
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    message(STATUS "Updating git submodules...")
    execute_process(
        COMMAND           ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        RESULT_VARIABLE   GIT_SUBMODULE_RESULT
        ERROR_VARIABLE    GIT_SUBMODULE_ERROR
    )
    if(NOT GIT_SUBMODULE_RESULT EQUAL "0")
        message(WARNING "git submodule update failed:\n${GIT_SUBMODULE_ERROR}")
    endif()
endif()

# Hard check: if a submodule directory is still empty give an
# actionable error rather than a confusing "no CMakeLists.txt" crash.
foreach(_submodule libs/JUCE libs/MIRLib libs/BTrack)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${_submodule}/CMakeLists.txt")
        message(FATAL_ERROR
            "\nSubmodule '${_submodule}' is missing or empty.\n"
            "Please run:\n"
            "    git submodule update --init --recursive\n"
            "then re-run cmake.")
    endif()
endforeach()

# ── JUCE (git submodule at libs/JUCE) ────────────────────────────
add_subdirectory(libs/JUCE)

# ── MIRLib (local library at libs/MIRLib) ────────────────────────
add_subdirectory(libs/MIRLib)

# ── BTrack (beat tracker) ─────────────────────────────────────────
# BTrack's own CMakeLists.txt is not suitable for add_subdirectory
# (hard-coded Homebrew paths, broken relative includes). We build it
# directly using the three source files it needs.
add_library(BTrack STATIC
    libs/BTrack/src/BTrack.cpp
    libs/BTrack/src/OnsetDetectionFunction.cpp
    libs/BTrack/libs/kiss_fft130/kiss_fft.c
)
target_include_directories(BTrack PUBLIC
    libs/BTrack/src
    libs/BTrack/libs/kiss_fft130
)
target_compile_definitions(BTrack PUBLIC USE_KISS_FFT)
# kiss_fft may use M_PI etc.
target_compile_definitions(BTrack PRIVATE _USE_MATH_DEFINES)
# BTrack.cpp includes samplerate.h — provide the header path from MIRLib's bundled copy.
target_include_directories(BTrack PRIVATE
    libs/MIRLib/Libs/libsamplerate-0.2.2/include
)

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
        src/DescriptorAnalyser.cpp
        src/DescriptorDisplay.cpp
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
        MIRLib                   # Mood analysis (ExtractorMood + SVM models)
        BTrack                   # Real-time beat tracking
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
