#include "EffectsPanel.h"

EffectsPanel::EffectsPanel(TDSpeedVSTAudioProcessor& p,const juce::File& presetsFile):processor(p)
{
    favouritesFile=presetsFile==juce::File()?juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("TDSpeed Preview").getChildFile("EffectsPresets.xml"):presetsFile;
    if(presetsFile==juce::File()){
        const auto marker=favouritesFile.getSiblingFile("racks-import-v1.txt");if(!marker.existsAsFile()){
            favouritesFile.getParentDirectory().createDirectory();auto old=favouritesFile.getParentDirectory().getSiblingFile("TDSpeed RC").getChildFile("EffectsPresets.xml");if(!old.existsAsFile())old=favouritesFile.getParentDirectory().getSiblingFile("TDSpeed").getChildFile("EffectsPresets.xml");
            if(favouritesFile.existsAsFile() || !old.existsAsFile() || old.copyFileTo(favouritesFile))marker.replaceWithText("Copied favourites once. Imported percentages adopt true RC Mix; originals are unchanged.");
        }
    }
    if(auto xml=juce::XmlDocument::parse(favouritesFile)){auto loaded=juce::ValueTree::fromXml(*xml);if(loaded.hasType("FAVOURITES"))favourites=loaded;}
    for(int i=0;i<3;++i)
    {
        slots[(size_t)i].setButtonText("FX "+juce::String(i+1));addAndMakeVisible(slots[(size_t)i]);
        slots[(size_t)i].onClick=[this,i]{selectSlot(i);};
        pickers[(size_t)i].setButtonText("Choose...");addAndMakeVisible(pickers[(size_t)i]);pickers[(size_t)i].onClick=[this,i]{selectSlot(i);showPicker(i);};
        enabled[(size_t)i].setButtonText("On");addAndMakeVisible(enabled[(size_t)i]);
        enabledAttachments[(size_t)i]=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,tdEffects::slotId(i,"on"),enabled[(size_t)i]);
    }
    for(juce::Component* component:std::initializer_list<juce::Component*>{&racksLabel,&presets,&favourite,&load,&reset,&ir,&limiter,&mix,&level,&mixLabel,&levelLabel,&description,&advanced,&wetLevel,&wetLabel,&legacyCard,&legacyOn,&legacyMode})addAndMakeVisible(component);
    legacyAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"revOn",legacyOn);
    legacyMode.addItem("Spring",1);legacyMode.addItem("PSX",2);
    modeAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts,"revMode",legacyMode);
    legacyCard.onClick=[this]{selectSlot(-1);};
    advanced.setToggleState((bool)p.apvts.state.getProperty("uiAdvanced",false),juce::dontSendNotification);
    advanced.onClick=[this]{processor.apvts.state.setProperty("uiAdvanced",advanced.getToggleState(),nullptr);rebuildControls();};
    wetLevel.setSliderStyle(juce::Slider::LinearHorizontal);wetLevel.setTextBoxStyle(juce::Slider::TextBoxRight,false,75,24);wetLevel.setTextValueSuffix(" dB");wetLevel.setDoubleClickReturnValue(true,0);
    wetLabel.setText("WET LEVEL",juce::dontSendNotification);
    presets.onChange=[this]
    {
        const int id=presets.getSelectedId();if(changing || id<=0)return;
        if(id<100)tdEffects::applyPreset(processor.apvts,id-1);
        else tdEffects::restoreState(processor.apvts,favourites.getChild(id-100));
        processor.effects.reloadImpulseResponses();selectSlot(selectedSlot);
        if(id>=100 && (int)favourites.getChild(id-100).getProperty("version",0)<3){
            const auto marker=favouritesFile.getSiblingFile("rc-mix-explained-v1.txt");if(!marker.existsAsFile()){
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Mix restored","Imported percentages keep their numbers. RC Mix is true dry/wet: 0% is dry, 100% is fully processed. The audible balance can change.","OK",this);marker.replaceWithText("RC Mix import message shown once.");
            }
        }
    };
    favourite.onClick=[this]{saveFavourite();};
    load.onClick=[this]{if(presets.getSelectedId()>0)presets.onChange();else description.setText("Choose a saved rack first.",juce::dontSendNotification);};
    load.setTooltip("Recall the selected rack, including after editing its controls.");
    reset.onClick=[this]{if(selectedSlot<0)processor.resetLegacyEffect();else {tdEffects::resetSlot(processor.apvts,selectedSlot);processor.effects.resetEffect(selectedSlot);}rebuildControls();};
    ir.onClick=[this]{loadIR();};
    limiterAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts,"rackLimiter",limiter);
    mix.setSliderStyle(juce::Slider::LinearHorizontal);mix.setTextBoxStyle(juce::Slider::TextBoxRight,false,65,24);mix.setTextValueSuffix(" %");mix.setDoubleClickReturnValue(true,25);
    level.setSliderStyle(juce::Slider::LinearHorizontal);level.setTextBoxStyle(juce::Slider::TextBoxRight,false,65,24);level.setTextValueSuffix(" dB");level.setDoubleClickReturnValue(true,0);
    mixLabel.setText("MIX",juce::dontSendNotification);levelLabel.setText("SLOT OUTPUT",juce::dontSendNotification);
    description.setFont(juce::Font(11));description.setColour(juce::Label::textColourId,juce::Colour(0xff9aafb5));
    racksLabel.setText("SAVED RACKS",juce::dontSendNotification);presets.setTextWhenNothingSelected("Whole chain / favourites");
    fillPresets();selectSlot(0);timerCallback();startTimerHz(10);
}
EffectsPanel::~EffectsPanel(){stopTimer();}
void EffectsPanel::fillPresets()
{
    changing=true;presets.clear(juce::dontSendNotification);const auto names=tdEffects::presetNames();for(int i=0;i<names.size();++i)if(i!=1 && i!=2)presets.addItem(names[i],i+1);
    if(favourites.getNumChildren()>0){presets.addSeparator();for(int i=0;i<favourites.getNumChildren();++i)presets.addItem(favourites.getChild(i).getProperty("name","My rack").toString(),100+i);}
    changing=false;
}
void EffectsPanel::showPicker(int slot)
{
    juce::PopupMenu menu;
    const auto& list=tdEffects::catalogue();
    for(const auto& group:juce::StringArray{"Reverb","Space","Modulation","Delay","Tone","Colour","Dynamics","Advanced"})
    {
        juce::PopupMenu sub;
        for(size_t i=0;i<list.size();++i)if((group=="Advanced"?!list[i].mainPicker:list[i].category==group && list[i].mainPicker))
        {
            if(list[i].id=="hall"){juce::PopupMenu variants;variants.addItem((int)i+1,"Default");variants.addItem(1001,"Warm hall");variants.addItem(1002,"Wide hall");sub.addSubMenu("Smooth hall",variants);}
            else sub.addItem((int)i+1,list[i].name,list[i].available);
        }
        if(group=="Space"){juce::PopupMenu variants;for(int i=0;i<4;++i)variants.addItem(2001+i,juce::StringArray{"Deep Space","Nebula","Universe","Dark Cloud"}[i]);sub.addSubMenu("Slot variants",variants);}
        if(sub.getNumItems()>0)menu.addSubMenu(group,sub);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pickers[(size_t)slot]),[safe=juce::Component::SafePointer<EffectsPanel>(this),slot](int id)
    {
        if(!safe || id==0)return;
        auto& state=safe->processor.apvts;
        if(id>=2001)tdEffects::applySpaceVariant(state,slot,id-2001);else tdEffects::chooseEffect(state,slot,id>=1001?0:id-1,id>=1001?id-1000:0);
        safe->selectSlot(slot);
    });
}
void EffectsPanel::selectSlot(int slot)
{
    selectedSlot=slot;lastEffect=-1;mixAttachment.reset();levelAttachment.reset();wetAttachment.reset();
    mixAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,slot<0?juce::String("revMix"):tdEffects::slotId(slot,"mix"),mix);
    levelAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,slot<0?juce::String("revOutput"):tdEffects::slotId(slot,"level"),level);
    for(int i=0;i<3;++i)slots[(size_t)i].setColour(juce::TextButton::buttonColourId,i==slot?juce::Colour(0xff315d4c):juce::Colour(0xff304047));
    wetAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,slot<0?juce::String("revWetLevel"):tdEffects::slotId(slot,"wetLevel"),wetLevel);
    legacyCard.setColour(juce::TextButton::buttonColourId,slot<0?juce::Colour(0xff315d4c):juce::Colour(0xff304047));
    rebuildControls();
}
void EffectsPanel::rebuildControls()
{
    knobs.clear();lastMissing=selectedSlot<0?juce::String():processor.apvts.state.getProperty(tdEffects::slotId(selectedSlot,"missingEngine")).toString();const auto& effects=tdEffects::catalogue();
    level.setVisible(true);levelLabel.setVisible(true);
    legacyMode.setVisible(selectedSlot<0);ir.setVisible(false);wetLevel.setVisible(false);wetLabel.setVisible(false);
    if(selectedSlot<0){
        lastEffect=-2;lastAssigned=true;mix.setEnabled(true);level.setEnabled(true);description.setText("Legacy reverb / first in the chain. 100% Mix is fully wet.",juce::dontSendNotification);
        for(const auto& c:std::vector<tdEffects::Control>{{"revTime","Decay"," s",.1f,4,1.5f},{"revTone","Tone"," Hz",1000,12000,6000}}){
            auto k=std::make_unique<Knob>();k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,94,22);k->slider.setTextValueSuffix(c.unit);k->label.setText(c.name,juce::dontSendNotification);k->label.setJustificationType(juce::Justification::centred);addAndMakeVisible(k->slider);addAndMakeVisible(k->label);k->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,c.id,k->slider);knobs.push_back(std::move(k));
        }
        wetLevel.setVisible(advanced.getToggleState());wetLabel.setVisible(advanced.getToggleState());resized();if(sizeChanged)sizeChanged();return;
    }
    lastEffect=tdEffects::effectIndex(processor.apvts.getRawParameterValue(tdEffects::slotId(selectedSlot,"effect"))->load());
    const auto& selected=effects[(size_t)lastEffect];
    lastAssigned=(bool)processor.apvts.state.getProperty(tdEffects::slotId(selectedSlot,"assigned"),false) || processor.apvts.getRawParameterValue(tdEffects::slotId(selectedSlot,"on"))->load()>0.5f;
    mix.setEnabled(lastAssigned);level.setEnabled(lastAssigned);
    if(!lastAssigned){description.setText("Choose Add effect in this slot to begin.",juce::dontSendNotification);ir.setVisible(false);resized();repaint();if(sizeChanged)sizeChanged();return;}
    const auto missing=processor.apvts.state.getProperty(tdEffects::slotId(selectedSlot,"missingEngine")).toString();
    if(missing.isNotEmpty()){description.setText("Unavailable saved engine: "+missing+" / bypassed. Choose another effect to replace it.",juce::dontSendNotification);mix.setEnabled(false);level.setEnabled(false);resized();if(sizeChanged)sizeChanged();return;}
    mixLabel.setText("MIX",juce::dontSendNotification);
    description.setText((selected.available?selected.description:"Unavailable / dry bypass: "+selected.unavailableReason)+" Effect changes briefly mute this slot (10 ms).",juce::dontSendNotification);
    for(const auto& control:selected.controls)
    {
        auto knob=std::make_unique<Knob>();knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,94,22);knob->slider.setTextValueSuffix(control.unit);
        knob->slider.setDoubleClickReturnValue(true,control.initial);knob->slider.setTooltip(control.name+" / double-click to reset");
        knob->label.setText(control.name,juce::dontSendNotification);knob->label.setFont(juce::Font(12));knob->label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(knob->slider);addAndMakeVisible(knob->label);
        knob->attachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts,tdEffects::controlId(selectedSlot,selected,control),knob->slider);
        knobs.push_back(std::move(knob));
    }
    ir.setVisible(selected.engine==60);
    const bool wet=advanced.getToggleState() && (selected.category=="Reverb" || selected.category=="Space" || selected.category=="Delay");wetLevel.setVisible(wet);wetLabel.setVisible(wet);
    resized();repaint();if(sizeChanged)sizeChanged();
}
void EffectsPanel::timerCallback()
{
    const auto& effects=tdEffects::catalogue();
    const bool legacy=processor.apvts.getRawParameterValue("revOn")->load()>.5f;
    if(legacy!=legacyVisible){legacyVisible=legacy;legacyCard.setVisible(legacy);legacyOn.setVisible(legacy);if(!legacy && selectedSlot<0)selectSlot(0);resized();if(sizeChanged)sizeChanged();}
    if(selectedSlot>=0){
        const bool assigned=(bool)processor.apvts.state.getProperty(tdEffects::slotId(selectedSlot,"assigned"),false) || processor.apvts.getRawParameterValue(tdEffects::slotId(selectedSlot,"on"))->load()>.5f;
        if(tdEffects::effectIndex(processor.apvts.getRawParameterValue(tdEffects::slotId(selectedSlot,"effect"))->load())!=lastEffect || assigned!=lastAssigned || processor.apvts.state.getProperty(tdEffects::slotId(selectedSlot,"missingEngine")).toString()!=lastMissing)rebuildControls();
    }
    for(int i=0;i<3;++i)
    {
        const int selected=tdEffects::effectIndex(processor.apvts.getRawParameterValue(tdEffects::slotId(i,"effect"))->load());
        const bool on=processor.apvts.getRawParameterValue(tdEffects::slotId(i,"on"))->load()>0.5f;
        if(on)processor.apvts.state.setProperty(tdEffects::slotId(i,"assigned"),true,nullptr);
        enabled[(size_t)i].setEnabled(processor.apvts.state.getProperty(tdEffects::slotId(i,"missingEngine")).toString().isEmpty());
        const bool empty=!(bool)processor.apvts.state.getProperty(tdEffects::slotId(i,"assigned"),false) && !on;
        slots[(size_t)i].setButtonText(juce::String(i+1)+" / "+(processor.apvts.state.getProperty(tdEffects::slotId(i,"missingEngine")).toString().isNotEmpty()?"Unavailable: "+processor.apvts.state.getProperty(tdEffects::slotId(i,"missingEngine")).toString():empty?juce::String("Empty slot"):effects[(size_t)selected].name)+(processor.apvts.state.getProperty(tdEffects::slotId(i,"variant")).toString().isNotEmpty()?" / "+processor.apvts.state.getProperty(tdEffects::slotId(i,"variant")).toString():juce::String())+(processor.effects.faulted(i)?" / FAULT":""));
        pickers[(size_t)i].setButtonText(empty?"Add effect...":"Change effect...");
        slots[(size_t)i].setTooltip(effects[(size_t)selected].name);
    }
    if(selectedSlot>=0 && lastEffect>=0 && effects[(size_t)lastEffect].engine==60)description.setText(processor.effects.impulseStatus(selectedSlot),juce::dontSendNotification);
    if(selectedSlot<0 && processor.legacyEffectFaulted())description.setText("Legacy effect fault / dry bypass. Press Reset effect.",juce::dontSendNotification);
    if(processor.effects.faulted(selectedSlot))description.setText("Effect fault / dry bypass. Press Reset effect to recover.",juce::dontSendNotification);
    repaint();
}
bool EffectsPanel::saveNamedRack(const juce::String& suppliedName)
{
    const auto name=suppliedName.trim().substring(0,80);
    if(name.isEmpty()){description.setText("Enter a name for your rack.",juce::dontSendNotification);return false;}
    auto state=tdEffects::captureState(processor.apvts);state.setProperty("name",name,nullptr);
    auto candidate=favourites.createCopy();int index=candidate.getNumChildren();
    for(int i=0;i<candidate.getNumChildren();++i)if(candidate.getChild(i).getProperty("name").toString()==name){index=i;candidate.removeChild(i,nullptr);break;}
    candidate.addChild(state,index,nullptr);
    if(favouritesFile.getParentDirectory().createDirectory().failed())
    {description.setText("Could not create the saved-racks folder.",juce::dontSendNotification);return false;}
    juce::TemporaryFile temp(favouritesFile);
    if(!temp.getFile().replaceWithText(candidate.toXmlString()) || !temp.overwriteTargetFileWithTemporary())
    {description.setText("Could not save rack to disk. Your saved racks were kept.",juce::dontSendNotification);return false;}
    favourites=candidate;fillPresets();presets.setSelectedId(100+index,juce::dontSendNotification);
    description.setText("Saved rack: "+name,juce::dontSendNotification);return true;
}
void EffectsPanel::saveFavourite()
{
    if(naming && naming->isCurrentlyModal()){naming->toFront(true);return;}
    naming=std::make_unique<juce::AlertWindow>("Save rack","Name this combination of effects and settings. Saving an existing name updates that rack.",juce::AlertWindow::NoIcon,this);
    naming->setLookAndFeel(&getLookAndFeel());
    naming->addTextEditor("name",presets.getSelectedId()>=100?favourites.getChild(presets.getSelectedId()-100).getProperty("name").toString():"My rack "+juce::String(favourites.getNumChildren()+1));
    naming->addButton("Save",1,juce::KeyPress(juce::KeyPress::returnKey));naming->addButton("Cancel",0,juce::KeyPress(juce::KeyPress::escapeKey));
    // Entering modal state alone does not make a manually owned AlertWindow visible.
    naming->setVisible(true);naming->toFront(true);
    naming->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<EffectsPanel>(this)](int result)
    {
        if(!safe || !safe->naming)return;
        const auto name=safe->naming->getTextEditorContents("name");safe->naming->setVisible(false);
        if(result==1)safe->saveNamedRack(name);
    }),false);
}
void EffectsPanel::loadIR()
{
    const int slot=selectedSlot;
    chooser=std::make_unique<juce::FileChooser>("Choose an impulse response",juce::File(),"*.wav;*.aiff;*.aif");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<EffectsPanel>(this),slot](const juce::FileChooser& choice)
    {
        if(!safe)return;const auto file=choice.getResult();if(!file.existsAsFile())return;
        juce::AudioFormatManager formats;formats.registerBasicFormats();auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
        if(!reader || reader->lengthInSamples<=0 || reader->lengthInSamples>reader->sampleRate*20)
        {safe->description.setText("Choose a valid impulse response of up to 20 seconds.",juce::dontSendNotification);return;}
        safe->processor.effects.loadImpulseResponse(slot,file);safe->description.setText("IR: "+file.getFileName(),juce::dontSendNotification);
    });
}
void EffectsPanel::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff9aafb5));g.setFont(11);
    const float peak=processor.effects.peak();
    g.drawText(peak>0?"Peak "+juce::String(juce::jlimit(-120.0f,30.0f,juce::Decibels::gainToDecibels(std::isfinite(peak)?peak:32.0f)),1)+" dBFS":"Rack / silent",getWidth()-158,100,152,22,juce::Justification::centredRight);
    if(peak>1){g.setColour(juce::Colour(0xfff5aa70));g.drawText("CLIP / lower output",getWidth()-320,103,158,18,juce::Justification::centredRight);}
}
int EffectsPanel::preferredHeight() const
{
    const int rows=knobs.empty()?0:((int)knobs.size()+3)/4;
    return 188+(legacyVisible?34:0)+rows*116+(wetLevel.isVisible()?40:0);
}
void EffectsPanel::resized()
{
    auto area=getLocalBounds().reduced(8);auto row=area.removeFromTop(legacyVisible?30:0);legacyOn.setBounds(row.removeFromRight(46));legacyCard.setBounds(row);if(legacyVisible)area.removeFromTop(4);
    row=area.removeFromTop(58);const int width=row.getWidth()/3;
    for(int i=0;i<3;++i){auto cell=row.removeFromLeft(width).reduced(2,0);auto head=cell.removeFromTop(28);enabled[(size_t)i].setBounds(head.removeFromRight(46));slots[(size_t)i].setBounds(head);cell.removeFromTop(4);pickers[(size_t)i].setBounds(cell);}
    area.removeFromTop(6);row=area.removeFromTop(28);racksLabel.setBounds(row.removeFromLeft(92));presets.setBounds(row.removeFromLeft(220));row.removeFromLeft(6);load.setBounds(row.removeFromLeft(60));row.removeFromLeft(6);favourite.setBounds(row.removeFromLeft(90));row.removeFromLeft(6);reset.setBounds(row.removeFromLeft(110));
    area.removeFromTop(4);row=area.removeFromTop(24);advanced.setBounds(row.removeFromLeft(100));ir.setBounds(row.removeFromLeft(90));legacyMode.setBounds(row.removeFromLeft(120));
    const int count=(int)knobs.size(),rows=(count+3)/4,columns=rows?juce::jmin(4,count):1;
    auto controls=area.removeFromTop(rows*116);
    for(int i=0;i<count;++i){auto cell=juce::Rectangle<int>(controls.getX()+(i%columns)*controls.getWidth()/columns,controls.getY()+(i/columns)*116,controls.getWidth()/columns,116);knobs[(size_t)i]->label.setBounds(cell.removeFromTop(18));knobs[(size_t)i]->slider.setBounds(cell);}
    row=area.removeFromTop(36);auto wet=row.removeFromLeft(row.getWidth()/2);mixLabel.setBounds(wet.removeFromLeft(50));mix.setBounds(wet.reduced(3,0));levelLabel.setBounds(row.removeFromLeft(92));level.setBounds(row.reduced(3,0));
    if(wetLevel.isVisible()){row=area.removeFromTop(40);wetLabel.setBounds(row.removeFromLeft(100));wetLevel.setBounds(row.removeFromLeft(280));}
    description.setBounds(area.removeFromTop(24));limiter.setVisible(false); // Peak protection lives beside the final output.
}
