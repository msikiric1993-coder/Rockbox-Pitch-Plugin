#pragma once
#include "WasapiEngine.h"
#include <stdexcept>

namespace td
{
inline juce::var runStandaloneChecks()
{
    auto* report=new juce::DynamicObject();
    try
    {
        auto require=[](bool value,const char* text){if(!value)throw std::runtime_error(text);};
        CaptureQueue queue;
        float signal[16]{};for(int i=0;i<16;++i)signal[i]=(float)i/16;
        require(queue.push(signal,8,false) && queue.available()==8,"Capture queue push failed");
        require(queue.frames[3].l==signal[6] && queue.frames[3].r==signal[7],"Stereo capture mapping failed");
        queue.read.store(8);require(queue.available()==0,"Capture queue consumption failed");
        queue.clear();require(queue.push(nullptr,CaptureQueue::capacity,true),"Silent capture queue fill failed");
        require(!queue.push(signal,1,false),"Capture queue overwrites unread samples");
        queue.read.store(CaptureQueue::capacity-4);
        require(queue.push(signal,8,false),"Capture queue wrap failed");
        require(queue.available()==12,"Capture queue wrap count wrong");

        Endpoint render;render.id="render";render.name="Renamed cable";render.container="device1";render.instance="instance1";
        render.cableKind=1;render.state=DEVICE_STATE_ACTIVE;render.mixRate=48000;
        Endpoint capture=render;capture.capture=true;capture.id="capture";
        auto pairs=cables({render,capture},false);
        require(pairs.size()==1 && pairs[0].usable,"Metadata cable pairing failed");
        capture.container="different";require(cables({render,capture},false).empty(),"Unrelated cable endpoints paired");
        capture.container=render.container;capture.state=DEVICE_STATE_DISABLED;
        pairs=cables({render,capture},false);require(pairs.size()==1 && !pairs[0].usable,"Disabled cable treated as usable");
        capture.state=DEVICE_STATE_ACTIVE;capture.mixRate=44100;
        pairs=cables({render,capture},false);require(!pairs[0].usable,"Mismatched Hi-Fi formats treated as usable");

        Endpoint physical;physical.id="speakers";physical.state=DEVICE_STATE_ACTIVE;
        const std::vector<Endpoint> routingDevices{render,capture,physical};
        auto idle=idleRolesToRestore(routingDevices,"render","speakers","render","render");
        require(idle.size()==2,"Idle startup does not repair orphaned cable routing");
        idle=idleRolesToRestore(routingDevices,"render","speakers","other-device","render");
        require(idle.size()==1 && idle[0]==eMultimedia,"Idle repair overwrites a manual output choice");
        require(idleRolesToRestore(routingDevices,"render","missing","render","render").empty(),"Idle repair routes to a missing output");
        require(idleRolesToRestore(routingDevices,"render","render","render","render").empty(),"Idle repair permits a cable feedback destination");

        Endpoint second=physical;second.id="second";Endpoint third=physical;third.id="third";
        auto ordered=physicalCandidates({render,capture,third,second,physical},"missing","third");
        require(ordered.size()==3 && ordered[0].id=="third" && ordered[1].id=="second","Missing saved output does not fall back to physical default / stable ID order");
        ordered=physicalCandidates({render,capture,third,second,physical},"speakers","third");require(ordered[0].id=="speakers" && ordered[1].id=="third","Saved output priority lost");
        ordered=physicalCandidates({render,capture,third,second,physical},"render","capture");require(ordered.size()==3 && ordered[0].id=="second","Virtual endpoint accepted as physical output");
        // Same processor/editor core is linked directly into the standalone.
        TDSpeedVSTAudioProcessor processor(2.0);processor.prepareToPlay(48000,512);
        require(tdEffects::catalogue().size()==52,"Standalone effects catalogue differs from tested build");
        for(int slot=0;slot<3;++slot)require(processor.apvts.getRawParameterValue(tdEffects::slotId(slot,"on"))->load()==0,"New effect slots are enabled by default");
        juce::AudioBuffer<float> impulse(2,14000);impulse.clear();impulse.setSample(0,0,0.5f);impulse.setSample(1,0,-0.25f);
        juce::MidiBuffer midi;processor.processBlock(impulse,midi);
        require(impulse.getSample(0,12000)==0.5f && impulse.getSample(1,12000)==-0.25f,"Standalone DSP differs from VST core");
        report->setProperty("passed",true);report->setProperty("checks","capture queue/stereo/wrap/overflow, cable identity/disabled/rate mismatch, idle-startup routing/ownership, standalone DSP impulse, shared 52-effect catalogue/default bypass");
    }
    catch(const std::exception& e){report->setProperty("passed",false);report->setProperty("error",juce::String(e.what()));}
    return juce::var(report);
}
}
