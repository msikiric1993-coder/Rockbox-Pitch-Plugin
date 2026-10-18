#pragma once
#include <JuceHeader.h>
#include <array>

namespace tdEffects
{
struct Control
{
    juce::String id, name, unit;
    float low, high, initial, step = 0.01f, skew = 1.0f;
};
enum class Mixing { additive, equalPower, insert };
struct Effect
{
    juce::String id, name, category, description;
    std::vector<Control> controls;
    int engine = 0;
    Mixing mixing = Mixing::insert;
    float wetGain = 1;
    bool mainPicker = false, available = true;
    juce::String unavailableReason;
};
const std::vector<Effect>& catalogue();
int effectIndex(float rawChoice);
juce::String slotId(int slot, const juce::String& suffix);
juce::String controlId(int slot, const Effect&, const Control&);
void addParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>&);
void setParameter(juce::AudioProcessorValueTreeState&, const juce::String&, float);
void resetSlot(juce::AudioProcessorValueTreeState&, int slot);
void chooseEffect(juce::AudioProcessorValueTreeState&, int slot, int effect, int hallVariant=0);
void applyPreset(juce::AudioProcessorValueTreeState&, int preset);
juce::StringArray presetNames();
void applySpaceVariant(juce::AudioProcessorValueTreeState&, int slot, int variant);
std::pair<float,float> mixGains(const Effect&, float mix, float wetLevelDb=0) noexcept;
juce::ValueTree captureState(juce::AudioProcessorValueTreeState&);
void restoreState(juce::AudioProcessorValueTreeState&, const juce::ValueTree&);
bool mixImportExplained();
void acknowledgeMixImport();

// All engines are built and allocated in prepare(), never in process().
class Rack : private juce::AsyncUpdater
{
public:
    explicit Rack(juce::AudioProcessorValueTreeState&);
    ~Rack();
    void prepare(double rate, bool offline = false);
    void beginBlock();
    void process(juce::AudioBuffer<float>&, int start, int count);
    void reset();
    float peak() const noexcept;
    bool faulted(int slot) const noexcept;
    void resetEffect(int slot);
    void loadImpulseResponse(int slot, const juce::File&);
    void reloadImpulseResponses();
    juce::String impulseStatus(int slot) const;
    double tailSeconds() const noexcept;
private:
    void handleAsyncUpdate() override;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
