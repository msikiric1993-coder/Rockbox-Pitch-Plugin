#include "PluginProcessor.h"
#include <iostream>
#include <numeric>
#include <stdexcept>
namespace reference030 {
#include "Reference/SpringVerb-0.3.0.h"
}

static void set(TDSpeedVSTAudioProcessor& p,const juce::String& id,float v){tdEffects::setParameter(p.apvts,id,v);}
static void require(bool ok,const juce::String& message){if(!ok)throw std::runtime_error(message.toStdString());}
static void writeWav(const juce::File& file,const juce::AudioBuffer<float>& b,double rate){
    juce::WavAudioFormat format;auto stream=file.createOutputStream();require(stream!=nullptr,"WAV output failed");stream->setPosition(0);stream->truncate();
    auto writer=std::unique_ptr<juce::AudioFormatWriter>(format.createWriterFor(stream.get(),rate,2,24,{},0));require(writer!=nullptr,"WAV writer failed");stream.release();writer->writeFromAudioSampleBuffer(b,0,b.getNumSamples());
}
static juce::AudioBuffer<float> fixture(double rate,int seconds,int type){
    juce::AudioBuffer<float> b(2,(int)(rate*seconds));b.clear();juce::Random random(8147);float pink[2][3]{};
    for(int i=0;i<b.getNumSamples();++i)for(int c=0;c<2;++c){
        const double t=i/rate;float x=0;
        if(type==0){const float w=random.nextFloat()*2-1;pink[c][0]=.99765f*pink[c][0]+w*.099046f;pink[c][1]=.963f*pink[c][1]+w*.2965164f;pink[c][2]=.57f*pink[c][2]+w*1.0526913f;x=(pink[c][0]+pink[c][1]+pink[c][2]+w*.1848f)*.012f;}
        if(type==1 && i==0 && c==0)x=.25f;
        if(type==2 && t<8){const double notes[]{220,261.625565,329.627557,440};const double beat=std::fmod(t,.75);const double envelope=std::exp(-beat*5);for(int n=0;n<4;++n)x+=(float)(.025*envelope*std::sin(juce::MathConstants<double>::twoPi*notes[(int(t/.75)+n)%4]*(1+c*.001)*t));}
        b.setSample(c,i,x);
    }return b;
}
static juce::AudioBuffer<float> render(TDSpeedVSTAudioProcessor& p,const juce::AudioBuffer<float>& in){
    auto out=juce::AudioBuffer<float>(in);p.effects.resetEffect(0);p.effects.beginBlock();
    for(int pos=0;pos<out.getNumSamples();pos+=257){p.effects.beginBlock();p.effects.process(out,pos,juce::jmin(257,out.getNumSamples()-pos));require(!p.effects.faulted(0),"Fault bypass cannot count as wet output");}
    return out;
}
static double energy(const juce::AudioBuffer<float>& b,int start,int end){double e=0;for(int c=0;c<2;++c)for(int i=start;i<end;++i){const float v=b.getSample(c,i);require(std::isfinite(v)&&std::abs(v)<=32,"Unbounded render");e+=v*v;}return e;}
int main(int argc,char** argv){
    juce::ScopedJuceInitialiser_GUI gui;const juce::File folder(argc>1?argv[1]:"audit");folder.createDirectory();juce::Array<juce::var> rows;bool passed=true;
    try{
        TDSpeedVSTAudioProcessor p(2);const auto& effects=tdEffects::catalogue();
        for(double rate:{44100.,48000.,96000.}){
            set(p,"rack1_on",0);p.prepareToPlay(rate,257);auto noise=fixture(rate,16,0),impulse=fixture(rate,12,1);
            for(size_t ix=0;ix<effects.size();++ix){const auto& e=effects[ix];auto* row=new juce::DynamicObject();juce::var value(row);row->setProperty("id",e.id);row->setProperty("sampleRate",rate);row->setProperty("available",e.available);
                if(!e.available){row->setProperty("reason",e.unavailableReason);rows.add(value);continue;}
                try{
                    tdEffects::chooseEffect(p.apvts,0,(int)ix);set(p,"rack1_on",1);set(p,"rack1_mix",100);set(p,"rack1_level",0);
                    auto wet=render(p,noise);const double dryEnergy=energy(noise,(int)(rate*8),noise.getNumSamples()),wetEnergy=energy(wet,(int)(rate*8),wet.getNumSamples());
                    require(wetEnergy>1e-9,"Engine is silent at 100% Mix");const double rmsRatio=std::sqrt(wetEnergy/dryEnergy);row->setProperty("wetRmsRatio",rmsRatio);row->setProperty("wetPeak",wet.getMagnitude(0,wet.getNumSamples()));
                    const auto gains0=tdEffects::mixGains(e,0),gains1=tdEffects::mixGains(e,1);require(gains0.first==1 && gains0.second==0 && gains1.first==0,"Mix endpoints incorrect");
                    if(e.category=="Reverb" || e.category=="Space"){
                        auto silence=juce::AudioBuffer<float>(2,257);silence.clear();auto quiet=render(p,silence);const float resetPeak=quiet.getMagnitude(0,257);row->setProperty("resetSilencePeak",resetPeak);require(resetPeak<(e.engine>=100 && e.engine<200?1e-6f:1e-8f),"Reset retains previous audio: "+juce::String(resetPeak,10));
                        auto response=render(p,impulse);const double tail=energy(response,(int)(rate*.05),response.getNumSamples());require(tail>1e-10,"Reverb has no wet decay");row->setProperty("tailEnergy",tail);
                        int onset=-1;double stereoDifference=0;for(int n=0;n<response.getNumSamples();++n){if(onset<0 && (std::abs(response.getSample(0,n))+std::abs(response.getSample(1,n)))>1e-7)onset=n;stereoDifference+=std::pow(response.getSample(0,n)-response.getSample(1,n),2);}
                        row->setProperty("onsetMs",onset*1000/rate);if(e.engine==400)require(onset>=int(rate*.020),"Signalsmith leaks sound before pre-delay");row->setProperty("stereoDifference",stereoDifference);require(stereoDifference>1e-12,"Reverb is unexpectedly mono");
                        juce::Array<juce::var> controls;
                        for(const auto& c:e.controls){tdEffects::resetSlot(p.apvts,0);set(p,"rack1_mix",100);set(p,tdEffects::controlId(0,e,c),c.low);auto low=render(p,impulse);set(p,tdEffects::controlId(0,e,c),c.high);auto high=render(p,impulse);double delta=0;for(int ch=0;ch<2;++ch)for(int n=0;n<low.getNumSamples();++n)delta+=std::pow(low.getSample(ch,n)-high.getSample(ch,n),2);auto* detail=new juce::DynamicObject();detail->setProperty("control",c.id);detail->setProperty("differenceEnergy",delta);detail->setProperty("measurable",delta>1e-12);controls.add(juce::var(detail));if(c.id!="freeze")require(delta>1e-12,"Control has no measurable effect: "+c.id);}
                        row->setProperty("controls",controls);
                        // Sustain combined extreme controls, then change them
                        // during playback. Individual impulse sweeps alone do
                        // not exercise feedback growth or live setter behavior.
                        tdEffects::resetSlot(p.apvts,0);set(p,"rack1_mix",100);
                        for(const auto& c:e.controls)set(p,tdEffects::controlId(0,e,c),c.id=="freeze"?c.low:c.high);
                        auto sustained=fixture(rate,30,0);auto extreme=render(p,sustained);
                        row->setProperty("extremeThirtySecondPeak",extreme.getMagnitude(0,extreme.getNumSamples()));
                        p.effects.resetEffect(0);p.effects.beginBlock();auto live=fixture(rate,4,2);int changes=0;
                        for(int pos=0;pos<live.getNumSamples();pos+=257){
                            if(pos%((int)(rate*.2)/257*257)==0){for(const auto& c:e.controls)set(p,tdEffects::controlId(0,e,c),c.id=="freeze"?c.low:(changes%2?c.low:c.high));++changes;}
                            p.effects.beginBlock();p.effects.process(live,pos,juce::jmin(257,live.getNumSamples()-pos));require(!p.effects.faulted(0),"Live control changes fault effect");
                        }
                        energy(live,0,live.getNumSamples());row->setProperty("liveControlChanges",changes);
                        if(rate==48000)writeWav(folder.getChildFile(e.id+"-impulse-wet.wav"),response,rate);
                    }
                    row->setProperty("passed",true);std::cout<<"PASS "<<e.id.toStdString()<<" "<<rate<<"\n"<<std::flush;
                }catch(const std::exception& err){row->setProperty("passed",false);row->setProperty("error",juce::String(err.what()));passed=false;std::cerr<<e.id.toStdString()<<" "<<rate<<": "<<err.what()<<"\n";}
                rows.add(value);
            }
        }
        // Original generated music fixture; no third-party recording rights required.
        auto music=fixture(48000,20,2);writeWav(folder.getChildFile("00-original-fixture.wav"),music,48000);p.prepareToPlay(48000,257);
        for(int v=0;v<4;++v){tdEffects::applySpaceVariant(p.apvts,0,v);set(p,"rack1_on",1);set(p,"rack1_mix",100);auto wet=render(p,music);writeWav(folder.getChildFile(juce::StringArray{"Deep-Space","Nebula","Universe","Dark-Cloud"}[v]+"-100-percent-wet.wav"),wet,48000);}
        for(int mode=0;mode<2;++mode){reference030::SpringVerb old;old.prepare(48000,257);old.setParameters(1.5f,6000,mode);auto oldWet=juce::AudioBuffer<float>(music);float* ch[]{oldWet.getWritePointer(0),oldWet.getWritePointer(1)};old.process(ch,2,oldWet.getNumSamples(),1);writeWav(folder.getChildFile(mode?"PSX-v0.3.0-wet.wav":"Spring-v0.3.0-wet.wav"),oldWet,48000);
            tdEffects::chooseEffect(p.apvts,0,48+mode);set(p,"rack1_on",1);set(p,"rack1_mix",100);auto rcWet=render(p,music);writeWav(folder.getChildFile(mode?"PSX-RC-wet.wav":"Spring-RC-wet.wav"),rcWet,48000);
        }
    }catch(const std::exception& err){passed=false;std::cerr<<err.what()<<"\n";}
    auto* result=new juce::DynamicObject();result->setProperty("passed",passed);result->setProperty("measurements",rows);folder.getChildFile("engine-audit.json").replaceWithText(juce::JSON::toString(juce::var(result)));return passed?0:1;
}
