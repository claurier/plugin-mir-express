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

# ── beat_this_cpp (neural beat tracker — dev/experiment only) ────────
# The 79 MB ONNX model lives in libs/beat_this_cpp/onnx/beat_this.onnx.
# ONNX Runtime 1.18.0 is downloaded once via FetchContent.
# The model path is baked in at compile time for development use.

include(FetchContent)
set(ONNXRUNTIME_VERSION "1.18.0")

# Select the right pre-built package for macOS.
if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(_ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
else()
    # universal2 covers both x86_64 and arm64.
    set(_ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-universal2-${ONNXRUNTIME_VERSION}.tgz")
endif()

FetchContent_Declare(ONNXRuntime URL ${_ORT_URL})
FetchContent_MakeAvailable(ONNXRuntime)

# Wrap the downloaded dylib in an imported target so we can link against it.
add_library(onnxruntime_imported SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime_imported PROPERTIES
    IMPORTED_LOCATION
        "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.${ONNXRUNTIME_VERSION}.dylib"
    INTERFACE_INCLUDE_DIRECTORIES
        "${onnxruntime_SOURCE_DIR}/include"
)

# beat_this static library — compiled at C++20 (their requirement),
# isolated from our C++17 plugin code.
add_library(beat_this_static STATIC
    libs/beat_this_cpp/Source/beat_this_api.cpp
    libs/beat_this_cpp/Source/MelSpectrogram.cpp
    libs/beat_this_cpp/Source/InferenceProcessor.cpp
    libs/beat_this_cpp/Source/Postprocessor.cpp
)
target_compile_features(beat_this_static PRIVATE cxx_std_20)
target_include_directories(beat_this_static PUBLIC
    libs/beat_this_cpp/Source
    libs/beat_this_cpp/Submodule/pocketfft
    libs/beat_this_cpp/Submodule/miniaudio
)
target_link_libraries(beat_this_static PUBLIC onnxruntime_imported)

# ── aubio (real-time tempo + beat tracker — experiment) ──────────────
# Compiled directly from source — aubio's CMakeLists only builds shared libs.
# We include every .c file except file-I/O and the two FFT back-ends that
# need external libraries (fftw, ipp).  With HAVE_ACCELERATE defined the
# spectral/fft.c module uses Apple's vDSP (already linked by JUCE on macOS).

file(GLOB_RECURSE AUBIO_SOURCES
    "libs/aubio/src/*.c"
)
# Remove file-I/O (needs libsndfile / libav / etc.) and external-FFT back-ends.
list(FILTER AUBIO_SOURCES EXCLUDE REGEX ".*/io/.*")
list(FILTER AUBIO_SOURCES EXCLUDE REGEX "dct_fftw\\.c$")
list(FILTER AUBIO_SOURCES EXCLUDE REGEX "dct_ipp\\.c$")

add_library(aubio_static STATIC ${AUBIO_SOURCES})
target_include_directories(aubio_static PUBLIC libs/aubio/src)
target_compile_definitions(aubio_static PUBLIC
    HAVE_ACCELERATE=1          # use Apple vDSP for FFT (Accelerate already linked by JUCE)
    HAVE_C99_VARARGS_MACROS=1  # always true on Clang/GCC
    # Silence the "AUBIO_UNSTABLE" guard in 0.5.x headers
    AUBIO_UNSTABLE=1
)
# Suppress warnings from aubio's own code (we can't fix them).
target_compile_options(aubio_static PRIVATE -w)

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
    PRIVATE
        # Development-only: absolute path to the beat_this ONNX model.
        BEAT_THIS_ONNX_PATH="${CMAKE_SOURCE_DIR}/libs/beat_this_cpp/onnx/beat_this.onnx"
)

target_link_libraries(MirExpress
    PRIVATE
        juce::juce_audio_utils   # AudioProcessor, AudioProcessorEditor, StandaloneWrapper…
        juce::juce_gui_extra     # Extra GUI utilities
        MIRLib                   # Mood analysis (ExtractorMood + SVM models)
        BTrack                   # Real-time beat tracking
        beat_this_static         # Neural beat tracker (experiment)
        aubio_static             # Real-time tempo + beat (experiment)
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Allow the Standalone (and plugin bundles) to find the ONNX Runtime dylib
# at the path where FetchContent placed it — development builds only.
set_target_properties(MirExpress PROPERTIES
    BUILD_RPATH "${onnxruntime_SOURCE_DIR}/lib"
)
