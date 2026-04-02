#pragma once

#include <onnxruntime_cxx_api.h>

//==============================================================================
/**
 * Returns the single, process-wide Ort::Env shared by all ONNX sessions
 * in this plugin (beat_this).
 *
 * Problem: Ort::Global<void>::api_ is a weak-external template static.  On
 * macOS, dyld may coalesce it with Ableton's private copy (from
 * libonnxruntime_abl.dylib) that was initialised with an older API version,
 * leaving api_ null.  Two consequences were observed:
 *
 *   1. Ort::Env ctor crashes at offset 0x18  (first crash)
 *   2. Ort::AllocatorWithDefaultOptions ctor crashes at offset 0x270 (second)
 *
 * Fix: call Ort::InitApi() *explicitly* before constructing any ORT C++
 * object.  This writes the correct ORT 1.18 API pointer into api_ via our
 * two-level-namespace-bound OrtGetApiBase, overriding whatever dyld may have
 * put there at load time.
 *
 * A single Ort::Env is also used to avoid creating multiple environments
 * alongside Ableton's bundled ONNX Runtime.
 */
inline Ort::Env& getSharedOrtEnv()
{
    static Ort::Env s_env = [] {
        // Explicitly re-initialise api_ from OUR libonnxruntime.1.18.0.dylib.
        // This overrides any null / stale value that dyld may have placed there
        // when coalescing weak symbols across loaded images (particularly
        // Ableton's libonnxruntime_abl.dylib which initialises with an older
        // API version and can leave api_ null for ORT_API_VERSION 18).
        //
        // Ort::InitApi() only exists under ORT_API_MANUAL_INIT; in auto-init
        // mode we write the static directly.  The pointer is non-const
        // (const OrtApi* = pointer-to-const), so direct assignment is legal.
        Ort::Global<void>::api_ = OrtGetApiBase()->GetApi (ORT_API_VERSION);
        return Ort::Env (ORT_LOGGING_LEVEL_WARNING, "mir_express");
    }();
    return s_env;
}
