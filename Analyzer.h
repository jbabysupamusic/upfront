// =============================================================================
//  Analyzer.h  -  "ANALYZE" button logic
//
//  Listens to audio for any length of time (even forever) using a fixed amount
//  of memory: each slice of audio is turned into a spectrum and added to a
//  running total, grouped by loudness so quiet tails and silence are ignored.
//  When finished it reports the fundamental frequency and the averaged spectrum.
// =============================================================================
#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DSP.h"
#include <array>

class Analyzer
{
public:
    struct Result
    {
        bool ok = false;
        juce::String message;
        double f0 = 0.0;
        std::vector<float> logSpec;   // power density (dB) at upfront::logPointFreq(i)
    };

    void start (double sampleRate);
    void addSamples (const float* data, int num);   // message thread
    double secondsHeard() const { return sr > 0 ? (double) samplesHeard / sr : 0.0; }
    Result finish();

private:
    void processFrame (int frameLen);

    static constexpr int order = 15, N = 1 << order;   // 32768-point FFT (~1.4 Hz detail)
    static constexpr int hop = 8192;
    static constexpr int numBuckets = 20;               // loudness groups, 5 dB each

    juce::dsp::FFT fft { order };
    double sr = 48000.0;
    juce::int64 samplesHeard = 0;
    std::vector<float> history, fftBuf;
    int historyPos = 0, sinceLastFrame = 0, framesDone = 0;

    std::array<std::vector<double>, numBuckets> bucketPower;
    std::array<int, numBuckets> bucketFrames {};
};
