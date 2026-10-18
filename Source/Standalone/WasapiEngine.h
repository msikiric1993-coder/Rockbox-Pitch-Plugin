#pragma once
#include "WindowsAudio.h"
#include "../PluginProcessor.h"
#include <array>
#include <thread>

namespace td
{
juce::var signalCheck(const juce::String& captureId,const juce::String& cablePlaybackId,const juce::String& outputId,bool dry=false);
juce::var soakCheck(const juce::String& capture,const juce::String& cable,const juce::String& output,const juce::File& report,int seconds);
struct StereoFrame { float l=0,r=0; };
class CaptureQueue
{
public:
    static constexpr unsigned capacity=32768;
    std::array<StereoFrame,capacity> frames{};
    std::atomic<std::uint64_t> write{0},read{0};
    void clear() {write.store(0);read.store(0);}
    unsigned available() const
    {
        const auto r=read.load(std::memory_order_acquire),w=write.load(std::memory_order_acquire);
        return (unsigned)juce::jmin<std::uint64_t>(capacity,w>=r?w-r:0);
    }
    bool push(const float* samples,unsigned n,bool silent)
    {
        const auto w=write.load(std::memory_order_relaxed),r=read.load(std::memory_order_acquire);
        if(n>capacity-(w-r)) return false;
        for(unsigned i=0;i<n;++i) frames[(w+i)%capacity]=silent?StereoFrame{}:StereoFrame{samples[2*i],samples[2*i+1]};
        write.store(w+n,std::memory_order_release);return true;
    }
};
class WasapiEngine
{
public:
    explicit WasapiEngine(TDSpeedVSTAudioProcessor& p):processor(p) {}
    ~WasapiEngine(){stop();}
    juce::String start(const juce::String& capture,const juce::String& output);
    void stop();
    bool running() const {return live.load();}
    juce::String error() const {return failed.load()?"Audio device disconnected or stream failed; processing disabled":"";}
    unsigned getStartCount() const {return startCount.load();}
    unsigned getUnderruns() const {return underruns.load();}
    unsigned getOverruns() const {return overruns.load();}
    float getQueueMs() const {return queue.available()/48.0f;}
    float getInputPeak() const {return inputPeak.load();}
    float getOutputPeak() const {return outputPeak.load();}
private:
    void captureLoop(juce::String);
    void renderLoop(juce::String);
    void processQuantum();
    TDSpeedVSTAudioProcessor& processor;
    CaptureQueue queue;
    std::thread captureThread,renderThread;
    std::atomic<bool> quitting{false},live{false},failed{false};
    std::atomic<int> captureReady{0},renderReady{0};
    std::atomic<HRESULT> captureError{S_OK},renderError{S_OK};
    std::atomic<unsigned> startCount{0};
    std::atomic<unsigned> underruns{0},overruns{0};
    std::atomic<float> inputPeak{0},outputPeak{0};
    juce::AudioBuffer<float> block;
    juce::MidiBuffer midi;
    juce::LagrangeInterpolator leftInterpolator,rightInterpolator;
    std::array<float,600> inputLeft{},inputRight{};
    double clockRatio=1.0;
    bool primed=false;
    HANDLE forwardingLease=nullptr;
};
}
