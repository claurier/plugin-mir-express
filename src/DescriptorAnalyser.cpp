#include "DescriptorAnalyser.h"
#include "Types.h"   // mirlib::realVector = std::vector<float>

//==============================================================================
DescriptorAnalyser::DescriptorAnalyser()
    : juce::Thread ("DescriptorWorker")
{
}

DescriptorAnalyser::~DescriptorAnalyser()
{
    stopThread (3000);
}

//==============================================================================
void DescriptorAnalyser::prepare (double sr)
{
    sampleRate.store (sr, std::memory_order_release);

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

    while (!threadShouldExit())
    {
        wait (250);   // wake every 250 ms

        if (threadShouldExit())
            break;

        computeDissonance();          // every 250 ms  (0.5 s window)

        if (++moodCounter >= 4)       // every ~1 s    (3 s window)
        {
            moodCounter = 0;
            computeMood();
        }
    }
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

    extractor.setSamplerate (sr);
    extractor.process (samples);

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
