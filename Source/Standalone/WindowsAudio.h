#pragma once
#include <JuceHeader.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <vector>

namespace td
{
using Microsoft::WRL::ComPtr;
struct ComScope { HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED); ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); } };
struct RoutingLock
{
    HANDLE mutex=nullptr;
    bool owned=false;
    RoutingLock()
    {
        const auto name="Local\\TDSpeed-Routing-"+juce::SystemStats::getLogonName();
        mutex=CreateMutexW(nullptr,FALSE,name.toWideCharPointer());
        if(mutex){const auto result=WaitForSingleObject(mutex,10000);owned=result==WAIT_OBJECT_0 || result==WAIT_ABANDONED;}
    }
    ~RoutingLock(){if(owned)ReleaseMutex(mutex);if(mutex)CloseHandle(mutex);}
};
struct Endpoint
{
    juce::String id, name, interfaceName, instance, container;
    DWORD state = 0;
    bool capture = false;
    int cableKind = 0; // 1 Hi-Fi, 2 standard, 3 other virtual
    UINT mixRate = 0, mixChannels = 0;
};
struct Cable { Endpoint playback, capture; bool usable = false; juce::String problem; };
// Saved preference, then normal Windows default, then stable endpoint-ID order.
inline std::vector<Endpoint> physicalCandidates(const std::vector<Endpoint>& all,const juce::String& saved,const juce::String& current)
{
    std::vector<Endpoint> result;
    for(const auto& e:all)if(!e.capture && e.state==DEVICE_STATE_ACTIVE && e.cableKind==0)result.push_back(e);
    std::sort(result.begin(),result.end(),[&](const Endpoint& a,const Endpoint& b){
        const int pa=a.id==saved?0:a.id==current?1:2,pb=b.id==saved?0:b.id==current?1:2;
        return pa==pb?a.id<b.id:pa<pb;
    });return result;
}
std::vector<Endpoint> endpoints();
std::vector<Cable> cables(const std::vector<Endpoint>&, bool probe);
juce::String defaultOutput(ERole role);
bool activePhysical(const juce::String& id);
std::vector<ERole> idleRolesToRestore(const std::vector<Endpoint>& devices,const juce::String& cableId,
    const juce::String& physicalId,const juce::String& consoleId,const juce::String& multimediaId);
juce::String restoreIdleOutput(const juce::String& cableId,const juce::String& physicalId);
juce::String setDefaultOutput(const juce::String& id, ERole role);
int cableInstallationState(); // 0 absent, 1 usable, 2 installed but unavailable
juce::File dataFolder();
void recoverRouting(const juce::File& journal,const juce::String& expectedSession = {});
bool launchHidden(const juce::StringArray& args);
}
