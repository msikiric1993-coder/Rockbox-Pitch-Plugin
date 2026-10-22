#include "ReferenceConfig.h"
#include "PluginProcessor.h"
#include "VarispeedEngine.h"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
void set(TDSpeedVSTAudioProcessor& p,const char* id,float value) {
    auto* parameter=p.apvts.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
template<class T> void write(std::ofstream& out,T value) { out.write(reinterpret_cast<const char*>(&value),sizeof(value)); }
float signal(int kind,std::uint32_t frame,int channel,int fs) {
    if(kind==0) return static_cast<float>(.2*std::sin(2*juce::MathConstants<double>::pi*(channel==0?440:659)*frame/fs));
    if(kind==1) return (channel==0?1.f:-.5f)*static_cast<float>(frame%100003)/200006.f;
    if(kind==3) return frame%997==static_cast<std::uint32_t>(channel*31)?.25f:0.f;
    std::uint32_t x=frame*1664525u+1013904223u+static_cast<std::uint32_t>(channel)*374761393u;
    x^=x>>13; x*=1274126177u; x^=x>>16;
    return (static_cast<float>(x&65535)/32768.f-1.f)*.125f;
}
}

int main(int argc,char** argv) {
    try {
        juce::ScopedJuceInitialiser_GUI gui;
        std::ofstream fixtures(argc>1?argv[1]:"reference-fixtures.bin",std::ios::binary);
        require(static_cast<bool>(fixtures),"cannot create fixtures");
        fixtures.write("TDSP1F01",8); write<std::uint32_t>(fixtures,24);
        double maximumError=0; std::uint64_t compared=0;
        for(int fs : {44100,48000,96000}) for(int scenario=0;scenario<8;++scenario) {
            const float initial=std::array<float,8>{0,-12,-4,4,0,-4,-12,12}[scenario];
            const int capacitySeconds=scenario>=6?2:8;
            const int length=fs*(scenario==7?7:9);
            const int signalKind=scenario<4?scenario:2;
            TDSpeedVSTAudioProcessor original(capacitySeconds);
            set(original,"semitones",initial); set(original,"revOn",0); set(original,"rackBypass",1); set(original,"rackLimiter",0);
            original.prepareToPlay(fs,1024);
            tdspeed::VarispeedEngine portable; portable.prepare(fs,fs*capacitySeconds,initial);
            juce::AudioBuffer<float> reference(2,1024); juce::MidiBuffer midi;
            std::array<float,2048> pcm{};
            const std::array<int,8> sizes{1,17,257,64,511,3,1024,127};
            std::uint32_t blocks=0; for(int off=0;off<length;++blocks) off+=std::min(sizes[blocks%sizes.size()],length-off);
            write<std::uint32_t>(fixtures,fs); write<std::uint32_t>(fixtures,fs*capacitySeconds);
            write<std::uint32_t>(fixtures,signalKind); write<float>(fixtures,initial); write<std::uint32_t>(fixtures,blocks);
            bool didReset=false; int offset=0; double caseError=0;
            for(std::uint32_t block=0;block<blocks;++block) {
                const int n=std::min(sizes[block%sizes.size()],length-offset);
                float pitch=initial;
                if(scenario==4) pitch=offset<fs*2?0:offset<fs*4?-4:offset<fs*6?4:0;
                if(scenario==5 && offset>=fs*4) pitch=0;
                const bool reset=scenario==5 && offset>=fs*2 && !didReset;
                if(reset) { original.requestBacklogReset(); didReset=true; }
                set(original,"semitones",pitch);
                reference.setSize(2,n,false,false,true);
                for(int i=0;i<n;++i) for(int ch=0;ch<2;++ch)
                    pcm[2*i+ch]=signal(signalKind,offset+i,ch,fs),reference.setSample(ch,i,pcm[2*i+ch]);
                original.processBlock(reference,midi); portable.processInterleaved(pcm.data(),n,{pitch,reset});
                for(int i=0;i<n;++i) for(int ch=0;ch<2;++ch) {
                    const double error=std::abs(static_cast<double>(reference.getSample(ch,i))-pcm[2*i+ch]);
                    require(std::isfinite(pcm[2*i+ch]),"portable nonfinite output");
                    caseError=std::max(caseError,error); ++compared;
                }
                const auto s=portable.state();
                require(std::abs(original.getBacklogSeconds()-static_cast<float>(s.backlogSeconds))<1e-6f,"reference backlog differs");
                require(original.getBufferStatus()==static_cast<int>(s.recovery),"reference recovery status differs");
                require(std::abs(original.getPauseSeconds()-static_cast<float>(s.recoveryFramesRemaining/static_cast<double>(fs)))<1e-6f,"reference recovery countdown differs");
                write<std::uint32_t>(fixtures,n); write<float>(fixtures,pitch); write<std::uint32_t>(fixtures,reset?1:0);
                // Sparse samples from the real processor, plus every block's state,
                // let Android test its own JNI/Clang build against this oracle.
                for(int i : {0,n/2,n-1}) for(int ch=0;ch<2;++ch) write<float>(fixtures,reference.getSample(ch,i));
                write<float>(fixtures,original.getBacklogSeconds()); write<std::int32_t>(fixtures,original.getBufferStatus());
                write<float>(fixtures,original.getPauseSeconds());
                offset+=n;
            }
            require(caseError<=1e-6,"sample error exceeds approved dry target");
            maximumError=std::max(maximumError,caseError);
            std::cout << "PASS fs="<<fs<<" scenario="<<scenario<<" frames="<<length<<" maxAbsError="<<caseError<<'\n';
        }
        require(static_cast<bool>(fixtures),"fixture write failed");
        std::cout << "PASS cases=24 comparedStereoSamples="<<compared<<" maxAbsError="<<maximumError<<" tolerance=1e-6\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
