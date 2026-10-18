#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

// Avoid M_PI; define our own constant
constexpr float kPi = 3.14159265358979323846f;

struct OnePoleLPF
{
    void prepare(double sr, float cutoffHz) { fs = (float)sr; setCutoff(cutoffHz); z = 0.0f; }
    void setCutoff(float cutoffHz)
    {
        cutoffHz = std::max(10.0f, std::min(cutoffHz, fs * 0.45f));
        const float pole = std::exp(-2.0f * kPi * cutoffHz / fs);
        a = 1.0f - pole;
        b = pole;
    }
    inline float process(float xIn) { z = a * xIn + b * z; return z; }
    float a = 0.0f, b = 0.0f, z = 0.0f, fs = 48000.0f;
};

struct OnePoleHPF
{
    void prepare(double sr, float cutoffHz) { fs = (float)sr; setCutoff(cutoffHz); z = 0.0f; }
    void setCutoff(float cutoffHz)
    {
        cutoffHz = std::max(10.0f, std::min(cutoffHz, fs * 0.45f));
        const float pole = std::exp(-2.0f * kPi * cutoffHz / fs);
        a = pole;
        b = 1.0f - pole;
    }
    inline float process(float xIn)
    {
        const float y = a * (xIn - z);
        z = xIn + a * (z - xIn);
        return y;
    }
    float a = 0.0f, b = 0.0f, z = 0.0f, fs = 48000.0f;
};

struct Allpass
{
    void resize(int d) { d = std::max(1, d); buf.assign((size_t)d, 0.0f); wp = 0; }
    void setGain(float gIn) { g = std::clamp(gIn, -0.999f, 0.999f); }
    inline float process(float x)
    {
        float y = -g * x + buf[wp];
        buf[wp] = x + g * y;
        if (++wp >= (int)buf.size()) wp = 0;
        return y;
    }
    std::vector<float> buf; int wp = 0; float g = 0.7f;
};

struct DelayLine
{
    void resize(int d) { d = std::max(1, d); buf.assign((size_t)d, 0.0f); wp = 0; }
    inline float pushPop(float x) { float y = buf[wp]; buf[wp] = x; if (++wp >= (int)buf.size()) wp = 0; return y; }
    std::vector<float> buf; int wp = 0;
};

struct SpringVerbChannel
{
    void prepare(double sr)
    {
        fs = (float)sr;
        ap1.resize(int(0.0025f * fs)); ap1.setGain(0.7f);
        ap2.resize(int(0.0080f * fs)); ap2.setGain(0.7f);
        lpf.prepare(fs, toneHz);
        hpf.prepare(fs, 180.0f);
        zDrive = fbSample = prevOut = 0.0f;
    }
    void setDelaysFromMs(float d1ms, float d2ms)
    {
        d1.resize(int(std::max(1.0f, d1ms) * fs * 0.001f));
        d2.resize(int(std::max(1.0f, d2ms) * fs * 0.001f));
    }
    void setTone(float hz) { toneHz = hz; lpf.setCutoff(toneHz); }
    void setFeedback(float fb) { feedback = std::clamp(fb, 0.0f, 0.98f); }
    void setDrive(float d) { drive = std::clamp(d, 0.0f, 1.0f); }
    void setModePSX(bool b) { modePSX = b; }

    inline float processSample(float xIn, float cross)
    {
        xIn = hpf.process(xIn);
        float x = ap2.process(ap1.process(xIn));
        float t1 = d1.pushPop(x + cross * 0.15f + fbSample);
        float t2 = d2.pushPop(t1);
        float damp = lpf.process(t2);
        float shaped = damp + drive * std::tanh(0.7f * (damp - zDrive));
        zDrive = shaped;
        fbSample = shaped * feedback;
        float out = 0.5f * (t1 + t2);
        if (modePSX) { out = 0.85f * out + 0.15f * prevOut; prevOut = out; }
        return out;
    }

    float fs = 48000.0f;
    Allpass ap1, ap2; DelayLine d1, d2; OnePoleLPF lpf; OnePoleHPF hpf;
    float feedback = 0.85f, toneHz = 6000.0f, drive = 0.2f;
    float zDrive = 0.0f, fbSample = 0.0f, prevOut = 0.0f;
    bool modePSX = false;
};

class SpringVerb
{
public:
    void prepare(double sr, int /*block*/)
    {
        fs = sr;
        left.prepare(sr); right.prepare(sr);
        left.setDelaysFromMs(31.0f, 43.0f);
        right.setDelaysFromMs(33.0f, 47.0f);
        setParameters(timeSeconds, toneHz, modeIndex);
    }

    void reset()
    {
        auto clear = [](SpringVerbChannel& c)
        {
            for (auto* b : { &c.ap1.buf, &c.ap2.buf, &c.d1.buf, &c.d2.buf })
                std::fill(b->begin(), b->end(), 0.0f);
            c.ap1.wp = c.ap2.wp = c.d1.wp = c.d2.wp = 0;
            c.lpf.z = c.hpf.z = c.zDrive = c.fbSample = c.prevOut = 0.0f;
        };
        clear(left); clear(right);
    }

    void setParameters(float timeSec, float tone, int mode)
    {
        timeSeconds = std::clamp(timeSec, 0.1f, 4.0f);
        toneHz = std::clamp(tone, 1000.0f, 12000.0f);
        modeIndex = (mode <= 0 ? 0 : 1);

        // Feedback returns after BOTH serial delays, not one average delay.
        const auto feedbackFor = [this](const SpringVerbChannel& c)
        {
            const float loopSeconds = static_cast<float>((c.d1.buf.size() + c.d2.buf.size()) / fs);
            return std::pow(0.001f, loopSeconds / timeSeconds);
        };
        left.setFeedback(feedbackFor(left)); right.setFeedback(feedbackFor(right));

        const bool psx = (modeIndex == 1);
        const float toneAdj = psx ? std::min(toneHz, 8000.0f) : toneHz;
        left.setTone(toneAdj); right.setTone(toneAdj);
        left.setDrive(psx ? 0.05f : 0.20f); right.setDrive(psx ? 0.05f : 0.20f);
        left.setModePSX(psx); right.setModePSX(psx);
    }

    void process(float** ch, int numCh, int numSamples, float mix01)
    {
        if (numCh == 0 || numSamples <= 0) return;
        mix01 = std::clamp(mix01, 0.0f, 1.0f);

        for (int n = 0; n < numSamples; ++n)
        {
            const float inL = ch[0][n];
            const float inR = (numCh > 1 ? ch[1][n] : inL);
            const float wetL = left.processSample(inL, inR);
            const float wetR = right.processSample(inR, inL);
            ch[0][n] = inL * (1.0f - mix01) + wetL * mix01;
            if (numCh > 1) ch[1][n] = inR * (1.0f - mix01) + wetR * mix01;
        }
    }

private:
    double fs = 48000.0;
    SpringVerbChannel left, right;
    float timeSeconds = 1.5f, toneHz = 6000.0f;
    int   modeIndex = 0;
};
