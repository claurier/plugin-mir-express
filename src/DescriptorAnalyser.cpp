#include "DescriptorAnalyser.h"

// aubio.h is already included via DescriptorAnalyser.h
#include "Types.h"       // mirlib::realVector = std::vector<float>
#include <samplerate.h>  // libsamplerate — src_simple()
#include <cmath>

//==============================================================================
DescriptorAnalyser::DescriptorAnalyser()
    : juce::Thread ("DescriptorWorker")
{
    // Set up the onset function filter bank once (spectrum size for a 2048-sample frame).
    bpmOnsetFunction.parameterSize = 2048 / 2 + 1;
    bpmOnsetFunction.setup();

    // Attempt to load the beat_this ONNX model. The path is baked in at compile time
    // (BEAT_THIS_ONNX_PATH). If the file is missing or the ONNX session fails to
    // initialise, beatThisProcessor stays null and beat_this is silently disabled.
    // Create aubio tempo tracker (44100 Hz nominal; recreated on sample-rate change).
    aubioTempo  = new_aubio_tempo ("default", kAubioBufSize, kAubioHopSize, 44100);
    aubioInput  = new_fvec (kAubioHopSize);
    aubioOutput = new_fvec (2);
    if (aubioTempo != nullptr)
        aubio_tempo_set_silence (aubioTempo, -60.f);  // built-in silence gate at -60 dB

#ifdef BEAT_THIS_ONNX_PATH
    try
    {
        beatThisProcessor = std::make_unique<BeatThis::BeatThis> (BEAT_THIS_ONNX_PATH);
    }
    catch (const std::exception& e)
    {
        DBG ("beat_this init failed: " << e.what());
        beatThisProcessor = nullptr;
    }
    catch (...)
    {
        DBG ("beat_this init failed: unknown error");
        beatThisProcessor = nullptr;
    }
#endif
}

DescriptorAnalyser::~DescriptorAnalyser()
{
    stopThread (3000);

    if (aubioTempo  != nullptr) { del_aubio_tempo (aubioTempo);  aubioTempo  = nullptr; }
    if (aubioInput  != nullptr) { del_fvec (aubioInput);         aubioInput  = nullptr; }
    if (aubioOutput != nullptr) { del_fvec (aubioOutput);        aubioOutput = nullptr; }
    aubio_cleanup();
}

//==============================================================================
void DescriptorAnalyser::prepare (double sr)
{
    sampleRate.store (sr, std::memory_order_release);

    // Sync read positions so both BTrack and aubio start from "now" on
    // the first call rather than replaying stale ring data.
    const int currentPos = writePos.load (std::memory_order_acquire);
    btrackReadPos = currentPos;
    aubioReadPos  = currentPos;

    // Recreate aubio at the new sample rate if it changed.
    const uint_t newSR = static_cast<uint_t> (sr);
    if (aubioTempo != nullptr) { del_aubio_tempo (aubioTempo); aubioTempo = nullptr; }
    aubioTempo = new_aubio_tempo ("default", kAubioBufSize, kAubioHopSize, newSR);
    if (aubioTempo != nullptr)
        aubio_tempo_set_silence (aubioTempo, -60.f);

    if (!isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

//==============================================================================
void DescriptorAnalyser::pushSamples (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    const float channelScale = 1.0f / static_cast<float> (numChannels);
    int pos  = writePos          .load (std::memory_order_relaxed);
    int dpos = dissonanceWritePos.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getSample (ch, i);

        const float mono = sum * channelScale;
        ring            [static_cast<size_t> (pos)]  = mono;
        dissonanceRing  [static_cast<size_t> (dpos)] = mono;

        pos  = (pos  + 1) % kRingSize;
        dpos = (dpos + 1) % kDissonanceRingSize;
    }

    writePos          .store (pos,  std::memory_order_release);
    dissonanceWritePos.store (dpos, std::memory_order_release);
}

//==============================================================================
void DescriptorAnalyser::run()
{
    int moodCounter = 0;
    int bpmCounter  = 0;

    while (!threadShouldExit())
    {
        wait (250);   // wake every 250 ms

        if (threadShouldExit())
            break;

        computeDissonance();          // every 250 ms  (0.5 s window)
        processBTrack();              // every 250 ms  (continuous feed)
        processAubio();               // every 250 ms  (continuous feed)

        if (++moodCounter >= 4)       // every ~1 s    (3 s window)
        {
            moodCounter = 0;
            computeMood();
        }

        if (++bpmCounter >= 8)        // every ~2 s    (3 s window)
        {
            bpmCounter = 0;
            computeBPM();
        }

        if (++beatThisCounter >= 8)   // every ~2 s    (3 s window, neural inference)
        {
            beatThisCounter = 0;
            computeBeatThis();
        }
    }
}

//==============================================================================
// Resample `in` from `fromSR` to 44100 Hz using libsamplerate.
// If the rates already match the vector is returned as-is (no allocation).
static mirlib::realVector resampleTo44100 (const mirlib::realVector& in, double fromSR)
{
    constexpr double kTargetSR = 44100.0;
    if (std::abs (fromSR - kTargetSR) < 1.0)
        return in;

    const double ratio      = kTargetSR / fromSR;
    const int    outputSize = static_cast<int> (std::ceil (static_cast<double> (in.size()) * ratio));
    mirlib::realVector out (static_cast<size_t> (outputSize));

    SRC_DATA data {};
    data.data_in       = in.data();
    data.data_out      = out.data();
    data.input_frames  = static_cast<long> (in.size());
    data.output_frames = static_cast<long> (outputSize);
    data.src_ratio     = ratio;
    data.end_of_input  = 1;
    src_simple (&data, SRC_SINC_FASTEST, 1);

    out.resize (static_cast<size_t> (data.output_frames_gen));
    return out;
}

//==============================================================================
void DescriptorAnalyser::computeMood()
{
    const double sr = sampleRate.load (std::memory_order_acquire);
    const int windowSamples = juce::jmin (static_cast<int> (sr * 3.0), kRingSize);

    // Copy the last 3 s from the mood ring buffer into a contiguous vector.
    const int endPos = writePos.load (std::memory_order_acquire);
    mirlib::realVector samples (static_cast<size_t> (windowSamples));

    for (int i = 0; i < windowSamples; ++i)
    {
        const int idx = (endPos - windowSamples + i + kRingSize) % kRingSize;
        samples[static_cast<size_t> (i)] = ring[static_cast<size_t> (idx)];
    }

    // ExtractorMood's samplerateConvert asserts(false) when sr ≠ 44100.
    // Pre-convert here so the extractor always sees exactly 44100.
    const mirlib::realVector samples44k = resampleTo44100 (samples, sr);
    extractor.setSamplerate (44100.0);
    extractor.process (const_cast<mirlib::realVector&> (samples44k));

    resultAngry.store (extractor.getAngry(), std::memory_order_release);
    resultCalm .store (extractor.getCalm(),  std::memory_order_release);
    resultHappy.store (extractor.getHappy(), std::memory_order_release);
    resultSad  .store (extractor.getSad(),   std::memory_order_release);
}

//==============================================================================
void DescriptorAnalyser::computeDissonance()
{
    const double sr = sampleRate.load (std::memory_order_acquire);
    if (sr < 1.0) return;

    const int windowSamples = juce::jmin (static_cast<int> (sr * 0.5), kDissonanceRingSize);
    const int frameSize     = 2048;
    const int hopSize       = 1024;

    // Copy the last 0.5 s from the dissonance ring buffer.
    const int endPos = dissonanceWritePos.load (std::memory_order_acquire);
    mirlib::realVector windowBuffer (static_cast<size_t> (windowSamples));

    for (int i = 0; i < windowSamples; ++i)
    {
        const int idx = (endPos - windowSamples + i + kDissonanceRingSize) % kDissonanceRingSize;
        windowBuffer[static_cast<size_t> (i)] = dissonanceRing[static_cast<size_t> (idx)];
    }

    // Process frame by frame, accumulate dissonance.
    mirlib::realVector spectrumBuffer;
    float total = 0.0f;
    int   count = 0;

    for (int start = 0; start + frameSize <= windowSamples; start += hopSize)
    {
        mirlib::realVector frame (windowBuffer.begin() + start,
                                  windowBuffer.begin() + start + frameSize);

        spectrum.setInputBuffer  (frame);
        spectrum.setOutputBuffer (spectrumBuffer);
        spectrum.process();

        dissonance.setInputBuffer (spectrumBuffer);
        dissonance.process();

        total += static_cast<float> (dissonance.getOutputValue());
        ++count;
    }

    if (count > 0)
        resultDissonance.store (total / static_cast<float> (count),
                                std::memory_order_release);
}

//==============================================================================
void DescriptorAnalyser::computeBPM()
{
    const double sr = sampleRate.load (std::memory_order_acquire);
    if (sr < 1.0) return;

    const int windowSamples = juce::jmin (static_cast<int> (sr * 3.0), kRingSize);
    const int frameSize     = 2048;
    const int hopSize       = 512;   // TempoTap formula assumes MIRLIB_SAMPLERATE / 512 frames/s

    // Copy the last 3 s from the mood ring buffer.
    const int endPos = writePos.load (std::memory_order_acquire);
    mirlib::realVector windowBuffer (static_cast<size_t> (windowSamples));

    for (int i = 0; i < windowSamples; ++i)
    {
        const int idx = (endPos - windowSamples + i + kRingSize) % kRingSize;
        windowBuffer[static_cast<size_t> (i)] = ring[static_cast<size_t> (idx)];
    }

    // Silence check: if RMS is below -60 dB the signal carries no rhythm.
    float rmsSum = 0.0f;
    for (auto s : windowBuffer) rmsSum += s * s;
    const float rms = std::sqrt (rmsSum / static_cast<float> (windowBuffer.size()));
    if (rms < 0.001f)
    {
        resultBPM          .store (0.0f, std::memory_order_release);
        resultBPMConfidence.store (0.0f, std::memory_order_release);
        return;
    }

    // Reset onset function state so each call starts fresh.
    bpmOnsetFunction.clear();

    // Accumulate per-frame onset function values.
    mirlib::realVector spectrumBuffer;
    mirlib::realVector onsetValues;

    for (int start = 0; start + frameSize <= windowSamples; start += hopSize)
    {
        mirlib::realVector frame (windowBuffer.begin() + start,
                                  windowBuffer.begin() + start + frameSize);

        spectrum.setInputBuffer  (frame);
        spectrum.setOutputBuffer (spectrumBuffer);
        spectrum.process();

        bpmOnsetFunction.setInputBuffer (spectrumBuffer);
        bpmOnsetFunction.process();
        onsetValues.push_back (static_cast<float> (bpmOnsetFunction.getOutputValue()));
    }

    if (onsetValues.empty()) return;

    // TempoTap is created locally — its setup() allocates internal vectors
    // sized to the onset function, so we recreate it each call.
    mirlib::TempoTap tempoTap;
    tempoTap.parameterOnsetFunctionSize = static_cast<int> (onsetValues.size());
    tempoTap.setup();
    tempoTap.setInputBuffer (onsetValues);
    tempoTap.process();

    const float bpm = static_cast<float> (tempoTap.getOutputValue());
    if (bpm > 0.0f)
    {
        resultBPM.store (bpm, std::memory_order_release);

        // Confidence: ratio of onset peaks detected to beats expected from BPM.
        // Ratio ≈ 1.0  → estimate is well-supported by actual onsets.
        // Ratio ≪ 1.0  → few onsets found, estimate is unreliable.
        // Ratio ≫ 1.0  → many more onsets than beats (noisy / half-tempo issue).
        mirlib::PeakDetection peakDetection;
        mirlib::realVector    peaks;
        peakDetection.setInputBuffer  (onsetValues);
        peakDetection.setOutputBuffer (peaks);
        peakDetection.process();

        const float detectedPeaks  = static_cast<float> (peakDetection.getOutputValue());
        const float windowDuration = static_cast<float> (windowSamples) / static_cast<float> (sr);
        const float expectedBeats  = bpm * windowDuration / 60.0f;
        const float confidence     = (expectedBeats > 0.0f)
                                     ? detectedPeaks / expectedBeats
                                     : 0.0f;
        resultBPMConfidence.store (confidence, std::memory_order_release);
    }
    else
    {
        resultBPMConfidence.store (0.0f, std::memory_order_release);
    }
}

//==============================================================================
void DescriptorAnalyser::processBTrack()
{
    const double sr = sampleRate.load (std::memory_order_acquire);
    if (sr < 1.0) return;

    const int currentWritePos = writePos.load (std::memory_order_acquire);

    // How many new samples have arrived since the last call?
    int newSamples = (currentWritePos - btrackReadPos + kRingSize) % kRingSize;

    // Cap at 2 s to avoid a huge catch-up after a prepare() / restart.
    const int maxSamples = static_cast<int> (sr * 2.0);
    if (newSamples > maxSamples)
    {
        btrackReadPos = (currentWritePos - maxSamples + kRingSize) % kRingSize;
        newSamples    = maxSamples;
    }

    if (newSamples < 1)
        return;

    // Copy new samples out of the ring buffer into a contiguous vector.
    mirlib::realVector rawSamples (static_cast<size_t> (newSamples));
    for (int i = 0; i < newSamples; ++i)
        rawSamples[static_cast<size_t> (i)] =
            ring[static_cast<size_t> ((btrackReadPos + i) % kRingSize)];

    btrackReadPos = currentWritePos;

    // ── Silence gate (same threshold as computeBPM) ────────────────────────
    float rmsSum = 0.0f;
    for (auto s : rawSamples) rmsSum += s * s;
    const float rms = std::sqrt (rmsSum / static_cast<float> (rawSamples.size()));

    if (rms < 0.001f)
    {
        resultBTrackBPM       .store (0.0f,   std::memory_order_release);
        resultBTrackBeatTime  .store (-1.0e9, std::memory_order_release);
        // Clear beat_this immediately — its own gate only runs every ~2 s.
        resultBeatThisBPM     .store (0.0f,   std::memory_order_release);
        resultBeatThisLastBeat.store (-1.0e9, std::memory_order_release);
        // Clear aubio immediately too.
        resultAubioBPM        .store (0.0f,   std::memory_order_release);
        resultAubioConfidence .store (0.0f,   std::memory_order_release);
        resultAubioBeatTime   .store (-1.0e9, std::memory_order_release);
        btrackWasSilent  = true;
        aubioWasSilent   = true;
        return;
    }

    // Silence → sound transition: reset BTrack's internal state so it doesn't
    // resume from 6 seconds of stale history.
    if (btrackWasSilent)
    {
        btrack.updateHopAndFrameSize (512, 1024);
        btrackWasSilent = false;
    }

    // BTrack is hard-wired to 44100 Hz — pre-convert just like computeMood().
    const mirlib::realVector samples44k = resampleTo44100 (rawSamples, sr);

    // Capture wall-clock time once for the whole batch.
    // samples44k[total44k - 1] corresponds to the most recent audio sample
    // (written just before we read writePos). We back-calculate each hop's
    // actual wall-clock time from this anchor so the beat flash is not delayed
    // by the worker wake-up latency.
    const double nowMs   = juce::Time::getMillisecondCounterHiRes();
    const int    total44k = static_cast<int> (samples44k.size());

    // Feed samples to BTrack in 512-sample hops.
    constexpr int kHop = 512;

    for (int hopIdx = 0, start = 0; start + kHop <= total44k; start += kHop, ++hopIdx)
    {
        for (int i = 0; i < kHop; ++i)
            btrackHopBuf[static_cast<size_t> (i)] =
                static_cast<double> (samples44k[static_cast<size_t> (start + i)]);

        btrack.processAudioFrame (btrackHopBuf.data());

        if (btrack.beatDueInCurrentFrame())
        {
            // Back-date: this hop's audio starts at sample [hopIdx*kHop] within
            // samples44k. The distance from that sample to the end of the buffer
            // (= "now") is (total44k - hopIdx*kHop) samples at 44100 Hz.
            const double ageMs = static_cast<double> (total44k - hopIdx * kHop)
                                 / 44100.0 * 1000.0;
            resultBTrackBeatTime.store (nowMs - ageMs, std::memory_order_release);
        }
    }

    const float bpm = static_cast<float> (btrack.getCurrentTempoEstimate());
    if (bpm > 0.0f)
        resultBTrackBPM.store (bpm, std::memory_order_release);
}

//==============================================================================
void DescriptorAnalyser::computeBeatThis()
{
    if (!beatThisProcessor) return;

    const double sr = sampleRate.load (std::memory_order_acquire);
    if (sr < 1.0) return;

    // Copy the last 3 s from the ring buffer (same window as mood / BPM).
    const int windowSamples = juce::jmin (static_cast<int> (sr * 3.0), kRingSize);
    const int endPos = writePos.load (std::memory_order_acquire);
    mirlib::realVector windowBuffer (static_cast<size_t> (windowSamples));

    for (int i = 0; i < windowSamples; ++i)
    {
        const int idx = (endPos - windowSamples + i + kRingSize) % kRingSize;
        windowBuffer[static_cast<size_t> (i)] = ring[static_cast<size_t> (idx)];
    }

    // Silence gate.
    float rmsSum = 0.0f;
    for (auto s : windowBuffer) rmsSum += s * s;
    const float rms = std::sqrt (rmsSum / static_cast<float> (windowBuffer.size()));
    if (rms < 0.001f)
    {
        resultBeatThisBPM     .store (0.0f,   std::memory_order_release);
        resultBeatThisLastBeat.store (-1.0e9, std::memory_order_release);
        return;
    }

    // beat_this handles resampling and mono conversion internally.
    BeatThis::BeatResult result;
    try
    {
        result = beatThisProcessor->process_audio (
            windowBuffer.data(),
            static_cast<size_t> (windowSamples),
            static_cast<int> (sr),
            1 /* mono */);
    }
    catch (...)
    {
        return;
    }

    if (result.beats.size() < 2)
        return;

    // BPM from average inter-beat interval.
    float totalInterval = 0.0f;
    for (size_t i = 1; i < result.beats.size(); ++i)
        totalInterval += result.beats[i] - result.beats[i - 1];
    const float avgInterval = totalInterval / static_cast<float> (result.beats.size() - 1);
    const float bpm = (avgInterval > 0.0f) ? 60.0f / avgInterval : 0.0f;
    if (bpm <= 0.0f) return;

    resultBeatThisBPM.store (bpm, std::memory_order_release);

    // Back-date the last beat timestamp to wall-clock time.
    // result.beats are in seconds relative to the START of the window buffer.
    // The END of the buffer corresponds to "now" (when writePos was read).
    const double nowMs          = juce::Time::getMillisecondCounterHiRes();
    const double windowDurMs    = static_cast<double> (windowSamples) / sr * 1000.0;
    const double lastBeatOffMs  = static_cast<double> (result.beats.back()) * 1000.0;
    const double lastBeatWallMs = nowMs - (windowDurMs - lastBeatOffMs);
    resultBeatThisLastBeat.store (lastBeatWallMs, std::memory_order_release);
}

//==============================================================================
void DescriptorAnalyser::processAubio()
{
    if (aubioTempo == nullptr || aubioInput == nullptr || aubioOutput == nullptr)
        return;

    const double sr = sampleRate.load (std::memory_order_acquire);
    if (sr < 1.0) return;

    const int currentWritePos = writePos.load (std::memory_order_acquire);

    // How many new samples since the last call?
    int newSamples = (currentWritePos - aubioReadPos + kRingSize) % kRingSize;

    // Cap at 2 s to avoid a catch-up burst after prepare() / restart.
    const int maxSamples = static_cast<int> (sr * 2.0);
    if (newSamples > maxSamples)
    {
        aubioReadPos = (currentWritePos - maxSamples + kRingSize) % kRingSize;
        newSamples   = maxSamples;
    }

    if (newSamples < static_cast<int> (kAubioHopSize))
        return;

    // aubio_tempo expects 44100 Hz — pre-convert if needed.
    mirlib::realVector rawSamples (static_cast<size_t> (newSamples));
    for (int i = 0; i < newSamples; ++i)
        rawSamples[static_cast<size_t> (i)] =
            ring[static_cast<size_t> ((aubioReadPos + i) % kRingSize)];
    aubioReadPos = currentWritePos;

    const mirlib::realVector samples44k = resampleTo44100 (rawSamples, sr);

    // Silence → sound: recreate the tempo object so aubio starts fresh.
    if (aubioWasSilent)
    {
        del_aubio_tempo (aubioTempo);
        aubioTempo = new_aubio_tempo ("default", kAubioBufSize, kAubioHopSize, 44100);
        if (aubioTempo == nullptr) return;
        aubio_tempo_set_silence (aubioTempo, -60.f);
        aubioWasSilent = false;
    }

    const double nowMs    = juce::Time::getMillisecondCounterHiRes();
    const int    total44k = static_cast<int> (samples44k.size());

    for (int hopIdx = 0, start = 0; start + static_cast<int> (kAubioHopSize) <= total44k;
         start += static_cast<int> (kAubioHopSize), ++hopIdx)
    {
        // Fill the aubio input fvec.
        for (uint_t i = 0; i < kAubioHopSize; ++i)
            fvec_set_sample (aubioInput, samples44k[static_cast<size_t> (start + i)], i);

        fvec_zeros (aubioOutput);
        aubio_tempo_do (aubioTempo, aubioInput, aubioOutput);

        // aubioOutput->data[0] != 0.f means a beat was detected in this hop.
        if (fvec_get_sample (aubioOutput, 0) != 0.f)
        {
            // Back-date: the start of this hop is (total44k − hopIdx*kAubioHopSize)
            // samples before "now".
            const double ageMs = static_cast<double> (total44k - hopIdx * static_cast<int> (kAubioHopSize))
                                 / 44100.0 * 1000.0;
            resultAubioBeatTime.store (nowMs - ageMs, std::memory_order_release);
        }
    }

    const float bpm        = aubio_tempo_get_bpm        (aubioTempo);
    const float confidence = aubio_tempo_get_confidence (aubioTempo);

    if (bpm > 0.f)
    {
        resultAubioBPM       .store (bpm,        std::memory_order_release);
        resultAubioConfidence.store (confidence, std::memory_order_release);
    }
}
