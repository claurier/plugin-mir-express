#pragma once

#include <JuceHeader.h>
#include "ExtractorMood.h"

#include <array>
#include <atomic>

//==============================================================================
/**
 * MoodAnalyser
 *
 * Bridges the audio thread and ExtractorMood on a dedicated worker thread.
 *
 * Thread model:
 *   Audio thread  — pushSamples() mixes channels to mono and writes into a
 *                   lock-free ring buffer (plain array + atomic write index).
 *
 *   Worker thread — wakes up every second, copies the last 3 s from the ring
 *                   buffer into a contiguous vector, calls ExtractorMood::process(),
 *                   then stores the four mood scores as atomics.
 *
 *   Message thread — reads the atomic results via getAngry() / getCalm() /
 *                    getHappy() / getSad() at any time.
 *
 * Ring-buffer safety:
 *   kRingSize = 3 × 192 000 samples. The audio thread can write at most
 *   192 000 samples per second; the worker consumes them at most once per
 *   second. This gives a two-second safety margin before any sample the
 *   worker is reading could be overwritten.
 */
class MoodAnalyser final : public juce::Thread
{
public:
    MoodAnalyser();
    ~MoodAnalyser() override;

    /** Call from AudioProcessor::prepareToPlay(). Stores the sample rate and
     *  starts the worker thread (idempotent: safe to call multiple times). */
    void prepare (double sampleRate);

    /** Call from AudioProcessor::processBlock(). Mixes all channels to mono
     *  and appends them to the ring buffer. Audio-thread safe. */
    void pushSamples (const juce::AudioBuffer<float>& buffer);

    // ── Results ── safe to read from any thread ────────────────────────────
    float getAngry() const noexcept { return resultAngry.load (std::memory_order_relaxed); }
    float getCalm()  const noexcept { return resultCalm .load (std::memory_order_relaxed); }
    float getHappy() const noexcept { return resultHappy.load (std::memory_order_relaxed); }
    float getSad()   const noexcept { return resultSad  .load (std::memory_order_relaxed); }

private:
    void run() override;

    // ── Ring buffer ────────────────────────────────────────────────────────
    static constexpr int kRingSize = 3 * 192000;  // 3 s at max 192 kHz

    std::array<float, kRingSize> ring {};
    std::atomic<int>    writePos   { 0 };
    std::atomic<double> sampleRate { 44100.0 };

    // ── Results ────────────────────────────────────────────────────────────
    std::atomic<float> resultAngry { 0.0f };
    std::atomic<float> resultCalm  { 0.0f };
    std::atomic<float> resultHappy { 0.0f };
    std::atomic<float> resultSad   { 0.0f };

    // ExtractorMood is only ever touched on the worker thread.
    mirlib::ExtractorMood extractor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoodAnalyser)
};
