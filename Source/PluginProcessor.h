#pragma once
#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <cmath>
#include "SpringVerb.h"
#include "EffectsRack.h"
#include "DawSync.h"
#include "TrackCapture.h"

class TDSpeedVSTAudioProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    static constexpr std::int64_t maxHistoryFrames = (2LL * 1024 * 1024 * 1024) / (2 * sizeof(float));
    explicit TDSpeedVSTAudioProcessor(double historySeconds = 86400.0);
    ~TDSpeedVSTAudioProcessor() override;

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { trackCapture.requestFinish(); }
    void setNonRealtime(bool offline) noexcept override {juce::AudioProcessor::setNonRealtime(offline);if(!offline)trackCapture.requestFinish();}
    TrackCapture trackCapture;
    void reset() override;
#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override;
    double getBufferedTailSeconds() const noexcept { return bufferedTailSeconds.load(std::memory_order_relaxed); }
    void prepareFileEffects(double sampleRate);
    void processFileEffects(juce::AudioBuffer<float>&);
    float getOutputPeak() const noexcept {return outputPeak.load();}

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    tdEffects::Rack effects { apvts };
    bool legacyEffectFaulted() const noexcept {return legacyFault.load();}
    void resetLegacyEffect() noexcept {legacyResetRequested.store(true);}
    void requestBacklogReset() noexcept { resetRequested.store(true); }
    float getBacklogSeconds() const noexcept { return backlogSeconds.load(); }
    float getHistoryCapacitySeconds() const noexcept { return historyCapacitySeconds.load(); }
    // 0 = running, 1 = full-history pause, 2 = caught-live-input pause.
    int getBufferStatus() const noexcept { return bufferStatus.load(); }
    float getPauseSeconds() const noexcept { return pauseSeconds.load(); }
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isDawMode() const noexcept {return pTiming->load()>.5f;}
    void setListeningMode(){tdEffects::setParameter(apvts,"timingMode",0);}
    void createSyncGroup();
    void joinSyncGroup(const juce::String&,bool leader=false);
    void leaveSyncGroup();
    juce::String syncGroupId() const;
    bool syncLeader() const noexcept;
    bool groupBypassed() const noexcept;
    float activeDawPitch() const noexcept {return dawPitch.load();}
    float pendingDawPitch() const noexcept {if(auto* g=syncGroup.load())return g->pendingPitch.load();return pSemitones->load();}
    juce::String dawStatusText() const;
    bool dawRunning() const noexcept {return dawStatus.load()==2;}
    bool dawNeedsRestart() const noexcept {return dawStatus.load()==4;}
    std::vector<std::pair<juce::String,juce::String>> availableSyncGroups(){return tdDaw::Registry::instance().list();}

private:
    void parameterChanged(const juce::String&,float) override;
    void handleAsyncUpdate() override;
    void processDaw(juce::AudioBuffer<float>&,bool hostBypass);
    void prepareEffectControls();
    bool captureOfflineInput(const juce::AudioBuffer<float>&);
    void freshDawHistory();
    tdDaw::Group solo;
    const uint64_t memberId=tdDaw::Registry::instance().nextMember.fetch_add(1);
    std::vector<std::shared_ptr<tdDaw::Group>> retainedGroups;
    std::atomic<tdDaw::Group*> syncGroup{nullptr};
    tdDaw::Group* lastDawGroup=nullptr;
    std::atomic<float>* pTiming=nullptr;
    std::atomic<int> dawStatus{1};
    std::atomic<float> dawPitch{0};
    uint64_t dawRun=0;int64_t dawExpected=INT64_MIN,dawElapsed=0;
    bool dawWasPlaying=false,dawRender=false,dawRestart=false,externalBypass=false;
    double dawRate=1;int64_t dawPlayLength=INT64_MAX;
    uint64_t pendingSeekSerial=0,pendingSeekRun=0;
    int dawMemberSlot=0;
    std::atomic<bool> dawHostReset{false};
    // ---------- Varispeed (Rockbox-like) ----------
    void applyPitchVarispeed(juce::AudioBuffer<float>& buffer);

    double   fs = 48000.0;
    int      rbSize = 0;
    int      writeIdx = 0;
    std::int64_t writtenSamples = 0;
    std::int64_t lastSignalSourceFrame = -1;
    std::atomic<double> bufferedTailSeconds { 0.0 };
    void updateBufferedTail() noexcept;
    const double requestedHistorySeconds;
    double   readIdx = 0.0;            // fractional
    float    lastEffRate = 1.0f;       // smoothed effective rate
    int      targetBacklog = 0;        // initial/reset delay in samples
    float rateSmoothing = 0.02f;
    bool resetWasHigh = false;
    bool wasBypassed = false;
    bool reverbWasOn = false;
    std::atomic<bool> legacyFault{false},legacyResetRequested{false};
    std::atomic<bool> resetRequested { false };
    std::atomic<float> backlogSeconds { 0.25f };
    std::atomic<float> historyCapacitySeconds { 0.0f };
    std::atomic<int> bufferStatus { 0 };
    int pauseReason = 0;
    std::int64_t pauseSamplesRemaining = 0;
    std::atomic<float> pauseSeconds { 0.0f };
    float blockReverbMix = 0.0f,blockLegacyCalibration=1;
    std::atomic<float>* pRevOutput=nullptr,*pRevWetLevel=nullptr;
    juce::SmoothedValue<float> legacyMix,legacyGain,legacyWetGain;
    std::atomic<float> outputPeak{0};
    juce::SmoothedValue<float> masterGain;
    void finishOutput(juce::AudioBuffer<float>&);
    std::vector<float> rbL, rbR;       // ring buffers

    void beginRecoveryPause(int reason);
    void advanceRecoveryPause();
    void resetBacklog();               // honour "Reset" button

    // ---------- Reverb ----------
    SpringVerb springVerb;
    std::atomic<float>* pRevOn = nullptr;
    std::atomic<float>* pRevMix = nullptr;
    std::atomic<float>* pRevTime = nullptr;
    std::atomic<float>* pRevTone = nullptr;
    std::atomic<float>* pRevMode = nullptr;

    // ---------- Parameters already in your GUI ----------
    std::atomic<float>* pBypass = nullptr; // 0/1
    std::atomic<float>* pSemitones = nullptr; // -12..+12
    std::atomic<float>* pMasterOutput=nullptr,*pEffectsBypass=nullptr,*pProtect=nullptr;
    std::atomic<float>* pReset = nullptr; // 0/1 momentary

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TDSpeedVSTAudioProcessor)
};
