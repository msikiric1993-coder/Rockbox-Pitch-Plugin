#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff10171b }, panel { 0xff1a242a }, ink { 0xffeef5f2 };
const juce::Colour muted { 0xff9aafb5 }, accent { 0xff81dfb5 }, border { 0xff304047 };
}

TDSpeedLookAndFeel::TDSpeedLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, accent);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::backgroundColourId, border);
    setColour(juce::Slider::textBoxTextColourId, ink);
    setColour(juce::Slider::textBoxBackgroundColourId, background);
    setColour(juce::Slider::textBoxOutlineColourId, border);
    setColour(juce::TextButton::buttonColourId, border);
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ComboBox::backgroundColourId, background);
    setColour(juce::ComboBox::textColourId, ink);
    setColour(juce::ComboBox::outlineColourId, border);
    setColour(juce::Label::textColourId, muted);
    setColour(juce::AlertWindow::backgroundColourId,panel);
    setColour(juce::AlertWindow::textColourId,ink);
    setColour(juce::AlertWindow::outlineColourId,border);
    setColour(juce::TextEditor::backgroundColourId,background);
    setColour(juce::TextEditor::textColourId,ink);
    setColour(juce::TextEditor::outlineColourId,border);
    setColour(juce::TextEditor::focusedOutlineColourId,accent);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, ink);
}

void TDSpeedLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float position, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height).reduced(12.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + position * (end - start);
    juce::Path baseArc, valueArc;
    baseArc.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    valueArc.addCentredArc(centre.x, centre.y, radius, radius, 0, start, angle, true);
    g.setColour(border); g.strokePath(baseArc, juce::PathStrokeType(4.0f));
    g.setColour(slider.isEnabled() ? accent : muted.withAlpha(0.3f));
    g.strokePath(valueArc, juce::PathStrokeType(4.0f));
    g.setColour(background); g.fillEllipse(centre.x-radius+8, centre.y-radius+8, 2*radius-16, 2*radius-16);
    g.setColour(slider.isEnabled() ? ink : muted.withAlpha(0.3f));
    g.drawLine(centre.x + std::sin(angle)*(radius*0.35f), centre.y - std::cos(angle)*(radius*0.35f),
               centre.x + std::sin(angle)*(radius*0.72f), centre.y - std::cos(angle)*(radius*0.72f), 3.0f);
}

TDSpeedVSTAudioProcessorEditor::TDSpeedVSTAudioProcessorEditor(TDSpeedVSTAudioProcessor& p):AudioProcessorEditor(&p),processor(p),rackPanel(p)
{
    setLookAndFeel(&theme);setResizable(false,false);addAndMakeVisible(content);
    for(auto* c:std::initializer_list<juce::Component*>{&renderButton,&cancelRender,&showRender,&renderStatus})content.addAndMakeVisible(c);

    for(auto* c:std::initializer_list<juce::Component*>{&armCapture,&finishCapture,&cancelCapture,&captureStatus})content.addAndMakeVisible(c);
    armCapture.setColour(juce::TextButton::buttonColourId,juce::Colour(0xffe9b66a));
    armCapture.setColour(juce::TextButton::textColourOffId,juce::Colour(0xff10171b));
    armCapture.onClick=[this]{armTrackRender();};finishCapture.onClick=[this]{processor.trackCapture.finishManually();};
    cancelCapture.onClick=[this]{processor.trackCapture.cancel();};
    finishCapture.setTooltip("Use after the DAW bounce finishes if capture does not finish automatically");
    captureStatus.setFont(juce::Font(11));captureStatus.setText("Arm, then run a silent Fast Bounce",juce::dontSendNotification);
    finishCapture.setVisible(false);cancelCapture.setVisible(false);
    renderButton.setButtonText("Render source file");
    renderButton.onClick=[this]{chooseRenderSource();};
    cancelRender.onClick=[this]{if(fileJob)fileJob->signalThreadShouldExit();};
    showRender.onClick=[this]{if(!showRender.dragged && renderedFile.existsAsFile())renderedFile.revealToUser();};
    cancelRender.setVisible(false);showRender.setVisible(false);
    renderStatus.setText("Render a complete file at the chosen pitch and effects",juce::dontSendNotification);
    renderStatus.setFont(juce::Font(11));

    for(auto* c:std::initializer_list<juce::Component*>{&timingMode,&createGroup,&joinGroup,&leaveGroup,&syncStatus})content.addAndMakeVisible(c);
    timingMode.addItem("Listening",1);timingMode.addItem("Follow DAW",2);timingAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"timingMode",timingMode);
    timingMode.onChange=[this]{if(!processor.isDawMode())processor.leaveSyncGroup();timerCallback();};
    createGroup.onClick=[this]{processor.createSyncGroup();timerCallback();};
    leaveGroup.onClick=[this]{processor.leaveSyncGroup();timerCallback();};
    joinGroup.onChange=[this]{const int n=joinGroup.getSelectedId()-2;if(n>=0 && n<(int)groupList.size())processor.joinSyncGroup(groupList[(size_t)n].first);};
    syncStatus.setFont(juce::Font(11));
    showRack=(bool)p.apvts.state.getProperty("uiEffectsExpanded",false);
    for(auto* c:std::initializer_list<juce::Component*>{&rackPanel,&bypassButton,&effectsBypass,&protect,&resetButton,&zeroButton,&rackTab,&semitoneSlider,&output,&speedLabel,&delayLabel,&outputLabel,&meter,&uiSize,&importNotice,&importOK})content.addAndMakeVisible(c);
    bypassAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"bypass",bypassButton);
    effectsBypassAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"rackBypass",effectsBypass);
    protectAtt=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"rackLimiter",protect);
    semitoneSlider.setSliderStyle(juce::Slider::LinearHorizontal);semitoneSlider.setTextBoxStyle(juce::Slider::TextBoxRight,false,112,34);semitoneSlider.setTextValueSuffix(" st");semitoneSlider.setDoubleClickReturnValue(true,0);
    semitoneAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"semitones",semitoneSlider);
    output.setSliderStyle(juce::Slider::LinearHorizontal);output.setTextBoxStyle(juce::Slider::TextBoxRight,false,78,24);output.setTextValueSuffix(" dB");output.setDoubleClickReturnValue(true,0);
    outputAtt=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts,"masterOutput",output);
    outputLabel.setText("FINAL OUTPUT",juce::dontSendNotification);meter.setJustificationType(juce::Justification::centredRight);
    zeroButton.onClick=[this]{semitoneSlider.setValue(0,juce::sendNotificationSync);};resetButton.onClick=[this]{processor.requestBacklogReset();};resetButton.setTooltip("Jump to current music, discarding the accumulated delay.");
    rackTab.onClick=[this]{setEffectsExpanded(!showRack);};rackPanel.sizeChanged=[this]{updateSize();};
    tdUi::populate(uiSize);uiSize.setSelectedId(tdUi::valid((int)p.apvts.state.getProperty("uiScaleChoice",1)),juce::dontSendNotification);
    uiSize.onChange=[this]{processor.apvts.state.setProperty("uiScaleChoice",uiSize.getSelectedId(),nullptr);requestedScale=tdUi::requested(uiSize.getSelectedId());timerCallback();updateSize();};
    requestedScale=tdUi::requested(uiSize.getSelectedId());uiScale=requestedScale;
    importNotice.setText("Imported Mix keeps its percentage; 100% is now fully wet.",juce::dontSendNotification);importNotice.setFont(juce::Font(12));importOK.onClick=[this]{tdEffects::acknowledgeMixImport();mixExplained=true;processor.apvts.state.setProperty("rcMixNotice",false,nullptr);updateSize();};
    updateSize();timerCallback();startTimerHz(10);
}
TDSpeedVSTAudioProcessorEditor::~TDSpeedVSTAudioProcessorEditor(){stopTimer();rackPanel.sizeChanged=nullptr;setLookAndFeel(nullptr);}
void TDSpeedVSTAudioProcessorEditor::updateSize()
{
    rackTab.setButtonText(showRack?"Effects v":"Effects >");rackPanel.setVisible(showRack);const int w=juce::roundToInt(820*uiScale),h=juce::roundToInt(preferredHeight()*uiScale);
    processor.apvts.state.setProperty("uiEffectsExpanded",showRack,nullptr);
    // setResizeLimits may resize the editor synchronously. Remember the old
    // dimensions so the standalone parent is notified in that case as well.
    const bool dimensionsChanged = getWidth()!=w || getHeight()!=h;
    setResizeLimits(w,h,w,h);
    if(getWidth()!=w || getHeight()!=h)setSize(w,h);
    if(dimensionsChanged && sizeChanged)sizeChanged();
    resized();repaint();
}
void TDSpeedVSTAudioProcessorEditor::timerCallback()
{
    if(!standalone) {
        auto& capture=processor.trackCapture;capture.poll();const auto state=capture.status();
        const bool busyCapture=state==TrackCapture::armed || state==TrackCapture::capturing || state==TrackCapture::finishing;
        armCapture.setEnabled(!fileJob && !busyCapture);finishCapture.setVisible(busyCapture);cancelCapture.setVisible(busyCapture);
        renderButton.setEnabled(!fileJob && !busyCapture);
        if(state==TrackCapture::armed)captureStatus.setText("Armed / run DAW Fast Bounce over original range",juce::dontSendNotification);
        if(state==TrackCapture::capturing)captureStatus.setText("Captured "+juce::String(capture.capturedFrames()/capture.sampleRate(),1)+" s / finish after bounce",juce::dontSendNotification);
        if(state==TrackCapture::finishing)captureStatus.setText("Finalizing captured audio...",juce::dontSendNotification);
        if(state==TrackCapture::failed)captureStatus.setText(capture.problem(),juce::dontSendNotification);
        if(state==TrackCapture::ready && !fileJob) {
            tdFile::Request request;request.source=capture.file;request.destination=capture.destination;request.rack=capture.rack.createCopy();request.semitones=capture.semitones;
            showRender.setVisible(false);renderButton.setEnabled(false);cancelRender.setVisible(true);
            fileJob=std::make_unique<tdFile::Job>(request);
            if(!fileJob->startThread()){fileJob.reset();capture.cancel();}
        }
        if(state==TrackCapture::complete)captureStatus.setText("Track render finished / drag the saved WAV into the DAW",juce::dontSendNotification);
    }
    if(fileJob) {
        if(fileJob->done.load(std::memory_order_acquire)) {
            const auto result=fileJob->result;if(fileJob->request.source==processor.trackCapture.file)processor.trackCapture.markComplete();fileJob.reset();renderButton.setEnabled(true);cancelRender.setVisible(false);
            if(result.error.isNotEmpty())renderStatus.setText(result.error,juce::dontSendNotification);
            else {renderedFile=result.file;showRender.file=renderedFile;showRender.setVisible(true);renderStatus.setText("Ready / "+juce::String(result.duration,1)+" s / import into DAW; mute original and avoid applying pitch/effects twice",juce::dontSendNotification);}
        } else renderStatus.setText("Rendering "+juce::String(100*fileJob->progress.load(),0)+"% / playback is unchanged",juce::dontSendNotification);
    }
    if(!standalone){const auto* d=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());if(d){const float fit=juce::jmin((d->userArea.getWidth()-32)/820.0f,(d->userArea.getHeight()-72)/(float)preferredHeight());const float scale=juce::jmin(uiSize.getSelectedId()==1?tdUi::automatic(d->userArea):requestedScale,fit);if(std::abs(scale-uiScale)>.001f){uiScale=scale;updateSize();}}}
    if(!standalone){
        auto groups=processor.availableSyncGroups();if(groups!=groupList){groupList=groups;joinGroup.clear(juce::dontSendNotification);joinGroup.addItem("Join group...",1);for(size_t i=0;i<groupList.size();++i)joinGroup.addItem(groupList[i].second,(int)i+2);}
        const auto id=processor.syncGroupId();int selected=1;for(size_t i=0;i<groupList.size();++i)if(groupList[i].first==id)selected=(int)i+2;joinGroup.setSelectedId(selected,juce::dontSendNotification);
        const bool daw=processor.isDawMode(),leader=processor.syncLeader();semitoneSlider.setEnabled(!daw || leader);zeroButton.setEnabled(!daw || leader);resetButton.setEnabled(!daw);
        const bool editable=daw && !processor.dawRunning();createGroup.setEnabled(editable);joinGroup.setEnabled(editable);leaveGroup.setEnabled(editable && id.isNotEmpty());
        if(daw && !leader && std::abs(processor.apvts.getRawParameterValue("semitones")->load()-processor.pendingDawPitch())>.049f)tdEffects::setParameter(processor.apvts,"semitones",processor.pendingDawPitch());
        if(daw && id.isNotEmpty() && (processor.apvts.getRawParameterValue("bypass")->load()>.5f)!=processor.groupBypassed())tdEffects::setParameter(processor.apvts,"bypass",processor.groupBypassed()?1:0);
        bypassButton.setButtonText(daw && id.isNotEmpty()?"Bypass group":"Bypass");resetButton.setButtonText(daw?"Stop/Play resets":"Re-centre buffer");
        syncStatus.setText(processor.dawStatusText()+(id.isNotEmpty()?(leader?" / leader":" / follower"):""),juce::dontSendNotification);
    }
    if(importNotice.isVisible()!=mixNotice())updateSize();
    backlog=processor.getBacklogSeconds();capacity=processor.getHistoryCapacitySeconds();bufferState=processor.getBufferStatus();
    const float semi=processor.apvts.getRawParameterValue("semitones")->load();speedLabel.setText(juce::String(std::pow(2.0f,semi/12)*100,1)+"% playback speed",juce::dontSendNotification);
    const bool bypass=processor.apvts.getRawParameterValue("bypass")->load()>.5f;
    delayLabel.setText(processor.isDawMode()?processor.dawStatusText():bypass?"Dry forwarding / buffered playback held":"Buffer delay / "+juce::String(backlog<60?backlog:backlog/60,2)+(backlog<60?" s":" min"),juce::dontSendNotification);
    int active=processor.apvts.getRawParameterValue("revOn")->load()>.5f?1:0;for(int i=0;i<3;++i)if(processor.apvts.getRawParameterValue(tdEffects::slotId(i,"on"))->load()>.5f)++active;
    rackTab.setButtonText(juce::String(showRack?"Effects v":"Effects >")+" / "+juce::String(active)+" active");
    const float peak=processor.getOutputPeak();meter.setText(juce::String(juce::jlimit(-120.0f,30.1f,juce::Decibels::gainToDecibels(peak)),1)+" dBFS"+(peak>=1?" / CLIP":""),juce::dontSendNotification);meter.setColour(juce::Label::textColourId,peak>=1?juce::Colour(0xfff5aa70):accent);
    repaint();
}
void TDSpeedVSTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);g.addTransform(juce::AffineTransform::scale(paintScale));g.setColour(ink);g.setFont(28);g.drawText("TDSPEED PREVIEW",28,17,410,36,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(13);g.drawText("VARISPEED / PERSONAL LISTENING",29,53,400,20,juce::Justification::centredLeft);
    for(auto r:{pitchPanel,reverbPanel,statusPanel}){g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),12);g.setColour(border);g.drawRoundedRectangle(r.toFloat().reduced(.5f),12,1);}
    g.setColour(ink);g.setFont(17);g.drawText("Pitch & speed",44,pitchPanel.getY()+12,200,26,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(12);g.drawText("-12 / HALF SPEED",44,pitchPanel.getBottom()-34,170,20,juce::Justification::centredLeft);g.drawText("+12 / DOUBLE SPEED",600,pitchPanel.getBottom()-34,175,20,juce::Justification::centredRight);
    auto track=statusPanel.toFloat().reduced(20,0).withY((float)statusPanel.getY()+48).withHeight(5);g.setColour(border);g.fillRoundedRectangle(track,2);g.setColour(accent);g.fillRoundedRectangle(track.withWidth(track.getWidth()*juce::jlimit(0.0f,1.0f,backlog/juce::jmax(1.0f,capacity))),2);
    g.setColour(muted);const auto msg=bufferState?"Buffer limit / 3-second recovery / "+juce::String(processor.getPauseSeconds(),1)+" s remaining":"Pitch down slows the song: delay grows naturally. History limit: 3-second pause, reset and resume.";
    g.drawText(msg,statusPanel.getX()+20,statusPanel.getY()+66,statusPanel.getWidth()-40,20,juce::Justification::centredLeft);
}
void TDSpeedVSTAudioProcessorEditor::resized()
{
    paintScale=juce::jmin(uiScale,juce::jmin(getWidth()/820.0f,getHeight()/(float)preferredHeight()));
    content.setTransform(juce::AffineTransform());content.setBounds(0,0,820,preferredHeight());content.setTransform(juce::AffineTransform::scale(paintScale));
    bypassButton.setBounds(676,26,110,30);uiSize.setBounds(567,26,90,30);
    const int offset=(mixNotice()?28:0)+(standalone?0:136);importNotice.setVisible(mixNotice());importOK.setVisible(mixNotice());importNotice.setBounds(28,73+(standalone?0:136),690,28);importOK.setBounds(730,73+(standalone?0:136),56,28);
    armCapture.setBounds(28,78,236,34);finishCapture.setBounds(276,78,120,34);cancelCapture.setBounds(408,78,100,34);captureStatus.setBounds(520,76,268,38);
    timingMode.setBounds(28,130,124,28);createGroup.setBounds(166,130,124,28);joinGroup.setBounds(304,130,284,28);leaveGroup.setBounds(602,130,184,28);syncStatus.setBounds(28,164,758,38);
    pitchPanel={24,86+offset,772,126};zeroButton.setBounds(636,101+offset,140,28);semitoneSlider.setBounds(40,135+offset,740,42);speedLabel.setBounds(285,178+offset,250,20);speedLabel.setJustificationType(juce::Justification::centred);
    reverbPanel={24,226+offset,772,44+(showRack?rackPanel.preferredHeight():0)};rackTab.setBounds(40,233+offset,200,30);effectsBypass.setBounds(620,233+offset,160,30);
    rackPanel.setBounds(32,270+offset,756,rackPanel.preferredHeight());
    const int y=reverbPanel.getBottom()+8;outputLabel.setBounds(40,y,100,32);output.setBounds(143,y,270,32);protect.setBounds(420,y,130,32);meter.setBounds(554,y,226,32);
    renderButton.setBounds(40,y+142,160,28);cancelRender.setBounds(210,y+142,110,28);showRender.setBounds(210,y+142,160,28);renderStatus.setBounds(378,y+140,402,34);
    statusPanel={24,y+40,772,100};delayLabel.setBounds(40,statusPanel.getY()+10,550,30);resetButton.setBounds(612,statusPanel.getY()+12,164,28);
}

void TDSpeedVSTAudioProcessorEditor::chooseRenderSource() {
    if(fileJob)return;
    renderChooser=std::make_unique<juce::FileChooser>("Choose the original audio file to render",juce::File(),"*.wav;*.flac;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.ogg;*.opus");
    renderChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this)](const juce::FileChooser& chooser){
        if(safe && chooser.getResult().existsAsFile())safe->confirmFileRender(chooser.getResult());
    });
}
void TDSpeedVSTAudioProcessorEditor::confirmFileRender(const juce::File& source) {
    tdFile::Request request;request.source=source;request.rack=tdEffects::captureState(processor.apvts);
    request.semitones=processor.isDawMode()?processor.pendingDawPitch():processor.apvts.getRawParameterValue("semitones")->load();
    const auto speed=std::pow(2.,request.semitones/12.);
    const auto message="Render the entire selected file at "+juce::String(request.semitones,1)+" semitones ("+juce::String(speed*100,1)+"% speed) with this instance's current effects and output levels?\n\nThe original format, sample rate and bit depth are kept where supported. Compressed files are re-encoded at their original bitrate; longer audio makes a proportionally larger file. MP3/AAC export uses FFmpeg.\n\nThis creates a new file, not a replacement DAW clip. Import or drag it into your DAW, mute the original and avoid applying pitch/effects twice. Other DAW effects, automation, clip edits and group members are not included. Plugin bypass does not disable this file render.";
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,"Render slowed file",message,"Choose output","Cancel",this,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this),request](int yes){if(safe && yes)safe->chooseRenderDestination(request);}));
}
void TDSpeedVSTAudioProcessorEditor::chooseRenderDestination(tdFile::Request request) {
    const auto initial=request.source.getSiblingFile(request.source.getFileNameWithoutExtension()+" - TDSpeed"+request.source.getFileExtension());
    renderChooser=std::make_unique<juce::FileChooser>("Save rendered audio",initial,"*.wav;*.flac;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.ogg;*.opus");
    renderChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this),request](const juce::FileChooser& chooser) mutable {
        if(!safe || chooser.getResult()==juce::File())return;request.destination=chooser.getResult();
        safe->showRender.setVisible(false);safe->renderButton.setEnabled(false);safe->cancelRender.setVisible(true);
        safe->fileJob=std::make_unique<tdFile::Job>(request);if(!safe->fileJob->startThread()){safe->fileJob.reset();safe->renderButton.setEnabled(true);safe->cancelRender.setVisible(false);safe->renderStatus.setText("Could not start render worker",juce::dontSendNotification);}
    });
}

void TDSpeedVSTAudioProcessorEditor::armTrackRender() {
    if(fileJob)return;
    const auto message="Capture this track without choosing an original song?\n\nAfter arming, run a silent Fast Bounce/offline export in your DAW over the ORIGINAL track range. This plugin captures its incoming audio (including clip edits and effects before it), then saves a complete slowed WAV with its current pitch and effects.\n\nThe DAW's bounce file is only a capture pass; use the separate WAV created here. Other tracks, downstream effects and this plugin's automation are not included. If automatic finishing does not occur, click Finish capture AFTER the bounce ends.";
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,"Arm track render",message,"Choose WAV output","Cancel",this,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this)](int yes){
        if(!safe || !yes)return;
        tdFile::Request request;request.rack=tdEffects::captureState(safe->processor.apvts);request.semitones=safe->processor.isDawMode()?safe->processor.pendingDawPitch():safe->processor.apvts.getRawParameterValue("semitones")->load();
        safe->renderChooser=std::make_unique<juce::FileChooser>("Save complete slowed track",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Track - TDSpeed.wav"),"*.wav");
        safe->renderChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe,request](const juce::FileChooser& choice){
            if(!safe || choice.getResult()==juce::File())return;
            auto destination=choice.getResult().withFileExtension(".wav");
            if(!safe->processor.trackCapture.arm(safe->processor.getSampleRate()>0?safe->processor.getSampleRate():48000,safe->processor.getTotalNumInputChannels(),request.rack,request.semitones,destination))
                safe->captureStatus.setText("Could not arm capture; retry after stopping processing",juce::dontSendNotification);
        });
    }));
}
