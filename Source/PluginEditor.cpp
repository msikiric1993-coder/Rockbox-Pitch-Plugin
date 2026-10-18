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

TDSpeedVSTAudioProcessorEditor::TDSpeedVSTAudioProcessorEditor(TDSpeedVSTAudioProcessor& p):AudioProcessorEditor(&p),waveform(p.preview),processor(p),rackPanel(p)
{
    setLookAndFeel(&theme);setResizable(false,false);addAndMakeVisible(content);
    for(auto* c:std::initializer_list<juce::Component*>{&loadButton,&renderButton,&cancelRender,&renderStatus,&previewPlay,&previewStop,&auditionSelector,&previewSave,&previewInfo,&waveform,&sizeLabel,&fileRestart,&saveFormat,&formatLabel,&formatInfo})content.addAndMakeVisible(c);
    renderButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xffe9b66a));
    renderButton.setColour(juce::TextButton::textColourOffId,background);
    loadButton.onClick=[this]{chooseRenderSource();};
    renderButton.setTooltip("Render the entire loaded song from its beginning with the current pitch, effects and output level. Playback position and live buffer are ignored; no buffer reset is needed.");
    renderButton.onClick=[this]{saveAfterRender=false;renderLoadedSong();};
    cancelRender.onClick=[this]{if(fileJob)fileJob->signalThreadShouldExit();};
    cancelRender.setVisible(false);
    renderStatus.setText("Load or drop a song, play and tweak, then render.",juce::dontSendNotification);
    renderStatus.setFont(juce::Font(11));previewInfo.setFont(juce::Font(11));
    sizeLabel.setText("UI SIZE",juce::dontSendNotification);sizeLabel.setFont(juce::Font(11));
    auditionSelector.addItem("Live input",1);auditionSelector.addItem("Song + effects",2);auditionSelector.addItem("Rendered file",3);
    auditionSelector.onChange=[this]{processor.setAuditionMode(auditionSelector.getSelectedId()-1);refreshAuditionView();};
    previewPlay.onClick=[this]{
        if(processor.auditionMode.load()==0){processor.setAuditionMode(hasSource()?1:2);refreshAuditionView();}
        auto& song=processor.auditionMode.load()==2?processor.preview:processor.source;
        if(song.playing.load())song.playing=false;else song.play();
    };
    previewStop.onClick=[this]{auto& song=processor.auditionMode.load()==2?processor.preview:processor.source;song.stop();processor.restartSong=true;};
    previewSave.onClick=[this]{
        if(hasSource() && processor.auditionMode.load()!=2 && (!hasPreview() || renderStale)){saveAfterRender=true;renderLoadedSong();}
        else saveRenderedFile();
    };
    previewSave.setTooltip("Save the processed song. In Song + effects mode, changed settings are rendered from the beginning before saving.");
    previewPlay.setTooltip("Play through the host audio output. Song + effects follows your current pitch and effects; Rendered file auditions the finished result.");
    fileRestart.onClick=[this]{processor.restartAudition();};
    fileRestart.setTooltip("Restart the selected song from the beginning and clear effect tails. Rendering already starts from the beginning automatically.");
    formatLabel.setText("SAVE AS",juce::dontSendNotification);formatLabel.setFont(juce::Font(11));formatInfo.setFont(juce::Font(11));
    saveFormat.addItem("AAC / 320 kbps (.m4a)",1);saveFormat.addItem("FLAC / lossless PCM",2);
    const bool aacReady=tdFile::aacAvailable();saveFormat.setItemEnabled(1,aacReady);
    const int requested=(int)processor.apvts.state.getProperty("songSaveFormat",1);
    saveFormat.setSelectedId(requested==2 || !aacReady?2:1,juce::dontSendNotification);
    saveFormat.onChange=[this]{processor.apvts.state.setProperty("songSaveFormat",saveFormat.getSelectedId(),nullptr);};
    saveFormat.setTooltip(aacReady?"AAC targets 320 kbps. FLAC preserves compatible integer PCM; float/32-bit renders are converted to 24-bit PCM first.":"AAC needs FFmpeg on PATH. Install with winget install --id Gyan.FFmpeg -e, then restart. FLAC works without FFmpeg.");
    waveform.fileDropped=[this](const juce::File& file){if(!fileJob && !loadJob)loadSource(file);};
    waveform.canDrop=[this]{return !fileJob && !loadJob;};
    waveform.seeked=[this]{processor.restartSong=true;};refreshAuditionView();
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
    zeroButton.onClick=[this]{semitoneSlider.setValue(0,juce::sendNotificationSync);};resetButton.onClick=[this]{if(!standalone && processor.auditionMode.load()!=0)processor.restartAudition();else processor.requestBacklogReset();};resetButton.setTooltip("Jump to current music, discarding the accumulated delay.");
    rackTab.onClick=[this]{setEffectsExpanded(!showRack);};rackPanel.sizeChanged=[this]{updateSize();};
    tdUi::populate(uiSize);uiSize.setSelectedId(tdUi::valid((int)p.apvts.state.getProperty("uiScaleChoice",1)),juce::dontSendNotification);
    uiSize.onChange=[this]{processor.apvts.state.setProperty("uiScaleChoice",uiSize.getSelectedId(),nullptr);requestedScale=tdUi::requested(uiSize.getSelectedId());timerCallback();updateSize();};
    requestedScale=tdUi::requested(uiSize.getSelectedId());uiScale=requestedScale;
    importNotice.setText("Imported Mix keeps its percentage; 100% is now fully wet.",juce::dontSendNotification);importNotice.setFont(juce::Font(12));importOK.onClick=[this]{tdEffects::acknowledgeMixImport();mixExplained=true;processor.apvts.state.setProperty("rcMixNotice",false,nullptr);updateSize();};
    updateSize();timerCallback();startTimerHz(10);
}
TDSpeedVSTAudioProcessorEditor::~TDSpeedVSTAudioProcessorEditor(){processor.preview.playing=false;processor.source.playing=false;stopTimer();rackPanel.sizeChanged=nullptr;setLookAndFeel(nullptr);}
void TDSpeedVSTAudioProcessorEditor::setStandaloneMode() {
    standalone=true;processor.setListeningMode();bypassButton.setVisible(false);uiSize.setVisible(false);
    for(auto* c:std::initializer_list<juce::Component*>{&timingMode,&createGroup,&joinGroup,&leaveGroup,&syncStatus})c->setVisible(false);
    showSongPanel=(bool)processor.apvts.state.getProperty("uiSongPanelExpanded",false);if(!showSongPanel)processor.setAuditionMode(0);setUiScale(1);
}
void TDSpeedVSTAudioProcessorEditor::setFilePanelExpanded(bool expanded) {
    if(showSongPanel==expanded)return;
    showSongPanel=expanded;
    if(standalone){processor.apvts.state.setProperty("uiSongPanelExpanded",expanded,nullptr);
        if(!expanded){lastFileMode=processor.auditionMode.load();processor.setAuditionMode(0);}
        else if(hasSource() || hasPreview())processor.setAuditionMode(lastFileMode==2 && hasPreview()?2:hasSource()?1:2);
        refreshAuditionView();
    }
    updateSize();
}
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
    processor.preview.collect();processor.source.collect();
    if(loadJob && loadJob->done.load(std::memory_order_acquire)) {
        fileError=loadJob->error.isNotEmpty();
        if(fileError)renderStatus.setText(loadJob->error,juce::dontSendNotification);
        else {processor.source.install(std::move(loadJob->audio));processor.preview.clear();if(showSongPanel)processor.setAuditionMode(1);refreshAuditionView();renderStatus.setText("Loaded / press Play and adjust pitch and effects",juce::dontSendNotification);}
        loadJob.reset();updateSize();
    }
    if(fileJob) {
        if(fileJob->done.load(std::memory_order_acquire)) {
            const auto result=fileJob->result;fileError=result.error.isNotEmpty();
            if(result.error.isNotEmpty())renderStatus.setText(result.error,juce::dontSendNotification);
            else {
                processor.preview.install(std::move(fileJob->audio));if(showSongPanel)processor.setAuditionMode(2);refreshAuditionView();waveform.resetView();
                renderStatus.setText("Ready / full length rendered / drag the waveform into your DAW",juce::dontSendNotification);
            }
            fileJob.reset();cancelRender.setVisible(false);updateSize();
            if(saveAfterRender){saveAfterRender=false;if(result.error.isEmpty())saveRenderedFile();}
        } else renderStatus.setText("Rendering "+juce::String(100*fileJob->progress.load(),0)+"% / preparing waveform",juce::dontSendNotification);
    }
    if(saveJob && saveJob->done.load(std::memory_order_acquire)) {
        fileError=saveJob->error.isNotEmpty();saveNoticeUntil=juce::Time::getMillisecondCounterHiRes()+3000;
        renderStatus.setText(saveJob->error.isEmpty()?"Saved: "+saveJob->destination.getFileName():saveJob->error,juce::dontSendNotification);saveJob.reset();
    }
    const bool busy=fileJob!=nullptr || loadJob!=nullptr;
    fileRestart.setEnabled(hasSource() || hasPreview());
    if(standalone){const auto* audio=processor.preview.audio();if(!audio)audio=processor.source.audio();
        formatInfo.setText(saveFormat.getSelectedId()==1?"Up to about 2.4 MB/min":audio && (audio->floatingPoint || audio->bitsPerSample>24)?"Float/32-bit becomes 24-bit PCM":"Lossless / source PCM depth",juce::dontSendNotification);
    }
    renderButton.setEnabled(hasSource() && !busy);loadButton.setEnabled(!busy);
    previewSave.setEnabled(!busy && !saveJob && (hasSource() || hasPreview()));
    previewPlay.setEnabled(hasSource() || hasPreview());previewStop.setEnabled(hasSource() || hasPreview());
    auditionSelector.setItemEnabled(2,hasSource());auditionSelector.setItemEnabled(3,hasPreview());
    int mode=processor.auditionMode.load();
    renderStale=false;
    const auto controls=tdEffects::captureState(processor.apvts);
    const float pitch=processor.isDawMode()?processor.pendingDawPitch():processor.apvts.getRawParameterValue("semitones")->load();
    const bool changed=lastAuditionRack.isValid() && (std::abs(pitch-lastAuditionPitch)>.001f || !controls.isEquivalentTo(lastAuditionRack));
    if(const auto* rendered=processor.preview.audio();rendered && hasSource()) {
        renderStale=std::abs(pitch-rendered->semitones)>.001f || !controls.isEquivalentTo(rendered->rack);
        auditionSelector.changeItemText(3,renderStale?"Rendered (earlier)":"Rendered file");
        // Editing a baked result returns to the live song at the matching source
        // position. Choosing Rendered explicitly still allows comparison.
        if(mode==2 && changed && !busy){const bool wasPlaying=processor.preview.playing.load();const double sourceTime=processor.preview.position.load()*std::pow(2.,rendered->semitones/12.);processor.setAuditionMode(1);processor.source.seek(sourceTime);processor.source.playing=wasPlaying;mode=1;refreshAuditionView();}
    }
    lastAuditionRack=controls;lastAuditionPitch=pitch;
    auto& song=mode==2?processor.preview:processor.source;
    if(const auto* data=song.audio()) {
        const auto pos=song.position.load();const bool playing=song.playing.load();previewPlay.setButtonText(playing?"Pause":"Play");
        if(playing && pos==lastPreviewPosition)++stalledPreviewTicks;else stalledPreviewTicks=0;lastPreviewPosition=pos;
        const auto label=data->original.getFileNameWithoutExtension().substring(0,16);
        previewInfo.setText(stalledPreviewTicks>15?"Activate the DAW audio engine":label+" / "+RenderedWaveform::time(pos,true)+" / "+RenderedWaveform::time(data->duration(),true),juce::dontSendNotification);
        previewInfo.setTooltip(data->original.getFullPathName()+(mode==2?" / contains settings used at render time":" / live pitch and effects controls"));
    } else {previewPlay.setButtonText("Play");previewInfo.setText("Drop audio here or use Load song",juce::dontSendNotification);}
    if(renderStale && !busy && !saveJob && !fileError && juce::Time::getMillisecondCounterHiRes()>saveNoticeUntil)renderStatus.setText("Settings changed / Render from start updates the file",juce::dontSendNotification);
    waveform.refresh();
    if(!standalone){const auto* d=juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());if(d){const float fit=juce::jmin((d->userArea.getWidth()-32)/820.0f,(d->userArea.getHeight()-72)/(float)preferredHeight());const float scale=juce::jmin(uiSize.getSelectedId()==1?tdUi::automatic(d->userArea):requestedScale,fit);if(std::abs(scale-uiScale)>.001f){uiScale=scale;updateSize();}}}
    if(!standalone){
        auto groups=processor.availableSyncGroups();if(groups!=groupList){groupList=groups;joinGroup.clear(juce::dontSendNotification);joinGroup.addItem("Join group...",1);for(size_t i=0;i<groupList.size();++i)joinGroup.addItem(groupList[i].second,(int)i+2);}
        const auto id=processor.syncGroupId();int selected=1;for(size_t i=0;i<groupList.size();++i)if(groupList[i].first==id)selected=(int)i+2;joinGroup.setSelectedId(selected,juce::dontSendNotification);
        const bool daw=processor.isDawMode(),leader=processor.syncLeader();semitoneSlider.setEnabled(!daw || leader);zeroButton.setEnabled(!daw || leader);resetButton.setEnabled(!daw || mode!=0);
        const bool editable=daw && !processor.dawRunning();createGroup.setEnabled(editable);joinGroup.setEnabled(editable);leaveGroup.setEnabled(editable && id.isNotEmpty());
        if(daw && !leader && std::abs(processor.apvts.getRawParameterValue("semitones")->load()-processor.pendingDawPitch())>.049f)tdEffects::setParameter(processor.apvts,"semitones",processor.pendingDawPitch());
        if(daw && id.isNotEmpty() && (processor.apvts.getRawParameterValue("bypass")->load()>.5f)!=processor.groupBypassed())tdEffects::setParameter(processor.apvts,"bypass",processor.groupBypassed()?1:0);
        bypassButton.setButtonText(daw && id.isNotEmpty()?"Bypass group":"Bypass");resetButton.setButtonText(mode!=0?"Restart song":daw?"Stop/Play resets":"Re-centre buffer");
        syncStatus.setText(processor.dawStatusText()+(id.isNotEmpty()?(leader?" / leader":" / follower"):""),juce::dontSendNotification);
    }
    if(importNotice.isVisible()!=mixNotice())updateSize();
    backlog=processor.getBacklogSeconds();capacity=processor.getHistoryCapacitySeconds();bufferState=processor.getBufferStatus();
    const float semi=processor.apvts.getRawParameterValue("semitones")->load();speedLabel.setText(juce::String(std::pow(2.0f,semi/12)*100,1)+"% playback speed",juce::dontSendNotification);
    const bool bypass=processor.apvts.getRawParameterValue("bypass")->load()>.5f;
    delayLabel.setText(processor.isDawMode()?processor.dawStatusText():bypass?"Dry forwarding / buffered playback held":"Buffer delay / "+juce::String(backlog<60?backlog:backlog/60,2)+(backlog<60?" s":" min"),juce::dontSendNotification);
    if(mode!=0){delayLabel.setText(mode==2?"Rendered file / ready to drag or save":bypass?(standalone?"Enable processing above to hear pitch and effects":"Song bypassed / turn off Bypass to hear pitch and effects"):"Loaded song / live pitch and effects",juce::dontSendNotification);resetButton.setButtonText("Restart song");resetButton.setTooltip("Restart the song and clear effect tails. Rendering always starts at the beginning.");}
    if(standalone){resetButton.setButtonText("Re-centre buffer");resetButton.setEnabled(mode==0);resetButton.setTooltip(mode==0?"Jump to current music, discarding accumulated live delay.":"Live audio buffer control. Use Restart song in Song render for the loaded file.");}
    int active=processor.apvts.getRawParameterValue("revOn")->load()>.5f?1:0;for(int i=0;i<3;++i)if(processor.apvts.getRawParameterValue(tdEffects::slotId(i,"on"))->load()>.5f)++active;
    rackTab.setButtonText(juce::String(showRack?"Effects v":"Effects >")+" / "+juce::String(active)+" active");
    const float peak=processor.getOutputPeak();meter.setText(juce::String(juce::jlimit(-120.0f,30.1f,juce::Decibels::gainToDecibels(peak)),1)+" dBFS"+(peak>=1?" / CLIP":""),juce::dontSendNotification);meter.setColour(juce::Label::textColourId,peak>=1?juce::Colour(0xfff5aa70):accent);
    repaint();
}
void TDSpeedVSTAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);g.addTransform(juce::AffineTransform::scale(paintScale));g.setColour(ink);g.setFont(28);g.drawText("TDSPEED PREVIEW",28,17,410,36,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(13);g.drawText("VARISPEED / PERSONAL LISTENING",29,53,400,20,juce::Justification::centredLeft);
    if(showSongPanel){g.setColour(panel);g.fillRoundedRectangle(previewPanel.toFloat(),10);g.setColour(juce::Colour(0xffe9b66a).withAlpha(.35f));g.drawRoundedRectangle(previewPanel.toFloat().reduced(.5f),10,1);}
    for(auto r:{pitchPanel,reverbPanel,statusPanel}){g.setColour(panel);g.fillRoundedRectangle(r.toFloat(),12);g.setColour(border);g.drawRoundedRectangle(r.toFloat().reduced(.5f),12,1);}
    g.setColour(ink);g.setFont(17);g.drawText("Pitch & speed",44,pitchPanel.getY()+12,200,26,juce::Justification::centredLeft);
    g.setColour(muted);g.setFont(12);g.drawText("-12 / HALF SPEED",44,pitchPanel.getBottom()-34,170,20,juce::Justification::centredLeft);g.drawText("+12 / DOUBLE SPEED",600,pitchPanel.getBottom()-34,175,20,juce::Justification::centredRight);
    const int mode=processor.auditionMode.load();
    auto track=statusPanel.toFloat().reduced(20,0).withY((float)statusPanel.getY()+48).withHeight(5);g.setColour(border);g.fillRoundedRectangle(track,2);g.setColour(accent);
    const auto& song=mode==2?processor.preview:processor.source;
    const double fraction=mode!=0?song.position.load()/juce::jmax(.001,song.durationSeconds.load()):backlog/juce::jmax(1.f,capacity);
    g.fillRoundedRectangle(track.withWidth(track.getWidth()*(float)juce::jlimit(0.,1.,fraction)),2);
    g.setColour(muted);const auto msg=mode!=0?juce::String("File playback / Render from start always includes the whole song, regardless of this cursor."):bufferState?"Buffer limit / 3-second recovery / "+juce::String(processor.getPauseSeconds(),1)+" s remaining":"Pitch down slows the song: delay grows naturally. History limit: 3-second pause, reset and resume.";
    g.drawText(msg,statusPanel.getX()+20,statusPanel.getY()+66,statusPanel.getWidth()-40,20,juce::Justification::centredLeft);
}
void TDSpeedVSTAudioProcessorEditor::resized()
{
    paintScale=juce::jmin(uiScale,juce::jmin(getWidth()/820.0f,getHeight()/(float)preferredHeight()));
    content.setTransform(juce::AffineTransform());content.setBounds(0,0,820,preferredHeight());content.setTransform(juce::AffineTransform::scale(paintScale));
    bypassButton.setBounds(676,26,110,30);uiSize.setBounds(567,26,90,30);sizeLabel.setBounds(514,26,52,30);sizeLabel.setVisible(!standalone);
    const int waveHeight=218+(standalone?42:0);
    const int offset=(mixNotice()?28:0)+(standalone?0:76)+filePanelHeight();
    for(auto* c:std::initializer_list<juce::Component*>{&loadButton,&renderButton,&renderStatus,&waveform,&previewPlay,&previewStop,&auditionSelector,&previewSave,&previewInfo})c->setVisible(showSongPanel);
    for(auto* c:std::initializer_list<juce::Component*>{&fileRestart,&saveFormat,&formatLabel,&formatInfo})c->setVisible(standalone && showSongPanel);
    cancelRender.setVisible(showSongPanel && fileJob!=nullptr);
    importNotice.setVisible(mixNotice());importOK.setVisible(mixNotice());importNotice.setBounds(28,73+(standalone?0:76)+filePanelHeight(),690,28);importOK.setBounds(730,73+(standalone?0:76)+filePanelHeight(),56,28);
    loadButton.setBounds(28,82,148,34);renderButton.setBounds(188,82,190,34);cancelRender.setBounds(390,82,74,34);renderStatus.setBounds(fileJob?476:390,80,fileJob?310:396,38);
    previewPanel={24,126,772,waveHeight};
    previewPlay.setBounds(40,134,72,28);previewStop.setBounds(120,134,62,28);auditionSelector.setBounds(190,134,170,28);previewSave.setBounds(370,134,100,28);previewInfo.setBounds(480,131,296,34);waveform.setBounds(36,170,748,162);
    if(standalone){fileRestart.setBounds(190,134,122,28);auditionSelector.setBounds(326,134,170,28);previewInfo.setBounds(508,131,268,34);formatLabel.setBounds(40,342,68,28);saveFormat.setBounds(110,342,270,28);formatInfo.setBounds(390,342,238,28);previewSave.setBounds(646,342,130,28);}
    const int groupY=130+waveHeight;
    timingMode.setBounds(28,groupY,124,28);createGroup.setBounds(166,groupY,124,28);joinGroup.setBounds(304,groupY,284,28);leaveGroup.setBounds(602,groupY,184,28);syncStatus.setBounds(28,groupY+34,758,38);
    pitchPanel={24,86+offset,772,126};zeroButton.setBounds(636,101+offset,140,28);semitoneSlider.setBounds(40,135+offset,740,42);speedLabel.setBounds(285,178+offset,250,20);speedLabel.setJustificationType(juce::Justification::centred);
    reverbPanel={24,226+offset,772,44+(showRack?rackPanel.preferredHeight():0)};rackTab.setBounds(40,233+offset,200,30);effectsBypass.setBounds(620,233+offset,160,30);
    rackPanel.setBounds(32,270+offset,756,rackPanel.preferredHeight());
    const int y=reverbPanel.getBottom()+8;outputLabel.setBounds(40,y,100,32);output.setBounds(143,y,270,32);protect.setBounds(420,y,130,32);meter.setBounds(554,y,226,32);
    statusPanel={24,y+40,772,100};delayLabel.setBounds(40,statusPanel.getY()+10,550,30);resetButton.setBounds(612,statusPanel.getY()+12,164,28);
}

void TDSpeedVSTAudioProcessorEditor::chooseRenderSource() {
    if(fileJob || loadJob)return;
    renderChooser=std::make_unique<juce::FileChooser>("Load a song to audition and adjust",juce::File(),"*.wav;*.flac;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.ogg;*.opus");
    renderChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this)](const juce::FileChooser& chooser){
        if(safe && chooser.getResult().existsAsFile())safe->loadSource(chooser.getResult());
    });
}
void TDSpeedVSTAudioProcessorEditor::loadSource(const juce::File& file) {
    if(fileJob || loadJob)return;
    if(standalone && !showSongPanel)setFilePanelExpanded(true);
    if(!file.existsAsFile()){fileError=true;renderStatus.setText("Dropped audio is not a readable file. Use Load song instead.",juce::dontSendNotification);return;}
    fileError=false;saveNoticeUntil=0;loadJob=std::make_unique<tdFile::LoadJob>(file);renderStatus.setText("Loading song / no rendering yet",juce::dontSendNotification);
    if(!loadJob->startThread()){fileError=true;loadJob.reset();renderStatus.setText("Could not start loading",juce::dontSendNotification);}
}
void TDSpeedVSTAudioProcessorEditor::refreshAuditionView() {
    const int mode=processor.auditionMode.load();auditionSelector.setSelectedId(mode+1,juce::dontSendNotification);
    waveform.bind(mode==2?processor.preview:processor.source,mode==2);
    if(!hasSource() && !hasPreview())waveform.resetView();
}
void TDSpeedVSTAudioProcessorEditor::renderLoadedSong() {
    const auto* data=processor.source.audio();if(!data || fileJob || loadJob)return;
    tdFile::Request request;request.source=data->original;request.input=processor.source.snapshot();request.rack=tdEffects::captureState(processor.apvts);
    request.semitones=processor.isDawMode()?processor.pendingDawPitch():processor.apvts.getRawParameterValue("semitones")->load();
    startFileRender(request);
}
void TDSpeedVSTAudioProcessorEditor::startFileRender(tdFile::Request request) {
    if(fileJob)return;
    const auto cache=juce::File(juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getFullPathName())).getChildFile("TDSpeed Preview/Rendered audio");
    if(cache.createDirectory().failed()){fileError=true;renderStatus.setText("Could not create the temporary render folder",juce::dontSendNotification);return;}
    // Keep dragged files available after the editor/plugin closes: some DAWs
    // reference dropped audio instead of copying it into the project.
    request.destination=cache.getChildFile(request.source.getFileNameWithoutExtension()+" - TDSpeed - "+juce::Uuid().toString()+".wav");
    fileError=false;saveNoticeUntil=0;renderButton.setEnabled(false);cancelRender.setVisible(true);
    fileJob=std::make_unique<tdFile::Job>(request,true);
    if(!fileJob->startThread()){fileError=true;fileJob.reset();renderButton.setEnabled(true);cancelRender.setVisible(false);renderStatus.setText("Could not start render worker",juce::dontSendNotification);}
    resized();
}
void TDSpeedVSTAudioProcessorEditor::saveRenderedFile() {
    const auto* data=processor.preview.audio();if(!data || saveJob)return;
    const auto quality=standalone?(saveFormat.getSelectedId()==1?tdFile::SaveQuality::aac320:tdFile::SaveQuality::flac):tdFile::SaveQuality::original;
    auto extension=quality==tdFile::SaveQuality::aac320?juce::String(".m4a"):quality==tdFile::SaveQuality::flac?juce::String(".flac"):data->original.getFileExtension();if(extension.isEmpty())extension=".wav";
    const auto initial=data->original.getSiblingFile(data->original.getFileNameWithoutExtension()+" - TDSpeed"+extension);
    const auto rendered=data->file,original=data->original;const auto lease=processor.preview.snapshot();
    const auto filter=quality==tdFile::SaveQuality::original?juce::String("*.wav;*.flac;*.aif;*.aiff;*.mp3;*.m4a;*.aac;*.ogg;*.opus"):"*"+extension;
    const auto title=quality==tdFile::SaveQuality::aac320?"Save song / AAC 320 kbps":quality==tdFile::SaveQuality::flac?"Save song / FLAC":"Save rendered audio";
    renderChooser=std::make_unique<juce::FileChooser>(title,initial,filter);
    renderChooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<TDSpeedVSTAudioProcessorEditor>(this),rendered,original,lease,quality,extension](const juce::FileChooser& chooser) {
        if(!safe || chooser.getResult()==juce::File())return;
        auto destination=chooser.getResult();if(destination.getFileExtension().isEmpty())destination=destination.withFileExtension(extension);
        safe->fileError=false;safe->saveJob=std::make_unique<tdFile::SaveJob>(rendered,original,destination,lease,quality);
        if(!safe->saveJob->startThread()){safe->fileError=true;safe->saveJob.reset();safe->renderStatus.setText("Could not start save worker",juce::dontSendNotification);}else safe->renderStatus.setText("Saving rendered audio...",juce::dontSendNotification);
    });
}
