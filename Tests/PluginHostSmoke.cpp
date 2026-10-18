#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include <stdexcept>
#include <fstream>

static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        require(argc==2 || argc==3,"Pass the built VST3 bundle path and optional compatibility-render file");
        juce::VST3PluginFormat format;juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions,juce::File(argv[1]).getFullPathName());
        require(descriptions.size()==1,"RC VST3 scan failed or factory is ambiguous");
        require(descriptions[0]->name.contains("TDSpeedPreview"),"Wrong plugin identity");
        juce::String error;auto instance=format.createInstanceFromDescription(*descriptions[0],48000,257,error);
        if(!instance)throw std::runtime_error(error.toStdString());
        auto parameter=[&](const juce::String& id)->juce::AudioProcessorParameter* {
            const juce::String exportedId(juce::VST3ClientExtensions::convertJuceParameterId(id,true));
            for(auto* p:instance->getParameters())if(auto* hosted=dynamic_cast<juce::HostedAudioProcessorParameter*>(p))if(hosted->getParameterID()==exportedId)return p;
            throw std::runtime_error("Missing hosted parameter: "+id.toStdString());
        };
        auto set=[&](const char* id,float normalized){parameter(id)->setValueNotifyingHost(normalized);};
        if (argc==3 && juce::String(argv[2])=="--export-tail") {
            set("timingMode",0);set("bypass",0);set("semitones",0);
            set("rack1_on",0);set("rack2_on",0);set("rack3_on",0);set("revOn",0);
            instance->setRateAndBufferSizeDetails(48000,257);instance->prepareToPlay(48000,257);
            juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;int position=0;
            for(;position<48000;position+=257){audio.clear();for(int n=0;n<257;++n)if(position+n>=47992 && position+n<48000)for(int c=0;c<2;++c)audio.setSample(c,n,.25f);instance->processBlock(audio,midi);}
            const double tail=instance->getTailLengthSeconds();
            require(tail>1.4 && tail<1.6,"Real VST3 tail omits slowed buffered ending");
            double energy=0;for(int n=0;n<96000;n+=257){audio.clear();instance->processBlock(audio,midi);for(int i=0;i<257;++i)energy+=audio.getSample(0,i)*audio.getSample(0,i);}
            require(energy>.01,"Real VST3 loses the clip's final marker");
            require(instance->getTailLengthSeconds()==0,"Real VST3 reports an endless silence tail");
            instance->releaseResources();std::cout<<"PASS actual VST3 buffered tail, final marker and drainage\n";return 0;
        }
        // Optional deterministic render: run this same host against the accepted
        // and release modules, then compare their raw float files byte-for-byte.
        if (argc == 3) {
            struct Clock : juce::AudioPlayHead {
                int64_t sample=0;
                juce::Optional<PositionInfo> getPosition() const override {
                    PositionInfo p;p.setTimeInSamples(sample);p.setIsPlaying(true);return p;
                }
            } clock;
            instance->setPlayHead(&clock);
            std::ofstream out(argv[2],std::ios::binary|std::ios::trunc);
            require(out.good(),"Cannot create compatibility render");
            juce::MidiBuffer midi;juce::AudioBuffer<float> audio(2,257);
            for (double rate : {44100.,48000.,96000.})
                for (int mode : {0,1}) for (float pitch : {-12.f,7.f}) {
                    set("timingMode",(float)mode);set("bypass",0);set("semitones",(pitch+12)/24);
                    set("rack1_effect",mode?50.f/51:51.f/51);set("rack1_mix",.73f);
                    set("rack1_on",1);set("rack2_on",0);set("rack3_on",0);set("revOn",0);
                    instance->setRateAndBufferSizeDetails(rate,257);instance->prepareToPlay(rate,257);instance->reset();
                    for (int offset=0;offset<(int)(rate*2);offset+=257) {
                        clock.sample=100000+offset;
                        for(int c=0;c<2;++c)for(int n=0;n<257;++n)
                            audio.setSample(c,n,.04f*std::sin((offset+n)*(.037f+c*.003f))+.02f*std::cos((offset+n)*.013f));
                        instance->processBlock(audio,midi);
                        for(int c=0;c<2;++c)out.write((const char*)audio.getReadPointer(c),257*sizeof(float));
                    }
                    instance->releaseResources();
                }
            require(out.good(),"Compatibility render write failed");
            instance->setPlayHead(nullptr);
            std::cout<<"PASS compatibility render: 12 rate/mode/pitch/effect cases\n";return 0;
        }
        set("timingMode",0);set("bypass",1);set("rack1_on",0);set("rack2_on",0);set("rack3_on",0);set("revOn",0);
        juce::AudioBuffer<float> block(2,257);juce::MidiBuffer midi;
        for(double rate:{44100.,48000.,96000.}) {
            instance->setRateAndBufferSizeDetails(rate,257);instance->prepareToPlay(rate,257);
            for(int k=0;k<8;++k){for(int c=0;c<2;++c)for(int i=0;i<257;++i)block.setSample(c,i,(c?-.04f:.03f)*std::sin(i*.07f));
                auto dry=juce::AudioBuffer<float>(block);instance->processBlock(block,midi);
                for(int c=0;c<2;++c)for(int i=0;i<257;++i)require(block.getSample(c,i)==dry.getSample(c,i),"Hosted dry forwarding changes samples");
            }
            instance->releaseResources();
        }
        set("rack1_effect",50.0f/51);set("rack1_mix",1);set("rack1_on",1);set("bypass",0);
        instance->setRateAndBufferSizeDetails(48000,257);instance->prepareToPlay(48000,257);
        double wetEnergy=0;float peak=0;
        for(int offset=0;offset<3*48000;offset+=257){block.clear();if(offset==0)block.setSample(0,0,.25f);instance->processBlock(block,midi);
            for(int c=0;c<2;++c)for(int n=0;n<257;++n){const float v=block.getSample(c,n);require(std::isfinite(v)&&std::abs(v)<32,"Hosted wet output is unbounded");wetEnergy+=v*v;peak=juce::jmax(peak,std::abs(v));
                if(offset+n==12000)require(std::abs(v)<1e-7,"Fully wet reverb leaks the original impulse");}
        }
        require(wetEnergy>1e-8 && peak>1e-5,"Hosted Signalsmith is silent at 100% Mix");
        set("rack1_mix",.73f);juce::MemoryBlock saved;instance->getStateInformation(saved);set("rack1_mix",0);instance->setStateInformation(saved.getData(),(int)saved.getSize());
        require(std::abs(parameter("rack1_mix")->getValue()-.73f)<1e-5,"Hosted VST3 state loses Mix");
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance->createEditor());require(editor && !editor->isResizable(),"Hosted editor is missing or resizable");editor.reset();instance->releaseResources();
        std::cout<<"PASS real VST3 factory/parameter IDs, dry 44.1/48/96 kHz, wet-only Signalsmith, state recall, locked editor; wet energy="<<wetEnergy<<" peak="<<peak<<"\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
