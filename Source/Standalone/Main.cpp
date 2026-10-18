#include "WindowsAudio.h"
#include "WasapiEngine.h"
#include "Checks.h"
#include <tlhelp32.h>
#include <BinaryData.h>
#include "../PluginEditor.h"
#include "../UiScale.h"
#include <iostream>
#include <fstream>

namespace td
{
static juce::String peakText(float value)
{
    const float peak=std::isfinite(value)?std::abs(value):32.0f;
    return juce::String(juce::jlimit(-120.0f,30.1f,juce::Decibels::gainToDecibels(peak)),1)+" dBFS"+(peak>=1?" CLIP":"");
}
static bool atomicJson(const juce::File& file,const juce::var& value)
{
    juce::TemporaryFile temp(file);
    return temp.getFile().replaceWithText(juce::JSON::toString(value)) && temp.overwriteTargetFileWithTemporary();
}
static juce::var describeDevices(bool probe)
{
    auto all=endpoints();juce::Array<juce::var> devices,pairs;
    for(const auto& e:all)
    {
        auto* d=new juce::DynamicObject();d->setProperty("id",e.id);d->setProperty("name",e.name);
        d->setProperty("interface",e.interfaceName);d->setProperty("instance",e.instance);d->setProperty("container",e.container);
        d->setProperty("capture",e.capture);d->setProperty("state",(int)e.state);d->setProperty("cableKind",e.cableKind);d->setProperty("mixRate",(int)e.mixRate);
        devices.add(juce::var(d));
    }
    for(const auto& c:cables(all,probe))
    {
        auto* d=new juce::DynamicObject();d->setProperty("name",c.playback.name);d->setProperty("playback",c.playback.id);
        d->setProperty("capture",c.capture.id);d->setProperty("usable",c.usable);d->setProperty("problem",c.problem);pairs.add(juce::var(d));
    }
    auto* report=new juce::DynamicObject();report->setProperty("devices",devices);report->setProperty("cables",pairs);
    report->setProperty("console",defaultOutput(eConsole));report->setProperty("multimedia",defaultOutput(eMultimedia));
    return juce::var(report);
}
static bool oldHostActive()
{
    HANDLE processes=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool found=false;
    if(processes!=INVALID_HANDLE_VALUE)
    {
        if(Process32FirstW(processes,&entry))do {if(juce::String(entry.szExeFile).containsIgnoreCase("vsthost"))found=true;}while(Process32NextW(processes,&entry));
        CloseHandle(processes);
    }
    const auto name="Local\\TDSpeed-"+juce::SystemStats::getLogonName();HANDLE other=OpenMutexW(SYNCHRONIZE,FALSE,name.toWideCharPointer());if(other){found=true;CloseHandle(other);}return found;
}
class MainContent : public juce::Component,private juce::Timer
{
public:
    explicit MainContent(bool autoStart=true,juce::File location=dataFolder()):engine(processor),settingsFile(location.getChildFile("settings.json")),journal(location.getChildFile("routing.json"))
    {
        persist=autoStart;
        if(persist && location==dataFolder() && !location.getChildFile("import-v1.json").existsAsFile()){
            auto old=location.getSiblingFile("TDSpeed RC");if(!old.getChildFile("settings.json").existsAsFile())old=location.getSiblingFile("TDSpeed");bool importedOK=true;
            if(!settingsFile.existsAsFile() && old.getChildFile("settings.json").existsAsFile()){
                auto imported=juce::JSON::parse(old.getChildFile("settings.json"));
                if(imported.isObject()){imported.getDynamicObject()->setProperty("startup",false);if(!imported.getDynamicObject()->hasProperty("uiScale")){const int old=juce::jlimit(1,4,(int)imported["uiSize"]);imported.getDynamicObject()->setProperty("uiScale",25*juce::roundToInt((110-old*10)/25.0f));}imported.getDynamicObject()->setProperty("mixImportNotice",true);importedOK=atomicJson(settingsFile,imported);}else importedOK=false;
                importSettingsBlocked=!importedOK;
            }
            const auto racks=location.getChildFile("EffectsPresets.xml");if(!racks.existsAsFile() && old.getChildFile("EffectsPresets.xml").existsAsFile()){
                auto xml=juce::XmlDocument::parse(old.getChildFile("EffectsPresets.xml"));
                const bool valid=xml && juce::ValueTree::fromXml(*xml).hasType("FAVOURITES");
                importedOK=(valid && old.getChildFile("EffectsPresets.xml").copyFileTo(racks)) && importedOK;
            }
            if(importedOK)importedOK=atomicJson(location.getChildFile("import-v1.json"),juce::var("Imported copies only; original files retained. RC Mix is true dry/wet."));
            if(!importedOK)juce::MessageManager::callAsync([]{juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,"RC import incomplete","Could not read or copy existing settings or racks. Originals are unchanged. The import will be retried on the next launch. Check file permissions and the old settings files.");});
        }
        if(persist)recoverRouting(journal);
        settings=juce::JSON::parse(settingsFile);if(!settings.isObject())settings=juce::var(new juce::DynamicObject());
        if(tdEffects::mixImportExplained())settings.getDynamicObject()->setProperty("mixImportNotice",false);
        juce::MemoryBlock state;
        if(state.fromBase64Encoding(settings["effect"].toString()) && state.getSize()>0)processor.setStateInformation(state.getData(),(int)state.getSize());
        processor.setListeningMode();
        tdEffects::setParameter(processor.apvts,"bypass",1);
        editor.reset(processor.createEditor());addAndMakeVisible(*editor);
        auto* effectEditor=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());effectEditor->setStandaloneMode();
        effectEditor->sizeChanged=[this]{updateLayout();};
        for(juce::Component* c:std::initializer_list<juce::Component*>{&cableBox,&outputBox,&enableButton,&refreshButton,&startupButton,&status,&cableLabel,&outputLabel,&audioSettings,&details,&renderTools,&uiSize,&diagnostics,&importNotice,&dismissImport})addAndMakeVisible(c);
        setLookAndFeel(&theme);
        cableBox.setTextWhenNothingSelected("Choose virtual cable");outputBox.setTextWhenNothingSelected("Choose headphones or speakers");
        cableLabel.setText("AUDIO INPUT / VIRTUAL CABLE",juce::dontSendNotification);
        outputLabel.setText("LISTEN THROUGH",juce::dontSendNotification);
        diagnostics.setText("Transport idle / fixed 48 kHz stereo / no active audio streams",juce::dontSendNotification);
        status.setColour(juce::Label::textColourId,juce::Colour(0xff81dfb5));status.setFont(juce::Font(13));
        startupButton.setToggleState((bool)settings["startup"],juce::dontSendNotification);
        cableBox.onChange=[this]{reconnectPending=false;saveSettings();restoreIdleAudio();};outputBox.onChange=[this]{reconnectPending=false;temporaryOutput=false;saveSettings();restoreIdleAudio();};
        enableButton.onClick=[this]{if(enabled())disable();else enable();};
        refreshButton.onClick=[this]{refresh();};
        startupButton.onClick=[this]{configureStartup();saveSettings();};
        renderTools.setColour(juce::TextButton::buttonColourId,juce::Colour(0xffe9b66a));renderTools.setColour(juce::TextButton::textColourOffId,juce::Colour(0xff10171b));
        renderTools.onClick=[this]{auto* e=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());e->setFilePanelExpanded(!e->filePanelExpanded());};
        audioSettings.onClick=[this]{settingsOpen=!settingsOpen;updateLayout();};details.onClick=[this]{detailsOpen=!detailsOpen;updateLayout();};
        tdUi::populate(uiSize);
        const int size=tdUi::valid((int)settings["uiScale"]);uiSize.setSelectedId(size,juce::dontSendNotification);uiSize.onChange=[this]{saveSettings();if(sizeChanged)sizeChanged();};
        importNotice.setText("Imported Mix keeps its percentage; 100% is now fully wet. Startup remains dry.",juce::dontSendNotification);
        dismissImport.onClick=[this]{tdEffects::acknowledgeMixImport();settings.getDynamicObject()->setProperty("mixImportNotice",false);processor.apvts.state.setProperty("rcMixNotice",false,nullptr);saveSettings();updateLayout();};
        updateLayout();refresh();restoreIdleAudio();startTimer(500);
        if(autoStart)juce::MessageManager::callAsync([safe=juce::Component::SafePointer<MainContent>(this)]{if(safe && safe->outputBox.getSelectedId()>0)safe->connect();});
    }
    ~MainContent() override {stopTimer();stopAndRestore();saveSettings();setLookAndFeel(nullptr);editor.reset();}
    void paint(juce::Graphics& g) override {g.fillAll(juce::Colour(0xff10171b));}
    std::function<void()> sizeChanged;
    float requestedScale() const {return tdUi::requested(uiSize.getSelectedId());}
    int scaleChoice() const {return uiSize.getSelectedId();}
    void updateLayout()
    {
        for(auto* c:std::initializer_list<juce::Component*>{&cableBox,&outputBox,&refreshButton,&startupButton,&cableLabel,&outputLabel})c->setVisible(settingsOpen);
        renderTools.setButtonText(static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get())->filePanelExpanded()?"Song render v":"Song render >");
        diagnostics.setVisible(detailsOpen);details.setButtonText(detailsOpen?"Details v":"Details >");audioSettings.setButtonText(settingsOpen?"Audio settings v":"Audio settings >");
        const bool notice=(bool)settings["mixImportNotice"];importNotice.setVisible(notice);dismissImport.setVisible(notice);
        const int top=84+(settingsOpen?98:0)+(detailsOpen?40:0)+(notice?36:0);
        const int h=top+static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get())->preferredHeight();if(getHeight()!=h || getWidth()!=820){setSize(820,h);if(sizeChanged)sizeChanged();}resized();
    }
    void resized() override
    {
        auto r=getLocalBounds().reduced(24,12);auto row=r.removeFromTop(30);enableButton.setBounds(row.removeFromLeft(158));row.removeFromLeft(10);audioSettings.setBounds(row.removeFromLeft(150));row.removeFromLeft(10);details.setBounds(row.removeFromLeft(88));row.removeFromLeft(10);renderTools.setBounds(row.removeFromLeft(154));uiSize.setBounds(row.removeFromRight(90));
        status.setBounds(r.removeFromTop(30));r.removeFromTop(12);
        if(settingsOpen){row=r.removeFromTop(22);cableLabel.setBounds(row.removeFromLeft(340));outputLabel.setBounds(row);row=r.removeFromTop(32);cableBox.setBounds(row.removeFromLeft(340));row.removeFromLeft(12);outputBox.setBounds(row);r.removeFromTop(8);row=r.removeFromTop(30);refreshButton.setBounds(row.removeFromLeft(100));startupButton.setBounds(row);r.removeFromTop(6);}
        if(detailsOpen)diagnostics.setBounds(r.removeFromTop(40));
        if(importNotice.isVisible()){row=r.removeFromTop(36);dismissImport.setBounds(row.removeFromRight(54));importNotice.setBounds(row);}
        editor->setBounds(0,getHeight()-editor->getHeight(),820,editor->getHeight());
    }
    void enable()
    {
        if(!engine.running())connect();
        if(!engine.running())return;
        if(!everEnabled){processor.requestBacklogReset();everEnabled=true;}
        tdEffects::setParameter(processor.apvts,"bypass",0);enableButton.setButtonText("Disable processing");
        status.setText("Processing / "+outputBox.getText()+(temporaryOutput?" / temporary output":""),juce::dontSendNotification);
    }
    void disable()
    {
        tdEffects::setParameter(processor.apvts,"bypass",1);enableButton.setButtonText("Enable processing");
        status.setText("Dry pass-through / buffered playback held",juce::dontSendNotification);saveSettings();
    }
    void connect()
    {
        if(engine.running())return;
        tdEffects::setParameter(processor.apvts,"bypass",1);everEnabled=false;
        RoutingLock routingLock;if(!routingLock.owned){status.setText("Audio routing is busy. Retry Enable shortly.",juce::dontSendNotification);return;}
        if(oldHostActive()){status.setText("Close VSTHost first, then Enable processing here to avoid duplicate audio.",juce::dontSendNotification);return;}
        if(journal.existsAsFile()){recoverRouting(journal);if(journal.existsAsFile()){status.setText("Could not restore previous routing. Use Restore normal audio before enabling.",juce::dontSendNotification);return;}}
        const int ci=cableBox.getSelectedId()-1,oi=outputBox.getSelectedId()-1;
        if(ci<0 || ci>=(int)availableCables.size() || oi<0 || oi>=(int)outputs.size())
        {status.setText("Choose a usable virtual cable and your headphones or speakers.",juce::dontSendNotification);return;}
        const auto& cable=availableCables[(size_t)ci];const auto& physical=outputs[(size_t)oi];
        if(!cable.usable || !activePhysical(physical.id)){status.setText("Selected devices are unavailable. Refresh the device list.",juce::dontSendNotification);return;}
        auto error=engine.start(cable.capture.id,physical.id);
        if(error.isNotEmpty())
        {
            for(size_t candidate=0;candidate<outputs.size();++candidate)if((int)candidate!=oi && activePhysical(outputs[candidate].id))
            {
                error=engine.start(cable.capture.id,outputs[candidate].id);
                if(error.isEmpty()){outputBox.setSelectedId((int)candidate+1,juce::dontSendNotification);temporaryOutput=true;break;}
            }
        }
        const auto& destination=outputs[(size_t)(outputBox.getSelectedId()-1)];
        if(error.isNotEmpty()){restoreIdleAudio();status.setText(error,juce::dontSendNotification);return;}
        auto* record=new juce::DynamicObject();record->setProperty("cable",cable.playback.id);record->setProperty("physical",destination.id);
        record->setProperty("console",defaultOutput(eConsole));record->setProperty("multimedia",defaultOutput(eMultimedia));
        const auto session=juce::Uuid().toString();record->setProperty("session",session);record->setProperty("pid",(int)GetCurrentProcessId());
        if(!atomicJson(journal,juce::var(record)))error="Cannot save routing recovery information";
        if(error.isEmpty() && !launchHidden({juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),"--watchdog",juce::String((int)GetCurrentProcessId()),session,journal.getFullPathName()}))
            error="Could not start audio-routing watchdog";
        if(error.isEmpty())error=setDefaultOutput(cable.playback.id,eConsole);
        if(error.isEmpty())error=setDefaultOutput(cable.playback.id,eMultimedia);
        if(error.isNotEmpty()){recoverRouting(journal);engine.stop();status.setText(error,juce::dontSendNotification);return;}
        activeCable=cable.playback.id;enableButton.setButtonText("Enable processing");cableBox.setEnabled(false);outputBox.setEnabled(false);refreshButton.setEnabled(false);
        saveSettings();status.setText("Dry pass-through / "+destination.name+(temporaryOutput?" / temporary output":""),juce::dontSendNotification);
    }
    void stopAndRestore()
    {
        reconnectPending=false;
        if(persist)recoverRouting(journal);engine.stop();activeCable.clear();restoreIdleAudio();enableButton.setButtonText("Enable processing");
        cableBox.setEnabled(true);outputBox.setEnabled(true);refreshButton.setEnabled(true);
        status.setText(journal.existsAsFile()?"Routing restoration failed. Restore normal audio from the tray or Windows sound settings.":"Processing disabled / normal Windows audio restored",juce::dontSendNotification);
        saveSettings();
    }
    void refresh()
    {
        if(engine.running())return;
        const auto all=endpoints();availableCables=cables(all,true);outputs.clear();
        cableBox.clear(juce::dontSendNotification);outputBox.clear(juce::dontSendNotification);
        int cableId=0,outputId=0;
        for(size_t i=0;i<availableCables.size();++i)
        {
            const auto& c=availableCables[i];cableBox.addItem(c.playback.name+(c.usable?"":" / unavailable"),(int)i+1);
            cableBox.setItemEnabled((int)i+1,c.usable);
            if(c.usable && c.playback.id==settings["cable"].toString())cableId=(int)i+1;
        }
        if(cableId==0)for(size_t i=0;i<availableCables.size();++i)if(availableCables[i].usable){cableId=(int)i+1;break;}
        const auto current=defaultOutput(eMultimedia);
        auto sorted=physicalCandidates(all,settings["output"].toString(),current);
        for(const auto& e:sorted)if(!e.capture && e.state==DEVICE_STATE_ACTIVE && e.cableKind==0)
        {
            outputs.push_back(e);const int id=(int)outputs.size();outputBox.addItem(e.name,id);
            if(e.id==settings["output"].toString())outputId=id;
        }
        if(outputId==0)for(size_t i=0;i<outputs.size();++i)if(outputs[i].id==current){outputId=(int)i+1;break;}
        if(outputId==0 && !outputs.empty())outputId=1;
        temporaryOutput=outputId>0 && settings["output"].toString().isNotEmpty() && outputs[(size_t)(outputId-1)].id!=settings["output"].toString();
        cableBox.setSelectedId(cableId,juce::dontSendNotification);outputBox.setSelectedId(outputId,juce::dontSendNotification);
        if(cableId==0)
        {
            bool installed=false;for(const auto& e:all)if(e.cableKind==1 || e.cableKind==2)installed=true;
            status.setText(installed?"Cable detected but unavailable. Enable it in Windows sound settings; no reinstallation needed.":"No virtual cable found. Run TDSpeed Setup to install VB-CABLE.",juce::dontSendNotification);
        }
        else status.setText(outputId==0?"Existing cable detected. Choose your headphones or speakers to start.":"Existing cable detected / ready",juce::dontSendNotification);
    }
    bool enabled() const{return engine.running() && processor.apvts.getRawParameterValue("bypass")->load()<0.5f;}
    void showSongTools(bool expanded){static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get())->setFilePanelExpanded(expanded);}
    bool showEffects(bool expanded){auto* e=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());e->setEffectsExpanded(expanded);return e->effectsFit();}
    void configureUiCheck(int scale,bool legacy,bool advancedOpen,bool settingsExpanded){
        tdEffects::chooseEffect(processor.apvts,0,50);tdEffects::setParameter(processor.apvts,"revOn",legacy?1:0);
        auto* e=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());e->setFilePanelExpanded(true);e->refreshEffects();e->setAdvanced(advancedOpen);e->setEffectsExpanded(true);
        settingsOpen=settingsExpanded;detailsOpen=settingsExpanded;uiSize.setSelectedId(scale==1?100:scale==2?125:scale==3?150:200,juce::dontSendNotification);updateLayout();if(sizeChanged)sizeChanged();
    }
    juce::var diagnosticState() const
    {
        auto* r=new juce::DynamicObject();r->setProperty("running",engine.running());r->setProperty("processing",enabled());r->setProperty("starts",(int)engine.getStartCount());
        r->setProperty("temporary",temporaryOutput);r->setProperty("preference",settings["output"]);r->setProperty("status",status.getText());r->setProperty("backlog",processor.getBacklogSeconds());
        r->setProperty("console",defaultOutput(eConsole));r->setProperty("multimedia",defaultOutput(eMultimedia));r->setProperty("cable",activeCable);return juce::var(r);
    }
    void saveSettings()
    {
        if(!persist || importSettingsBlocked || !settings.isObject())return;
        const int ci=cableBox.getSelectedId()-1,oi=outputBox.getSelectedId()-1;
        if(!reconnectPending && ci>=0 && ci<(int)availableCables.size())settings.getDynamicObject()->setProperty("cable",availableCables[(size_t)ci].playback.id);
        if(!temporaryOutput && !reconnectPending && oi>=0 && oi<(int)outputs.size())settings.getDynamicObject()->setProperty("output",outputs[(size_t)oi].id);
        settings.getDynamicObject()->setProperty("startup",startupButton.getToggleState());settings.getDynamicObject()->setProperty("uiScale",uiSize.getSelectedId());
        juce::MemoryBlock state;processor.getStateInformation(state);settings.getDynamicObject()->setProperty("effect",state.toBase64Encoding());
        atomicJson(settingsFile,settings);
    }
private:
    void restoreIdleAudio()
    {
        if(!persist || engine.running() || oldHostActive())return;
        const int ci=cableBox.getSelectedId()-1,oi=outputBox.getSelectedId()-1;
        if(ci<0 || ci>=(int)availableCables.size() || oi<0 || oi>=(int)outputs.size())return;
        const auto error=restoreIdleOutput(availableCables[(size_t)ci].playback.id,outputs[(size_t)oi].id);
        if(error.isNotEmpty())status.setText(error,juce::dontSendNotification);
    }
    void configureStartup()
    {
        HKEY key=nullptr;
        if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        {startupButton.setToggleState(false,juce::dontSendNotification);return;}
        if(startupButton.getToggleState())
        {
            const auto command="\""+juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName()+"\" --tray";
            const std::wstring text(command.toWideCharPointer());RegSetValueExW(key,L"TDSpeed Preview",0,REG_SZ,(const BYTE*)text.c_str(),(DWORD)((text.size()+1)*sizeof(wchar_t)));
        }
        else RegDeleteValueW(key,L"TDSpeed Preview");RegCloseKey(key);
    }
    void timerCallback() override
    {
        if(engine.running())
        {
            if(engine.error().isNotEmpty())
            {const auto failure=engine.error();stopAndRestore();reconnectPending=true;refresh();status.setText(failure+" / normal output restored. Waiting for the selected device to reconnect.",juce::dontSendNotification);reconnectPending=true;}
            else if(defaultOutput(eConsole)!=activeCable || defaultOutput(eMultimedia)!=activeCable)
            {stopAndRestore();status.setText("Windows output changed manually / processing disabled",juce::dontSendNotification);}
            else {status.setText(juce::String(enabled()?"Processing":"Dry forwarding")+" / "+outputBox.getText()+(temporaryOutput?" / temporary output":""),juce::dontSendNotification);diagnostics.setText(juce::String(enabled()?"Processing":"Dry pass-through")+(temporaryOutput?" / temporary output":"")+" / input "+peakText(engine.getInputPeak())+" / output "+peakText(engine.getOutputPeak())+" / transport "+juce::String(engine.getQueueMs(),0)+" ms / gaps "+juce::String(engine.getUnderruns())+" / drops "+juce::String(engine.getOverruns()),juce::dontSendNotification);}
        }
        if(reconnectPending && !engine.running() && ++reconnectTicks>=4)
        {
            reconnectTicks=0;refresh();
            const int oi=outputBox.getSelectedId()-1;
            if(oi>=0 && oi<(int)outputs.size() && outputs[(size_t)oi].id==settings["output"].toString())
            {
                connect();if(engine.running())reconnectPending=false;
            }
        }
        if(++saveTicks==10){saveSettings();saveTicks=0;}
    }
    TDSpeedVSTAudioProcessor processor;
    WasapiEngine engine;
    TDSpeedLookAndFeel theme;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::ComboBox cableBox,outputBox;
    juce::TextButton enableButton{"Enable processing"},refreshButton{"Refresh"},audioSettings{"Audio settings >"},details{"Details >"},renderTools{"Song render >"},dismissImport{"OK"};
    juce::ComboBox uiSize;juce::Label diagnostics,importNotice;bool settingsOpen=false,detailsOpen=false;
    juce::ToggleButton startupButton{"Start with Windows"};
    juce::Label status,cableLabel,outputLabel;
    std::vector<Cable> availableCables;std::vector<Endpoint> outputs;
    juce::File settingsFile,journal;juce::var settings;juce::String activeCable;int saveTicks=0,reconnectTicks=0;bool persist=true,importSettingsBlocked=false,reconnectPending=false,everEnabled=false,temporaryOutput=false;
};
class ScaledContent : public juce::Component, private juce::Timer
{
public:
    MainContent content;bool fitting=false;float effectiveScale=1;
    explicit ScaledContent(bool autoStart=true):content(autoStart){addAndMakeVisible(content);content.sizeChanged=[this]{fit();};fit();startTimer(500);}
    ~ScaledContent(){stopTimer();content.sizeChanged=nullptr;}
    void fit(){if(fitting || (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0)return;juce::ScopedValueSetter<bool> guard(fitting,true);const auto* d=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());if(!d)d=juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        const float fitScale=juce::jmin((d->userArea.getWidth()-40)/820.0f,(d->userArea.getHeight()-64)/(float)content.getHeight());effectiveScale=tdUi::fit(content.scaleChoice(),820,content.getHeight(),d->userArea,40,64);
        content.setTransform(juce::AffineTransform::scale(effectiveScale));setSize(juce::roundToInt(820*effectiveScale),juce::roundToInt(content.getHeight()*effectiveScale));
        if(auto* w=findParentComponentOfClass<juce::DocumentWindow>()){w->setContentComponentSize(getWidth(),getHeight());auto b=w->getBounds();auto a=d->userArea.reduced(8);b.setX(juce::jlimit(a.getX(),juce::jmax(a.getX(),a.getRight()-b.getWidth()),b.getX()));b.setY(juce::jlimit(a.getY(),juce::jmax(a.getY(),a.getBottom()-b.getHeight()),b.getY()));w->setBounds(b);}
    }
    void resized()override{content.setTopLeftPosition(0,0);}
private:void timerCallback()override{fit();}
};
class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow(bool autoStart=true,bool visible=true):DocumentWindow("TDSpeed Preview",juce::Colour(0xff10171b),DocumentWindow::closeButton|DocumentWindow::minimiseButton)
    {
        setIcon(juce::ImageCache::getFromMemory(BinaryData::TDSpeed_png,BinaryData::TDSpeed_pngSize));
        setUsingNativeTitleBar(true);setContentOwned(new ScaledContent(autoStart),true);setResizable(false,false);
        auto* root=static_cast<ScaledContent*>(getContentComponent());root->fit();centreWithSize(root->getWidth(),root->getHeight()+getTitleBarHeight());setVisible(visible);root->fit();
    }
    void closeButtonPressed()override{setVisible(false);}
    MainContent& content(){return static_cast<ScaledContent*>(getContentComponent())->content;}
};
class Tray : public juce::SystemTrayIconComponent
{
public:
    explicit Tray(MainWindow& w):window(w)
    {
        auto icon=juce::ImageCache::getFromMemory(BinaryData::TDSpeed_png,BinaryData::TDSpeed_pngSize).rescaled(32,32,juce::Graphics::highResamplingQuality);
        setIconImage(icon,{});setIconTooltip("TDSpeed Preview / pitch & speed");
    }
    void mouseDown(const juce::MouseEvent& e)override
    {
        if(!e.mods.isPopupMenu()){window.setVisible(true);window.toFront(true);return;}
        juce::PopupMenu menu;menu.addItem(1,"Show TDSpeed Preview");menu.addItem(2,window.content().enabled()?"Disable processing":"Enable processing");
        menu.addItem(3,"Stop and restore Windows audio");menu.addSeparator();menu.addItem(4,"Exit");
        menu.showMenuAsync(juce::PopupMenu::Options(),[safe=juce::Component::SafePointer<Tray>(this)](int id)
        {if(!safe)return;auto& w=safe->window;if(id==1){w.setVisible(true);w.toFront(true);}if(id==2){if(w.content().enabled())w.content().disable();else w.content().enable();}if(id==3)w.content().stopAndRestore();if(id==4)juce::JUCEApplication::getInstance()->systemRequestedQuit();});
    }
private:MainWindow& window;
};
class Application : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName()override{return "TDSpeed Preview";}
    const juce::String getApplicationVersion()override{return "0.4.2-rc.1";}
    bool moreThanOneInstanceAllowed()override{return true;} // CLI diagnostics/watchdog are separate; GUI mutex below.
    void initialise(const juce::String& parameters)override
    {
        juce::ignoreUnused(parameters);
        auto args=getCommandLineParameterArray();
        args.removeEmptyStrings();
        if(args.size()>0 && args[0]=="--watchdog-test-parent") {Sleep(3000);ExitProcess(99);}
        if(args.size()>1 && args[0]=="--routing-test")
        {
            const juce::File reportFile(args[1].unquoted());
            const auto oldConsole=defaultOutput(eConsole),oldMultimedia=defaultOutput(eMultimedia);
            const auto all=endpoints();const auto pairs=cables(all,true);
            juce::String cable,physical;
            for(const auto& c:pairs)if(c.usable){cable=c.playback.id;break;}
            for(const auto& e:all)if(!e.capture && e.state==DEVICE_STATE_ACTIVE && e.cableKind==0)
                if(physical.isEmpty() || e.name.containsIgnoreCase("Eversolo"))physical=e.id;
            auto* result=new juce::DynamicObject();juce::var resultValue(result);juce::String error;
            const auto journal=reportFile.getSiblingFile("watchdog-test-journal.json");
            const auto executable=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName();
            STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
            PROCESS_INFORMATION child{};std::wstring command(("\""+executable+"\" --watchdog-test-parent").toWideCharPointer());
            const bool started=CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)!=FALSE;
            if(cable.isEmpty() || physical.isEmpty() || !started)error="Missing test devices or child process";
            if(error.isEmpty())
            {
                auto* record=new juce::DynamicObject();juce::var recordValue(record);const auto session=juce::Uuid().toString();
                record->setProperty("cable",cable);record->setProperty("physical",physical);
                record->setProperty("console",physical);record->setProperty("multimedia",physical);record->setProperty("session",session);
                atomicJson(journal,recordValue);
                if(!launchHidden({executable,"--watchdog",juce::String((int)child.dwProcessId),session,journal.getFullPathName()}))error="Watchdog could not start";
                if(error.isEmpty())error=setDefaultOutput(cable,eConsole);
                if(error.isEmpty())error=setDefaultOutput(cable,eMultimedia);
                WaitForSingleObject(child.hProcess,6000);
                for(int i=0;i<30 && journal.existsAsFile();++i)Sleep(100);
                result->setProperty("watchdogRestored",defaultOutput(eConsole)==physical && defaultOutput(eMultimedia)==physical && !journal.existsAsFile());
                // Verify that recovery does not overwrite manually chosen roles.
                record->setProperty("session",juce::Uuid().toString());atomicJson(journal,recordValue);
                setDefaultOutput(cable,eMultimedia);
                recoverRouting(journal);
                result->setProperty("manualChoicePreserved",defaultOutput(eConsole)==physical && defaultOutput(eMultimedia)==physical);
            }
            if(started){CloseHandle(child.hThread);CloseHandle(child.hProcess);}
            recoverRouting(journal);
            const auto restoreConsole=setDefaultOutput(oldConsole,eConsole),restoreMedia=setDefaultOutput(oldMultimedia,eMultimedia);
            result->setProperty("originalRestored",restoreConsole.isEmpty() && restoreMedia.isEmpty());result->setProperty("error",error);
            result->setProperty("passed",error.isEmpty() && (bool)result->getProperty("watchdogRestored") && (bool)result->getProperty("manualChoicePreserved") && (bool)result->getProperty("originalRestored"));
            juce::File(args[1].unquoted()).replaceWithText(juce::JSON::toString(resultValue));setApplicationReturnValue((bool)result->getProperty("passed")?0:1);quit();return;
        }
        if(args.size()>0 && args[0]=="--shutdown")
        {
            const auto name="Local\\TDSpeedPreview-Shutdown-"+juce::SystemStats::getLogonName();
            HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.toWideCharPointer());
            if(event){SetEvent(event);CloseHandle(event);Sleep(1500);}quit();return;
        }
        if(args.size()>0 && args[0]=="--watchdog")
        {
            if(args.size()==4)
            {
                const DWORD pid=(DWORD)args[1].getIntValue();const auto session=args[2].unquoted();const juce::File journal(args[3].unquoted());
                const auto log=journal.getSiblingFile("watchdog.log");
                log.replaceWithText("pid="+juce::String((int)pid)+" session="+session+" journal="+journal.getFullPathName()+"\n");
                HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
                log.appendText("parent="+juce::String(parent!=nullptr?"open":"missing")+"\n");
                if(parent)
                {
                    for(;;)
                    {
                        const auto state=juce::JSON::parse(journal);
                        if(!state.isObject() || state["session"].toString()!=session){log.appendText("session changed or journal cleared\n");break;}
                        if(WaitForSingleObject(parent,500)==WAIT_OBJECT_0){log.appendText("parent terminated; restoring\n");recoverRouting(journal,session);log.appendText("journal remaining="+juce::String(journal.existsAsFile()?"yes":"no")+"\n");break;}
                    }
                    CloseHandle(parent);
                }
                else recoverRouting(journal,session);
            }
            quit();return;
        }
        if(args.size()>1 && args[0]=="--bridge-check")
        {
            const juce::File reportFile(args[1].unquoted());const auto folder=reportFile.getSiblingFile("bridge-check-data");folder.createDirectory();
            const auto beforeConsole=defaultOutput(eConsole),beforeMultimedia=defaultOutput(eMultimedia),beforeCommunications=defaultOutput(eCommunications);
            auto* setup=new juce::DynamicObject();setup->setProperty("output","deliberately-missing-output");atomicJson(folder.getChildFile("settings.json"),juce::var(setup));
            auto* report=new juce::DynamicObject();juce::var result(report);
            {
                MainContent content(true,folder);content.connect();Sleep(200);
                const auto dry=content.diagnosticState();report->setProperty("startup",dry);
                content.enable();Sleep(200);const auto enabled=content.diagnosticState();report->setProperty("enabled",enabled);
                content.disable();Sleep(200);const auto disabled=content.diagnosticState();report->setProperty("disabled",disabled);
                content.enable();Sleep(200);const auto resumed=content.diagnosticState();report->setProperty("resumed",resumed);
                content.stopAndRestore();const auto stopped=content.diagnosticState();report->setProperty("stopped",stopped);
                report->setProperty("passed",(bool)dry["running"] && !(bool)dry["processing"] && (bool)dry["temporary"] && dry["preference"].toString()=="deliberately-missing-output"
                    && (bool)enabled["processing"] && (bool)disabled["running"] && !(bool)disabled["processing"] && (bool)resumed["processing"]
                    && (float)enabled["backlog"]<0.27f && (float)resumed["backlog"]>=(float)disabled["backlog"]-0.02f
                    && (int)dry["starts"]==1 && (int)resumed["starts"]==1 && !(bool)stopped["running"]
                    && stopped["console"]!=dry["cable"] && stopped["multimedia"]!=dry["cable"] && defaultOutput(eCommunications)==beforeCommunications);
            }
            if(activePhysical(beforeConsole))setDefaultOutput(beforeConsole,eConsole);
            if(activePhysical(beforeMultimedia))setDefaultOutput(beforeMultimedia,eMultimedia);
            atomicJson(reportFile,result);setApplicationReturnValue((bool)result["passed"]?0:1);quit();return;
        }
        if(args.size()>1 && args[0]=="--ui-check")
        {
            MainWindow testWindow(false,false);auto* report=new juce::DynamicObject();juce::var result(report);juce::Array<juce::var> layouts;const int initial=testWindow.getHeight();bool allFit=true;
            testWindow.content().showSongTools(false); // Persisted panel state must not change the test baseline.
            const int songInitial=testWindow.content().getHeight();testWindow.content().showSongTools(true);const int songOpen=testWindow.content().getHeight();testWindow.content().showSongTools(false);const int songClosed=testWindow.content().getHeight();
            const bool songSizing=songOpen>songInitial && songClosed==songInitial;report->setProperty("songPanelSizing",songSizing);
            const auto hwnd=(HWND)testWindow.getPeer()->getNativeHandle();const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
            const bool locked=(style&WS_THICKFRAME)==0 && (style&WS_MAXIMIZEBOX)==0 && !testWindow.isResizable();report->setProperty("nativeLocked",locked);report->setProperty("nativeStyle",juce::String::toHexString((juce::int64)style));
            for(int scale=1;scale<=4;++scale){testWindow.content().configureUiCheck(scale,true,true,true);auto* root=static_cast<ScaledContent*>(testWindow.getContentComponent());root->fit();
                const auto* display=juce::Desktop::getInstance().getDisplays().getDisplayForRect(testWindow.getScreenBounds());RECT frame{};GetWindowRect(hwnd,&frame);MONITORINFO mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
                const bool frameFits=frame.left>=mi.rcWork.left && frame.top>=mi.rcWork.top && frame.right<=mi.rcWork.right && frame.bottom<=mi.rcWork.bottom;
                const bool fit=display && display->userArea.contains(testWindow.getScreenBounds()) && frameFits;allFit&=fit;
                auto* row=new juce::DynamicObject();row->setProperty("requestedPercent",scale==1?100:scale==2?125:scale==3?150:200);row->setProperty("effectiveScale",root->effectiveScale);row->setProperty("width",testWindow.getWidth());row->setProperty("height",testWindow.getHeight());row->setProperty("fitsMonitor",fit);row->setProperty("nativeFrameFits",frameFits);row->setProperty("monitorDpi",display?display->dpi:0);layouts.add(juce::var(row));
                juce::FileOutputStream stream(juce::File(args[1].unquoted()).getSiblingFile("RC-expanded-"+juce::String(scale==1?100:scale==2?125:scale==3?150:200)+".png"));stream.setPosition(0);stream.truncate();juce::PNGImageFormat png;png.writeImageToStream(root->createComponentSnapshot(root->getLocalBounds()),stream);
            }
            const int expanded=testWindow.getHeight(),expandedLogical=testWindow.content().getHeight();testWindow.content().showEffects(false);const int collapsed=testWindow.getHeight(),collapsedLogical=testWindow.content().getHeight();
            const auto* collapsedDisplay=juce::Desktop::getInstance().getDisplays().getDisplayForRect(testWindow.getScreenBounds());allFit&=collapsedDisplay && collapsedDisplay->userArea.contains(testWindow.getScreenBounds());
            report->setProperty("initialHeight",initial);report->setProperty("expandedHeight",expanded);report->setProperty("collapsedHeight",collapsed);report->setProperty("expandedLogicalHeight",expandedLogical);report->setProperty("collapsedLogicalHeight",collapsedLogical);report->setProperty("layouts",layouts);report->setProperty("passed",locked && allFit && collapsedLogical<expandedLogical && songSizing);
            atomicJson(juce::File(args[1].unquoted()),result);setApplicationReturnValue((bool)result["passed"]?0:1);quit();return;
        }
        if(args.size()>1 && args[0]=="--preview")
        {
            MainContent content(false);content.updateLayout();
            juce::FileOutputStream stream(juce::File(args[1].unquoted()));stream.setPosition(0);stream.truncate();
            juce::PNGImageFormat png;png.writeImageToStream(content.createComponentSnapshot(content.getLocalBounds()),stream);
            quit();return;
        }
        if(args.size()>1 && args[0]=="--self-test")
        {const auto result=runStandaloneChecks();juce::File(args[1].unquoted()).replaceWithText(juce::JSON::toString(result));setApplicationReturnValue((bool)result["passed"]?0:1);quit();return;}
        if(args.size()==5 && (args[0]=="--signal-check" || args[0]=="--dry-signal-check"))
        {
            const auto result=signalCheck(args[1].unquoted(),args[2].unquoted(),args[3].unquoted(),args[0]=="--dry-signal-check");
            juce::File(args[4].unquoted()).replaceWithText(juce::JSON::toString(result));setApplicationReturnValue((bool)result["passed"]?0:1);quit();return;
        }
        if(args.size()==6 && args[0]=="--soak-check"){
            const auto result=soakCheck(args[1].unquoted(),args[2].unquoted(),args[3].unquoted(),juce::File(args[4].unquoted()),juce::jlimit(10,7200,args[5].getIntValue()));setApplicationReturnValue((bool)result["passed"]?0:1);quit();return;
        }
        if((args.size()==4 || args.size()==5) && args[0]=="--stream-check")
        {
            TDSpeedVSTAudioProcessor processor;tdEffects::setParameter(processor.apvts,"bypass",1);WasapiEngine engine(processor);
            auto* report=new juce::DynamicObject();juce::var result(report);auto error=engine.start(args[1].unquoted(),args[2].unquoted());
            const int duration=juce::jlimit(1,7200,args.size()==5?args[4].getIntValue():2);
            if(error.isEmpty())for(int sec=0;sec<duration;++sec){Sleep(1000);error=engine.error();report->setProperty("elapsedSeconds",sec+1);report->setProperty("underruns",(int)engine.getUnderruns());report->setProperty("overruns",(int)engine.getOverruns());report->setProperty("queueMs",engine.getQueueMs());report->setProperty("running",engine.running());report->setProperty("complete",false);juce::File(args[3].unquoted()).replaceWithText(juce::JSON::toString(result));if(error.isNotEmpty())break;}
            report->setProperty("durationSeconds",duration);report->setProperty("complete",true);
            report->setProperty("running",engine.running());report->setProperty("error",error);report->setProperty("passed",error.isEmpty());report->setProperty("underruns",(int)engine.getUnderruns());
            report->setProperty("overruns",(int)engine.getOverruns());report->setProperty("queueMs",engine.getQueueMs());
            engine.stop();juce::File(args[3].unquoted()).replaceWithText(juce::JSON::toString(result));setApplicationReturnValue(error.isEmpty()?0:1);quit();return;
        }
        if(args.size()>0 && args[0]=="--remove-startup")
        {HKEY key=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){RegDeleteValueW(key,L"TDSpeed Preview");RegCloseKey(key);}quit();return;}
        if(args.size()>0 && args[0]=="--check-cable")
        {setApplicationReturnValue(cableInstallationState());quit();return;}
        if(args.size()>1 && args[0]=="--probe")
        {juce::File(args[1].unquoted()).replaceWithText(juce::JSON::toString(describeDevices(true)));quit();return;}
        if(args.size()>1 && args[0]=="--route-check")
        {
            const auto c=defaultOutput(eConsole),m=defaultOutput(eMultimedia);auto* r=new juce::DynamicObject();
            r->setProperty("consoleError",setDefaultOutput(c,eConsole));r->setProperty("multimediaError",setDefaultOutput(m,eMultimedia));
            juce::File(args[1].unquoted()).replaceWithText(juce::JSON::toString(juce::var(r)));quit();return;
        }
        if(args.size()==3 && args[0]=="--restore-defaults")
        {setDefaultOutput(args[1].unquoted(),eConsole);setDefaultOutput(args[2].unquoted(),eMultimedia);quit();return;}
        if(args.size()>0 && args[0]=="--restore") {recoverRouting(dataFolder().getChildFile("routing.json"));quit();return;}
        if(args.size()>0 && args[0]!="--tray"){setApplicationReturnValue(2);quit();return;}
        const auto mutexName="Local\\TDSpeedPreview-"+juce::SystemStats::getLogonName();
        mutex=CreateMutexW(nullptr,FALSE,mutexName.toWideCharPointer());
        if(GetLastError()==ERROR_ALREADY_EXISTS)
        {HWND existing=FindWindowW(nullptr,L"TDSpeed Preview");if(existing){ShowWindow(existing,SW_SHOW);SetForegroundWindow(existing);}quit();return;}
        const auto shutdownName="Local\\TDSpeedPreview-Shutdown-"+juce::SystemStats::getLogonName();
        shutdownEvent=CreateEventW(nullptr,TRUE,FALSE,shutdownName.toWideCharPointer());
        window=std::make_unique<MainWindow>();tray=std::make_unique<Tray>(*window);startTimer(250);
        if(args.contains("--tray"))window->setVisible(false);
    }
    void shutdown()override{stopTimer();tray.reset();window.reset();if(shutdownEvent){CloseHandle(shutdownEvent);shutdownEvent=nullptr;}if(mutex){CloseHandle(mutex);mutex=nullptr;}}
    void systemRequestedQuit()override{if(window)window->content().stopAndRestore();quit();}
private:
    void timerCallback()override{if(shutdownEvent && WaitForSingleObject(shutdownEvent,0)==WAIT_OBJECT_0)systemRequestedQuit();}
    HANDLE shutdownEvent=nullptr;
    HANDLE mutex=nullptr;std::unique_ptr<MainWindow> window;std::unique_ptr<Tray> tray;
};
}
START_JUCE_APPLICATION(td::Application)
