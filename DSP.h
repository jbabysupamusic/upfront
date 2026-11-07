// =============================================================================
//  DSP.h  -  the small building blocks UPFRONT is made of
//
//  * helpers   : band naming, note names, where to put the harmonic bands
//  * Biquad    : one EQ filter (bell / low cut / band-pass detector)
//  * AnalogStage : gentle tape/console-style saturation
// =============================================================================
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <complex>
#include <vector>

namespace upfront
{
constexpr int    kMaxBands  = 16;     // max harmonic bands
constexpr int    kLogPoints = 480;    // stored spectrum resolution (1/48 octave)
constexpr double kMinF = 20.0, kMaxF = 20000.0;

inline double logPointFreq (int i)
{
    return kMinF * std::pow (kMaxF / kMinF, (double) i / (kLogPoints - 1));
}

inline juce::String bandParamId (int band, const char* what)
{
    return "b" + juce::String (band + 1) + "_" + what;
}

inline juce::String noteNameForFrequency (double f)
{
    if (f <= 0.0) return "-";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
    const int n = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - n) * 100.0);
    juce::String s = juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    if (cents != 0) s << " " << (cents > 0 ? "+" : "") << cents << "c";
    return s;
}

// Which harmonics get a band: every harmonic at the bottom, then spread
// evenly (on a musical/log scale) up to ~20 kHz.
inline std::vector<int> harmonicNumbers (double f0, int maxBands)
{
    std::vector<int> ns;
    const double maxN = juce::jmax (1.0, std::floor (19500.0 / f0));
    int prev = 0;
    for (int i = 0; i < maxBands; ++i)
    {
        const double r = maxBands > 1 ? std::pow (maxN, (double) i / (maxBands - 1)) : 1.0;
        const int n = juce::jmax ((int) std::lround (r), prev + 1);
        if (n > maxN) break;
        ns.push_back (n);
        prev = n;
    }
    return ns;
}

// Each band's width covers the gap to its neighbours.
inline std::vector<double> qsFromSpacing (const std::vector<double>& f)
{
    std::vector<double> qs (f.size(), 1.0);
    const size_t n = f.size();
    for (size_t i = 0; i < n; ++i)
    {
        double prev, next;
        if (n == 1) { prev = f[0] * 0.5; next = f[0] * 1.5; }
        else
        {
            prev = i > 0     ? f[i - 1] : f[i] - (f[i + 1] - f[i]);
            next = i < n - 1 ? f[i + 1] : f[i] + (f[i] - f[i - 1]);
        }
        qs[i] = juce::jlimit (0.5, 12.0, f[i] / juce::jmax (1.0, (next - prev) * 0.5));
    }
    return qs;
}

// -----------------------------------------------------------------------------
// Biquad filter (RBJ cookbook), transposed direct form II, up to 2 channels.
// -----------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    void setPeak (double fs, double f, double q, double gainDb)
    {
        f = juce::jlimit (10.0, fs * 0.49, f);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;  b1 = -2.0 * cs / a0;  b2 = (1.0 - alpha * A) / a0;
        a1 = -2.0 * cs / a0;          a2 = (1.0 - alpha / A) / a0;
    }

    void setHighPass (double fs, double f, double q)
    {
        f = juce::jlimit (10.0, fs * 0.49, f);
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + cs) * 0.5 / a0;  b1 = -(1.0 + cs) / a0;  b2 = b0;
        a1 = -2.0 * cs / a0;         a2 = (1.0 - alpha) / a0;
    }

    void setBandPass (double fs, double f, double q)   // 0 dB peak, used as a level detector
    {
        f = juce::jlimit (10.0, fs * 0.49, f);
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const double cs = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = alpha / a0;  b1 = 0.0;  b2 = -alpha / a0;
        a1 = -2.0 * cs / a0;  a2 = (1.0 - alpha) / a0;
    }

    double magnitude (double fs, double f) const
    {
        const double w = juce::MathConstants<double>::twoPi * f / fs;
        const std::complex<double> e1 = std::polar (1.0, -w), e2 = std::polar (1.0, -2.0 * w);
        return std::abs ((b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2));
    }

    inline float process (float x, int ch) noexcept
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
};

// -----------------------------------------------------------------------------
// Analog stage: smooth, mostly even-harmonic saturation (warm, not fizzy).
// Runs 8x oversampled in the processor so it stays clean up top.
// -----------------------------------------------------------------------------
inline float softClip (float x) noexcept
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);   // smooth tanh-like curve
}

struct AnalogStage
{
    double dcX[2] {}, dcY[2] {};
    void reset() { dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0; }

    inline float process (float x, int ch, float drive) noexcept
    {
        constexpr float bias = 0.12f;   // slight asymmetry -> even harmonics (warmth)
        const float y = (softClip (x * drive + bias) - softClip (bias)) / drive;
        const double out = y - dcX[ch] + 0.9997 * dcY[ch];   // remove DC from the asymmetry
        dcX[ch] = y; dcY[ch] = out;
        return (float) out;
    }
};
} // namespace upfront
