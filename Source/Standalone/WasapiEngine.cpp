#include "WasapiEngine.h"
#include <avrt.h>

namespace td
{
static HRESULT openClient(const juce::String& id,ComPtr<IAudioClient>& client,HANDLE event)
{
    ComPtr<IMMDeviceEnumerator> en;ComPtr<IMMDevice> dev;
    auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en));
    if(SUCCEEDED(hr)) hr=en->GetDevice(id.toWideCharPointer(),&dev);
    if(SUCCEEDED(hr)) hr=dev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client);
    WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
    if(SUCCEEDED(hr)) hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
        1000000,0,&format,nullptr);
    if(SUCCEEDED(hr)) hr=client->SetEventHandle(event);
    return hr;
}
struct AudioPriority
{
    DWORD task=0;HANDLE handle=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
    ~AudioPriority(){if(handle)AvRevertMmThreadCharacteristics(handle);}
};
juce::String WasapiEngine::start(const juce::String& capture,const juce::String& output)
{
    stop();
    const auto leaseName="Local\\TDSpeed-"+juce::SystemStats::getLogonName();
    forwardingLease=CreateMutexW(nullptr,FALSE,leaseName.toWideCharPointer());
    if(!forwardingLease)return "Cannot reserve the audio bridge";
    if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(forwardingLease);forwardingLease=nullptr;return "Another TDSpeed is forwarding audio. Exit it from its tray menu first.";}
    queue.clear();quitting=false;failed=false;captureReady=0;renderReady=0;captureError=S_OK;renderError=S_OK;
    underruns=0;overruns=0;clockRatio=1;primed=false;leftInterpolator.reset();rightInterpolator.reset();
    block.setSize(2,512);midi.clear();
    try {processor.prepareToPlay(48000,512);} catch(const std::bad_alloc&) {stop();return "Not enough memory for the 2 GiB history. Close other applications and retry.";}
    captureThread=std::thread([this,capture]{captureLoop(capture);});
    renderThread=std::thread([this,output]{renderLoop(output);});
    for(int i=0;i<250 && (captureReady.load()==0 || renderReady.load()==0);++i) Sleep(10);
    if(captureReady.load()!=1 || renderReady.load()!=1 || failed.load())
    {
        const auto captureFailure=captureError.load(),renderFailure=renderError.load();
        stop();
        return "Audio stream unavailable / capture 0x"+juce::String::toHexString((int)captureFailure)
            +" / output 0x"+juce::String::toHexString((int)renderFailure)
            +". Close VSTHost or exclusive-mode players and retry.";
    }
    ++startCount;live=true;return {};
}
void WasapiEngine::stop()
{
    quitting=true;live=false;
    if(captureThread.joinable())captureThread.join();
    if(renderThread.joinable())renderThread.join();
    if(forwardingLease){CloseHandle(forwardingLease);forwardingLease=nullptr;}
}
void WasapiEngine::captureLoop(juce::String id)
{
    ComScope scope;AudioPriority priority;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
    auto hr=openClient(id,client,event);
    if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&capture));
    if(SUCCEEDED(hr))hr=client->Start();
    if(FAILED(hr)){captureError=hr;captureReady=-1;failed=true;CloseHandle(event);return;}
    captureReady=1;
    while(!quitting.load())
    {
        WaitForSingleObject(event,50);
        UINT32 count=0;hr=capture->GetNextPacketSize(&count);
        while(SUCCEEDED(hr) && count>0)
        {
            BYTE* bytes=nullptr;DWORD flags=0;UINT32 n=0;
            hr=capture->GetBuffer(&bytes,&n,&flags,nullptr,nullptr);if(FAILED(hr))break;
            const bool silent=(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0;
            float peak=0;
            if(!silent && bytes) for(UINT32 i=0;i<n*2;++i)peak=juce::jmax(peak,std::abs(((float*)bytes)[i]));
            inputPeak=peak;
            if(!queue.push((const float*)bytes,n,silent)){++overruns;failed=true;}
            capture->ReleaseBuffer(n);
            hr=capture->GetNextPacketSize(&count);
        }
        if(FAILED(hr)){failed=true;break;}
    }
    client->Stop();CloseHandle(event);
}
void WasapiEngine::processQuantum()
{
    const unsigned available=queue.available();
    if(!primed && available>=4800)primed=true;
    if(!primed){block.clear();return;}
    // Independent device-clock correction is bounded to +/- 0.1%, completely
    // separate from the processor's intentionally accumulating song history.
    const double wanted=juce::jlimit(0.999,1.001,1.0+((double)available-4800.0)*0.0000002);
    clockRatio+=0.01*(wanted-clockRatio);
    const unsigned needed=(unsigned)std::ceil(512*clockRatio)+8;
    if(available<needed)
    {++underruns;block.clear();primed=false;leftInterpolator.reset();rightInterpolator.reset();return;}
    const auto r=queue.read.load(std::memory_order_relaxed);
    for(unsigned i=0;i<needed;++i){const auto f=queue.frames[(r+i)%CaptureQueue::capacity];inputLeft[i]=f.l;inputRight[i]=f.r;}
    const int consumed=leftInterpolator.process(clockRatio,inputLeft.data(),block.getWritePointer(0),512);
    const int consumedRight=rightInterpolator.process(clockRatio,inputRight.data(),block.getWritePointer(1),512);
    jassert(consumed==consumedRight);
    queue.read.store(r+consumed,std::memory_order_release);
    processor.processBlock(block,midi);
}
void WasapiEngine::renderLoop(juce::String id)
{
    ComScope scope;AudioPriority priority;
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ComPtr<IAudioClient> client;ComPtr<IAudioRenderClient> render;
    auto hr=openClient(id,client,event);UINT32 size=0;
    if(SUCCEEDED(hr))hr=client->GetBufferSize(&size);
    if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&render));
    BYTE* initial=nullptr;
    if(SUCCEEDED(hr))hr=render->GetBuffer(size,&initial);
    if(SUCCEEDED(hr))hr=render->ReleaseBuffer(size,AUDCLNT_BUFFERFLAGS_SILENT);
    if(SUCCEEDED(hr))hr=client->Start();
    if(FAILED(hr)){renderError=hr;renderReady=-1;failed=true;CloseHandle(event);return;}
    renderReady=1;int position=512;
    while(!quitting.load())
    {
        WaitForSingleObject(event,50);
        UINT32 padding=0;hr=client->GetCurrentPadding(&padding);if(FAILED(hr))break;
        const UINT32 n=size-padding;if(n==0)continue;
        BYTE* bytes=nullptr;hr=render->GetBuffer(n,&bytes);if(FAILED(hr))break;
        auto* output=(float*)bytes;float peak=0;
        for(UINT32 i=0;i<n;++i)
        {
            if(position==512){processQuantum();position=0;}
            const float l=block.getSample(0,position),r=block.getSample(1,position);++position;
            output[2*i]=l;output[2*i+1]=r;peak=juce::jmax(peak,std::abs(l),std::abs(r));
        }
        outputPeak=peak;hr=render->ReleaseBuffer(n,0);if(FAILED(hr))break;
    }
    if(FAILED(hr))failed=true;
    client->Stop();CloseHandle(event);
}
juce::var signalCheck(const juce::String& captureId,const juce::String& cablePlaybackId,const juce::String& outputId,bool dry)
{
    ComScope scope;TDSpeedVSTAudioProcessor processor(12.0);WasapiEngine engine(processor);
    auto* semi=processor.apvts.getParameter("semitones");semi->setValueNotifyingHost(semi->convertTo0to1(-12.0f));
    tdEffects::setParameter(processor.apvts,"bypass",dry?1.0f:0.0f);
    auto* report=new juce::DynamicObject();auto error=engine.start(captureId,outputId);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ComPtr<IAudioClient> loop;ComPtr<IMMDeviceEnumerator> en;ComPtr<IMMDevice> dev;ComPtr<IAudioCaptureClient> monitor;
    auto hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&en));
    if(SUCCEEDED(hr))hr=en->GetDevice(outputId.toWideCharPointer(),&dev);
    if(SUCCEEDED(hr))hr=dev->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&loop);
    WAVEFORMATEX format{WAVE_FORMAT_IEEE_FLOAT,2,48000,48000*8,8,32,0};
    if(SUCCEEDED(hr))hr=loop->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,1000000,0,&format,nullptr);
    if(SUCCEEDED(hr))hr=loop->GetService(IID_PPV_ARGS(&monitor));
    if(SUCCEEDED(hr))hr=loop->Start();
    if(FAILED(hr))error="Output loopback unavailable: 0x"+juce::String::toHexString((int)hr);
    std::atomic<HRESULT> toneError{S_OK};std::thread tone;
    if(error.isEmpty())tone=std::thread([&]
    {
        ComScope threadScope;HANDLE toneEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        ComPtr<IAudioClient> client;ComPtr<IAudioRenderClient> renderer;
        auto result=openClient(cablePlaybackId,client,toneEvent);UINT32 size=0;
        if(SUCCEEDED(result))result=client->GetBufferSize(&size);
        if(SUCCEEDED(result))result=client->GetService(IID_PPV_ARGS(&renderer));
        if(SUCCEEDED(result))result=client->Start();
        unsigned phase=0;const auto finish=GetTickCount64()+7000;
        while(SUCCEEDED(result) && GetTickCount64()<finish)
        {
            WaitForSingleObject(toneEvent,50);UINT32 padding=0;result=client->GetCurrentPadding(&padding);
            if(FAILED(result))break;const UINT32 count=size-padding;if(count==0)continue;
            BYTE* bytes=nullptr;result=renderer->GetBuffer(count,&bytes);if(FAILED(result))break;
            for(UINT32 i=0;i<count;++i)
            {
                const float sample=0.01f*std::sin((float)(phase++*440.0*juce::MathConstants<double>::twoPi/48000.0));
                ((float*)bytes)[2*i]=((float*)bytes)[2*i+1]=sample;
            }
            result=renderer->ReleaseBuffer(count,0);
        }
        if(client)client->Stop();toneError=result;CloseHandle(toneEvent);
    });
    float previous=0,peak=0;unsigned crossings=0,samples=0;
    const auto begin=GetTickCount64(),finish=begin+7500;
    while(error.isEmpty() && GetTickCount64()<finish)
    {
        Sleep(5);UINT32 count=0;hr=monitor->GetNextPacketSize(&count);
        while(SUCCEEDED(hr) && count>0)
        {
            BYTE* bytes=nullptr;DWORD flags=0;UINT32 n=0;hr=monitor->GetBuffer(&bytes,&n,&flags,nullptr,nullptr);if(FAILED(hr))break;
            // Measure during the tone, excluding startup and silence after its end.
            if(GetTickCount64()-begin>3000 && GetTickCount64()-begin<6000 && !(flags&AUDCLNT_BUFFERFLAGS_SILENT))
                for(UINT32 i=0;i<n;++i)
                {
                    const float sample=((float*)bytes)[2*i];peak=juce::jmax(peak,std::abs(sample));
                    if(previous<0 && sample>=0)++crossings;previous=sample;++samples;
                }
            monitor->ReleaseBuffer(n);hr=monitor->GetNextPacketSize(&count);
        }
        if(FAILED(hr))error="Loopback capture failed";
    }
    if(tone.joinable())tone.join();if(loop)loop->Stop();engine.stop();CloseHandle(event);
    const double frequency=samples>0?crossings*48000.0/samples:0;
    report->setProperty("dryPassThrough",dry);report->setProperty("frequencyHz",frequency);report->setProperty("peak",peak);
    report->setProperty("underruns",(int)engine.getUnderruns());report->setProperty("overruns",(int)engine.getOverruns());
    report->setProperty("toneError",juce::String::toHexString((int)toneError.load()));report->setProperty("error",error);
    report->setProperty("passed",error.isEmpty() && SUCCEEDED(toneError.load()) && peak>0.001f && std::abs(frequency-(dry?440.0:220.0))<3.0 && engine.getUnderruns()==0 && engine.getOverruns()==0);
    return juce::var(report);
}

juce::var soakCheck(const juce::String& captureId,const juce::String& cableId,const juce::String& outputId,const juce::File& reportFile,int seconds)
{
    TDSpeedVSTAudioProcessor processor;tdEffects::setParameter(processor.apvts,"semitones",-3);tdEffects::applySpaceVariant(processor.apvts,0,0);
    tdEffects::chooseEffect(processor.apvts,1,51);tdEffects::setParameter(processor.apvts,"rack2_on",1);tdEffects::chooseEffect(processor.apvts,2,16);tdEffects::setParameter(processor.apvts,"rack3_on",1);
    WasapiEngine engine(processor);auto error=engine.start(captureId,outputId);std::atomic<bool> stop{false};std::atomic<HRESULT> sourceError{S_OK};std::thread source;
    if(error.isEmpty())source=std::thread([&]{ComScope scope;HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);ComPtr<IAudioClient> client;ComPtr<IAudioRenderClient> render;auto hr=openClient(cableId,client,event);UINT32 size=0;
        if(SUCCEEDED(hr))hr=client->GetBufferSize(&size);if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(&render));if(SUCCEEDED(hr))hr=client->Start();std::uint64_t phase=0;
        while(SUCCEEDED(hr) && !stop.load()){WaitForSingleObject(event,50);UINT32 padding=0;hr=client->GetCurrentPadding(&padding);if(FAILED(hr))break;const UINT32 count=size-padding;if(!count)continue;BYTE* bytes=nullptr;hr=render->GetBuffer(count,&bytes);if(FAILED(hr))break;
            for(UINT32 i=0;i<count;++i){const double t=(phase++)/48000.0;for(int c=0;c<2;++c)((float*)bytes)[2*i+c]=(float)(.001*std::sin(t*juce::MathConstants<double>::twoPi*(c?331:220))+.0003*std::sin(t*juce::MathConstants<double>::twoPi*659));}hr=render->ReleaseBuffer(count,0);
        }if(client)client->Stop();sourceError.store(hr);CloseHandle(event);
    });
    auto* report=new juce::DynamicObject();juce::var result(report);report->setProperty("requestedSeconds",seconds);report->setProperty("capture",captureId);report->setProperty("output",outputId);report->setProperty("pitchSemitones",-3);report->setProperty("effects","Signalsmith Ambient, MVerb, Stereo Delay");report->setProperty("startedUtc",juce::Time::getCurrentTime().toISO8601(true));
    float maximumPeak=0;bool effectFault=false;int elapsed=0;const auto begin=GetTickCount64();
    while(error.isEmpty() && elapsed<seconds){Sleep(1000);elapsed=(int)((GetTickCount64()-begin)/1000);maximumPeak=juce::jmax(maximumPeak,engine.getOutputPeak());for(int s=0;s<3;++s)effectFault|=processor.effects.faulted(s);error=engine.error();if(FAILED(sourceError.load()))error="Test source stream failed";
        report->setProperty("elapsedSeconds",elapsed);report->setProperty("complete",false);report->setProperty("underruns",(int)engine.getUnderruns());report->setProperty("overruns",(int)engine.getOverruns());report->setProperty("queueMs",engine.getQueueMs());report->setProperty("maximumPeak",maximumPeak);report->setProperty("backlogSeconds",processor.getBacklogSeconds());report->setProperty("effectFault",effectFault);reportFile.replaceWithText(juce::JSON::toString(result));
    }
    stop=true;if(source.joinable())source.join();engine.stop();report->setProperty("error",error);report->setProperty("complete",true);report->setProperty("passed",error.isEmpty() && elapsed>=seconds && maximumPeak>.00001f && !effectFault && engine.getUnderruns()==0 && engine.getOverruns()==0);reportFile.replaceWithText(juce::JSON::toString(result));return result;
}

}
