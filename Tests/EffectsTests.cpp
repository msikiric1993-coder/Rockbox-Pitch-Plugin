#include "PluginProcessor.h"
#include "EffectsPanel.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <new>
#include <limits>
#include <thread>

static thread_local bool watchAllocations=false;
static thread_local size_t audioAllocations=0;
void* operator new(std::size_t size)
{if(watchAllocations)++audioAllocations;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static void require(bool condition,const juce::String& message)
{if(!condition)throw std::runtime_error(message.toStdString());}
static void set(TDSpeedVSTAudioProcessor& p,const juce::String& id,float value)
{tdEffects::setParameter(p.apvts,id,value);}
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        TDSpeedVSTAudioProcessor processor(2);
        juce::AudioBuffer<float> block(2,257);juce::MidiBuffer midi;
        const auto& catalogue=tdEffects::catalogue();
        for(int choice=0;choice<(int)catalogue.size();++choice)require(tdEffects::effectIndex(choice-0.00001f)==choice,"Effect recall rounds down to the previous engine");
        double worstLoad=0;juce::String worstName;
        for(double rate:{44100.0,48000.0,96000.0})
        {
            processor.prepareToPlay(rate,257);
            for(int sample=0;sample<257;++sample){block.setSample(0,sample,0.1f*sample/257);block.setSample(1,sample,-0.2f*sample/257);}
            auto dry=juce::AudioBuffer<float>(block);processor.effects.beginBlock();processor.effects.process(block,0,257);
            for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)require(block.getSample(ch,i)==dry.getSample(ch,i),"Bypassed rack changes samples");
            for(size_t index=0;index<catalogue.size();++index)
            {
                const auto& effect=catalogue[index];if(!effect.available){std::cout<<"UNAVAILABLE "<<effect.id<<"\n";continue;}
                set(processor,"rack1_on",0);set(processor,"rack1_effect",(float)index);tdEffects::resetSlot(processor.apvts,0);
                set(processor,"rack1_mix",100);set(processor,"rack1_on",1);
                processor.effects.resetEffect(0);processor.effects.beginBlock();processor.effects.reset();
                double energy=0;const auto start=juce::Time::getHighResolutionTicks();
                const int blocks=(int)(rate*2.0/257);
                for(int k=0;k<blocks;++k)
                {
                    for(int i=0;i<257;++i){const float t=(float)((k*257+i)/rate);block.setSample(0,i,0.2f*std::sin(t*440*juce::MathConstants<float>::twoPi));block.setSample(1,i,0.15f*std::sin(t*660*juce::MathConstants<float>::twoPi));}
                    watchAllocations=true;processor.effects.beginBlock();processor.effects.process(block,0,257);watchAllocations=false;
                    for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i){const float value=block.getSample(ch,i);require(std::isfinite(value),effect.name+" produces non-finite audio");require(std::abs(value)<32,effect.name+" output is unbounded");energy+=value*value;}
                }
                const double elapsed=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start);
                const double load=elapsed/(blocks*257/rate);
                if(load>worstLoad){worstLoad=load;worstName=effect.name;}
                require(!processor.effects.faulted(0),effect.name+" faults at defaults");
                require(energy>1e-8,effect.name+" produces no sound at defaults");
                // Every exposed control is swept to both limits together, then
                // processed at a quiet signal to catch feedback/geometry faults.
                for(bool high:{false,true})
                {
                    for(const auto& control:effect.controls)set(processor,tdEffects::controlId(0,effect,control),high?control.high:control.low);
                    for(int k=0;k<12;++k)
                    {
                        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)block.setSample(ch,i,0.02f*std::sin((k*257+i)*0.03f));
                        watchAllocations=true;processor.effects.beginBlock();processor.effects.process(block,0,257);watchAllocations=false;
                        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)require(std::isfinite(block.getSample(ch,i)) && std::abs(block.getSample(ch,i))<32,effect.name+" fails control limits");
                        require(!processor.effects.faulted(0),effect.name+" faults at control limits");
                    }
                }
                set(processor,"rack1_mix",0);processor.effects.beginBlock();
                for(int i=0;i<257;++i){block.setSample(0,i,0.1f);block.setSample(1,i,-0.1f);}
                // Finish the mix ramp before requiring exact zero-wet identity.
                for(int k=0;k<24;++k){processor.effects.process(block,0,257);block.clear();}
                block.setSample(0,7,0.1234567f);block.setSample(1,8,-0.7654321f);processor.effects.process(block,0,257);
                require(block.getSample(0,7)==0.1234567f && block.getSample(1,8)==-0.7654321f,effect.name+" changes zero-mix dry audio");
                std::cout<<"PASS "<<rate<<" "<<effect.id<<"\n";
            }
            set(processor,"rack1_on",0);
        }
        // Equal-power calibration preserves more direct energy than the old linear stack.
        processor.prepareToPlay(48000,257);
        for(int slot=0;slot<3;++slot){set(processor,tdEffects::slotId(slot,"effect"),16);set(processor,tdEffects::slotId(slot,"mix"),25);set(processor,tdEffects::slotId(slot,"on"),1);processor.effects.resetEffect(slot);}
        processor.effects.beginBlock();block.clear();block.setSample(0,0,0.25f);block.setSample(1,0,-0.25f);processor.effects.process(block,0,257);
        const float expected=.25f*std::pow(std::cos(juce::MathConstants<float>::halfPi*.25f),3);require(std::abs(block.getSample(0,0)-expected)<1e-6f && std::abs(block.getSample(1,0)+expected)<1e-6f,"Three-slot equal-power law differs");
        set(processor,"rackBypass",1);processor.effects.beginBlock();block.clear();block.setSample(0,4,.123f);processor.effects.process(block,0,257);require(block.getSample(0,4)==.123f,"Effects bypass changes dry audio");set(processor,"rackBypass",0);
        for(int slot=1;slot<3;++slot)set(processor,tdEffects::slotId(slot,"on"),0);
        // Real engine failure: a compressor at unity ratio plus makeup exceeds
        // the effect safety bound. Its input remains untouched and fault latches.
        set(processor,"rack1_effect",31);tdEffects::resetSlot(processor.apvts,0);set(processor,"rack1_compressor_ratio",1);set(processor,"rack1_compressor_threshold",0);set(processor,"rack1_compressor_makeup",18);set(processor,"rack1_mix",100);processor.effects.resetEffect(0);processor.effects.beginBlock();
        for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)block.setSample(ch,i,8);
        processor.effects.process(block,0,257);require(processor.effects.faulted(0),"Runaway effect does not latch fault");require(block.getSample(0,0)==8,"Fault does not bypass to dry input");
        block.clear();block.setSample(0,0,.25f);processor.effects.reset();processor.effects.beginBlock();processor.effects.process(block,0,257);require(processor.effects.faulted(0) && block.getSample(0,0)==.25f,"History reset clears latched effect fault");
        tdEffects::resetSlot(processor.apvts,0);processor.effects.resetEffect(0);processor.effects.beginBlock();require(!processor.effects.faulted(0),"Reset effect does not recover slot");
        // Both hall variants: 20 seconds each, sustained input followed by an
        // impulse and silence, then maximum controls. Check tail, stability and
        // non-finite control sanitization at all supported processing rates.
        for(double rate:{44100.0,48000.0,96000.0})for(int variant:{1,2}){
            processor.prepareToPlay(rate,257);tdEffects::applyPreset(processor.apvts,variant);processor.effects.resetEffect(0);processor.effects.beginBlock();
            double tailEnergy=0;
            for(int k=0;k<(int)(20*rate/257);++k){
                const double time=k*257/rate;
                if(time>12 && time<12+257/rate){for(const auto& c:catalogue[0].controls)set(processor,tdEffects::controlId(0,catalogue[0],c),c.high);}
                block.clear();for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)if(time<8)block.setSample(ch,i,.02f*std::sin((k*257+i)*440*juce::MathConstants<double>::twoPi/rate));
                if(k==(int)(8*rate/257))for(int ch=0;ch<2;++ch)block.setSample(ch,0,.1f);
                processor.effects.beginBlock();processor.effects.process(block,0,257);
                require(!processor.effects.faulted(0),"Hall variant faults during long signal / maximum controls");
                for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i){const float v=block.getSample(ch,i);require(std::isfinite(v) && std::abs(v)<=32,"Hall output invalid");if(time>9 && time<11)tailEnergy+=v*v;}
            }
            require(tailEnergy>1e-12,"Hall has no audible decay");
            processor.apvts.getRawParameterValue("rack1_hall_decay")->store(std::numeric_limits<float>::quiet_NaN());processor.effects.beginBlock();block.clear();processor.effects.process(block,0,257);require(!processor.effects.faulted(0),"Invalid control not sanitized");
        }
        set(processor,"rack1_on",0);processor.prepareToPlay(96000,257);
        require(audioAllocations==0,"Audio callbacks allocate: "+juce::String((int)audioAllocations));

        // Import a tiny user IR while streaming. Its file/path handoff and the
        // convolution queue must remain allocation-free on the calling audio thread.
        const auto irFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("TDSpeed-test-IR",".wav");
        juce::WavAudioFormat wav;auto fileStream=irFile.createOutputStream();
        auto writer=std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(fileStream.get(),96000,2,24,{},0));
        require(writer!=nullptr,"IR test writer failed");fileStream.release();
        juce::AudioBuffer<float> ir(2,512);ir.clear();ir.setSample(0,0,1);ir.setSample(1,0,1);writer->writeFromAudioSampleBuffer(ir,0,512);writer.reset();
        processor.effects.loadImpulseResponse(0,irFile);
        set(processor,"rack1_effect",8);tdEffects::resetSlot(processor.apvts,0);set(processor,"rack1_mix",100);set(processor,"rack1_on",1);
        watchAllocations=true;processor.effects.beginBlock();watchAllocations=false;
        require(audioAllocations==0,"IR handoff allocates on the audio thread");
        for(int i=0;i<60;++i)
        {
            for(int ch=0;ch<2;++ch)for(int n=0;n<257;++n)block.setSample(ch,n,0.125f);
            watchAllocations=true;processor.effects.beginBlock();processor.effects.process(block,0,257);watchAllocations=false;
            juce::Thread::sleep(5); // Let the IR decoder/engine-builder finish off thread.
        }
        // JUCE normalized IR energy is calibrated back to unity before external Mix.
        require(std::abs(block.getSample(0,250)-0.125f)<0.0005f,"Imported unit IR does not become active: "+juce::String(block.getSample(0,250),6));
        require(audioAllocations==0,"Convolution engine adoption allocates on the audio thread");
        auto irState=tdEffects::captureState(processor.apvts);
        require(irState.getProperty("rack1_IR").toString()==irFile.getFullPathName(),"Rack preset loses IR path");
        // Repeated asynchronous replacements race the audio handoff and reclaim
        // retired kernels. Track allocations only on the actual audio thread.
        std::atomic<bool> irStressRunning{true},irStressFailed{false};
        std::atomic<size_t> irStressAllocations{0};
        std::thread audioThread([&]{juce::AudioBuffer<float> samples(2,257);while(irStressRunning.load()){
            samples.clear();samples.setSample(0,0,.01f);samples.setSample(1,0,.02f);
            watchAllocations=true;processor.effects.beginBlock();processor.effects.process(samples,0,257);watchAllocations=false;
            for(int c=0;c<2;++c)for(int n=0;n<257;++n)if(!std::isfinite(samples.getSample(c,n)))irStressFailed=true;
            std::this_thread::yield();
        }irStressAllocations=audioAllocations;});
        for(int i=0;i<80;++i){processor.effects.loadImpulseResponse(0,i%3==0?irFile.getSiblingFile("missing-stress-IR.wav"):irFile);juce::Thread::sleep(3);}
        processor.effects.loadImpulseResponse(0,irFile);juce::Thread::sleep(250);
        irStressRunning=false;audioThread.join();
        require(!irStressFailed && irStressAllocations==0,"Concurrent IR replacement corrupts samples or allocates on audio thread");
        for(int i=0;i<80 && !processor.effects.impulseStatus(0).startsWith("IR ready");++i){processor.effects.beginBlock();juce::Thread::sleep(5);}
        require(processor.effects.impulseStatus(0).startsWith("IR ready"),"Newest valid IR never becomes ready after replacement stress");
        irFile.deleteFile();

        // A missing IR must never leave the preceding kernel audible.
        const auto missing=irFile.getSiblingFile("missing-TDSpeed-IR.wav");processor.effects.loadImpulseResponse(0,missing);
        juce::Thread::sleep(30);processor.effects.beginBlock();block.clear();block.setSample(0,0,.1f);processor.effects.process(block,0,257);
        require(processor.effects.impulseStatus(0).startsWith("IR failed") && block.getSample(0,0)==.1f,"Missing IR silently uses a previous response");
        processor.effects.loadImpulseResponse(0,juce::File());juce::Thread::sleep(30);processor.effects.beginBlock();
        // Unsupported future engines retain identifiers and controls, and cannot play another engine even if On is selected.
        auto unknown=tdEffects::captureState(processor.apvts);unknown.setProperty("rack1_engineId","future-reverb",nullptr);unknown.setProperty("rack1_originalEffectIndex",123,nullptr);juce::ValueTree unsupported("PARAM");unsupported.setProperty("id","rack1_future_decay",nullptr);unsupported.setProperty("value",17,nullptr);unknown.addChild(unsupported,-1,nullptr);
        tdEffects::restoreState(processor.apvts,unknown);set(processor,"rack1_on",1);processor.effects.beginBlock();block.clear();block.setSample(0,0,.123f);processor.effects.process(block,0,257);require(block.getSample(0,0)==.123f,"Unavailable engine silently substituted");
        const auto roundTrip=tdEffects::captureState(processor.apvts);require(roundTrip.getProperty("rack1_engineId").toString()=="future-reverb" && (int)roundTrip.getProperty("rack1_originalEffectIndex")==123 && roundTrip.getChildWithProperty("id","rack1_future_decay").isValid(),"Missing-engine state lost");
        tdEffects::applyPreset(processor.apvts,1);require(processor.apvts.getRawParameterValue("rack1_unavailable")->load()==0 && !processor.apvts.state.hasProperty("rack1_missingEngine") && !processor.apvts.state.hasProperty("rack1_originalEffectIndex"),"Factory rack leaves a replaced engine unavailable");
        set(processor,"masterOutput",-6);set(processor,"revOutput",-3);set(processor,"revWetLevel",2);const auto trimmed=tdEffects::captureState(processor.apvts);
        set(processor,"masterOutput",0);set(processor,"revOutput",0);set(processor,"revWetLevel",0);tdEffects::restoreState(processor.apvts,trimmed);
        require(processor.apvts.getRawParameterValue("masterOutput")->load()==-6 && processor.apvts.getRawParameterValue("revOutput")->load()==-3 && processor.apvts.getRawParameterValue("revWetLevel")->load()==2,"Saved rack loses final or legacy trims");
        auto oldTrimmed=trimmed.createCopy();for(const char* id:{"masterOutput","revOutput","revWetLevel"})oldTrimmed.removeChild(oldTrimmed.getChildWithProperty("id",id),nullptr);tdEffects::restoreState(processor.apvts,oldTrimmed);
        require(processor.apvts.getRawParameterValue("masterOutput")->load()==0 && processor.apvts.getRawParameterValue("revOutput")->load()==0 && processor.apvts.getRawParameterValue("revWetLevel")->load()==0,"Old rack retains prior RC-only trims");

        // Rack state recall must never move the source-history read head.
        tdEffects::applyPreset(processor.apvts,4);auto state=tdEffects::captureState(processor.apvts);
        tdEffects::applyPreset(processor.apvts,0);tdEffects::restoreState(processor.apvts,state);
        require(processor.apvts.getRawParameterValue("rack1_on")->load()>0.5f,"Rack preset loses enable state");
        require(processor.apvts.getRawParameterValue("rack2_on")->load()>0.5f,"Rack preset loses second slot");
        juce::MemoryBlock saved;processor.getStateInformation(saved);set(processor,"rack1_mix",0);processor.setStateInformation(saved.getData(),(int)saved.getSize());
        require(processor.apvts.getRawParameterValue("rack1_mix")->load()==35,"Plugin state loses effect controls");
        auto legacy=processor.apvts.copyState();
        for(int i=legacy.getNumChildren()-1;i>=0;--i)if(legacy.getChild(i).getProperty("id").toString().startsWith("rack"))legacy.removeChild(i,nullptr);
        juce::MemoryBlock old;juce::MemoryOutputStream output(old,false);legacy.writeToStream(output);
        processor.setStateInformation(old.getData(),(int)old.getSize());
        require(processor.apvts.getRawParameterValue("rack1_on")->load()==0,"Legacy preset retains a new rack effect");
        // Reverbs cannot leak into a recovery pause, including partial blocks.
        set(processor,"semitones",-12);tdEffects::applyPreset(processor.apvts,7);processor.prepareToPlay(48000,257);
        int silent=0;bool reached=false,finished=false;
        for(int offset=0;offset<8*48000 && !finished;offset+=257)
        {
            for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)block.setSample(ch,i,0.2f);
            const auto writtenBefore=offset;processor.processBlock(block,midi);
            for(int i=0;i<257;++i)
            {
                const int t=writtenBefore+i;
                // At half speed and 2s history, recovery begins at this exact frame.
                const bool pause=t>=167992 && t<311992;
                if(pause){reached=true;++silent;require(block.getSample(0,i)==0 && block.getSample(1,i)==0,"Rack fills the three-second recovery pause");}
                if(reached && t>=311992){finished=true;break;}
            }
        }
        require(reached && silent==144000,"Recovery pause sample count changed with rack effects");
        // Old saved Spring/PSX states retain their controls and order but also
        // receive sanitized controls, latched dry fallback and scoped recovery.
        {
            TDSpeedVSTAudioProcessor legacyProcessor(2);legacyProcessor.prepareToPlay(48000,257);set(legacyProcessor,"revOn",1);
            for(const char* id:{"revMix","revTime","revTone","revMode"})legacyProcessor.apvts.getRawParameterValue(id)->store(std::numeric_limits<float>::quiet_NaN());
            for(int k=0;k<60;++k){for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)block.setSample(ch,i,.1f);legacyProcessor.processBlock(block,midi);for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)require(std::isfinite(block.getSample(ch,i)),"Legacy reverb accepts invalid controls");}
            require(!legacyProcessor.legacyEffectFaulted(),"Sanitized legacy controls fault");
            for(int k=0;k<60;++k){for(int ch=0;ch<2;++ch)for(int i=0;i<257;++i)block.setSample(ch,i,100);legacyProcessor.processBlock(block,midi);}
            require(legacyProcessor.legacyEffectFaulted(),"Legacy reverb failure does not latch dry bypass");
            legacyProcessor.requestBacklogReset();block.clear();legacyProcessor.processBlock(block,midi);require(legacyProcessor.legacyEffectFaulted(),"Song reset clears legacy effect fault");
            const float before=legacyProcessor.getBacklogSeconds();set(legacyProcessor,"revOn",0);legacyProcessor.resetLegacyEffect();block.clear();legacyProcessor.processBlock(block,midi);
            require(!legacyProcessor.legacyEffectFaulted() && legacyProcessor.getBacklogSeconds()==before,"Legacy effect reset changes song position");
        }
        // Allocation checks include the processor, final gain/meter/protection,
        // legacy reverb, and dry forwarding, not just the rack wrappers.
        {
            TDSpeedVSTAudioProcessor checked(2);checked.prepareToPlay(48000,257);set(checked,"revOn",1);
            for(bool bypass:{false,true}){set(checked,"bypass",bypass?1:0);for(int k=0;k<80;++k){for(int c=0;c<2;++c)for(int i=0;i<257;++i)block.setSample(c,i,.02f*std::sin(i*.03f));watchAllocations=true;checked.processBlock(block,midi);watchAllocations=false;}}
            require(audioAllocations==0,"Shared processor allocates in processing/dry forwarding: "+juce::String((int)audioAllocations));
        }
        // Picker variants modify one slot only; replacement retains bypass,
        // whereas inserting into an unassigned slot enables it.
        set(processor,"rack2_on",0);processor.apvts.state.setProperty("rack2_assigned",false,nullptr);
        const float firstAmount=processor.apvts.getRawParameterValue("rack1_mix")->load();
        tdEffects::chooseEffect(processor.apvts,1,0,2);
        require(processor.apvts.getRawParameterValue("rack2_on")->load()>0.5f && processor.apvts.getRawParameterValue("rack2_hall_width")->load()==1.4f,"Wide hall insertion failed");
        require(processor.apvts.getRawParameterValue("rack1_mix")->load()==firstAmount,"Hall variant edits another slot");
        set(processor,"rack2_on",0);tdEffects::chooseEffect(processor.apvts,1,0,1);require(processor.apvts.getRawParameterValue("rack2_on")->load()==0,"Replacing effect enables bypassed slot");
        auto editor=std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
        for(int i=0;i<editor->getNumChildComponents();++i)if(auto* button=dynamic_cast<juce::TextButton*>(editor->getChildComponent(i)))if(button->getButtonText().startsWith("Effects")){
            const auto before=tdEffects::captureState(processor.apvts).toXmlString();button->onClick();
            require(tdEffects::captureState(processor.apvts).toXmlString()==before,"Panel expansion changes effect parameters");
        }
        auto* locked=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());require(!locked->isResizable(),"Editor remains resizable");
        for(float scale:{1.0f,.9f,.8f,.7f}){
            locked->setUiScale(scale);require(locked->effectsFit(),"Effects do not fit locked layout");
            std::function<void(juce::Component&)> check=[&](juce::Component& parent){for(int i=0;i<parent.getNumChildComponents();++i){auto* child=parent.getChildComponent(i);if(child->isVisible()){require(parent.getLocalBounds().toFloat().expanded(1).contains(child->getBoundsInParent().toFloat()),"Control clipped: "+child->getName());check(*child);}}};check(*editor);
            if(argc>1){juce::FileOutputStream stream(juce::File(argv[1]).getChildFile("Effects-"+juce::String((int)(scale*100))+".png"));stream.setPosition(0);stream.truncate();juce::PNGImageFormat png;require(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),stream),"Effects preview write failed");}
        }
        // Save/reload uses an isolated path, leaving personal favourites intact.
        const auto savedFile=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("TDSpeed-rack-save",".xml");
        {
            EffectsPanel savePanel(processor,savedFile);savePanel.setSize(756,savePanel.preferredHeight());
            set(processor,"rack1_mix",37);require(savePanel.saveNamedRack("Regression rack"),"Named rack save fails");
            auto xml=juce::XmlDocument::parse(savedFile);require(xml!=nullptr,"Saved rack file missing");auto tree=juce::ValueTree::fromXml(*xml);require(tree.getNumChildren()==1,"Saved rack count wrong");
            set(processor,"rack1_mix",43);require(savePanel.saveNamedRack("Regression rack"),"Rack overwrite fails");
            xml=juce::XmlDocument::parse(savedFile);tree=juce::ValueTree::fromXml(*xml);require(tree.getNumChildren()==1,"Same-name save creates duplicate rack");
            require(!savePanel.saveNamedRack("   "),"Empty name is saved");
            EffectsPanel reopened(processor,savedFile);set(processor,"rack1_mix",0);
            for(int i=0;i<reopened.getNumChildComponents();++i)if(auto* combo=dynamic_cast<juce::ComboBox*>(reopened.getChildComponent(i)))combo->setSelectedId(100,juce::sendNotificationSync);
            require(processor.apvts.getRawParameterValue("rack1_mix")->load()==43,"Saved rack does not reload after reopening");
            set(processor,"rack1_mix",5);for(int i=0;i<reopened.getNumChildComponents();++i)if(auto* button=dynamic_cast<juce::TextButton*>(reopened.getChildComponent(i)))if(button->getButtonText()=="Load")button->onClick();
            require(processor.apvts.getRawParameterValue("rack1_mix")->load()==43,"Load cannot recall an already selected rack");
            for(int i=0;i<savePanel.getNumChildComponents();++i)if(auto* button=dynamic_cast<juce::TextButton*>(savePanel.getChildComponent(i)))
            {require(!button->getButtonText().contains("A / B"),"A/B remains visible");if(button->getButtonText()=="Save rack")button->onClick();}
            juce::AlertWindow* dialog=nullptr;auto& desktop=juce::Desktop::getInstance();
            for(int i=0;i<desktop.getNumComponents();++i)if(auto* alert=dynamic_cast<juce::AlertWindow*>(desktop.getComponent(i)))if(alert->getName()=="Save rack")dialog=alert;
            require(dialog && dialog->isVisible() && dialog->isCurrentlyModal(),"Save rack dialog is invisible");
            dialog->getTextEditor("name")->setText("Dialog rack");dialog->exitModalState(1);
            juce::Timer::callAfterDelay(100,[]{juce::MessageManager::getInstance()->stopDispatchLoop();});juce::MessageManager::getInstance()->runDispatchLoop();
            xml=juce::XmlDocument::parse(savedFile);tree=juce::ValueTree::fromXml(*xml);require(tree.getNumChildren()==2 && tree.getChild(1).getProperty("name").toString()=="Dialog rack","Save dialog callback does not persist rack");
        }
        require(savedFile.deleteFile(),"Could not clean up isolated rack test file");
        const auto blocked=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("TDSpeed-blocked-racks",".tmp");require(blocked.create().wasOk(),"Blocked-path fixture failed");
        {EffectsPanel failurePanel(processor,blocked.getChildFile("Effects.xml"));require(!failurePanel.saveNamedRack("Failed rack"),"Save reports success for invalid folder");}
        require(blocked.deleteFile(),"Could not clean up isolated blocked-path file");
        std::cout<<"PASS all engines, dry identity, limits, no audio allocation, state migration, exact recovery, UI. Worst measured engine load: "<<worstLoad*100<<"% ("<<worstName<<")\n";
        return 0;
    }
    catch(const std::exception& e){watchAllocations=false;std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
