// =============================================================================
//  PluginProcessor.h  -  UPFRONT's audio engine
//
//  Signal flow:  input -> Low Cut -> 16 harmonic bells (static or Dynamic)
//                      -> Analog stage (8x oversampled) -> Output
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP.h"
#include "Analyzer.h"

class UpfrontProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    UpfrontProcessor();
    ~UpfrontProcessor() override;

    // ---- standard plugin plumbing ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "UPFRONT"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- the two big buttons ----
    void toggleAnalyze();          // start listening / stop and analyse
    bool balance();                // set band gains automatically

    // ---- info for the interface ----
    bool isListening() const { return listening.load(); }
    bool isWaitingForSignal() const { return listening.load() && ! signalStarted.load(); }
    double getSecondsHeard() const { return secondsHeard.load(); }
    bool hasAnalysis() const;
    std::vector<float> getAnalysedSpectrum() const;   // per-octave level, dB
    juce::String getStatusText() const { return statusText; }
    float getLiveGain (int band) const { return liveGain[band].load(); }

    static constexpr int kDisplaySize = 8192;
    void copyDisplaySamples (float* dest, int num) const;
    double getCurrentSampleRate() const { return currentRate.load(); }
    void getEqResponseDb (const std::vector<double>& freqs, std::vector<float>& outDb, bool live) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;   // feeds the analyzer while listening
    void finishListening();
    void placeBands (double f0);
    void setParam (const juce::String& id, float plainValue);
    void drainFifo();

    // parameters (fast access from the audio thread)
    std::atomic<float>* pFreq[upfront::kMaxBands] {};
    std::atomic<float>* pGain[upfront::kMaxBands] {};
    std::atomic<float>* pQ[upfront::kMaxBands] {};
    std::atomic<float> *pBands {}, *pHpOn {}, *pHpFreq {}, *pOutput {}, *pDynamic {}, *pAnalog {}, *pDrive {},
                       *pListenSec {}, *pListenInf {};

    // filters
    upfront::Biquad bell[upfront::kMaxBands], detector[upfront::kMaxBands], hpf1, hpf2;
    float curGain[upfront::kMaxBands] {};      // smoothed gain actually in use (dB)
    float setGain[upfront::kMaxBands] {};      // gain the coefficients were last built for
    float setFreq[upfront::kMaxBands] {}, setQ[upfront::kMaxBands] {};
    float detFreq[upfront::kMaxBands] {}, detQ[upfront::kMaxBands] {};
    bool  bandRunning[upfront::kMaxBands] {};
    double fastEnv[upfront::kMaxBands] {}, slowEnv[upfront::kMaxBands] {};
    std::atomic<float> liveGain[upfront::kMaxBands] {};
    float hpSet = -1.0f;
    bool needsReset = true;

    // analog
    upfront::AnalogStage analog;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int maxBlock = 512;

    juce::SmoothedValue<float> outGain;
    std::vector<float> mono;
    std::atomic<double> currentRate { 44100.0 };

    // listening
    Analyzer analyzer;
    juce::AbstractFifo fifo { 1 << 17 };
    std::vector<float> fifoBuf = std::vector<float> (1 << 17);
    std::atomic<bool> listening { false }, signalStarted { false };
    std::atomic<double> secondsHeard { 0.0 };

    // live display
    std::array<float, kDisplaySize> displayBuf {};
    std::atomic<int> displayWrite { 0 };

    mutable juce::CriticalSection specLock;
    std::vector<float> logSpec;
    juce::String statusText { "Click ANALYZE, then play your sound" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpfrontProcessor)
};
