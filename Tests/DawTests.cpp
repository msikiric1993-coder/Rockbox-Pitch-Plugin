#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
static thread_local bool watch=false;static thread_local size_t allocations=0;
void* operator new(size_t n){if(watch)++allocations;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void* p) noexcept{std::free(p);}void operator delete[](void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}void operator delete[](void* p,size_t) noexcept{std::free(p);}
static void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
static void set(TDSpeedVSTAudioProcessor& p,const char* id,float value){tdEffects::setParameter(p.apvts,id,value);}
struct Clock : juce::AudioPlayHead {int64_t sample=0;bool playing=true;Optional<PositionInfo> getPosition() const override{PositionInfo p;p.setTimeInSamples(sample);p.setIsPlaying(playing);return p;}};
static std::vector<float> render(TDSpeedVSTAudioProcessor& p,Clock& clock,int64_t origin,int count,int blockSize,bool bypass=false){
    std::vector<float> result(count);juce::AudioBuffer<float> b(2,blockSize);juce::MidiBuffer midi;
    for(int pos=0;pos<count;pos+=blockSize){const int n=juce::jmin(blockSize,count-pos);b.setSize(2,n,false,false,true);for(int c=0;c<2;++c)for(int i=0;i<n;++i)b.setSample(c,i,.08f*std::sin((pos+i)*.037f)+.03f*std::cos((pos+i)*.013f));clock.sample=origin+pos;
        watch=true;if(bypass)p.processBlockBypassed(b,midi);else p.processBlock(b,midi);watch=false;std::copy_n(b.getReadPointer(0),n,result.data()+pos);
    }return result;
}
static void null(const std::vector<float>& a,const std::vector<float>& b){require(a.size()==b.size(),"Output lengths differ");float delta=0;double energy=0;for(float v:a)energy+=v*v;require(energy>1e-6,"Null test cannot pass with two silent tracks");for(size_t i=0;i<a.size();++i)delta=juce::jmax(delta,std::abs(a[i]-b[i]));if(delta>1e-7)throw std::runtime_error("Tracks do not null: "+std::to_string(delta));}
static void pair(TDSpeedVSTAudioProcessor& a,Clock& ca,TDSpeedVSTAudioProcessor& b,Clock& cb,int64_t origin,int count) {
    std::vector<float> output[2];for(auto& o:output)o.resize(count);int next[2]{0,0};const int sizes[]{128,257};juce::AudioBuffer<float> block(2,257);juce::MidiBuffer midi;
    while(next[0]<count || next[1]<count){const int track=next[0]>=count?1:next[1]>=count?0:next[1]<=next[0]?1:0;const int pos=next[track],n=juce::jmin(sizes[track],count-pos);block.setSize(2,n,false,false,true);
        for(int c=0;c<2;++c)for(int i=0;i<n;++i)block.setSample(c,i,.08f*std::sin((pos+i)*.037f)+.03f*std::cos((pos+i)*.013f));auto& clock=track?cb:ca;auto& processor=track?b:a;clock.sample=origin+pos;watch=true;processor.processBlock(block,midi);watch=false;std::copy_n(block.getReadPointer(0),n,output[track].data()+pos);next[track]+=n;
    }null(output[0],output[1]);
}
int main(){juce::ScopedJuceInitialiser_GUI gui;try{
    TDSpeedVSTAudioProcessor a(2),b(2);Clock ca,cb;a.setPlayHead(&ca);b.setPlayHead(&cb);set(a,"timingMode",1);set(b,"timingMode",1);set(a,"semitones",-12);a.createSyncGroup();b.joinSyncGroup(a.syncGroupId());
    a.prepareToPlay(48000,257);b.prepareToPlay(48000,257);
    auto first=render(a,ca,100000,8*48000,128);auto second=render(b,cb,100000,8*48000,257);null(first,second);
    require(!a.dawNeedsRestart() && !b.dawNeedsRestart(),"Sequential offline rendering falsely reports a missing member");
    require(a.getBufferStatus()==b.getBufferStatus(),"Recovery state differs");
    ca.playing=cb.playing=false;const float held=a.getBacklogSeconds();render(a,ca,500000,2000,257);render(b,cb,500000,2000,128);require(a.getBacklogSeconds()==held,"Stopped DAW advances history");
    ca.playing=cb.playing=true;null(render(b,cb,100000,8*48000,128),render(a,ca,100000,8*48000,257));
    set(a,"semitones",-3);require(a.activeDawPitch()==-12 && a.dawStatusText().contains("Pending"),"Pitch change does not latch until Play");
    ca.playing=cb.playing=false;render(a,ca,0,1,1);render(b,cb,0,1,1);ca.playing=cb.playing=true;
    null(render(a,ca,700000,96000,257),render(b,cb,700000,96000,128));require(std::abs(a.activeDawPitch()+3)<.001,"New pitch not applied at Play");
    // Seek forward/backward and loop restart are independent of callback order.
    pair(a,ca,b,cb,1000000,48000);
    pair(a,ca,b,cb,50000,48000);
    pair(a,ca,b,cb,50000,48000);
    pair(a,ca,b,cb,120000,48000); // Forward seek below an earlier high-water mark.
    pair(a,ca,b,cb,50000,48000);
    // Both engines keep consuming the same source phase behind dry bypass.
    set(b,"bypass",1);require(a.groupBypassed(),"Follower bypass not shared");
    null(render(a,ca,98000,24000,128),render(b,cb,98000,24000,257));set(a,"bypass",0);
    null(render(a,ca,122000,48000,257),render(b,cb,122000,48000,128));
    TDSpeedVSTAudioProcessor late(2);Clock cl;late.setPlayHead(&cl);set(late,"timingMode",1);late.joinSyncGroup(a.syncGroupId());late.prepareToPlay(48000,257);render(late,cl,170000,257,257);require(late.dawNeedsRestart(),"Late member invents earlier audio");
    render(a,ca,170000,1024,1024);render(b,cb,170000,257,257);render(b,cb,171000,257,257);require(b.dawNeedsRestart(),"Skipped input history is not reported");
    TDSpeedVSTAudioProcessor missing(2);set(missing,"timingMode",1);missing.prepareToPlay(48000,257);juce::AudioBuffer<float> empty(2,257);empty.clear();juce::MidiBuffer midi;missing.processBlock(empty,midi);require(missing.dawStatusText().contains("unavailable"),"Missing host timing silently uses Listening");
    // Rack recall is independent of group identity and transport mode.
    auto rack=tdEffects::captureState(a.apvts);const auto id=a.syncGroupId();tdEffects::restoreState(a.apvts,rack);require(a.syncGroupId()==id && a.isDawMode(),"Rack loading changes timing/group");
    juce::MemoryBlock state;a.getStateInformation(state);TDSpeedVSTAudioProcessor restored(2);restored.setStateInformation(state.getData(),(int)state.getSize());require(restored.isDawMode() && restored.syncGroupId()==id,"Preview state loses group");
    auto old=a.apvts.copyState();old.setProperty("rcStateVersion",3,nullptr);old.removeChild(old.getChildWithProperty("id","timingMode"),nullptr);juce::MemoryBlock oldData;juce::MemoryOutputStream stream(oldData,false);old.writeToStream(stream);restored.setStateInformation(oldData.getData(),(int)oldData.getSize());require(!restored.isDawMode(),"Old state does not retain Listening");
    a.leaveSyncGroup();b.leaveSyncGroup();set(a,"semitones",-7);a.reset();require(!a.dawRunning(),"Host reset leaves stale playing state");render(a,ca,0,48000,257);require(std::abs(a.activeDawPitch()+7)<.001,"Host processing restart fails to latch new pitch");
    // Re-prepare must release a host-bypass vote from the preceding stream.
    TDSpeedVSTAudioProcessor lifecycle(2);Clock lifecycleClock;lifecycle.setPlayHead(&lifecycleClock);
    set(lifecycle,"timingMode",1);lifecycle.createSyncGroup();lifecycle.prepareToPlay(48000,257);
    render(lifecycle,lifecycleClock,0,257,257,true);
    lifecycle.prepareToPlay(48000,257);lifecycle.reset();
    auto wetAfterPrepare=render(lifecycle,lifecycleClock,0,12000,257);
    double unexpectedDry=0;for(float v:wetAfterPrepare)unexpectedDry+=v*v;
    require(unexpectedDry==0,"Re-prepare leaves a stale group host-bypass vote");
    juce::AudioBuffer<float> zeroBlock(2,0);lifecycle.processBlockBypassed(zeroBlock,midi);
    // A finite clip's final marker must play during a sufficiently extended
    // render, and reported buffered tail must decrease as silence drains it.
    for(double sr : {44100.,48000.,96000.}) for(float semi : {-12.f,-3.f,0.f}) {
        TDSpeedVSTAudioProcessor exportTest(8);Clock exportClock;exportTest.setPlayHead(&exportClock);
        set(exportTest,"timingMode",1);set(exportTest,"semitones",semi);exportTest.prepareToPlay(sr,257);
        const double speed=std::pow(2.0,(double)semi/12.0);
        const int sourceFrames=(int)sr;
        juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer events;
        auto feed=[&](int start,int count,bool source){
            for(int offset=0;offset<count;offset+=257){const int n=juce::jmin(257,count-offset);audio.setSize(2,n,false,false,true);audio.clear();
                if(source)for(int c=0;c<2;++c)for(int i=0;i<n;++i)if(start+offset+i>=sourceFrames-8)audio.setSample(c,i,.25f);
                exportClock.sample=start+offset;watch=true;exportTest.processBlock(audio,events);watch=false;
            }
        };
        feed(0,sourceFrames,true);
        const double tail=exportTest.getBufferedTailSeconds();
        const double expected=(1.25/speed)-1.0;
        require(std::abs(tail-expected)<.001,"Buffered export tail omits pitch backlog or rate conversion");
        require(exportTest.getTailLengthSeconds()>=tail,"Host tail omits buffered audio");
        const int drainFrames=(int)std::ceil((tail+.05)*sr);double markerEnergy=0;
        for(int offset=0;offset<drainFrames;offset+=257){const int n=juce::jmin(257,drainFrames-offset);audio.setSize(2,n,false,false,true);audio.clear();exportClock.sample=sourceFrames+offset;
            watch=true;exportTest.processBlock(audio,events);watch=false;
            for(int i=0;i<n;++i)markerEnergy+=audio.getSample(0,i)*audio.getSample(0,i);
        }
        require(markerEnergy>.01,"Extended export loses the final source marker");
        require(exportTest.getBufferedTailSeconds()==0,"Silence creates an endless reported pitch tail");
    }
    require(allocations==0,"DAW audio callback allocates");
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(a.createEditor());auto* ui=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());ui->setEffectsExpanded(true);ui->setAdvanced(true);for(float scale:{.5f,1.0f,1.25f,1.5f,2.0f}){ui->setUiScale(scale);require(!ui->isResizable() && ui->effectsFit(),"Scaled editor layout invalid");}editor->setSize(400,300);require(ui->effectsFit(),"Host-imposed viewport clips effects");
    std::cout<<"PASS DAW null across callback sizes/order, offline, stops/seeks/loops, latched pitch, linked bypass, recovery, late/skipped history, migration, no allocations, scaling\n";return 0;
}catch(const std::exception& e){watch=false;std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
