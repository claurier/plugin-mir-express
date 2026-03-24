#include "MoodAnalyser.h"
#include "Types.h"   // realVector = std::vector<float>

//==============================================================================
MoodAnalyser::MoodAnalyser()
    : juce::Thread ("MoodWorker")
{
}

MoodAnalyser::~MoodAnalyser()
{
    stopThread (3000);
}

//==============================================================================
void MoodAnalyser::prepare (double sr)
{
    sampleRate.store (sr, std::memory_order_release);

    if (!isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

//==============================================================================
void MoodAnalyser::pushSamples (const juce::AudioBuffer<float>& buffer)
{
    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    const float channelScale = 1.0f / static_cast<float> (numChannels);
    int pos = writePos.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += buffer.getSample (ch, i);

        ring[static_cast<size_t> (pos)] = sum * channelScale;
        pos = (pos + 1) % kRingSize;
    }

    writePos.store (pos, std::memory_order_release);
}

//==============================================================================
void MoodAnalyser::run()
{
    while (!threadShouldExit())
    {
        wait (1000);   // sleep ~1 s, then analyse the last 3 s

        if (threadShouldExit())
            break;

        const double sr = sampleRate.load (std::memory_order_acquire);
        const int windowSamples = juce::jmin (static_cast<int> (sr * 3.0), kRingSize);

        // Copy the last 3 s from the circular ring buffer into a contiguous vector.
        const int endPos = writePos.load (std::memory_order_acquire);

        mirlib::realVector samples (static_cast<size_t> (windowSamples));

        for (int i = 0; i < windowSamples; ++i)
        {
            const int idx = (endPos - windowSamples + i + kRingSize) % kRingSize;
            samples[static_cast<size_t> (i)] = ring[static_cast<size_t> (idx)];
        }

        // Run the mood extraction (CPU-intensive; safe here on the worker thread).
        extractor.setSamplerate (sr);
        extractor.process (samples);

        resultAngry.store (extractor.getAngry(), std::memory_order_release);
        resultCalm .store (extractor.getCalm(),  std::memory_order_release);
        resultHappy.store (extractor.getHappy(), std::memory_order_release);
        resultSad  .store (extractor.getSad(),   std::memory_order_release);
    }
}
