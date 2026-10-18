#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "EffectsPanel.h"
#include "UiScale.h"

class TDSpeedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TDSpeedLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};

class TDSpeedVSTAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TDSpeedVSTAudioProcessorEditor(TDSpeedVSTAudioProcessor&);
    ~TDSpeedVSTAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setEffectsExpanded(bool b){showRack=b;updateSize();}
    bool effectsFit() const {return !showRack || content.getLocalBounds().contains(rackPanel.getBounds());}
    void setStandaloneMode(){standalone=true;processor.setListeningMode();bypassButton.setVisible(false);uiSize.setVisible(false);for(auto* c:std::initializer_list<juce::Component*>{&timingMode,&createGroup,&joinGroup,&leaveGroup,&syncStatus})c->setVisible(false);setUiScale(1);}
    void setUiScale(float scale){requestedScale=uiScale=scale;updateSize();}
    void setAdvanced(bool b){rackPanel.setAdvanced(b);}
    void refreshEffects(){rackPanel.refreshControls();}
    bool mixNotice() const {return !standalone && !mixExplained && (bool)processor.apvts.state.getProperty("rcMixNotice",false);}
    int preferredHeight() const {return 420+(standalone?0:86)+(mixNotice()?28:0)+(showRack?rackPanel.preferredHeight():0);}
    std::function<void()> sizeChanged;
private:
    void timerCallback() override;
    void updateSize();
    TDSpeedVSTAudioProcessor& processor;
    TDSpeedLookAndFeel theme;
    juce::TooltipWindow tooltips{this,600};
    juce::Component content;
    EffectsPanel rackPanel;
    juce::ToggleButton bypassButton{"Bypass"},effectsBypass{"Bypass effects"},protect{"Protect peaks"};
    juce::TextButton resetButton{"Re-centre buffer"},zeroButton{"Original pitch"},rackTab{"Effects >"};
    juce::Slider semitoneSlider,output;
    juce::Label speedLabel,delayLabel,outputLabel,meter,importNotice;
    juce::TextButton importOK{"OK"};
    juce::ComboBox uiSize;
    juce::ComboBox timingMode,joinGroup;
    juce::TextButton createGroup{"Create group"},leaveGroup{"Leave group"};
    juce::Label syncStatus;
    std::vector<std::pair<juce::String,juce::String>> groupList;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> timingAttachment;
    juce::Rectangle<int> pitchPanel,reverbPanel,statusPanel;
    bool showRack=false,standalone=false,mixExplained=tdEffects::mixImportExplained();float uiScale=1,paintScale=1,requestedScale=1,backlog=.25f,capacity=0;int bufferState=0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> semitoneAtt,outputAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt,effectsBypassAtt,protectAtt;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TDSpeedVSTAudioProcessorEditor)
};
