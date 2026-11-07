#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace upfront;

static constexpr int kSub = 32;   // gains/envelopes update every 32 samples (smooth + cheap)

//==============================================================================
//  Parameters
//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout UpfrontProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    using PF = juce::AudioParameterFloat;
    using PB = juce::AudioParameterBool;
    using Attr = juce::AudioParameterFloatAttributes;
    auto id = [] (const juce::String& s) { return juce::ParameterID { s, 1 }; };

    juce::NormalisableRange<float> freqR (20.0f, 20000.0f, 0.01f);  freqR.setSkewForCentre (1000.0f);
    juce::NormalisableRange<float> qR (0.3f, 20.0f, 0.001f);        qR.setSkewForCentre (2.0f);
    juce::NormalisableRange<float> f0R (20.0f, 2000.0f, 0.01f);     f0R.setSkewForCentre (200.0f);
    juce::NormalisableRange<float> hpR (20.0f, 1000.0f, 0.01f);     hpR.setSkewForCentre (100.0f);

    // default layout: harmonics of A2 (110 Hz)
    const double defF0 = 110.0;
    const auto ns = harmonicNumbers (defF0, kMaxBands);
    std::vector<double> fs;
    for (auto n : ns) fs.push_back (n * defF0);
    const auto qs = qsFromSpacing (fs);

    p.push_back (std::make_unique<PF> (id ("f0"), "Fundamental", f0R, (float) defF0, Attr().withLabel ("Hz")));
    p.push_back (std::make_unique<juce::AudioParameterInt> (id ("bands"), "Bands", 1, kMaxBands, (int) ns.size()));

    for (int b = 0; b < kMaxBands; ++b)
    {
        const size_t i = (size_t) juce::jmin (b, (int) ns.size() - 1);
        const juce::String nm = "Band " + juce::String (b + 1) + " ";
        p.push_back (std::make_unique<PF> (id (bandParamId (b, "freq")), nm + "Freq", freqR, (float) fs[i], Attr().withLabel ("Hz")));
        p.push_back (std::make_unique<PF> (id (bandParamId (b, "gain")), nm + "Gain",
                                           juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), 0.0f, Attr().withLabel ("dB")));
        p.push_back (std::make_unique<PF> (id (bandParamId (b, "q")), nm + "Q", qR, (float) qs[i]));
    }

    p.push_back (std::make_unique<PB> (id ("hpOn"), "Low Cut On", false));
    p.push_back (std::make_unique<PF> (id ("hpFreq"), "Low Cut", hpR, 30.0f, Attr().withLabel ("Hz")));
    p.push_back (std::make_unique<PF> (id ("amount"), "Amount", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 75.0f, Attr().withLabel ("%")));
    p.push_back (std::make_unique<PF> (id ("tilt"), "Target Tilt", juce::NormalisableRange<float> (-9.0f, 0.0f, 0.1f), -4.5f, Attr().withLabel ("dB/oct")));
    p.push_back (std::make_unique<PF> (id ("output"), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f, Attr().withLabel ("dB")));
    p.push_back (std::make_unique<PB> (id ("dynamic"), "Dynamic Mode", false));
    p.push_back (std::make_unique<PB> (id ("analog"), "Analog Mode", false));
    p.push_back (std::make_unique<PF> (id ("drive"), "Analog Drive", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, Attr().withLabel ("%")));
    p.push_back (std::make_unique<PF> (id ("listenSec"), "Listen Time", juce::NormalisableRange<float> (1.0f, 30.0f, 0.5f), 8.0f, Attr().withLabel ("s")));
    p.push_back (std::make_unique<PB> (id ("listenInf"), "Listen Until Stopped", false));

    return { p.begin(), p.end() };
}

UpfrontProcessor::UpfrontProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "UPFRONT", createLayout())
{
    for (int b = 0; b < kMaxBands; ++b)
    {
        pFreq[b] = apvts.getRawParameterValue (bandParamId (b, "freq"));
        pGain[b] = apvts.getRawParameterValue (bandParamId (b, "gain"));
        pQ[b]    = apvts.getRawParameterValue (bandParamId (b, "q"));
    }
    pBands     = apvts.getRawParameterValue ("bands");
    pHpOn      = apvts.getRawParameterValue ("hpOn");
    pHpFreq    = apvts.getRawParameterValue ("hpFreq");
    pOutput    = apvts.getRawParameterValue ("output");
    pDynamic   = apvts.getRawParameterValue ("dynamic");
    pAnalog    = apvts.getRawParameterValue ("analog");
    pDrive     = apvts.getRawParameterValue ("drive");
    pListenSec = apvts.getRawParameterValue ("listenSec");
    pListenInf = apvts.getRawParameterValue ("listenInf");
}

UpfrontProcessor::~UpfrontProcessor() { stopTimer(); }

bool UpfrontProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
//  Audio
//==============================================================================
void UpfrontProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate.store (sampleRate);
    maxBlock = juce::jmax (64, samplesPerBlock);
    mono.assign ((size_t) maxBlock, 0.0f);

    // 8x oversampling with steep half-band filters -> no aliasing from the analog stage
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxBlock);

    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    needsReset = true;
}

void UpfrontProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    for (int ch = numIn; ch < getTotalNumOutputChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    const int numCh = juce::jmin (numIn, 2);
    const int n = buffer.getNumSamples();
    if (numCh == 0 || n == 0) return;
    const double fs = currentRate.load();

    if (needsReset)
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            bell[b].reset(); detector[b].reset();
            curGain[b] = pGain[b]->load(); setGain[b] = 1e9f; bandRunning[b] = false;
            fastEnv[b] = slowEnv[b] = 0.0;
        }
        hpf1.reset(); hpf2.reset(); analog.reset(); oversampler->reset();
        hpSet = -1.0f;
        needsReset = false;
    }

    // --- mono copy of the input (for listening + Dynamic detectors) ---
    if ((int) mono.size() < n) mono.resize ((size_t) n);   // only if the host breaks its block size promise
    for (int s = 0; s < n; ++s)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) m += buffer.getSample (ch, s);
        mono[(size_t) s] = m / (float) numCh;
    }

    // --- 1. listening: hand audio to the analyzer (lock-free) ---
    if (listening.load())
    {
        int s = 0;
        if (! signalStarted.load())
        {
            while (s < n && std::abs (mono[(size_t) s]) < 0.001f) ++s;   // wait for the sound to start
            if (s < n) signalStarted.store (true);
        }
        if (s < n)
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (n - s, s1, n1, s2, n2);
            std::copy (mono.begin() + s, mono.begin() + s + n1, fifoBuf.begin() + s1);
            std::copy (mono.begin() + s + n1, mono.begin() + s + n1 + n2, fifoBuf.begin() + s2);
            fifo.finishedWrite (n1 + n2);
        }
    }

    // --- 2. low cut (4th-order Butterworth) ---
    if (pHpOn->load() > 0.5f)
    {
        const float hp = pHpFreq->load();
        if (hp != hpSet) { hpSet = hp; hpf1.setHighPass (fs, hp, 0.5412); hpf2.setHighPass (fs, hp, 1.3066); }
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int s = 0; s < n; ++s) d[s] = hpf2.process (hpf1.process (d[s], ch), ch);
        }
    }

    // --- 3. harmonic bells, in 32-sample slices so every gain move is smooth ---
    const int active = juce::jlimit (1, kMaxBands, (int) pBands->load());
    const bool dyn = pDynamic->load() > 0.5f;
    const double glide  = 1.0 - std::exp (-kSub / (0.030 * fs));   // 30 ms gain smoothing
    const double attack = 1.0 - std::exp (-kSub / (0.005 * fs));
    const double release= 1.0 - std::exp (-kSub / (0.120 * fs));
    const double slow   = 1.0 - std::exp (-kSub / (2.000 * fs));

    for (int start = 0; start < n; start += kSub)
    {
        const int len = juce::jmin (kSub, n - start);
        for (int b = 0; b < kMaxBands; ++b)
        {
            const float g = b < active ? pGain[b]->load() : 0.0f;
            const float f = pFreq[b]->load(), q = pQ[b]->load();
            float target = g;

            // skip bands doing nothing (this is where most CPU is saved)
            if (std::abs (g) < 0.01f && std::abs (curGain[b]) < 0.01f)
            {
                bandRunning[b] = false;
                curGain[b] = 0.0f;
                liveGain[b].store (0.0f);
                continue;
            }

            if (dyn && std::abs (g) >= 0.01f)
            {
                // Dynamic: compare this harmonic's level now vs its long-term level
                if (f != detFreq[b] || q != detQ[b]) { detFreq[b] = f; detQ[b] = q; detector[b].setBandPass (fs, f, juce::jmax (1.0f, q)); }
                double ms = 0.0;
                for (int s = 0; s < len; ++s)
                {
                    const float y = detector[b].process (mono[(size_t) (start + s)], 0);
                    ms += (double) y * y;
                }
                ms /= len;
                fastEnv[b] += (ms > fastEnv[b] ? attack : release) * (ms - fastEnv[b]);
                if (slowEnv[b] < 1e-14) slowEnv[b] = fastEnv[b];
                slowEnv[b] += slow * (fastEnv[b] - slowEnv[b]);
                const double r = juce::jlimit (-12.0, 12.0, 10.0 * std::log10 ((fastEnv[b] + 1e-12) / (slowEnv[b] + 1e-12)));
                // cuts dig in when the harmonic jumps out, relax when it's quiet; boosts do the opposite
                const double k = g < 0 ? juce::jlimit (0.0, 2.0, 1.0 + r / 6.0) : juce::jlimit (0.0, 1.5, 1.0 - r / 6.0);
                target = (float) juce::jlimit (-18.0, 18.0, g * k);
            }

            if (! bandRunning[b]) { bell[b].reset(); bandRunning[b] = true; setGain[b] = 1e9f; }
            curGain[b] += (float) glide * (target - curGain[b]);
            if (std::abs (target - curGain[b]) < 0.002f) curGain[b] = target;

            if (std::abs (curGain[b] - setGain[b]) > 0.02f || f != setFreq[b] || q != setQ[b])
            {
                bell[b].setPeak (fs, f, q, curGain[b]);
                setGain[b] = curGain[b]; setFreq[b] = f; setQ[b] = q;
            }
            liveGain[b].store (curGain[b]);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch) + start;
                for (int s = 0; s < len; ++s) d[s] = bell[b].process (d[s], ch);
            }
        }
    }

    // --- 4. analog stage, 8x oversampled ---
    if (pAnalog->load() > 0.5f)
    {
        const float drive = juce::Decibels::decibelsToGain (pDrive->load() / 100.0f * 12.0f);
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            auto chunk = block.getSubBlock ((size_t) pos, (size_t) juce::jmin (maxBlock, n - pos));
            auto up = oversampler->processSamplesUp (chunk);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                auto* d = up.getChannelPointer (ch);
                for (size_t s = 0; s < up.getNumSamples(); ++s) d[s] = analog.process (d[s], (int) ch, drive);
            }
            oversampler->processSamplesDown (chunk);
        }
    }

    // --- 5. output level ---
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    if (outGain.isSmoothing() || std::abs (outGain.getTargetValue() - 1.0f) > 1e-6f)
        for (int s = 0; s < n; ++s)
        {
            const float gn = outGain.getNextValue();
            for (int ch = 0; ch < numCh; ++ch) buffer.getWritePointer (ch)[s] *= gn;
        }

    // --- 6. feed the on-screen analyser ---
    int w = displayWrite.load();
    for (int s = 0; s < n; ++s)
    {
        float m = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) m += buffer.getSample (ch, s);
        displayBuf[(size_t) w] = m / (float) numCh;
        w = (w + 1) & (kDisplaySize - 1);
    }
    displayWrite.store (w);
}

void UpfrontProcessor::copyDisplaySamples (float* dest, int num) const
{
    num = juce::jmin (num, kDisplaySize);
    int r = (displayWrite.load() - num + kDisplaySize) & (kDisplaySize - 1);
    for (int i = 0; i < num; ++i) { dest[i] = displayBuf[(size_t) r]; r = (r + 1) & (kDisplaySize - 1); }
}

void UpfrontProcessor::getEqResponseDb (const std::vector<double>& freqs, std::vector<float>& outDb, bool live) const
{
    const double fs = currentRate.load();
    const int active = juce::jlimit (1, kMaxBands, (int) pBands->load());
    Biquad bq[kMaxBands], h1, h2;
    for (int b = 0; b < active; ++b)
        bq[b].setPeak (fs, pFreq[b]->load(), pQ[b]->load(), live ? liveGain[b].load() : pGain[b]->load());
    const bool hpOn = pHpOn->load() > 0.5f;
    h1.setHighPass (fs, pHpFreq->load(), 0.5412);
    h2.setHighPass (fs, pHpFreq->load(), 1.3066);

    outDb.resize (freqs.size());
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const double f = juce::jmin (freqs[i], fs * 0.499);
        double mag = 1.0;
        for (int b = 0; b < active; ++b) mag *= bq[b].magnitude (fs, f);
        if (hpOn) mag *= h1.magnitude (fs, f) * h2.magnitude (fs, f);
        outDb[i] = (float) juce::Decibels::gainToDecibels (mag, -60.0);
    }
}

//==============================================================================
//  ANALYZE
//==============================================================================
void UpfrontProcessor::toggleAnalyze()
{
    if (listening.load()) { finishListening(); return; }

    analyzer.start (currentRate.load());
    fifo.reset();
    secondsHeard.store (0.0);
    signalStarted.store (false);
    listening.store (true);
    statusText = "Listening... play your sound";
    startTimerHz (20);
}

void UpfrontProcessor::drainFifo()
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    if (n1 > 0) analyzer.addSamples (fifoBuf.data() + s1, n1);
    if (n2 > 0) analyzer.addSamples (fifoBuf.data() + s2, n2);
    fifo.finishedRead (n1 + n2);
    secondsHeard.store (analyzer.secondsHeard());
}

void UpfrontProcessor::timerCallback()
{
    if (! listening.load()) { stopTimer(); return; }
    drainFifo();
    const bool forever = pListenInf->load() > 0.5f;
    if (! forever && analyzer.secondsHeard() >= pListenSec->load())
        finishListening();
}

void UpfrontProcessor::finishListening()
{
    listening.store (false);
    stopTimer();
    drainFifo();

    const auto res = analyzer.finish();
    statusText = res.message;
    if (! res.ok) return;
    {
        const juce::ScopedLock sl (specLock);
        logSpec = res.logSpec;
    }
    placeBands (res.f0);
}

void UpfrontProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* prm = apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
        prm->endChangeGesture();
    }
}

void UpfrontProcessor::placeBands (double f0)
{
    setParam ("f0", (float) f0);
    const auto ns = harmonicNumbers (f0, kMaxBands);
    std::vector<double> fs;
    for (auto n : ns) fs.push_back (n * f0);
    const auto qs = qsFromSpacing (fs);

    setParam ("bands", (float) ns.size());
    for (int b = 0; b < kMaxBands; ++b)
    {
        const size_t i = (size_t) juce::jmin (b, (int) ns.size() - 1);
        setParam (bandParamId (b, "freq"), (float) fs[i]);
        setParam (bandParamId (b, "q"), (float) qs[i]);
        setParam (bandParamId (b, "gain"), 0.0f);
    }
    setParam ("hpFreq", (float) juce::jlimit (20.0, 1000.0, f0 * 0.7));
}

bool UpfrontProcessor::hasAnalysis() const
{
    const juce::ScopedLock sl (specLock);
    return ! logSpec.empty();
}

std::vector<float> UpfrontProcessor::getAnalysedSpectrum() const
{
    std::vector<float> spec;
    {
        const juce::ScopedLock sl (specLock);
        spec = logSpec;
    }
    for (size_t i = 0; i < spec.size(); ++i)   // density -> level per octave
        spec[i] += (float) (10.0 * std::log10 (logPointFreq ((int) i) * std::log (2.0)));
    return spec;
}

//==============================================================================
//  BALANCE  -  musical rules keep the result smooth and never harsh
//==============================================================================
bool UpfrontProcessor::balance()
{
    const auto L = getAnalysedSpectrum();
    if (L.empty()) { statusText = "Analyze a sound first"; return false; }

    const int active = juce::jlimit (1, kMaxBands, (int) pBands->load());
    const float amount = apvts.getRawParameterValue ("amount")->load() / 100.0f;
    const float tilt = apvts.getRawParameterValue ("tilt")->load();

    std::vector<double> f ((size_t) active);
    for (int b = 0; b < active; ++b) f[(size_t) b] = pFreq[b]->load();
    const auto baseQ = qsFromSpacing (f);

    auto pointIndex = [] (double freq) {
        return juce::jlimit (0, kLogPoints - 1,
                             (int) std::lround (std::log (freq / kMinF) / std::log (kMaxF / kMinF) * (kLogPoints - 1)));
    };

    // level of the region each band controls
    std::vector<double> lvl ((size_t) active), shape ((size_t) active);
    for (int b = 0; b < active; ++b)
    {
        const double fb = f[(size_t) b];
        const double lo = b > 0 ? std::sqrt (f[(size_t) b - 1] * fb) : fb / std::sqrt (2.0);
        const double hi = b < active - 1 ? std::sqrt (fb * f[(size_t) b + 1]) : juce::jmin (20000.0, fb * std::sqrt (2.0));
        const int i0 = pointIndex (lo), i1 = juce::jmax (i0, pointIndex (hi));
        double sum = 0;
        for (int i = i0; i <= i1; ++i) sum += std::pow (10.0, L[(size_t) i] / 10.0);
        lvl[(size_t) b] = 10.0 * std::log10 (sum / (i1 - i0 + 1) + 1e-20);
        shape[(size_t) b] = tilt * std::log2 (fb / 1000.0);
    }

    // target line = the tilt, placed at the median level of bands that hold the sound
    const double maxLvl = *std::max_element (lvl.begin(), lvl.end());
    std::vector<double> offs;
    for (int b = 0; b < active; ++b)
        if (lvl[(size_t) b] >= maxLvl - 45.0) offs.push_back (lvl[(size_t) b] - shape[(size_t) b]);
    std::sort (offs.begin(), offs.end());
    const double c = offs.empty() ? 0.0 : (offs.size() % 2 ? offs[offs.size() / 2]
                                                           : 0.5 * (offs[offs.size() / 2 - 1] + offs[offs.size() / 2]));

    double wSum = 0, gSum = 0;
    for (int b = 0; b < active; ++b)
    {
        const double fb = f[(size_t) b];
        const double dev = lvl[(size_t) b] - (c + shape[(size_t) b]);
        double g = -dev * amount;

        // --- quality rules ---
        const bool earZone = fb >= 2000.0 && fb <= 6000.0;   // where harshness lives
        if (earZone && g < 0) g *= 1.25;                      // tame it a bit more
        double maxBoost = fb < 120.0 ? 3.0 : fb < 2000.0 ? 4.0 : earZone ? 1.5 : fb < 9000.0 ? 2.5 : 4.0;
        if (lvl[(size_t) b] < maxLvl - 45.0) maxBoost = 0.0;  // never boost noise
        g = juce::jlimit (-12.0, maxBoost, g);
        if (b == 0) g = juce::jmax (g, -6.0);                  // keep the body
        if (std::abs (g) < 0.3) g = 0.0;                       // ignore tiny moves (cleaner + less CPU)

        // boosts wide and smooth, cuts can be precise
        double q = baseQ[(size_t) b];
        if (g > 0) q = juce::jmin (q, fb < 2000.0 ? 2.5 : 1.0);
        setParam (bandParamId (b, "q"), (float) q);
        setParam (bandParamId (b, "gain"), (float) g);

        const double e = std::pow (10.0, lvl[(size_t) b] / 10.0);
        wSum += e; gSum += e * g;
    }

    const double trim = wSum > 0 ? juce::jlimit (-12.0, 12.0, -gSum / wSum) : 0.0;   // fair A/B level
    setParam ("output", (float) trim);
    setParam ("hpFreq", (float) juce::jlimit (20.0, 1000.0, (double) apvts.getRawParameterValue ("f0")->load() * 0.7));
    setParam ("hpOn", 1.0f);

    statusText = "Balanced " + juce::String (active) + " bands (output trim " + juce::String (trim, 1) + " dB)";
    return true;
}

//==============================================================================
//  Save / load (analysis is stored with your project)
//==============================================================================
void UpfrontProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::StringArray s;
    {
        const juce::ScopedLock sl (specLock);
        for (auto v : logSpec) s.add (juce::String (v, 2));
    }
    state.setProperty ("spectrum", s.joinIntoString (","), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void UpfrontProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType (apvts.state.getType())) return;

    juce::StringArray s;
    s.addTokens (tree.getProperty ("spectrum").toString(), ",", "");
    std::vector<float> spec;
    if (s.size() == kLogPoints)
        for (auto& t : s) spec.push_back (t.getFloatValue());
    {
        const juce::ScopedLock sl (specLock);
        logSpec = std::move (spec);
    }
    tree.removeProperty ("spectrum", nullptr);
    apvts.replaceState (tree);
    if (hasAnalysis())
        statusText = "Loaded analysis: " + juce::String (apvts.getRawParameterValue ("f0")->load(), 1) + " Hz";
}

juce::AudioProcessorEditor* UpfrontProcessor::createEditor() { return new UpfrontEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UpfrontProcessor(); }
