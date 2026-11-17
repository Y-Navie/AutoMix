#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

namespace gam
{
//==============================================================================
// K-weighted loudness + 4-band spectral energy, measured on the mono sum.
class Analyzer
{
public:
    struct Block
    {
        double ms = 0;                    // K-weighted mean square
        double low = 0, lowMid = 0, mid = 0, high = 0;
        float  crestDb = 0;
        bool   active = false;            // above the -50 LUFS gate
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // ITU-R BS.1770 K-weighting approximation
        kShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, 1681.0f, 0.7071f, 1.585f);
        kHpf.coefficients   = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 38.0f, 0.5f);

        juce::dsp::ProcessSpec spec { sr, 4096, 1 };
        for (auto* f : { &lp120, &lp500, &lp4k })
        {
            f->setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            f->prepare (spec);
        }
        lp120.setCutoffFrequency (120.0f);
        lp500.setCutoffFrequency (500.0f);
        lp4k.setCutoffFrequency (4000.0f);
        reset();
    }

    void reset()
    {
        kShelf.reset(); kHpf.reset();
        lp120.reset(); lp500.reset(); lp4k.reset();
    }

    Block process (const float* l, const float* r, int n)
    {
        Block b;
        if (n <= 0) return b;

        double kSum = 0, rawSum = 0, eLow = 0, eLowMid = 0, eMid = 0, eHigh = 0;
        float peak = 0;

        for (int i = 0; i < n; ++i)
        {
            const float x = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
            peak = std::max (peak, std::abs (x));
            rawSum += (double) x * x;

            const float k = kHpf.processSample (kShelf.processSample (x));
            kSum += (double) k * k;

            const float a = lp120.processSample (0, x);
            const float b5 = lp500.processSample (0, x);
            const float c = lp4k.processSample (0, x);
            eLow    += (double) a * a;
            eLowMid += (double) (b5 - a) * (b5 - a);
            eMid    += (double) (c - b5) * (c - b5);
            eHigh   += (double) (x - c) * (x - c);
        }

        b.ms = kSum / n;
        b.low = eLow / n; b.lowMid = eLowMid / n; b.mid = eMid / n; b.high = eHigh / n;
        const double rawRms = std::sqrt (rawSum / n);
        b.crestDb = rawRms > 1e-9 ? juce::Decibels::gainToDecibels (peak / (float) rawRms) : 0.0f;
        b.active = (10.0 * std::log10 (b.ms + 1e-12) - 0.691) > -50.0;
        return b;
    }

    static double toLufs (double meanSquare) { return 10.0 * std::log10 (meanSquare + 1e-12) - 0.691; }

private:
    double sr = 44100.0;
    juce::dsp::IIR::Filter<float> kShelf, kHpf;
    juce::dsp::LinkwitzRileyFilter<float> lp120, lp500, lp4k;
};

//==============================================================================
// Simple look-ahead brick-wall limiter (master bus).
class LookaheadLimiter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        lookahead = juce::jmax (1, (int) std::round (0.003 * sr));   // 3 ms
        delay.assign ((size_t) numChannels, std::vector<float> ((size_t) lookahead + 1, 0.0f));
        window.assign ((size_t) lookahead + 1, 1.0f);
        writePos = 0;
        env = 1.0f;
        attackCoef  = (float) std::exp (-1.0 / (lookahead * 0.3));
        setRelease (80.0f);
    }

    int getLatency() const { return lookahead; }
    void setRelease (float ms) { releaseCoef = (float) std::exp (-1.0 / (0.001 * ms * sr)); }
    void setCeilingDb (float db) { ceiling = juce::Decibels::decibelsToGain (db); }
    float getGainReductionDb() const { return juce::Decibels::gainToDecibels (lastMinEnv); }

    void process (juce::AudioBuffer<float>& buf)
    {
        const int nCh = juce::jmin (buf.getNumChannels(), (int) delay.size());
        const int n = buf.getNumSamples();
        const int size = lookahead + 1;
        float minEnv = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float peak = 0;
            for (int c = 0; c < nCh; ++c)
                peak = std::max (peak, std::abs (buf.getSample (c, i)));

            window[(size_t) writePos] = peak > ceiling ? ceiling / peak : 1.0f;

            float target = 1.0f;
            for (float g : window) target = std::min (target, g);

            env = target < env ? target + (env - target) * attackCoef
                               : target + (env - target) * releaseCoef;
            minEnv = std::min (minEnv, env);

            const int readPos = (writePos + 1) % size;
            for (int c = 0; c < nCh; ++c)
            {
                auto& d = delay[(size_t) c];
                d[(size_t) writePos] = buf.getSample (c, i);
                float y = d[(size_t) readPos] * env;
                y = juce::jlimit (-ceiling, ceiling, y);     // safety clip
                buf.setSample (c, i, y);
            }
            writePos = readPos;
        }
        lastMinEnv = minEnv;
    }

private:
    double sr = 44100.0;
    int lookahead = 132, writePos = 0;
    std::vector<std::vector<float>> delay;
    std::vector<float> window;
    float env = 1.0f, attackCoef = 0.9f, releaseCoef = 0.999f, ceiling = 0.89f, lastMinEnv = 1.0f;
};

} // namespace gam
