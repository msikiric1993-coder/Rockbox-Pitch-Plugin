#include "WindowsAudio.h"
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <setupapi.h>

namespace td
{
static juce::String property(IPropertyStore* store, const PROPERTYKEY& key)
{
    PROPVARIANT v; PropVariantInit(&v);
    juce::String result;
    if (SUCCEEDED(store->GetValue(key, &v)))
    {
        if (v.vt == VT_LPWSTR) result = v.pwszVal;
        else if (v.vt == VT_CLSID && v.puuid) { wchar_t text[64]{}; StringFromGUID2(*v.puuid,text,64); result=text; }
    }
    PropVariantClear(&v); return result;
}
static int classify(const Endpoint& e)
{
    const auto text = (e.name+" "+e.interfaceName+" "+e.instance).toLowerCase();
    if (text.contains("hi-fi cable") || text.contains("hifi cable") || text.contains("vbaudiohf")) return 1;
    if ((text.contains("vb-audio") && text.contains("cable")) || text.contains("vb-audio virtual cable") || text.contains("vbaudiovac") || text.contains("cable input") || text.contains("cable output")) return 2;
    if (text.contains("virtual") || text.contains("voicemeeter") || text.contains("broadcast") || text.contains("droidcam")) return 3;
    return 0;
}
std::vector<Endpoint> endpoints()
{
    ComScope scope; std::vector<Endpoint> found;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en)))) return found;
    for (auto flow : {eRender,eCapture})
    {
        ComPtr<IMMDeviceCollection> all;
        if (FAILED(en->EnumAudioEndpoints(flow,DEVICE_STATEMASK_ALL,&all))) continue;
        UINT n=0; all->GetCount(&n);
        for(UINT i=0;i<n;++i)
        {
            ComPtr<IMMDevice> dev; ComPtr<IPropertyStore> props;
            if(FAILED(all->Item(i,&dev)) || FAILED(dev->OpenPropertyStore(STGM_READ,&props))) continue;
            Endpoint e; LPWSTR id=nullptr; dev->GetId(&id); e.id=id; CoTaskMemFree(id);
            dev->GetState(&e.state); e.capture=flow==eCapture;
            e.name=property(props.Get(),PKEY_Device_FriendlyName);
            e.interfaceName=property(props.Get(),PKEY_DeviceInterface_FriendlyName);
            e.instance=property(props.Get(),PKEY_Device_InstanceId);
            e.container=property(props.Get(),PKEY_Device_ContainerId);
            e.cableKind=classify(e);
            if(e.state==DEVICE_STATE_ACTIVE)
            {
                ComPtr<IAudioClient> client; WAVEFORMATEX* format=nullptr;
                if(SUCCEEDED(dev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client)) && SUCCEEDED(client->GetMixFormat(&format)))
                { e.mixRate=format->nSamplesPerSec; e.mixChannels=format->nChannels; CoTaskMemFree(format); }
            }
            found.push_back(e);
        }
    }
    return found;
}
static juce::String probeEndpoint(const Endpoint& e)
{
    ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev; ComPtr<IAudioClient> client;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en));
    if(SUCCEEDED(hr)) hr=en->GetDevice(e.id.toWideCharPointer(),&dev);
    if(SUCCEEDED(hr)) hr=dev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client);
    WAVEFORMATEX f{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
    if(SUCCEEDED(hr)) hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,1000000,0,&f,nullptr);
    return FAILED(hr) ? "Cannot open " + e.name + " (0x"+juce::String::toHexString((int)hr)+")" : juce::String();
}
std::vector<Cable> cables(const std::vector<Endpoint>& all,bool probe)
{
    ComScope scope; std::vector<Cable> found;
    for(const auto& p:all) if(!p.capture && (p.cableKind==1 || p.cableKind==2) && p.mixChannels<=2 && !p.name.containsIgnoreCase("16ch"))
        for(const auto& c:all) if(c.capture && c.cableKind==p.cableKind && c.mixChannels<=2 && !c.name.containsIgnoreCase("16ch"))
        {
            // Same device metadata is authoritative; family fallback handles
            // older VB drivers that do not expose a container/instance identity.
            if(p.container.isNotEmpty() && c.container.isNotEmpty() && p.container!=c.container) continue;
            if(p.instance.isNotEmpty() && c.instance.isNotEmpty() && p.instance!=c.instance) continue;
            Cable pair{p,c};
            if(p.state!=DEVICE_STATE_ACTIVE || c.state!=DEVICE_STATE_ACTIVE)
                pair.problem="Cable installed but disabled or disconnected";
            else if(p.cableKind==1 && p.mixRate!=c.mixRate)
                pair.problem="Hi-Fi Cable ends use different sample rates; select standard VB-CABLE or match the cable formats";
            else if(probe)
            { pair.problem=probeEndpoint(p); if(pair.problem.isEmpty()) pair.problem=probeEndpoint(c); }
            pair.usable=pair.problem.isEmpty(); found.push_back(pair);
        }
    std::stable_sort(found.begin(),found.end(),[](const Cable& a,const Cable& b)
    { if(a.usable!=b.usable) return a.usable>b.usable; return a.playback.cableKind<b.playback.cableKind; });
    return found;
}
juce::String defaultOutput(ERole role)
{
    ComScope scope; ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev; LPWSTR id=nullptr;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en))) || FAILED(en->GetDefaultAudioEndpoint(eRender,role,&dev))) return {};
    dev->GetId(&id); juce::String value(id); CoTaskMemFree(id); return value;
}
bool activePhysical(const juce::String& id)
{
    for(const auto& e:endpoints()) if(e.id==id) return !e.capture && e.cableKind==0 && e.state==DEVICE_STATE_ACTIVE;
    return false;
}
std::vector<ERole> idleRolesToRestore(const std::vector<Endpoint>& devices,const juce::String& cableId,
    const juce::String& physicalId,const juce::String& consoleId,const juce::String& multimediaId)
{
    bool physical=false,cable=false;
    for(const auto& e:devices)
    {
        if(e.id==physicalId && !e.capture && e.cableKind==0 && e.state==DEVICE_STATE_ACTIVE)physical=true;
        if(e.id==cableId && !e.capture && (e.cableKind==1 || e.cableKind==2))cable=true;
    }
    std::vector<ERole> roles;
    if(!physical || !cable)return roles;
    if(consoleId==cableId)roles.push_back(eConsole);
    if(multimediaId==cableId)roles.push_back(eMultimedia);
    return roles;
}
juce::String restoreIdleOutput(const juce::String& cableId,const juce::String& physicalId)
{
    RoutingLock lock;if(!lock.owned)return "Audio routing is busy. Retry shortly.";
    for(const auto role:idleRolesToRestore(endpoints(),cableId,physicalId,defaultOutput(eConsole),defaultOutput(eMultimedia)))
    {
        const auto error=setDefaultOutput(physicalId,role);if(error.isNotEmpty())return error;
    }
    return {};
}
// Windows desktop policy COM interface. Keep this undocumented compatibility
// surface isolated; a failure is reported and must never strand routing.
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) PolicyConfig : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR,INT,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR,INT,PINT64,PINT64)=0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR,PINT64)=0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR,ERole)=0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR,INT)=0;
};
juce::String setDefaultOutput(const juce::String& id,ERole role)
{
    ComScope scope; ComPtr<PolicyConfig> policy;
    const CLSID clsid{0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
    auto hr=CoCreateInstance(clsid,nullptr,CLSCTX_ALL,__uuidof(PolicyConfig),(void**)policy.GetAddressOf());
    if(SUCCEEDED(hr)) hr=policy->SetDefaultEndpoint(id.toWideCharPointer(),role);
    if(FAILED(hr)) return "Windows routing failed (0x"+juce::String::toHexString((int)hr)+")";
    return defaultOutput(role)==id ? juce::String() : "Windows did not confirm the output change";
}
int cableInstallationState()
{
    const auto all=endpoints(); const auto pairs=cables(all,true);
    for(const auto& p:pairs) if(p.usable) return 1;
    for(const auto& e:all) if(e.cableKind==1 || e.cableKind==2) return 2;
    // A partially installed driver may expose no audio endpoints at all.
    // Inspect registered hardware as well, so setup does not reinstall over it.
    HDEVINFO devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,DIGCF_ALLCLASSES);
    if(devices!=INVALID_HANDLE_VALUE)
    {
        SP_DEVINFO_DATA item{};item.cbSize=sizeof(item);
        for(DWORD i=0;SetupDiEnumDeviceInfo(devices,i,&item);++i)
        {
            wchar_t name[1024]{};
            if(SetupDiGetDeviceRegistryPropertyW(devices,&item,SPDRP_DEVICEDESC,nullptr,(BYTE*)name,sizeof(name),nullptr))
            {
                const juce::String description(name);
                if(description.containsIgnoreCase("VB-Audio") && description.containsIgnoreCase("Cable"))
                {SetupDiDestroyDeviceInfoList(devices);return 2;}
            }
        }
        SetupDiDestroyDeviceInfoList(devices);
    }
    return 0;
}
juce::File dataFolder()
{
    auto folder=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TDSpeed Preview");
    folder.createDirectory(); return folder;
}
void recoverRouting(const juce::File& journal,const juce::String& expectedSession)
{
    RoutingLock lock;if(!lock.owned)return;
    auto state=juce::JSON::parse(journal);
    if(!state.isObject() || (expectedSession.isNotEmpty() && state["session"].toString()!=expectedSession)) return;
    const juce::String cable=state["cable"].toString();
    bool restored=true;
    for(auto role:{eConsole,eMultimedia})
    {
        if(defaultOutput(role)!=cable) continue; // Respect manual changes.
        auto previous=state[role==eConsole?"console":"multimedia"].toString();
        if(!activePhysical(previous)) previous=state["physical"].toString();
        if(!activePhysical(previous))
            for(const auto& e:endpoints()) if(!e.capture && e.cableKind==0 && e.state==DEVICE_STATE_ACTIVE) {previous=e.id;break;}
        if(!activePhysical(previous) || setDefaultOutput(previous,role).isNotEmpty()) restored=false;
    }
    if(restored) journal.deleteFile();
}
bool launchHidden(const juce::StringArray& args)
{
    juce::String command;
    for(const auto& a:args) command += "\""+a.replace("\"","")+"\" ";
    STARTUPINFOW info{}; info.cb=sizeof(info); info.dwFlags=STARTF_USESHOWWINDOW; info.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{}; std::wstring writable(command.trimEnd().toWideCharPointer());
    const auto ok=CreateProcessW(nullptr,writable.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&process);
    if(ok){CloseHandle(process.hThread);CloseHandle(process.hProcess);} return ok!=FALSE;
}
}
