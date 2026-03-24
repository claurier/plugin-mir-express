#pragma once

#include <JuceHeader.h>
#include "ExtractorMood.h"
#include "Spectrum.h"
#include "Dissonance.h"
#include "OnsetFunction.h"
#include "TempoTap.h"
#include "PeakDetection.h"
#include "BTrack.h"

#include <array>
#include <atomic>
#include <vector>

//==============================================================================
/**
 * DescriptorAnalyser
 *
 * Bridges the audio thread and MIRLib extractors on a dedicated worker thread.
 *
 * Thread model:
 *   Audio thread  — pushSamples() mixes channels to mono and writes into two
 *                   lock-free ring buffers (plain arrays + atomic write indices):
 *                     • ring            — 3 s at max 192 kHz  (for mood)
 *                     • dissonanceRing  — 0.5 s at max 192 kHz (for dissonance)
 *
 *   Worker thread — wakes every 250 ms:
 *                     • Always: copies 0.5 s → Spectrum → Dissonance (fast).
 *                     • Every 4th wake (≈ 1 s): copies 3 s → ExtractorMood (slow).
 *                   Results are stored as std::atomic<float>.
 *
 *   Message thread — reads atomics via getAngry() / … / getDissonance().
 */
class DescriptorAnalyser final : public juce::Thread
{
public:
    DescriptorAnalyser();
    ~DescriptorAnalyser() override;

    /** Call from AudioProcessor::prepareToPlay(). Stores the sample rate and
     *  starts the worker thread (idempotent — safe to call multiple times). */
    void prepare (double sampleRate);

    /** Call from AudioProcessor::processBlock(). Mixes all channels to mono
     *  and appends them to both ring buffers. Audio-thread safe. */
    void pushSamples (const juce::AudioBuffer<float>& buffer);

    // ── Results — safe to read from any thread ─────────────────────────────
    float getAngry()      const noexcept { return resultAngry     .load (std::memory_order_relaxed); }
    float getCalm()       const noexcept { return resultCalm      .load (std::memory_order_relaxed); }
    float getHappy()      const noexcept { return resultHappy     .load (std::memory_order_relaxed); }
    float getSad()        const noexcept { return resultSad       .load (std::memory_order_relaxed); }
    float getDissonance() const noexcept { return resultDissonance.load (std::memory_order_relaxed); }
    float  getBPM()             const noexcept { return resultBPM          .load (std::memory_order_relaxed); }
    float  getBPMConfidence()   const noexcept { return resultBPMConfidence.load (std::memory_order_relaxed); }
    float  getBTrackBPM()       const noexcept { return resultBTrackBPM   .load (std::memory_order_relaxed); }
    double getBTrackBeatTime()  const noexcept { return resultBTrackBeatTime.load (std::memory_order_relaxed); }

private:
    void run() override;
    void computeMood();
    void computeDissonance();
    void computeBPM();
    void processBTrack();

    // ── Mood ring buffer (3 s at max 192 kHz) ─────────────────────────────
    static constexpr int kRingSize = 3 * 192000;

    std::array<float, kRingSize> ring {};
    std::atomic<int> writePos { 0 };

    // ── Dissonance ring buffer (0.5 s at max 192 kHz) ─────────────────────
    static constexpr int kDissonanceRingSize = 192000 / 2;

    std::array<float, kDissonanceRingSize> dissonanceRing {};
    std::atomic<int> dissonanceWritePos { 0 };

    // ── Shared state ───────────────────────────────────────────────────────
    std::atomic<double> sampleRate { 44100.0 };

    // ── Results ───────────────────────────────────────────────────────────
    std::atomic<float> resultAngry      { 0.0f };
    std::atomic<float> resultCalm       { 0.0f };
    std::atomic<float> resultHappy      { 0.0f };
    std::atomic<float> resultSad        { 0.0f };
    std::atomic<float> resultDissonance { 0.0f };
    std::atomic<float>  resultBPM              { 0.0f };
    std::atomic<float>  resultBPMConfidence    { 0.0f };
    std::atomic<float>  resultBTrackBPM        { 0.0f };
    // Stores the hi-res timestamp (ms) of the last BTrack beat.
    // Initialised to a large negative value so the display starts with no flash.
    std::atomic<double> resultBTrackBeatTime   { -1.0e9 };

    // All algorithm objects are only ever touched on the worker thread.
    mirlib::ExtractorMood extractor;
    mirlib::Spectrum      spectrum;
    mirlib::Dissonance    dissonance;
    mirlib::OnsetFunction bpmOnsetFunction;  // stateful — call clear() before each window

    // BTrack — hop=512, frame=1024 (BTrack default; assumes 44100 Hz input)
    BTrack              btrack { 512, 1024 };
    int                 btrackReadPos    = 0;      // worker thread only — next ring index to feed
    bool                btrackWasSilent  = true;   // used to detect silence→sound transitions
    std::vector<double> btrackHopBuf    = std::vector<double> (512, 0.0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DescriptorAnalyser)
};
