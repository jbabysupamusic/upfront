#include "Analyzer.h"

using namespace upfront;

void Analyzer::start (double sampleRate)
{
    sr = sampleRate;
    samplesHeard = 0;
    historyPos = sinceLastFrame = framesDone = 0;
    history.assign ((size_t) N, 0.0f);
    fftBuf.assign ((size_t) N * 2, 0.0f);
    for (auto& b : bucketPower) b.clear();
    bucketFrames.fill (0);
}

void Analyzer::addSamples (const float* data, int num)
{
    for (int i = 0; i < num; ++i)
    {
        history[(size_t) historyPos] = data[i];
        historyPos = (historyPos + 1) % N;
        ++samplesHeard;
        if (++sinceLastFrame >= hop && samplesHeard >= N)
        {
            sinceLastFrame = 0;
            processFrame (N);
        }
    }
}

void Analyzer::processFrame (int frameLen)
{
    // copy audio in time order (oldest first)
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    const int startIdx = frameLen == N ? historyPos : 0;
    double energy = 0.0;
    for (int i = 0; i < frameLen; ++i)
    {
        const float x = history[(size_t) ((startIdx + i) % N)];
        energy += (double) x * x;
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (frameLen - 1));
        fftBuf[(size_t) i] = x * w;
    }

    const double rmsDb = 10.0 * std::log10 (energy / frameLen + 1e-20);
    const int bucket = juce::jlimit (0, numBuckets - 1, (int) ((rmsDb + 100.0) / 5.0));

    fft.performRealOnlyForwardTransform (fftBuf.data(), true);

    auto& acc = bucketPower[(size_t) bucket];
    if (acc.empty()) acc.assign ((size_t) N / 2 + 1, 0.0);
    for (int k = 0; k <= N / 2; ++k)
    {
        const double re = fftBuf[(size_t) (2 * k)], im = fftBuf[(size_t) (2 * k + 1)];
        acc[(size_t) k] += re * re + im * im;
    }
    ++bucketFrames[(size_t) bucket];
    ++framesDone;
}

Analyzer::Result Analyzer::finish()
{
    Result res;

    // short sounds: analyse whatever we have as one frame
    if (framesDone == 0)
    {
        if (samplesHeard < (juce::int64) (sr * 0.1))
        {
            res.message = "Not enough audio heard - try again";
            return res;
        }
        processFrame ((int) samplesHeard);
    }

    // use only the loud parts: buckets within 20 dB of the loudest
    int top = -1;
    for (int b = numBuckets - 1; b >= 0; --b)
        if (bucketFrames[(size_t) b] > 0) { top = b; break; }
    if (top < 6)   // loudest part quieter than about -70 dBFS
    {
        res.message = "Signal too quiet - turn it up and try again";
        return res;
    }

    std::vector<double> P ((size_t) N / 2 + 1, 0.0);
    int frames = 0;
    for (int b = juce::jmax (0, top - 4); b <= top; ++b)
    {
        if (bucketFrames[(size_t) b] == 0) continue;
        for (size_t k = 0; k < P.size(); ++k) P[k] += bucketPower[(size_t) b][k];
        frames += bucketFrames[(size_t) b];
    }
    for (auto& v : P) v /= frames;

    const double binHz = sr / N;
    auto toDb = [] (double p) { return 10.0 * std::log10 (p + 1e-20); };
    auto peakNearDb = [&] (double f) {
        const int k0 = juce::jmax (1, (int) std::floor (f * 0.97 / binHz));
        const int k1 = juce::jmin (N / 2, (int) std::ceil (f * 1.03 / binHz));
        double m = 0;
        for (int k = k0; k <= k1; ++k) m = juce::jmax (m, P[(size_t) k]);
        return toDb (m);
    };

    // ---- fundamental: the pitch whose first 5 harmonics are strongest together ----
    constexpr int H = 5;
    double bestF = 0, bestScore = -1e30;
    for (double f = 25.0; f <= 1200.0; f *= std::pow (2.0, 1.0 / 48.0))
    {
        if (f * H > sr * 0.45) break;
        double s = 0;
        for (int h = 1; h <= H; ++h) s += peakNearDb (f * h);
        if (s > bestScore) { bestScore = s; bestF = f; }
    }
    // a strong peak an octave below means the true fundamental is lower
    while (bestF / 2.0 >= 25.0 && peakNearDb (bestF / 2.0) > peakNearDb (bestF) - 10.0)
        bestF /= 2.0;

    // refine to the exact peak
    {
        const int k0 = juce::jmax (2, (int) std::floor (bestF * 0.97 / binHz));
        const int k1 = juce::jmin (N / 2 - 2, (int) std::ceil (bestF * 1.03 / binHz));
        int km = k0;
        for (int k = k0; k <= k1; ++k) if (P[(size_t) k] > P[(size_t) km]) km = k;
        const double a = toDb (P[(size_t) km - 1]), b = toDb (P[(size_t) km]), c = toDb (P[(size_t) km + 1]);
        const double den = a - 2 * b + c;
        const double delta = std::abs (den) > 1e-9 ? juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den) : 0.0;
        bestF = (km + delta) * binHz;
    }
    res.f0 = juce::jlimit (20.0, 2000.0, bestF);

    // ---- averaged spectrum on a musical (log) scale ----
    res.logSpec.resize ((size_t) kLogPoints);
    for (int i = 0; i < kLogPoints; ++i)
    {
        const double f = logPointFreq (i);
        if (f >= sr * 0.5) { res.logSpec[(size_t) i] = -200.0f; continue; }
        const double lo = f * std::pow (2.0, -1.0 / 96.0), hi = f * std::pow (2.0, 1.0 / 96.0);
        const int k0 = (int) std::ceil (lo / binHz), k1 = juce::jmin (N / 2, (int) std::floor (hi / binHz));
        double p;
        if (k1 >= k0)
        {
            p = 0;
            for (int k = k0; k <= k1; ++k) p += P[(size_t) k];
            p /= (k1 - k0 + 1);
        }
        else
        {
            const double kf = f / binHz;
            const int k = (int) kf;
            const double t = kf - k;
            p = P[(size_t) k] * (1 - t) + P[(size_t) juce::jmin (k + 1, N / 2)] * t;
        }
        res.logSpec[(size_t) i] = (float) toDb (p / ((double) N * N));
    }

    res.ok = true;
    res.message = "Detected " + juce::String (res.f0, 1) + " Hz (" + noteNameForFrequency (res.f0)
                + ") from " + juce::String (secondsHeard(), 1) + " s - click BALANCE";
    return res;
}
