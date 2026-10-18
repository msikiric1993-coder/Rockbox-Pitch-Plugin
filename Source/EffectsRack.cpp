#include "EffectsRack.h"
#include "SpringVerb.h"
#include "FractionalDelay.h"
#include "WetCalibration.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/Galactic.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/Verbity2.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/StereoChorus.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/ChorusEnsemble.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/GalacticVibe.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/Chamber2.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/kPlateA.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/Drive.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/ToTape6.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/Flutter.h"
#include "ThirdParty/Airwindows/src/autogen_airwin/BitGlitter.h"
#include "ThirdParty/CloudSeed/DSP/ReverbController.h"
#include "ThirdParty/CloudSeed/Programs.h"
#include "ThirdParty/ChowMatrix/ChowMatrix-Diffusion.h"
#include "ThirdParty/ChowTape/AnalogTapeModel-HysteresisProcessing.h"
// Keep Freeverb3's compatibility macros out of JUCE and the other engines.
#include "freeverb/zrev2.hpp"
#include "freeverb/progenitor2.hpp"
#include "freeverb/strev.hpp"
#include "freeverb/nrev.hpp"
#include "freeverb/earlyref.hpp"
#undef isfinite
#undef isnormal
#undef fpclassify
#include <numeric>
#include <signalsmith-basics/reverb.h>
namespace tdMVerb {
#include "ThirdParty/MVerb/MVerb.h"
}
using tdMVerb::MVerb;

namespace tdEffects
{
namespace
{
constexpr int chunkSize = 256;
using Values = std::array<float, 8>;
Control c(const char* id,const char* name,const char* unit,float low,float high,float initial,float step=0.01f,float skew=1)
{ return {id,name,unit,low,high,initial,step,skew}; }
std::unique_ptr<AirwinConsolidatedBase> airEngine(int type)
{
    using namespace airwinconsolidated;
    switch(type)
    {
        case 100:return std::make_unique<Galactic::Galactic>(0);
        case 101:return std::make_unique<Verbity2::Verbity2>(0);
        case 102:return std::make_unique<StereoChorus::StereoChorus>(0);
        case 103:return std::make_unique<ChorusEnsemble::ChorusEnsemble>(0);
        case 104:return std::make_unique<GalacticVibe::GalacticVibe>(0);
        case 105:return std::make_unique<Chamber2::Chamber2>(0);
        case 106:return std::make_unique<kPlateA::kPlateA>(0);
        case 107:return std::make_unique<Drive::Drive>(0);
        case 108:return std::make_unique<ToTape6::ToTape6>(0);
        case 109:return std::make_unique<Flutter::Flutter>(0);
        case 110:return std::make_unique<BitGlitter::BitGlitter>(0);
    }
    return {};
}
int airCount(int type)
{
    using namespace airwinconsolidated;
    const int counts[]{Galactic::kNumParameters,Verbity2::kNumParameters,StereoChorus::kNumParameters,
        ChorusEnsemble::kNumParameters,GalacticVibe::kNumParameters,Chamber2::kNumParameters,
        kPlateA::kNumParameters,Drive::kNumParameters,ToTape6::kNumParameters,Flutter::kNumParameters,BitGlitter::kNumParameters};
    return counts[type-100];
}
bool wetParameter(const juce::String& name)
{ return name.containsIgnoreCase("wet") || name.equalsIgnoreCase("mix"); }

using Line = tdEffects::FractionalDelay;
struct Engine
{
    virtual ~Engine()=default;
    virtual void prepare(double)=0;
    virtual void reset()=0;
    virtual void controls(const Values&)=0;
    virtual void process(float**,int)=0;
    virtual void loadIR(const juce::File&){}
};

// Original engines: bounded delay lines and an eight-line orthogonal reverb.
struct Native final : Engine
{
    explicit Native(int kindIn):kind(kindIn){}
    int kind;double rate=48000;Values p{};double phase=0;
    std::array<Line,8> lines;std::array<Line,8> diffusion;
    std::array<float,8> damp{}, times{}, gains{};
    std::array<std::array<float,12>,2> allpass{};
    float feedback[2]{},envelope[2]{},held[2]{},filters[2]{},previous[2]{};int hold=0;
    tdChowMatrix::Diffusion chowDiffusion[2];
    HysteresisProcessing tape[2];
    juce::dsp::Compressor<float> compressor;
    juce::dsp::NoiseGate<float> gate;
    juce::dsp::StateVariableTPTFilter<float> svf;
    juce::dsp::IIR::Filter<float> equaliser[2];
    std::array<float,6> eqCoefficients{};
    void prepare(double sampleRate) override
    {
        rate=sampleRate;
        const bool longDelay=kind>=20 && kind<=25;
        const int usedLines=kind<=4?8:longDelay?(kind==25?4:2):(kind>=10 && kind<=14 || kind==33?2:0);
        for(int i=0;i<8;++i)lines[(size_t)i].prepare(i<usedLines?(int)(rate*(longDelay?2.6:0.32)):4);
        for(int i=0;i<8;++i)diffusion[(size_t)i].prepare(kind<=4?(int)(rate*(0.0051+i*0.00137)):4);
        juce::dsp::ProcessSpec spec{rate,chunkSize,2};compressor.prepare(spec);gate.prepare(spec);svf.prepare(spec);
        for(auto& eq:equaliser){eq.coefficients=new juce::dsp::IIR::Coefficients<float>(1,0,0,1,0,0);eq.prepare({rate,chunkSize,1});}
        for(int ch=0;ch<2;++ch){chowDiffusion[ch].prepare({rate,chunkSize,1});tape[ch].setSampleRate(rate);tape[ch].cook(0.4,0.5,0.5,false);}
        reset();
    }
    void reset() override
    {
        for(auto& line:lines)line.reset();for(auto& line:diffusion)line.reset();
        damp.fill(0);for(auto& a:allpass)a.fill(0);phase=0;hold=0;
        for(int ch=0;ch<2;++ch){feedback[ch]=envelope[ch]=held[ch]=filters[ch]=previous[ch]=0;chowDiffusion[ch].reset();tape[ch].reset();equaliser[ch].reset();}
        compressor.reset();gate.reset();svf.reset();
    }
    void controls(const Values& values) override
    {
        p=values;
        if(kind<=4)
        {
            const float base[]{0.0297f,0.0371f,0.0411f,0.0437f,0.0533f,0.0617f,0.0719f,0.0797f};
            for(int i=0;i<8;++i){times[(size_t)i]=(base[i]*(0.45f+p[1]*1.8f))*rate;gains[(size_t)i]=std::pow(10.0f,-3.0f*times[(size_t)i]/(rate*p[0]));}
        }
        if(kind==24 || kind==25)for(auto& d:chowDiffusion)d.setDepth(juce::jlimit(0.0f,1.0f,p[3]),false);
        if(kind==45)for(auto& t:tape)t.cook(p[0],p[1],p[2],false);
        if(kind==50){compressor.setThreshold(p[0]);compressor.setRatio(p[1]);compressor.setAttack(p[2]);compressor.setRelease(p[3]);}
        if(kind==51){gate.setThreshold(p[0]);gate.setRatio(10);gate.setAttack(p[1]);gate.setRelease(p[2]);}
        if(kind==40 || kind==41){svf.setType(kind==40?juce::dsp::StateVariableTPTFilterType::lowpass:juce::dsp::StateVariableTPTFilterType::highpass);svf.setCutoffFrequency(juce::jmin(p[0],(float)rate*0.45f));svf.setResonance(p[1]);}
        if(kind==46){svf.setType(juce::dsp::StateVariableTPTFilterType::bandpass);svf.setResonance(p[1]);}
        if(kind==42)
        {
            // ArrayCoefficients avoids reference-counted coefficient allocation in callbacks.
            eqCoefficients=juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter(rate,juce::jmin(p[0],(float)rate*0.45f),p[1],juce::Decibels::decibelsToGain(p[2]));
            for(auto& eq:equaliser){auto* raw=eq.coefficients->getRawCoefficients();const float inv=1.0f/eqCoefficients[3];raw[0]=eqCoefficients[0]*inv;raw[1]=eqCoefficients[1]*inv;raw[2]=eqCoefficients[2]*inv;raw[3]=eqCoefficients[4]*inv;raw[4]=eqCoefficients[5]*inv;}
        }
    }
    void process(float** channels,int count) override
    {
        const float tau=juce::MathConstants<float>::twoPi;
        const float pole=std::exp(-tau*juce::jmin(p[3],(float)rate*0.45f)/(float)rate);
        for(int n=0;n<count;++n)
        {
            const float in[2]{channels[0][n],channels[1][n]};float out[2]{};
            if(kind<=4)
            {
                float input[2]{in[0],in[1]};
                for(int ch=0;ch<2;++ch)for(int stage=0;stage<4;++stage)
                {
                    auto& line=diffusion[(size_t)(stage+4*ch)];const float delay=line.read((float)line.samples.size()-4);
                    const float value=delay-p[4]*input[ch];line.push(input[ch]+p[4]*value);input[ch]=value;
                }
                // A separate pre-delay does not allocate as its control changes.
                lines[6].push(input[0]);lines[7].push(input[1]);
                const float preL=lines[6].read(1+p[2]*rate*0.001f),preR=lines[7].read(1+p[2]*rate*0.001f);
                float sum=0,read[6]{};const bool frozen=kind==4 && p[6]>0.5f;
                for(int i=0;i<6;++i){read[i]=lines[(size_t)i].read(times[(size_t)i]);damp[(size_t)i]=frozen?read[i]:(1-pole)*read[i]+pole*damp[(size_t)i];sum+=damp[(size_t)i];}
                for(int i=0;i<6;++i)lines[(size_t)i].push((damp[(size_t)i]-sum/3.0f)*(frozen?0.99999f:gains[(size_t)i])+(frozen?0:0.2f*((i&1)?preR:preL)));
                out[0]=(read[0]+read[2]-read[4])*0.7f;out[1]=(read[1]+read[3]-read[5])*0.7f;
                const float mid=(out[0]+out[1])*0.5f,side=(out[0]-out[1])*0.5f*p[5];out[0]=mid+side;out[1]=mid-side;
            }
            else if(kind>=10 && kind<=17)
            {
                const double angle=phase*tau;
                for(int ch=0;ch<2;++ch)
                {
                    const float mod=std::sin(angle+ch*p[4]*juce::MathConstants<double>::pi);
                    if(kind==15 || kind==16 || kind==17)
                    {
                        const int stages=kind==17?4:kind==15?6:12;const float freq=juce::jlimit(40.0f,(float)rate*0.4f,p[2]*std::pow(2.0f,p[1]*mod));
                        const float k=std::tan(juce::MathConstants<float>::pi*freq/(float)rate),a=(1-k)/(1+k);
                        float x=in[ch]+feedback[ch]*p[3];
                        for(int stage=0;stage<stages;++stage){const float y=-a*x+allpass[(size_t)ch][(size_t)stage];allpass[(size_t)ch][(size_t)stage]=x+a*y;x=y;}
                        out[ch]=feedback[ch]=x;
                    }
                    else
                    {
                        const float delayMs=juce::jmax(0.05f,p[2]+p[1]*mod);
                        const float delayed=lines[(size_t)ch].read(delayMs*rate*0.001f);
                        float wet=delayed;
                        if(kind==11)wet=(delayed+lines[(size_t)ch].read((p[2]+p[1]*std::sin(angle*1.17+2.0+ch))*rate*0.001f))*0.5f;
                        lines[(size_t)ch].push(in[ch]+delayed*p[3]);out[ch]=wet;
                    }
                }
                phase+=p[0]/rate;
            }
            else if(kind>=20 && kind<=25)
            {
                const float tonePole=std::exp(-tau*juce::jmin(p[2],(float)rate*0.45f)/(float)rate);
                const float wobble=kind==22?std::sin(phase*tau)*p[3]*0.003f:0;
                float reads[4]{};
                const int nodes=kind==25?4:2;
                for(int node=0;node<nodes;++node)
                {
                    reads[node]=lines[(size_t)node].read((p[0]*0.001f*(1+(kind==25?node*0.373f:0))+wobble)*rate);
                    if(kind==23)reads[node]=(reads[node]+0.5f*lines[(size_t)node].read(p[0]*rate*0.0005f)+0.25f*lines[(size_t)node].read(p[0]*rate*0.0015f))/1.75f;
                    damp[(size_t)node]=(1-tonePole)*reads[node]+tonePole*damp[(size_t)node];
                }
                for(int node=0;node<nodes;++node)
                {
                    float fb=damp[(size_t)(kind==21?1-node:node)];
                    if(kind==24 || kind==25)fb=chowDiffusion[node&1].processSample(fb);
                    const float x=in[node&1]+p[1]*fb;lines[(size_t)node].push(kind==22?std::tanh(x*1.2f)/1.2f:x);
                    out[node&1]+=reads[node]/(nodes/2);
                }
                phase+=0.4/rate;
            }
            else for(int ch=0;ch<2;++ch)
            {
                const float x=in[ch];
                switch(kind)
                {
                    case 30:out[ch]=x*(1-p[1]*0.5f+p[1]*0.5f*std::sin(phase*tau));break;
                    case 31:out[ch]=x*std::sqrt(0.5f*(1+(ch==0?-1:1)*p[1]*std::sin(phase*tau)));break;
                    case 32:out[ch]=x*(1-p[1]+p[1]*std::sin(phase*tau));break;
                    case 33:{const float motion=std::sin(phase*tau+ch*juce::MathConstants<float>::pi);lines[(size_t)ch].push(x);const float shifted=lines[(size_t)ch].read((2+p[1]*motion)*rate*0.001f);const float gain=0.7f+p[1]*0.3f*motion;const float tone=std::exp(-tau*p[2]/(float)rate);filters[ch]=(1-tone)*shifted+tone*filters[ch];out[ch]=gain*(x*(1-p[3])+filters[ch]*p[3]);break;}
                    case 40:case 41:out[ch]=svf.processSample(ch,x);break;
                    case 42:out[ch]=equaliser[ch].processSample(x);break;
                    case 43:out[ch]=std::tanh(x*juce::Decibels::decibelsToGain(p[0]))*juce::Decibels::decibelsToGain(p[1]);break;
                    case 44:if(hold==0){const float levels=std::pow(2.0f,std::round(p[0])-1);held[ch]=std::round(x*levels)/levels;}out[ch]=held[ch];break;
                    case 45:out[ch]=(float)tape[ch].process<NR4,double>((double)x)*juce::Decibels::decibelsToGain(p[3]);break;
                    case 46:{const float coefficient=std::exp(-1.0f/(float)(rate*(std::abs(x)>envelope[ch]?p[3]*0.001f:0.15)));envelope[ch]=coefficient*envelope[ch]+(1-coefficient)*std::abs(x);svf.setCutoffFrequency(juce::jlimit(40.0f,(float)rate*0.4f,p[0]*(1+p[2]*envelope[ch]*5)));out[ch]=svf.processSample(ch,x);break;}
                    case 50:out[ch]=compressor.processSample(ch,x)*juce::Decibels::decibelsToGain(p[4]);break;
                    case 51:out[ch]=gate.processSample(ch,x);break;
                    default:out[ch]=x;break;
                }
            }
            if(kind>=30 && kind<=33)phase+=p[0]/rate;
            if(kind==44)hold=(hold+1)%juce::jmax(1,(int)p[1]);
            if(phase>=1)phase-=std::floor(phase);
            for(int ch=0;ch<2;++ch)channels[ch][n]=out[ch];
        }
    }
};

struct LegacyEngine final : Engine
{
    explicit LegacyEngine(int m):mode(m){}
    SpringVerb verb; int mode;
    void prepare(double rate) override{verb.prepare(rate,chunkSize);}
    void reset() override{verb.reset();}
    void controls(const Values& v) override{verb.setParameters(v[0],v[1],mode);}
    void process(float** ch,int n) override{verb.process(ch,2,n,1);}
};

struct Air final : Engine
{
    explicit Air(int typeIn):type(typeIn),effect(airEngine(typeIn)){}
    int type;std::unique_ptr<AirwinConsolidatedBase> effect;std::vector<int> map,wetIndices;Values last{};
    void prepare(double rate) override
    {
        effect->setSampleRate((float)rate);map.clear();wetIndices.clear();
        for(int i=0;i<airCount(type);++i){char name[64]{};effect->getParameterName(i,name);if(!wetParameter(name))map.push_back(i);else wetIndices.push_back(i);}
        reset();
    }
    void reset() override {effect->tdReset();last.fill(-1);}
    void controls(const Values& values) override
    {
        for(size_t i=0;i<map.size();++i)effect->setParameter(map[i],values[i]);
        for(int index:wetIndices)effect->setParameter(index,1);
    }
    void process(float** channels,int count) override{effect->processReplacing(channels,channels,count);}
};

struct Cloud final : Engine
{
    std::unique_ptr<Cloudseed::ReverbController> effect;Values last{};
    void prepare(double rate) override
    {
        effect=std::make_unique<Cloudseed::ReverbController>((int)rate);
        static const auto program=[] {Cloudseed::initPrograms();std::array<float,Cloudseed::Parameter::COUNT> p{};std::copy_n(Cloudseed::ProgramDarkPlate,p.size(),p.begin());return p;}();
        for(int i=0;i<Cloudseed::Parameter::COUNT;++i)effect->SetParameter(i,program[(size_t)i]);
        effect->SetParameter(Cloudseed::Parameter::DryOut,0);
        effect->SetParameter(Cloudseed::Parameter::EarlyOut,0.5);
        effect->SetParameter(Cloudseed::Parameter::LateOut,0.85);
        effect->SetParameter(Cloudseed::Parameter::EarlyDiffuseEnabled,1);
        effect->SetParameter(Cloudseed::Parameter::EqLowpassEnabled,1);
        reset();
    }
    void reset() override{effect->ClearBuffers();last.fill(-1);}
    void controls(const Values& v) override
    {
        const int ids[]{Cloudseed::Parameter::LateLineDecay,Cloudseed::Parameter::LateLineSize,Cloudseed::Parameter::EqCutoff,
            Cloudseed::Parameter::LateLineModAmount,Cloudseed::Parameter::LateLineModRate,Cloudseed::Parameter::EarlyDiffuseFeedback};
        for(int i=0;i<6;++i)if(last[(size_t)i]!=v[(size_t)i]){effect->SetParameter(ids[i],v[(size_t)i]);last[(size_t)i]=v[(size_t)i];}
    }
    void process(float** ch,int n) override{effect->Process(ch[0],ch[1],ch[0],ch[1],n);}
};

struct Dragon final : Engine
{
    explicit Dragon(int kindIn):kind(kindIn){}
    int kind;std::unique_ptr<fv3::revbase_f> base;
    Line predelay[2];double rate=48000;Values p{};
    float output[2][chunkSize]{};
    void prepare(double sampleRate) override
    {
        rate=sampleRate;
        if(kind==300)base=std::make_unique<fv3::zrev2_f>();
        if(kind==301)base=std::make_unique<fv3::progenitor2_f>();
        if(kind==302)base=std::make_unique<fv3::nrev_f>();
        if(kind==303)base=std::make_unique<fv3::earlyref_f>();
        base->setSampleRate((float)rate);base->setRSFactor(kind==301?0.65f:1);base->setdryr(0);base->setwetr(1);
        for(auto& line:predelay)line.prepare((int)(rate*0.3));reset();
    }
    void reset() override{base->mute();for(auto& line:predelay)line.reset();}
    void controls(const Values& values) override
    {
        p=values;base->setwidth(kind==303?p[2]:p[3]);
        if(kind==300){auto& r=static_cast<fv3::zrev2_f&>(*base);r.setrt60(p[0]);r.setloopdamp(p[1]);r.setoutputlpf(p[1]);r.setidiffusion1(p[4]);}
        if(kind==301){auto& r=static_cast<fv3::progenitor2_f&>(*base);r.setrt60(p[0]);r.setdamp(p[1]);r.setoutputdamp(p[1]);r.setdiffusion1(p[4]);}
        if(kind==302){auto& r=static_cast<fv3::nrev_f&>(*base);r.setrt60(p[0]);r.setdamp(std::exp(-juce::MathConstants<float>::twoPi*p[1]/(float)rate));r.setfeedback(p[4]);}
        if(kind==303)static_cast<fv3::earlyref_f&>(*base).setoutputlpf(p[0]);
    }
    void process(float** ch,int n) override
    {
        for(int i=0;i<n;++i)for(int c=0;c<2;++c){predelay[c].push(ch[c][i]);ch[c][i]=predelay[c].read(1+p[kind==303?1:2]*rate*0.001f);}
        // EarlyRef writes the dry output before reading its reflection input.
        // Distinct output storage is required even though other tanks allow aliasing.
        base->processreplace(ch[0],ch[1],output[0],output[1],n);
        for(int c=0;c<2;++c)std::copy_n(output[c],n,ch[c]);
    }
};

// Signalsmith uses separate buffers. Its decay parameter is RT20, not RT60.
struct Signalsmith final : Engine
{
    signalsmith::basics::ReverbFloat effect;
    Line predelay[2]; double rate=48000; Values p{};
    float output[2][chunkSize]{};
    void prepare(double sr) override {rate=sr;effect.dry=0;effect.wet=1;effect.configure(sr,chunkSize,2);for(auto& d:predelay)d.prepare((int)(sr*.25));reset();}
    void reset() override {effect.reset();for(auto& d:predelay)d.reset();}
    void controls(const Values& v) override {
        p=v;effect.dry=0;effect.wet=1;effect.rt20=v[0]/3.0;effect.roomMs=v[1];
        effect.highCutHz=v[3];effect.lowCutHz=v[4];effect.detune=v[5];effect.early=v[6];
        effect.lowDampRate=1.5;effect.highDampRate=2;
    }
    void process(float** ch,int n) override {
        for(int i=0;i<n;++i)for(int c=0;c<2;++c){predelay[c].push(ch[c][i]);ch[c][i]=predelay[c].read(1+p[2]*rate*.001);}
        float* out[]{output[0],output[1]};effect.process(ch,out,n);
        for(int i=0;i<n;++i){const float mid=(out[0][i]+out[1][i])*.5f,side=(out[0][i]-out[1][i])*.5f*p[7];ch[0][i]=mid+side;ch[1][i]=mid-side;}
    }
};
struct MVerbEngine final : Engine
{
    std::unique_ptr<MVerb<float>> effect; Values last{};
    float output[2][chunkSize]{};
    void prepare(double sr) override {effect=std::make_unique<MVerb<float>>();effect->setSampleRate((float)sr);effect->setParameter(MVerb<float>::PREDELAY,.1f);effect->setParameter(MVerb<float>::MIX,1);effect->setParameter(MVerb<float>::GAIN,1);last.fill(-1);reset();}
    void reset() override {effect->reset();last.fill(-1);}
    void controls(const Values& v) override {
        const int ids[]{MVerb<float>::DECAY,MVerb<float>::SIZE,MVerb<float>::PREDELAY,MVerb<float>::DAMPINGFREQ,MVerb<float>::DENSITY,MVerb<float>::BANDWIDTHFREQ,MVerb<float>::EARLYMIX};
        for(int i=0;i<7;++i)if(v[i]!=last[i]){effect->setParameter(ids[i],i==2?v[i]/200.0f:v[i]);last[i]=v[i];}
    }
    void process(float** ch,int n) override {float* out[]{output[0],output[1]};effect->process(ch,out,n);for(int c=0;c<2;++c)std::copy_n(out[c],n,ch[c]);}
};

struct Convolution final : Engine
{
    unsigned generation=0;
    juce::MemoryBlock builtinIR;
    juce::dsp::Convolution convolution;Values p{};double lengthSeconds=.8;
    juce::dsp::StateVariableTPTFilter<float> filter;
    void prepare(double rate) override
    {
        // Original, deterministic IR: a short chamber with discrete reflections.
        juce::AudioBuffer<float> impulse(2,(int)(rate*0.8));impulse.clear();
        juce::Random random(718312);
        for(int ch=0;ch<2;++ch)for(int i=(int)(rate*0.012);i<impulse.getNumSamples();++i)
            impulse.setSample(ch,i,(random.nextFloat()*2-1)*0.025f*std::exp(-7.0f*i/(float)impulse.getNumSamples()));
        builtinIR.reset();juce::WavAudioFormat wav;
        auto stream=std::make_unique<juce::MemoryOutputStream>(builtinIR,false);
        auto writer=std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.get(),rate,2,32,{},0));
        if(writer){stream.release();writer->writeFromAudioSampleBuffer(impulse,0,impulse.getNumSamples());writer.reset();}
        convolution.loadImpulseResponse(std::move(impulse),rate,juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::no,juce::dsp::Convolution::Normalise::yes);
        convolution.prepare({rate,chunkSize,2});filter.prepare({rate,chunkSize,2});filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);reset();
    }
    bool prepareFile(const juce::File& file,double rate)
    {
        juce::AudioFormatManager fm;fm.registerBasicFormats();auto reader=std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(file));
        if(!reader || reader->lengthInSamples<=0 || reader->lengthInSamples>reader->sampleRate*20)return false;
        lengthSeconds=reader->lengthInSamples/reader->sampleRate;juce::AudioBuffer<float> ir(2,(int)reader->lengthInSamples);reader->read(&ir,0,ir.getNumSamples(),0,true,true);
        convolution.loadImpulseResponse(std::move(ir),reader->sampleRate,juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::no,juce::dsp::Convolution::Normalise::yes);
        convolution.prepare({rate,chunkSize,2});filter.prepare({rate,chunkSize,2});filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);reset();
        return convolution.getCurrentIRSize()>0;
    }
    void loadIR(const juce::File& file) override
    {
        if(file.getFullPathName().isEmpty())convolution.loadImpulseResponse(builtinIR.getData(),builtinIR.getSize(),juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::no,0,juce::dsp::Convolution::Normalise::yes);
        else convolution.loadImpulseResponse(file,juce::dsp::Convolution::Stereo::yes,juce::dsp::Convolution::Trim::yes,0,juce::dsp::Convolution::Normalise::yes);
    }
    void reset() override{convolution.reset();filter.reset();}
    void controls(const Values& values) override{p=values;filter.setCutoffFrequency(p[0]);filter.setResonance(0.707f);}
    void process(float** channels,int n) override
    {juce::dsp::AudioBlock<float> block(channels,2,(size_t)n);juce::dsp::ProcessContextReplacing<float> context(block);convolution.process(context);filter.process(context);}
};
}

const std::vector<Effect>& catalogue()
{
    static const auto effects=[]
    {
        std::vector<Effect> list;
        const std::vector<Control> hall{c("decay","Decay"," s",0.2f,15,2.5f),c("size","Size","",0.05f,1,0.65f),c("predelay","Pre-delay"," ms",0,200,20),c("damping","Damping"," Hz",500,16000,6000,1,0.4f),c("diffusion","Diffusion","",0.1f,0.85f,0.65f),c("width","Width","",0,1.5f,1)};
        auto room=hall;room[0].initial=0.7f;room[1].initial=0.2f;room[2].initial=5;
        list.push_back({"hall","Smooth hall","Reverb","Original warm stereo hall, inspired by your Poweramp listening preference.",hall,1});
        list.push_back({"room","Small room","Reverb","Compact natural ambience with a short, dense tail.",room,2});
        auto space=hall;space[0].initial=8;space[1].initial=1;space[3].initial=4000;
        list.push_back({"space","Deep space","Reverb","Long, dark, diffuse original ambience.",space,3});
        const std::vector<Control> dragon{c("decay","Decay"," s",0.2f,15,2.5f),c("damping","Damping"," Hz",500,16000,6000,1,0.4f),c("predelay","Pre-delay"," ms",0,200,20),c("width","Width","",0,1,1),c("diffusion","Diffusion","",0.1f,0.8f,0.6f)};
        list.push_back({"dragon_hall","Dragonfly / Zita hall","Reverb","Freeverb3 ZRev2 engine used by Dragonfly Hall; fixed room geometry.",dragon,300});
        list.push_back({"dragon_room","Dragonfly / Progenitor room","Reverb","Freeverb3 Progenitor2 engine used by Dragonfly Room; compact geometry.",dragon,301});
        list.push_back({"dragon_plate","Dragonfly / NRev plate","Reverb","Freeverb3 NRev plate family used by Dragonfly Plate.",dragon,302});
        auto reflections=dragon;reflections.erase(reflections.begin());reflections.pop_back();
        // Keep engine value layout stable: hidden decay/diffusion are not relevant to reflections.
        list.push_back({"dragon_early","Dragonfly / Early reflections","Reverb","Freeverb3 early-reflection pattern; no late reverb tank.",reflections,303});
        list.push_back({"cloudseed","CloudSeed cloud","Reverb","Original CloudSeedCore DSP: thick, modulated, vintage digital ambience.",{
            c("decay","Decay shape","",0,0.92f,0.65f),c("size","Line size","",0,0.9f,0.55f),c("tone","Tone","",0,1,0.7f),c("mod","Modulation","",0,1,0.25f),c("rate","Movement","",0,1,0.3f),c("diffusion","Diffusion","",0,0.9f,0.7f)},200});
        list.push_back({"convolution","Convolution / Load IR","Reverb","Original built-in chamber. Load your own WAV/AIFF impulse response.",{c("tone","High cut"," Hz",500,16000,12000,1,0.4f)},60});
        const std::vector<Control> chorus{c("rate","Rate"," Hz",0.02f,8,0.3f,0.01f,0.45f),c("depth","Depth"," ms",0,10,3),c("delay","Delay"," ms",0.1f,35,15),c("feedback","Feedback","",-0.9f,0.9f,0.1f),c("width","Stereo phase","",0,1,0.5f)};
        list.push_back({"chorus","Stereo chorus","Modulation","Gentle stereo thickening with independent phase.",chorus,10});
        list.push_back({"ensemble","Ensemble chorus","Modulation","Two moving voices per channel.",chorus,11});
        auto flanger=chorus;flanger[1]={c("depth","Depth"," ms",0,4,1)};flanger[2]={c("delay","Delay"," ms",0.05f,10,2)};flanger[3].initial=0.65f;
        list.push_back({"flanger","Stereo flanger","Modulation","Short moving comb filter with signed feedback.",flanger,12});
        flanger[3].initial=-0.6f;list.push_back({"negative_flanger","Negative flanger","Modulation","Hollow comb sweeps with negative feedback.",flanger,13});
        auto vibrato=chorus;vibrato[3].initial=0;
        list.push_back({"vibrato","Vibrato","Modulation","Moving-delay pitch wobble. Use full mix for pure vibrato.",vibrato,14});
        const std::vector<Control> phaser{c("rate","Rate"," Hz",0.02f,8,0.25f,0.01f,0.45f),c("depth","Sweep"," oct",0,3,1.5f),c("centre","Centre"," Hz",80,6000,700,1,0.4f),c("feedback","Feedback","",-0.85f,0.85f,0.3f),c("phase","Stereo phase","",0,1,0.5f)};
        list.push_back({"phaser6","6-stage phaser","Modulation","Six allpass stages for a classic phase sweep.",phaser,15});
        list.push_back({"phaser12","12-stage phaser","Modulation","A deeper, more pronounced phase sweep.",phaser,16});
        const std::vector<Control> delay{c("time","Time"," ms",10,1200,350,1,0.5f),c("feedback","Feedback","",0,0.92f,0.35f),c("tone","High cut"," Hz",500,16000,6500,1,0.4f),c("movement","Movement","",0,1,0.15f)};
        for(int i=0;i<6;++i){const char* ids[]{"delay","pingpong","tape_delay","multitap","diffuse_delay","matrix_delay"};const char* names[]{"Stereo delay","Ping-pong delay","Tape-style delay","Multi-tap delay","Diffuse delay","ChowMatrix diffusion delay"};auto controls=delay;if(i==4 || i==5)controls[3].name="Diffusion";if(i==0 || i==1 || i==3)controls.pop_back();list.push_back({ids[i],names[i],"Delay",i==5?"Original four-node delay using ChowMatrix's allpass diffusion DSP.":"Stereo echo with bounded feedback and filtered repeats.",controls,20+i});}
        for(int i=0;i<3;++i){const char* ids[]{"tremolo","autopan","ringmod"};const char* names[]{"Tremolo","Autopan","Ring modulation"};list.push_back({ids[i],names[i],"Movement","Amplitude movement; ring modulation creates metallic sidebands.",{c("rate","Rate"," Hz",0.02f,i==2?2000:15,i==2?70:0.7f,0.01f,0.4f),c("depth","Depth","",0,1,0.8f)},30+i});}
        const std::vector<Control> filter{c("cutoff","Frequency"," Hz",40,16000,4000,1,0.4f),c("resonance","Resonance"," Q",0.5f,4,0.707f)};
        list.push_back({"lowpass","Low-pass filter","Tone","Remove high frequencies with adjustable resonance.",filter,40});
        list.push_back({"highpass","High-pass filter","Tone","Remove low frequencies with adjustable resonance.",filter,41});
        list.push_back({"eq","Parametric EQ","Tone","One adjustable bell filter.",{c("frequency","Frequency"," Hz",40,16000,1000,1,0.4f),c("q","Bandwidth"," Q",0.2f,8,1),c("gain","Gain"," dB",-18,18,0,0.1f)},42});
        list.push_back({"saturation","Soft saturation","Colour","Soft clipping with independent output gain.",{c("drive","Drive"," dB",0,24,6,0.1f),c("output","Output"," dB",-24,0,-6,0.1f)},43});
        list.push_back({"bitcrusher","Bit crusher","Colour","Quantisation and sample-and-hold reduction.",{c("bits","Resolution"," bits",2,24,12,1),c("hold","Sample hold"," samples",1,64,2,1)},44});
        list.push_back({"chow_tape","CHOW magnetic tape","Colour","CHOW Tape scalar magnetic hysteresis core. Not the full CHOW Tape plugin.",{c("drive","Drive","",0.01f,0.95f,0.4f),c("width","Bias / width","",0.01f,0.95f,0.5f),c("saturation","Saturation","",0.01f,0.95f,0.5f),c("output","Output"," dB",-24,12,0,0.1f)},45});
        list.push_back({"compressor","Compressor","Dynamics","Gentle level control without lookahead.",{c("threshold","Threshold"," dB",-48,0,-18,0.1f),c("ratio","Ratio"," :1",1,12,2),c("attack","Attack"," ms",0.1f,100,15),c("release","Release"," ms",10,1500,150,1),c("makeup","Makeup"," dB",0,18,0,0.1f)},50});
        list.push_back({"gate","Noise gate","Dynamics","Reduce quiet background noise.",{c("threshold","Threshold"," dB",-80,-6,-45,0.1f),c("attack","Attack"," ms",0.1f,100,5),c("release","Release"," ms",10,1500,200,1)},51});
        AirwinConsolidatedBase::defaultSampleRate=48000;
        const char* names[]{"Galactic","Verbity2","StereoChorus","ChorusEnsemble","GalacticVibe","Chamber2","kPlateA","Drive","ToTape6","Flutter","BitGlitter"};
        for(int i=0;i<11;++i)
        {
            auto engine=airEngine(100+i);std::vector<Control> controls;
            for(int j=0;j<airCount(100+i);++j){char name[64]{};engine->getParameterName(j,name);if(!wetParameter(name))controls.push_back({"p"+juce::String(j),name,"",0,1,engine->getParameter(j),0.001f,1});}
            list.push_back({"air_"+juce::String(names[i]),"Airwindows / "+juce::String(names[i]),i<2 || i==5 || i==6?"Reverb":i<5?"Modulation":"Colour","Original Airwindows DSP; controls retain the author's normalized ranges.",controls,100+i});
        }
        auto freeze=hall;freeze.push_back(c("freeze","Freeze","",0,1,0,1));
        list.push_back({"freeze","Hold ambience / Freeze","Reverb","Freeze captures the current reverb tank; switch Freeze back off to release it.",freeze,4});
        list.push_back({"phaser4","4-stage phaser","Modulation","A light, classic four-stage phase sweep.",phaser,17});
        list.push_back({"rotary","Rotary-style motion","Movement","Original speaker-style movement with Doppler delay and amplitude modulation.",{c("rate","Rotation"," Hz",0.1f,10,1.2f),c("depth","Motion","",0,1,0.6f),c("tone","Tone"," Hz",500,16000,7000,1,0.4f),c("colour","Colour","",0,1,0.5f)},33});
        list.push_back({"autowah","Auto-wah","Tone","Envelope-controlled resonant band-pass filter.",{c("frequency","Base frequency"," Hz",80,3000,400,1,0.4f),c("resonance","Resonance"," Q",0.5f,5,1.5f),c("sensitivity","Sensitivity","",0,8,3),c("attack","Response"," ms",0.1f,100,10)},46});
        for(int mode=0;mode<2;++mode)list.push_back({mode==0?"spring":"psx",mode==0?"Spring":"PSX","Reverb","Original TDSpeed reverb character.",{c("decay","Decay"," s",0.1f,4,1.5f),c("tone","Tone"," Hz",1000,12000,6000,1,0.4f)},70+mode});
        list.push_back({"signalsmith","Signalsmith Ambient","Space","Signalsmith FDN ambience. Decay is RT60 (RT20 x 3).",{
            c("rt60","Decay / RT60"," s",.3f,90,25.5f),c("size","Room size"," ms",10,200,80),c("predelay","Pre-delay"," ms",0,200,20),c("tone","High cut"," Hz",1000,20000,7200,1,.4f),c("lowcut","Low cut"," Hz",10,500,50,1,.4f),c("motion","Detune","",0,50,8.5f),c("early","Early reflections","",0,2.5f,.55f),c("width","Width","",0,1.5f,1)},400});
        list.push_back({"mverb","MVerb plate / chamber","Reverb","Dattorro-style tank; geometry changes rebuild its tank only once.",{
            c("decay","Decay shape","",.05f,.99f,.7f),c("size","Size","",.05f,1,.75f),c("predelay","Pre-delay"," ms",0,200,20),c("damping","Damping","",0,1,.45f),c("density","Density","",.05f,1,.75f),c("bandwidth","Bandwidth","",.05f,1,.75f),c("early","Late balance","",0,1,.8f)},401});
        const juce::StringArray mainIds{"hall","room","mverb","dragon_plate","spring","psx","convolution","signalsmith","cloudseed","air_Galactic","chorus","ensemble","flanger","phaser6","delay","pingpong","tape_delay","eq","lowpass","highpass","saturation","chow_tape","compressor"};
        for(auto& e:list){
            if(e.id=="cloudseed" || e.id=="air_Galactic")e.category="Space";
            e.mainPicker=mainIds.contains(e.id);
            e.mixing=e.category=="Reverb" || e.category=="Space" || e.category=="Delay" || e.category=="Modulation"?Mixing::equalPower:Mixing::insert;
            e.wetGain=e.engine==60?8.0f:tdWetCalibration(e.engine); // JUCE normalized IR target energy is 0.125.
        }
        return list;
    }();
    return effects;
}
std::pair<float,float> mixGains(const Effect& e,float mix,float wetDb) noexcept
{
    mix=juce::jlimit(0.0f,1.0f,std::isfinite(mix)?mix:0.0f);
    const bool ambience=e.category=="Reverb" || e.category=="Space" || e.category=="Delay";
    const float wet=e.wetGain*(ambience?juce::Decibels::decibelsToGain(juce::jlimit(-24.0f,12.0f,std::isfinite(wetDb)?wetDb:0.0f)):1.0f);
    if(mix==0)return {1,0};if(mix==1)return {0,wet};
    return e.mixing==Mixing::equalPower?std::make_pair(std::cos(mix*juce::MathConstants<float>::halfPi),wet*std::sin(mix*juce::MathConstants<float>::halfPi)):std::make_pair(1-mix,wet*mix);
}
void applySpaceVariant(juce::AudioProcessorValueTreeState& state,int slot,int variant)
{
    const int ix=variant==3?7:variant==1?33:50;chooseEffect(state,slot,ix);
    setParameter(state,slotId(slot,"mix"),variant==2?65:50);
    if(ix==50){setParameter(state,slotId(slot,"signalsmith_rt60"),variant==2?60:25.5f);setParameter(state,slotId(slot,"signalsmith_size"),variant==2?160:100);}
    if(ix==7){setParameter(state,slotId(slot,"cloudseed_decay"),.82f);setParameter(state,slotId(slot,"cloudseed_tone"),.4f);}
    state.state.setProperty(slotId(slot,"variant"),juce::StringArray{"Deep Space","Nebula","Universe","Dark Cloud"}[juce::jlimit(0,3,variant)],nullptr);
}
juce::String slotId(int slot,const juce::String& suffix){return "rack"+juce::String(slot+1)+"_"+suffix;}
static juce::File mixImportMarker(){return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TDSpeed Preview").getChildFile("rc-mix-explained-v1.txt");}
bool mixImportExplained(){return mixImportMarker().existsAsFile();}
void acknowledgeMixImport(){const auto file=mixImportMarker();file.getParentDirectory().createDirectory();file.replaceWithText("RC true dry/wet Mix explanation acknowledged.");}
int effectIndex(float rawChoice){return juce::jlimit(0,(int)catalogue().size()-1,juce::roundToInt(std::isfinite(rawChoice)?rawChoice:0.0f));}
juce::String controlId(int slot,const Effect& effect,const Control& control){return slotId(slot,effect.id+"_"+control.id);}
void addParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& parameters)
{
    juce::StringArray choices;for(const auto& e:catalogue())choices.add(e.name);
    for(int slot=0;slot<3;++slot)
    {
        const auto label="FX "+juce::String(slot+1)+" ";
        parameters.push_back(std::make_unique<juce::AudioParameterBool>(slotId(slot,"on"),label+"Enabled",false));
        parameters.push_back(std::make_unique<juce::AudioParameterChoice>(slotId(slot,"effect"),label+"Effect",choices,slot==0?0:slot==1?9:16));
        parameters.push_back(std::make_unique<juce::AudioParameterFloat>(slotId(slot,"mix"),label+"Mix",0,100,25));
        parameters.push_back(std::make_unique<juce::AudioParameterFloat>(slotId(slot,"level"),label+"Output",-24,12,0));
        for(size_t ix=0;ix<50;++ix)for(const auto& control:catalogue()[ix].controls)
            parameters.push_back(std::make_unique<juce::AudioParameterFloat>(controlId(slot,catalogue()[ix],control),label+catalogue()[ix].name+" "+control.name,juce::NormalisableRange<float>(control.low,control.high,control.step,control.skew),control.initial));
    }
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("rackBypass","Bypass effects",false));
    parameters.push_back(std::make_unique<juce::AudioParameterBool>("rackLimiter","Rack protective limiter",false));
    for(int slot=0;slot<3;++slot){
        parameters.push_back(std::make_unique<juce::AudioParameterFloat>(slotId(slot,"wetLevel"),"Wet Level",-24,12,0));
        parameters.push_back(std::make_unique<juce::AudioParameterBool>(slotId(slot,"unavailable"),"Saved engine unavailable",false));
        for(size_t ix=50;ix<catalogue().size();++ix)for(const auto& c:catalogue()[ix].controls)
            parameters.push_back(std::make_unique<juce::AudioParameterFloat>(controlId(slot,catalogue()[ix],c),catalogue()[ix].name+" "+c.name,juce::NormalisableRange<float>(c.low,c.high,c.step,c.skew),c.initial));
    }

}
void setParameter(juce::AudioProcessorValueTreeState& state,const juce::String& id,float value)
{if(auto* parameter=state.getParameter(id)){parameter->beginChangeGesture();parameter->setValueNotifyingHost(parameter->convertTo0to1(value));parameter->endChangeGesture();}}
void resetSlot(juce::AudioProcessorValueTreeState& state,int slot)
{
    const auto index=effectIndex(state.getRawParameterValue(slotId(slot,"effect"))->load());
    const auto& effect=catalogue()[(size_t)index];
    for(const auto& control:effect.controls)setParameter(state,controlId(slot,effect,control),control.initial);
    setParameter(state,slotId(slot,"mix"),25);setParameter(state,slotId(slot,"level"),0);setParameter(state,slotId(slot,"wetLevel"),0);
}
void chooseEffect(juce::AudioProcessorValueTreeState& state,int slot,int index,int hallVariant)
{
    if(slot<0 || slot>=3)return;
    index=juce::jlimit(0,(int)catalogue().size()-1,index);
    const auto assigned=slotId(slot,"assigned");
    const bool empty=!state.state.getProperty(assigned,false) && state.getRawParameterValue(slotId(slot,"on"))->load()<0.5f;
    state.state.removeProperty(slotId(slot,"variant"),nullptr);setParameter(state,slotId(slot,"effect"),(float)index);resetSlot(state,slot);
    if(index==0 && hallVariant==1){setParameter(state,slotId(slot,"mix"),18);setParameter(state,slotId(slot,"hall_damping"),4200);}
    if(index==0 && hallVariant==2){setParameter(state,slotId(slot,"mix"),25);setParameter(state,slotId(slot,"hall_width"),1.4f);}
    state.state.setProperty(assigned,true,nullptr);
    state.state.removeProperty(slotId(slot,"missingEngine"),nullptr);state.state.removeProperty(slotId(slot,"originalEffectIndex"),nullptr);setParameter(state,slotId(slot,"unavailable"),0);
    if(empty)setParameter(state,slotId(slot,"on"),1);
}
juce::StringArray presetNames(){return {"Dry rack","Warm hall","Wide hall","Deep space","Chorus shimmer","Slow flanger","Ping-pong room","Cloud dream","Vintage tape","Dragonfly plate"};}
void applyPreset(juce::AudioProcessorValueTreeState& state,int preset)
{
    setParameter(state,"revOn",0);
    for(int slot=0;slot<3;++slot)setParameter(state,slotId(slot,"on"),0);
    if(preset==0)return;
    auto choose=[&](int slot,const char* id,float mix){int index=0;for(const auto& e:catalogue()){if(e.id==id)break;++index;}if(index==(int)catalogue().size())return;chooseEffect(state,slot,index);setParameter(state,slotId(slot,"mix"),mix);setParameter(state,slotId(slot,"on"),1);};
    switch(preset)
    {
        case 1:choose(0,"hall",18);setParameter(state,"rack1_hall_damping",4200);break;
        case 2:choose(0,"hall",25);setParameter(state,"rack1_hall_width",1.4f);break;
        case 3:choose(0,"space",40);break;
        case 4:choose(0,"chorus",35);choose(1,"hall",18);break;
        case 5:choose(0,"flanger",40);setParameter(state,"rack1_flanger_rate",0.08f);break;
        case 6:choose(0,"pingpong",25);choose(1,"room",15);break;
        case 7:choose(0,"cloudseed",30);break;
        case 8:choose(0,"air_ToTape6",60);break;
        case 9:choose(0,"dragon_plate",22);break;
        default:break;
    }
}
juce::ValueTree captureState(juce::AudioProcessorValueTreeState& state)
{
    auto all=state.copyState();juce::ValueTree result("RACK");result.setProperty("version",3,nullptr);result.setProperty("mixLaw","RC-equal-power-v1",nullptr);
    for(auto child:all)if(child.getProperty("id").toString().startsWith("rack") || child.getProperty("id").toString().startsWith("rev") || child.getProperty("id")==juce::var("masterOutput"))result.addChild(child.createCopy(),-1,nullptr);
    for(int slot=0;slot<3;++slot)if(all.hasProperty(slotId(slot,"originalEffectIndex")))result.setProperty(slotId(slot,"originalEffectIndex"),all.getProperty(slotId(slot,"originalEffectIndex")),nullptr);
    for(int slot=0;slot<3;++slot){result.setProperty(slotId(slot,"engineId"),all.getProperty(slotId(slot,"missingEngine"),catalogue()[(size_t)effectIndex(state.getRawParameterValue(slotId(slot,"effect"))->load())].id),nullptr);result.setProperty(slotId(slot,"IR"),all.getProperty(slotId(slot,"IR")),nullptr);result.setProperty(slotId(slot,"assigned"),all.getProperty(slotId(slot,"assigned"),false),nullptr);result.setProperty(slotId(slot,"variant"),all.getProperty(slotId(slot,"variant")),nullptr);}
    if(auto xml=juce::XmlDocument::parse(all.getProperty("rcUnknownParameters").toString()))for(auto child:juce::ValueTree::fromXml(*xml))if(!result.getChildWithProperty("id",child.getProperty("id")).isValid())result.addChild(child.createCopy(),-1,nullptr);
    return result;
}
void restoreState(juce::AudioProcessorValueTreeState& state,const juce::ValueTree& rack)
{
    if(!rack.hasType("RACK"))return;
    for(int slot=0;slot<3;++slot)setParameter(state,slotId(slot,"wetLevel"),0);
    for(const char* id:{"masterOutput","revOutput","revWetLevel"})setParameter(state,id,0);
    juce::ValueTree unknown("UNKNOWN");for(auto child:rack){const auto id=child.getProperty("id").toString();if(id.startsWith("rack") || id.startsWith("rev") || id=="masterOutput"){if(state.getParameter(id))setParameter(state,id,(float)child.getProperty("value"));else unknown.addChild(child.createCopy(),-1,nullptr);}}state.state.setProperty("rcUnknownParameters",unknown.toXmlString(),nullptr);
    for(int slot=0;slot<3;++slot){const auto key=slotId(slot,"originalEffectIndex");if(rack.hasProperty(key))state.state.setProperty(key,rack.getProperty(key),nullptr);else state.state.removeProperty(key,nullptr);}
    for(int slot=0;slot<3;++slot){const auto id=rack.getProperty(slotId(slot,"engineId")).toString();bool found=id.isEmpty();for(const auto& e:catalogue())if(e.id==id)found=true;
        if(!found){state.state.setProperty(slotId(slot,"missingEngine"),id,nullptr);setParameter(state,slotId(slot,"unavailable"),1);}else {state.state.removeProperty(slotId(slot,"missingEngine"),nullptr);setParameter(state,slotId(slot,"unavailable"),0);}}
    for(int slot=0;slot<3;++slot){state.state.setProperty(slotId(slot,"IR"),rack.getProperty(slotId(slot,"IR")),nullptr);state.state.setProperty(slotId(slot,"assigned"),rack.getProperty(slotId(slot,"assigned"),true),nullptr);state.state.setProperty(slotId(slot,"variant"),rack.getProperty(slotId(slot,"variant")),nullptr);}
}

struct Rack::Impl
{
    explicit Impl(juce::AudioProcessorValueTreeState& stateIn):state(stateIn)
    {
        limiter=state.getRawParameterValue("rackLimiter");bypass=state.getRawParameterValue("rackBypass");
        for(int i=0;i<3;++i)
        {
            auto& slot=slots[(size_t)i];slot.on=state.getRawParameterValue(slotId(i,"on"));slot.choice=state.getRawParameterValue(slotId(i,"effect"));slot.mix=state.getRawParameterValue(slotId(i,"mix"));slot.level=state.getRawParameterValue(slotId(i,"level"));slot.wetLevel=state.getRawParameterValue(slotId(i,"wetLevel"));slot.unavailable=state.getRawParameterValue(slotId(i,"unavailable"));
            for(const auto& e:catalogue()){std::array<std::atomic<float>*,8> controls{};for(size_t p=0;p<e.controls.size();++p)controls[p]=state.getRawParameterValue(controlId(i,e,e.controls[p]));slot.parameters.push_back(controls);}
        }
    }
    struct Slot
    {
        std::atomic<float>* on=nullptr,*choice=nullptr,*mix=nullptr,*level=nullptr,*wetLevel=nullptr,*unavailable=nullptr;
        std::vector<std::array<std::atomic<float>*,8>> parameters;
        std::vector<std::unique_ptr<Engine>> engines;
        int selected=-1;bool enabled=false;int mute=0;
        Values values{},current{};bool first=true;
        juce::SmoothedValue<float> wet,gain,wetDb;
        std::atomic<Convolution*> pendingConvolution{nullptr};
        std::atomic<Convolution*> activeConvolution{nullptr};
        std::atomic<double> irTail{.8};
        // Generation and state move together, so a completed older load cannot
        // mark a newer missing/loading IR as ready on the audio thread.
        std::atomic<uint64_t> irState{0};
        int irStatus() const noexcept {return (int)(irState.load() & 3);}
        std::atomic<unsigned> irGeneration{0};
        Engine* activeEngine() {return selected==8 && activeConvolution.load()?activeConvolution.load():engines[(size_t)selected].get();}
        std::atomic<bool> fault{false},resetRequested{false};
    };
    juce::AudioProcessorValueTreeState& state;std::array<Slot,3> slots;
    std::atomic<float>* limiter=nullptr,*bypass=nullptr;std::atomic<float> outputPeak{0};
    float dry[2][chunkSize]{},work[2][chunkSize]{};double rate=48000;bool prepared=false;
    // Files own only paths, not decoded audio. Lifetime ends off the audio thread.
    struct OwnedIR {std::unique_ptr<Convolution> engine;std::uint64_t retired=UINT64_MAX;};
    std::vector<OwnedIR> irEngines;std::atomic<std::uint64_t> blockEpoch{0};
    juce::ThreadPool irPool{1};
    ~Impl(){irPool.removeAllJobs(true,-1);}
};
Rack::Rack(juce::AudioProcessorValueTreeState& state):impl(std::make_unique<Impl>(state)){}
Rack::~Rack(){cancelPendingUpdate();impl->irPool.removeAllJobs(true,-1);}
void Rack::prepare(double rate)
{
    impl->irPool.removeAllJobs(true,-1);impl->rate=rate;
    for(int i=0;i<3;++i)
    {
        auto& slot=impl->slots[(size_t)i];slot.engines.clear();
        for(const auto& e:catalogue())
        {
            std::unique_ptr<Engine> engine;
            if(e.engine>=100 && e.engine<200)engine=std::make_unique<Air>(e.engine);
            else if(e.engine==200)engine=std::make_unique<Cloud>();
            else if(e.engine==400)engine=std::make_unique<Signalsmith>();
            else if(e.engine==401)engine=std::make_unique<MVerbEngine>();
            else if(e.engine>=300)engine=std::make_unique<Dragon>(e.engine);
            else if(e.engine==70 || e.engine==71)engine=std::make_unique<LegacyEngine>(e.engine-70);
            else if(e.engine==60)engine=std::make_unique<Convolution>();
            else engine=std::make_unique<Native>(e.engine);
            engine->prepare(rate);
            if(e.engine==60){slot.activeConvolution=static_cast<Convolution*>(engine.get());slot.pendingConvolution.store(nullptr);slot.irState.store(0);}
            slot.engines.push_back(std::move(engine));
        }
        slot.fault.store(false);slot.resetRequested.store(false);slot.selected=-1;slot.enabled=false;slot.mute=0;slot.first=true;
        slot.wet.reset(rate,0.02);slot.gain.reset(rate,0.02);slot.wetDb.reset(rate,.02);
    }
    impl->irEngines.clear();impl->prepared=true;beginBlock();reloadImpulseResponses();
}
void Rack::beginBlock()
{
    if(!impl->prepared)return;
    for(auto& slot:impl->slots)
    {
        if(auto* convolution=slot.pendingConvolution.exchange(nullptr)){
            uint64_t expected=((uint64_t)convolution->generation<<32)|1;
            // Publish the pointer first; a competing new load leaves state at
            // loading and therefore dry-bypasses this response.
            if(slot.irState.load()==expected){slot.activeConvolution=convolution;slot.irTail.store(convolution->lengthSeconds);slot.irState.compare_exchange_strong(expected,((uint64_t)convolution->generation<<32)|2);}
        }
        const int selected=effectIndex(slot.choice->load());const bool on=slot.on->load()>0.5f;
        if(selected!=slot.selected || on!=slot.enabled)
        {
            if(selected==8 && slot.activeConvolution)slot.activeConvolution.load()->reset();else slot.engines[(size_t)selected]->reset();
            if(slot.selected>=0 && slot.enabled && on && selected!=slot.selected)slot.mute=(int)(impl->rate*0.01);
            slot.selected=selected;slot.enabled=on;slot.first=true;
        }
        if(slot.resetRequested.exchange(false)){if(selected==8 && slot.activeConvolution)slot.activeConvolution.load()->reset();else slot.engines[(size_t)selected]->reset();slot.fault.store(false);slot.first=true;slot.mute=0;}
        auto& pointers=slot.parameters[(size_t)selected];
        const auto& controls=catalogue()[(size_t)selected].controls;
        for(size_t i=0;i<8;++i){const float v=pointers[i]?pointers[i]->load():0;slot.values[i]=i<controls.size()?juce::jlimit(controls[i].low,controls[i].high,std::isfinite(v)?v:controls[i].initial):0;}
        if(slot.first){slot.current=slot.values;slot.wet.setCurrentAndTargetValue(juce::jlimit(0.0f,1.0f,std::isfinite(slot.mix->load())?slot.mix->load()*0.01f:0.0f));slot.gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-24.0f,12.0f,std::isfinite(slot.level->load())?slot.level->load():0.0f)));slot.wetDb.setCurrentAndTargetValue(std::isfinite(slot.wetLevel->load())?slot.wetLevel->load():0);slot.first=false;}
        slot.wetDb.setTargetValue(juce::jlimit(-24.0f,12.0f,std::isfinite(slot.wetLevel->load())?slot.wetLevel->load():0));
        slot.wet.setTargetValue(juce::jlimit(0.0f,1.0f,std::isfinite(slot.mix->load())?slot.mix->load()*0.01f:0.0f));slot.gain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-24.0f,12.0f,std::isfinite(slot.level->load())?slot.level->load():0.0f)));
    }
    ++impl->blockEpoch;
}
void Rack::process(juce::AudioBuffer<float>& buffer,int start,int count)
{
    if(!impl->prepared || count<=0 || buffer.getNumChannels()==0)return;
    float peak=0;
    for(int offset=0;offset<count;offset+=chunkSize)
    {
        const int n=juce::jmin(chunkSize,count-offset);float* work[]{impl->work[0],impl->work[1]};
        for(int ch=0;ch<2;++ch)std::copy_n(buffer.getReadPointer(juce::jmin(ch,buffer.getNumChannels()-1))+start+offset,n,work[ch]);
        const float smoothing=1-std::exp(-(float)n/(float)(impl->rate*0.02));
        for(auto& slot:impl->slots)
        {
            if(!slot.enabled || slot.unavailable->load()>.5f || !catalogue()[(size_t)slot.selected].available || (slot.selected==8 && (slot.irStatus()==1 || slot.irStatus()==3)) || slot.fault.load() || impl->bypass->load()>0.5f)continue;
            for(int ch=0;ch<2;++ch)std::copy_n(work[ch],n,impl->dry[ch]);
            for(size_t i=0;i<8;++i)slot.current[i]+=smoothing*(slot.values[i]-slot.current[i]);
            // MVerb geometry must not be smoothed into repeated SIZE tank clears.
            slot.activeEngine()->controls(catalogue()[(size_t)slot.selected].engine==401?slot.values:slot.current);
            slot.activeEngine()->process(work,n);
            bool invalid=false;
            for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)invalid |= !std::isfinite(work[ch][i]) || std::abs(work[ch][i])>32;
            if(invalid){slot.fault.store(true);for(int ch=0;ch<2;++ch)std::copy_n(impl->dry[ch],n,work[ch]);continue;}
            const auto& contract=catalogue()[(size_t)slot.selected];
            for(int sample=0;sample<n;++sample)
            {
                const float mix=slot.wet.getNextValue(),gain=slot.gain.getNextValue(),wetDb=slot.wetDb.getNextValue();const bool muted=slot.mute>0;if(muted)--slot.mute;
                const auto gains=mixGains(contract,mix,wetDb);const float dryGain=gains.first,wetGain=gains.second;
                for(int ch=0;ch<2;++ch){
                    float value=(impl->dry[ch][sample]*dryGain+work[ch][sample]*wetGain)*gain;if(mix==0 && gain==1)value=impl->dry[ch][sample];work[ch][sample]=muted?0:value;}
            }
            bool mixedFault=false;
            for(int ch=0;ch<2;++ch)for(int i=0;i<n;++i)mixedFault |= !std::isfinite(work[ch][i]) || std::abs(work[ch][i])>32;
            if(mixedFault){slot.fault.store(true);for(int ch=0;ch<2;++ch)std::copy_n(impl->dry[ch],n,work[ch]);}
        }
        for(int sample=0;sample<n;++sample)for(int ch=0;ch<buffer.getNumChannels();++ch)
        {
            float value=buffer.getNumChannels()==1?(work[0][sample]+work[1][sample])*0.5f:work[juce::jmin(ch,1)][sample];
            peak=juce::jmax(peak,std::isfinite(value)?std::abs(value):32.0f);
            buffer.setSample(ch,start+offset+sample,value);
        }
    }
    impl->outputPeak.store(peak,std::memory_order_relaxed);
}
void Rack::reset()
{
    if(!impl->prepared)return;
    for(auto& slot:impl->slots)if(slot.selected>=0){slot.activeEngine()->reset();slot.mute=0;}
    impl->outputPeak.store(0);
}
float Rack::peak() const noexcept{return impl->outputPeak.load(std::memory_order_relaxed);}
bool Rack::faulted(int slot) const noexcept{return slot>=0 && slot<3 && impl->slots[(size_t)slot].fault.load();}
void Rack::resetEffect(int slot){if(slot>=0 && slot<3)impl->slots[(size_t)slot].resetRequested.store(true);}
void Rack::loadImpulseResponse(int slotIndex,const juce::File& file)
{
    if(slotIndex<0 || slotIndex>=3)return;auto& slot=impl->slots[(size_t)slotIndex];
    impl->state.state.setProperty(slotId(slotIndex,"IR"),file.getFullPathName(),nullptr);
    const auto generation=++slot.irGeneration;slot.irState.store(((uint64_t)generation<<32)|1);slot.pendingConvolution.store(nullptr);
    if(!impl->prepared)return;
    impl->irPool.addJob([this,slotIndex,file,generation]{
        auto& slot=impl->slots[(size_t)slotIndex];auto engine=std::make_unique<Convolution>();bool ready=true;
        if(file.getFullPathName().isEmpty())engine->prepare(impl->rate);else ready=file.existsAsFile() && engine->prepareFile(file,impl->rate);
        if(slot.irGeneration.load()!=generation)return;
        if(!ready){uint64_t expected=((uint64_t)generation<<32)|1;slot.irState.compare_exchange_strong(expected,((uint64_t)generation<<32)|3);return;}
        const auto epoch=impl->blockEpoch.load();
        // Epoch grace period covers the audio thread's pending-to-active handoff.
        // Ownership and destruction are exclusively on the loader thread.
        for(auto it=impl->irEngines.begin();it!=impl->irEngines.end();){
            bool referenced=false;for(const auto& s:impl->slots)referenced|=s.pendingConvolution.load()==it->engine.get() || s.activeConvolution.load()==it->engine.get();
            if(referenced)it->retired=UINT64_MAX;else if(it->retired==UINT64_MAX)it->retired=epoch;
            if(!referenced && it->retired!=UINT64_MAX && epoch>=it->retired+2)it=impl->irEngines.erase(it);else ++it;
        }
        engine->generation=generation;auto* raw=engine.get();impl->irEngines.push_back({std::move(engine),UINT64_MAX});slot.pendingConvolution.store(raw);
    });
}
void Rack::reloadImpulseResponses(){triggerAsyncUpdate();}
void Rack::handleAsyncUpdate()
{
    for(int slot=0;slot<3;++slot){const auto path=impl->state.state.getProperty(slotId(slot,"IR")).toString();
        if(path.isNotEmpty() || impl->slots[(size_t)slot].irStatus()!=0)loadImpulseResponse(slot,juce::File(path));}
}
juce::String Rack::impulseStatus(int i) const
{
    if(i<0 || i>=3)return {};switch(impl->slots[(size_t)i].irStatus()){
        case 0:return "IR ready / built-in chamber";case 1:return "IR loading / slot dry bypass";case 2:return "IR ready / "+juce::File(impl->state.state.getProperty(slotId(i,"IR")).toString()).getFileName();
        default:return "IR failed or missing / slot dry bypass. Load a valid IR to recover.";}
}
double Rack::tailSeconds() const noexcept
{
    if(impl->bypass->load()>.5f)return 0;double tail=0;
    for(const auto& slot:impl->slots){
        const int ix=effectIndex(slot.choice->load());const auto& e=catalogue()[(size_t)ix];
        if(slot.on->load()<.5f || slot.mix->load()<=0 || slot.unavailable->load()>.5f || !e.available || slot.fault.load())continue;
        auto value=[&](int i){const auto& c=e.controls[(size_t)i];const float v=slot.parameters[(size_t)ix][(size_t)i]->load();return juce::jlimit(c.low,c.high,std::isfinite(v)?v:c.initial);};
        if(e.engine==4 && value(6)>.5f || e.engine==100 && value(0)==0)return std::numeric_limits<double>::infinity();
        if(e.engine==60){if(slot.irStatus()==0 || slot.irStatus()==2)tail+=slot.irTail.load();}
        else if(e.engine==400)tail+=value(0)+value(2)*.001+.2;
        else if(e.engine==200)tail+=Cloudseed::ScaleParam(value(0),Cloudseed::Parameter::LateLineDecay)+1;
        else if(e.engine==401)tail+=30; // Conservative normalized Dattorro feedback/tank estimate.
        else if(e.category=="Reverb" || e.category=="Space")tail+=e.engine>=100 && e.engine<200?60:e.engine==303?.6:value(0)+.6;
        else if(e.category=="Delay"){const float fb=value(1);tail+=value(0)*.001*(fb>0?std::ceil(std::log(.001)/std::log(juce::jlimit(.001f,.92f,fb)))+2:2);}
        else if(e.category=="Modulation")tail+=e.engine>=100?2:e.engine>=15?2:.05*(1+std::ceil(std::log(.001)/std::log(juce::jlimit(.001f,.9f,std::abs(value(3))))));
        else if(e.category=="Tone")tail+=1; // Bounded resonant IIR decay at the exposed low-frequency limits.
    }return tail;
}
}
