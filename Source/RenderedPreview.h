#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>
#include <vector>

namespace tdPreview {
// Prepared on a worker. Both the audio callback and waveform only read it.
struct Audio {
    juce::File file, original;
    float semitones=0;
    uint64_t generation=0;
    bool temporary=false;
    mutable std::atomic<bool> dragged{false};
    ~Audio(){if(temporary && !dragged.load())file.deleteFile();}
    double sampleRate=48000;
    juce::AudioBuffer<float> samples;
    struct Peak {float low=0,high=0;};
    std::vector<std::vector<std::vector<Peak>>> peaks;
    double duration() const {return samples.getNumSamples()/sampleRate;}
    void buildPeaks() {
        peaks.resize((size_t)samples.getNumChannels());
        for(int c=0;c<samples.getNumChannels();++c) {
            auto& levels=peaks[(size_t)c];levels.emplace_back();
            for(int start=0;start<samples.getNumSamples();start+=128) {
                Peak p;for(int i=start;i<juce::jmin(start+128,samples.getNumSamples());++i){const auto v=samples.getSample(c,i);p.low=juce::jmin(p.low,v);p.high=juce::jmax(p.high,v);}levels[0].push_back(p);
            }
            while(levels.back().size()>1) {
                const auto& in=levels.back();std::vector<Peak> next;next.reserve((in.size()+1)/2);
                for(size_t i=0;i<in.size();i+=2){auto p=in[i];if(i+1<in.size()){p.low=juce::jmin(p.low,in[i+1].low);p.high=juce::jmax(p.high,in[i+1].high);}next.push_back(p);}levels.push_back(std::move(next));
            }
        }
    }
    Peak range(int channel,int begin,int end) const {
        Peak p;begin=juce::jlimit(0,samples.getNumSamples(),begin);end=juce::jlimit(begin,samples.getNumSamples(),end);
        if(end-begin<=256){for(int i=begin;i<end;++i){const float v=samples.getSample(channel,i);p.low=juce::jmin(p.low,v);p.high=juce::jmax(p.high,v);}return p;}
        const auto& levels=peaks[(size_t)channel];int level=0,bucket=128;
        while(level+1<(int)levels.size() && (end-begin)/bucket>4){++level;bucket*=2;}
        const auto& bins=levels[(size_t)level];
        for(int i=begin/bucket;i<(end+bucket-1)/bucket && i<(int)bins.size();++i){p.low=juce::jmin(p.low,bins[(size_t)i].low);p.high=juce::jmax(p.high,bins[(size_t)i].high);}return p;
    }
};

class Session {
public:
    std::atomic<bool> playing{false};
    std::atomic<double> position{0};
    // Message-thread ownership. Retired buffers are reclaimed on that thread,
    // only after every callback that could have acquired them has finished.
    void install(std::unique_ptr<Audio> data) {
        playing=false;data->generation=++nextGeneration;if(current)retired.push_back(std::move(current));current=std::move(data);
        active.store(current.get());seek(0);collect();
    }
    const Audio* audio() const {return current.get();} // message thread only
    std::shared_ptr<const Audio> snapshot() const {return current;} // message thread only
    void collect(){if(readers.load()==0)retired.clear();}
    void seek(double seconds){seekRequest.store(juce::jlimit(0.,current?current->duration():0.,seconds));position.store(seekRequest.load());}
    void play(){if(current){if(position.load()>=current->duration())seek(0);playing=true;}}
    void stop(){playing=false;seek(0);}
    bool render(juce::AudioBuffer<float>& output,double hostRate) noexcept {
        if(!playing.load() || !std::isfinite(hostRate) || hostRate<=0)return false;
        readers.fetch_add(1);
        const auto* data=active.load();
        if(!data){readers.fetch_sub(1);return false;}
        const double requested=seekRequest.exchange(-1.);
        if(lastGeneration!=data->generation){lastGeneration=data->generation;cursorSeconds=0;}
        double frame=(requested>=0?requested:cursorSeconds)*data->sampleRate;
        const double step=data->sampleRate/hostRate;const int count=data->samples.getNumSamples();
        for(int i=0;i<output.getNumSamples();++i,frame+=step) {
            const auto base=(int64_t)std::floor(frame);const float t=(float)(frame-base);
            for(int c=0;c<output.getNumChannels();++c) {
                const int source=juce::jmin(c,data->samples.getNumChannels()-1);
                auto sample=[&](int64_t index){return index>=0 && index<count?data->samples.getSample(source,(int)index):0.f;};
                const float a=sample(base-1),b=sample(base),d=sample(base+1),e=sample(base+2);
                output.setSample(c,i,((.5f*(e-a)+1.5f*(b-d))*t+(a-2.5f*b+2*d-.5f*e))*t*t+.5f*(d-a)*t+b);
            }
        }
        cursorSeconds=juce::jmin(frame,(double)count)/data->sampleRate;position.store(cursorSeconds);
        if(frame>=count)playing=false;
        readers.fetch_sub(1);return true;
    }
private:
    std::shared_ptr<Audio> current;
    std::vector<std::shared_ptr<Audio>> retired;
    std::atomic<const Audio*> active{nullptr};
    std::atomic<unsigned> readers{0};
    std::atomic<double> seekRequest{-1};
    uint64_t nextGeneration=0,lastGeneration=0;
    double cursorSeconds=0; // callback only; GUI seeks are commands, not a competing clock
};
}
