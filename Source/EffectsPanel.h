#pragma once
#include "PluginProcessor.h"

class EffectsPanel : public juce::Component, private juce::Timer
{
public:
    explicit EffectsPanel(TDSpeedVSTAudioProcessor&,const juce::File& presetsFile={});
    bool saveNamedRack(const juce::String& name);
    ~EffectsPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;
    void setAdvanced(bool b){advanced.setToggleState(b,juce::dontSendNotification);processor.apvts.state.setProperty("uiAdvanced",b,nullptr);rebuildControls();}
    void refreshControls(){timerCallback();rebuildControls();}
    std::function<void()> sizeChanged;
private:
    void timerCallback() override;
    void selectSlot(int);
    void rebuildControls();
    void showPicker(int);
    void fillPresets();
    void saveFavourite();
    void loadIR();
    TDSpeedVSTAudioProcessor& processor;
    int selectedSlot=0,lastEffect=-1;
    bool changing=false,lastAssigned=false;juce::String lastMissing;
    std::array<juce::TextButton,3> slots;
    std::array<juce::ToggleButton,3> enabled;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,3> enabledAttachments;
    juce::ComboBox presets;
    std::array<juce::TextButton,3> pickers;
    juce::Label racksLabel;
    juce::TextButton favourite{"Save rack"},load{"Load"},reset{"Reset effect"},ir{"Load IR"};
    juce::ToggleButton limiter{"Protect peaks"};
    juce::Slider mix,level,wetLevel;
    juce::ToggleButton advanced{"Advanced"},legacyOn{"On"};
    juce::TextButton legacyCard{"Legacy reverb"};
    juce::ComboBox legacyMode;
    juce::Label wetLabel;
    bool legacyVisible=false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> legacyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wetAttachment;
    juce::Label mixLabel,levelLabel,description;
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment,levelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limiterAttachment;
    juce::ValueTree favourites{"FAVOURITES"};
    juce::File favouritesFile;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> naming;
};
