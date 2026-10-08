#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

// All effects process a stereo buffer in place. prepare() allocates; process() never does.

//==============================================================================
struct DistortionFx
{
    enum Type { Soft, Hard, Fold, Crush };

    void prepare(double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        tone.prepare({ sampleRate, (juce::uint32)maxBlock, 2 });
        tone.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        tone.reset();
    }

    void reset() { tone.reset(); }

    void process(juce::AudioBuffer<float>& buf, int n, int type, float driveDb, float toneHz, float mix)
    {
        const float g = juce::Decibels::decibelsToGain(driveDb);
        const float makeup = juce::jmap(driveDb, 0.0f, 48.0f, 1.0f, 0.35f); // rough level match
        const float steps = std::pow(2.0f, 15.0f - driveDb / 48.0f * 13.0f); // Crush: 16 bits down to ~3
        tone.setCutoffFrequency(juce::jmin(toneHz, (float)sr * 0.45f));

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i];
                float y;
                switch (type)
                {
                case Soft:  y = std::tanh(x * g); break;
                case Hard:  y = juce::jlimit(-1.0f, 1.0f, x * g); break;
                case Fold:  y = std::sin(x * g); break;
                default:    y = std::round(x * steps) / steps; break;
                }
                y = tone.processSample(ch, y) * (type == Crush ? 1.0f : makeup);
                d[i] = x + mix * (y - x);
            }
        }
    }

    double sr = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> tone;
};

//==============================================================================
struct DelayFx
{
    void prepare(double sampleRate, int)
    {
        size = (int)(sampleRate * 4.0) + 2; // up to 4 s
        line.setSize(2, size);
        line.clear();
        writePos = 0;
        delay.reset(sampleRate, 0.08); // glide between times instead of clicking
    }

    void reset() { line.clear(); }

    void process(juce::AudioBuffer<float>& buf, int n, double delaySamples, float feedback, float mix, bool pingPong)
    {
        delay.setTargetValue((float)juce::jlimit(1.0, (double)size - 3.0, delaySamples));
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        auto* lineL = line.getWritePointer(0);
        auto* lineR = line.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            const float d = delay.getNextValue();
            double rp = writePos - (double)d;
            if (rp < 0) rp += size;
            const int i0 = (int)rp, i1 = (i0 + 1) % size;
            const float f = (float)(rp - i0);
            const float dl = lineL[i0] + f * (lineL[i1] - lineL[i0]);
            const float dr = lineR[i0] + f * (lineR[i1] - lineR[i0]);

            if (pingPong) // mono in on the left, echoes bounce L -> R -> L
            {
                lineL[writePos] = 0.5f * (l[i] + r[i]) + feedback * dr;
                lineR[writePos] = feedback * dl;
            }
            else
            {
                lineL[writePos] = l[i] + feedback * dl;
                lineR[writePos] = r[i] + feedback * dr;
            }
            if (++writePos == size) writePos = 0;

            l[i] += mix * (dl - l[i]);
            r[i] += mix * (dr - r[i]);
        }
    }

    juce::AudioBuffer<float> line;
    int size = 1, writePos = 0;
    juce::SmoothedValue<float> delay;
};

//==============================================================================
// Real-time granulator: records the slot's audio into a 3 s buffer and plays overlapping
// Hann-windowed grains from random points behind the record head.
struct GranularFx
{
    static constexpr int kMaxGrains = 64;
    static constexpr int kWindowSize = 1024;

    void prepare(double sampleRate, int)
    {
        sr = sampleRate;
        size = (int)(sampleRate * 3.0);
        history.setSize(2, size);
        for (int i = 0; i < kWindowSize; ++i)
            window[(size_t)i] = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)(kWindowSize - 1));
        reset();
    }

    void reset()
    {
        history.clear();
        writePos = 0;
        toNextGrain = 0.0;
        for (auto& g : grains) g.active = false;
    }

    void process(juce::AudioBuffer<float>& buf, int n, float sizeMs, float density, float sprayMs, float pitch, float mix)
    {
        const int grainLen = juce::jmax(32, (int)(sizeMs * 0.001f * (float)sr));
        const double inc = std::pow(2.0, pitch / 12.0);
        const float norm = 1.0f / std::sqrt(juce::jmax(1.0f, density * sizeMs * 0.001f)); // keep overlap from getting loud
        auto* l = buf.getWritePointer(0);
        auto* r = buf.getWritePointer(1);
        auto* hL = history.getWritePointer(0);
        auto* hR = history.getWritePointer(1);

        for (int i = 0; i < n; ++i)
        {
            hL[writePos] = l[i];
            hR[writePos] = r[i];

            if ((toNextGrain -= 1.0) <= 0.0)
            {
                spawn(grainLen, inc, sprayMs);
                toNextGrain += sr / density * (0.5 + rng.nextDouble()); // irregular spacing sounds less mechanical
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (auto& g : grains)
            {
                if (!g.active) continue;
                const float w = window[(size_t)(g.age * (kWindowSize - 1) / g.len)];
                const int i0 = (int)g.pos, i1 = (i0 + 1) % size;
                const float f = (float)(g.pos - i0);
                wetL += (hL[i0] + f * (hL[i1] - hL[i0])) * w * g.gainL;
                wetR += (hR[i0] + f * (hR[i1] - hR[i0])) * w * g.gainR;
                if ((g.pos += g.inc) >= size) g.pos -= size;
                if (++g.age >= g.len) g.active = false;
            }

            if (++writePos == size) writePos = 0;

            l[i] += mix * (wetL * norm - l[i]);
            r[i] += mix * (wetR * norm - r[i]);
        }
    }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0.0, inc = 1.0;
        int len = 1, age = 0;
        float gainL = 1.0f, gainR = 1.0f;
    };

    void spawn(int len, double inc, float sprayMs)
    {
        for (auto& g : grains)
        {
            if (g.active) continue;
            // Start far enough back that a pitched-up grain never overtakes the record head
            const double minBack = juce::jmax(0.0, (inc - 1.0) * len) + 2.0;
            const double back = juce::jmin((double)size - 2.0, minBack + rng.nextDouble() * sprayMs * 0.001 * sr);
            g.pos = writePos - back;
            if (g.pos < 0) g.pos += size;
            g.inc = inc;
            g.len = len;
            g.age = 0;
            const float pan = 0.5f + (rng.nextFloat() - 0.5f) * 0.8f; // random spread across the stereo field
            g.gainL = std::cos(pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
            g.gainR = std::sin(pan * juce::MathConstants<float>::halfPi) * juce::MathConstants<float>::sqrt2;
            g.active = true;
            return;
        }
    }

    double sr = 44100.0, toNextGrain = 0.0;
    int size = 1, writePos = 0;
    juce::AudioBuffer<float> history;
    std::array<Grain, kMaxGrains> grains;
    std::array<float, kWindowSize> window{};
    juce::Random rng;
};