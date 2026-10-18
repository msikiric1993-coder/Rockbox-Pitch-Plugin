#include "PluginProcessor.h"
#include "PluginEditor.h"

// 4-point Catmull-Rom (Hermite) interpolation on a ring buffer
static inline float hermite4(const std::vector<float>& buf, double pos)
{
    const int N = (int)buf.size();
    int i1 = (int)std::floor(pos);
    float frac = (float)(pos - (double)i1);

    auto at = [N, &buf](int i) -> float
        {
            i %= N; if (i < 0) i += N; return buf[(size_t)i];
        };

    const float xm1 = at(i1 - 1);
    const float x0 = at(i1 + 0);
    const float x1 = at(i1 + 1);
    const float x2 = at(i1 + 2);

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

static float dawHermite(const std::vector<float>& b,double absolute,int64_t written) noexcept {
    const int64_t i=(int64_t)std::floor(absolute);const float f=(float)(absolute-i);
    auto at=[&](int64_t n){if(n<0 || n>=written || n<written-(int64_t)b.size())return 0.0f;return b[(size_t)(n%(int64_t)b.size())];};
    const float a=at(i-1),c=at(i),d=at(i+1),e=at(i+2);
    return ((.5f*(e-a)+1.5f*(c-d))*f+(a-2.5f*c+2*d-.5f*e))*f*f+.5f*(d-a)*f+c;
}

//==============================================================================

TDSpeedVSTAudioProcessor::TDSpeedVSTAudioProcessor(double historySeconds)
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createParameterLayout()),
    requestedHistorySeconds(juce::jlimit(2.0, 86400.0, historySeconds))
{
    solo.activeMask=1;solo.memberKeys[0]=memberId;
    historyCapacitySeconds.store(static_cast<float>(std::min((double)maxHistoryFrames, fs * requestedHistorySeconds) / fs));
    // cache parameter pointers
    pBypass = apvts.getRawParameterValue("bypass");
    pTiming=apvts.getRawParameterValue("timingMode");
    apvts.addParameterListener("semitones",this);apvts.addParameterListener("bypass",this);
    pSemitones = apvts.getRawParameterValue("semitones");
    pReset = apvts.getRawParameterValue("reset");

    pRevOn = apvts.getRawParameterValue("revOn");
    pRevMix = apvts.getRawParameterValue("revMix");
    pRevTime = apvts.getRawParameterValue("revTime");
    pRevTone = apvts.getRawParameterValue("revTone");
    pRevMode = apvts.getRawParameterValue("revMode");
    pMasterOutput=apvts.getRawParameterValue("masterOutput");pEffectsBypass=apvts.getRawParameterValue("rackBypass");pProtect=apvts.getRawParameterValue("rackLimiter");pRevOutput=apvts.getRawParameterValue("revOutput");pRevWetLevel=apvts.getRawParameterValue("revWetLevel");
}

TDSpeedVSTAudioProcessor::~TDSpeedVSTAudioProcessor() {cancelPendingUpdate();if(externalBypass && lastDawGroup)lastDawGroup->externalBypasses.fetch_sub(1);apvts.removeParameterListener("semitones",this);apvts.removeParameterListener("bypass",this);leaveSyncGroup();}

void TDSpeedVSTAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // prepareToPlay can be repeated while a host bypass is active. Release
    // its old group vote before clearing the audio-thread ownership pointer.
    if (externalBypass && lastDawGroup) lastDawGroup->externalBypasses.fetch_sub(1);
    externalBypass = false;
    fs = sampleRate;
    dawWasPlaying=false;dawExpected=INT64_MIN;dawRun=0;dawRestart=false;pendingSeekSerial=0;lastDawGroup=nullptr;solo.run=0;solo.highWater=INT64_MIN;
    masterGain.reset(sampleRate,.02);masterGain.setCurrentAndTargetValue(1);
    legacyMix.reset(sampleRate,.02);legacyGain.reset(sampleRate,.02);legacyWetGain.reset(sampleRate,.02);legacyMix.setCurrentAndTargetValue(.25f);legacyGain.setCurrentAndTargetValue(1);legacyWetGain.setCurrentAndTargetValue(1);
    ignoreUnused(samplesPerBlock);

    // ---- ring buffer for varispeed ----
    // Allocate outside the audio callback. Cap stereo float history at 2 GiB.
    // On a limit, wait three seconds before discarding history and restarting.
    rbSize = (int)std::min(maxHistoryFrames, (std::int64_t)std::ceil(fs * requestedHistorySeconds));
    historyCapacitySeconds.store(static_cast<float>(rbSize / fs));
    rbL.assign((size_t)rbSize, 0.0f);
    rbR.assign((size_t)rbSize, 0.0f);
    writeIdx = 0;
    writtenSamples = 0;
    lastSignalSourceFrame = -1;
    bufferedTailSeconds.store(0.0, std::memory_order_relaxed);
    lastEffRate = std::pow(2.0f, pSemitones->load() / 12.0f);
    rateSmoothing = 1.0f - std::exp(-1.0f / static_cast<float>(fs * 0.001));
    resetWasHigh = false;
    wasBypassed = false;
    reverbWasOn = false;
    resetRequested.store(false);
    pauseReason = 0;
    pauseSamplesRemaining = 0;
    pauseSeconds.store(0.0f);
    bufferStatus.store(0);
    backlogSeconds.store(0.25f);

    // Preserve the original 250 ms starting offset.
    targetBacklog = (int)std::round(0.25 * fs);
    readIdx = -(double)targetBacklog; // absolute source position, including startup silence

    // ---- reverb ----
    legacyFault.store(false);legacyResetRequested.store(false);
    springVerb.prepare(fs, samplesPerBlock);
    effects.prepare(fs);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool TDSpeedVSTAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
#endif

void TDSpeedVSTAudioProcessor::resetBacklog()
{
    // Manual reset or the end of a three-second recovery pause.
    readIdx = (double)writtenSamples - targetBacklog;
    pauseReason = 0;
    pauseSamplesRemaining = 0;
    pauseSeconds.store(0.0f);
    bufferStatus.store(0);
    backlogSeconds.store(0.25f);
}

void TDSpeedVSTAudioProcessor::beginRecoveryPause(int reason)
{
    pauseReason = reason;
    pauseSamplesRemaining = (std::int64_t)std::llround(3.0 * fs);
    springVerb.reset(); // Reverb tails must not fill the requested silent pause.
    effects.reset();
}

void TDSpeedVSTAudioProcessor::advanceRecoveryPause()
{
    if (pauseSamplesRemaining > 0 && --pauseSamplesRemaining == 0)
        resetBacklog();
}

void TDSpeedVSTAudioProcessor::applyPitchVarispeed(juce::AudioBuffer<float>& buffer)
{
    const int numCh = juce::jmin(2, buffer.getNumChannels());
    const int N = buffer.getNumSamples();
    if (numCh == 0 || N <= 0 || rbSize <= 0) return;

    const float paramSemi = pSemitones ? pSemitones->load() : 0.0f;
    const float targetRate = std::pow(2.0f, juce::jlimit(-12.0f,12.0f,std::isfinite(paramSemi)?paramSemi:0.0f) / 12.0f);

    auto* inL = buffer.getReadPointer(0);
    auto* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inL;
    auto* outL = buffer.getWritePointer(0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outL;
    int rackStart = 0;
    auto flushRack = [&](int end) { effects.process(buffer, rackStart, end-rackStart); rackStart=end; };

    // This period is invariant for the block, including the recovery pause.
    const int64_t dawPeriod = dawPlayLength == INT64_MAX ? INT64_MAX
        : dawPlayLength + (int64_t)std::llround(3 * fs);
    for (int n = 0; n < N; ++n)
    {
        if(dawRender){
            const int64_t elapsed=dawElapsed+n;
            const int64_t period=dawPeriod;
            const int64_t within=period==INT64_MAX?elapsed:elapsed%period;
            const int64_t segment=elapsed-within;
            const bool recovering=within>=dawPlayLength;
            if(recovering && pauseReason==0){flushRack(n);springVerb.reset();effects.reset();}
            pauseReason=recovering?(dawRate<1?1:2):0;
            pauseSamplesRemaining=recovering?period-within:0;
            readIdx=segment+dawRate*within-targetBacklog;
            writeIdx=(int)(elapsed%rbSize);writtenSamples=elapsed;
        }
        if (!dawRender && pauseReason == 0 && (double)writtenSamples - readIdx >= rbSize - 4)
        {
            flushRack(n);
            beginRecoveryPause(1);
        }

        // Capture continues during recovery; the restart reads fresh history.
        if (inL[n] != 0.0f || inR[n] != 0.0f) lastSignalSourceFrame = writtenSamples;
        rbL[(size_t)writeIdx] = inL[n];
        rbR[(size_t)writeIdx] = inR[n];
        if (++writeIdx == rbSize) writeIdx = 0;
        ++writtenSamples;
        if(dawRender)lastEffRate=(float)dawRate;else lastEffRate += rateSmoothing * (targetRate - lastEffRate);

        // Hermite needs two future neighbours. Catching live input uses the
        // same three-second recovery policy as exhausting history storage.
        if (!dawRender && pauseReason == 0 && readIdx >= 0.0 && std::floor(readIdx) + 2 >= writtenSamples)
        {
            flushRack(n);
            beginRecoveryPause(2);
        }
        if (pauseReason != 0)
        {
            outL[n] = outR[n] = 0.0f;
            if(!dawRender)advanceRecoveryPause();
            rackStart=n+1;
            continue;
        }
        if (readIdx < 0.0)
            outL[n] = outR[n] = 0.0f;
        else
        {
            const double ringPosition = dawRender ? 0.0 : std::fmod(readIdx, (double)rbSize);
            outL[n] = dawRender?dawHermite(rbL,readIdx,writtenSamples):hermite4(rbL, ringPosition);
            outR[n] = dawRender?dawHermite(rbR,readIdx,writtenSamples):hermite4(rbR, ringPosition);
        }
        if (reverbWasOn && !legacyFault.load(std::memory_order_relaxed) && pEffectsBypass->load()<0.5f)
        {
            float* channels[2] { outL + n, outR + n };
            const float dryL=outL[n],dryR=outR[n];
            springVerb.process(channels, numCh, 1, 1);
            if(!std::isfinite(outL[n]) || !std::isfinite(outR[n]) || std::abs(outL[n])>32 || std::abs(outR[n])>32)
            {legacyFault.store(true,std::memory_order_relaxed);outL[n]=dryL;outR[n]=dryR;}
            else {const float mix=legacyMix.getNextValue(),gain=legacyGain.getNextValue(),wet=legacyWetGain.getNextValue()*blockLegacyCalibration;const float d=mix==1?0:std::cos(mix*juce::MathConstants<float>::halfPi),w=mix==0?0:wet*std::sin(mix*juce::MathConstants<float>::halfPi);outL[n]=mix==0 && gain==1?dryL:(dryL*d+outL[n]*w)*gain;if(numCh>1)outR[n]=mix==0 && gain==1?dryR:(dryR*d+outR[n]*w)*gain;
                if(!std::isfinite(outL[n]) || !std::isfinite(outR[n]) || std::abs(outL[n])>32 || std::abs(outR[n])>32){legacyFault.store(true);outL[n]=dryL;outR[n]=dryR;}
            }

        }
        readIdx += lastEffRate;
    }
    flushRack(N);
    updateBufferedTail();
    backlogSeconds.store(static_cast<float>((writtenSamples - readIdx) / fs), std::memory_order_relaxed);
    bufferStatus.store(pauseReason, std::memory_order_relaxed);
    pauseSeconds.store(static_cast<float>(pauseSamplesRemaining / fs), std::memory_order_relaxed);
}

void TDSpeedVSTAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumSamples() == 0 || rbSize <= 0) return;
    if(isDawMode()){processDaw(buffer,false);return;}
    if(legacyResetRequested.exchange(false)){springVerb.reset();legacyFault.store(false);}
    const bool resetHigh = pReset->load() > 0.5f;
    if (resetRequested.exchange(false) || (resetHigh && !resetWasHigh))
        resetBacklog();
    resetWasHigh = resetHigh;

    if (pBypass->load() > 0.5f)
    {
        processBlockBypassed(buffer, midi);
        return;
    }
    if (wasBypassed)
    {
        // Resume the same buffered song position after bypass.
        wasBypassed = false;
    }

    prepareEffectControls();
    effects.beginBlock();
    applyPitchVarispeed(buffer);
    finishOutput(buffer);
}

void TDSpeedVSTAudioProcessor::prepareEffectControls()
{
    // Set reverb controls once per block; process it only on audible samples
    // so the three-second recovery remains silent even with long decay tails.
    const bool reverbOn = pRevOn->load() > 0.5f;
    if (!reverbOn && reverbWasOn) springVerb.reset();
    const bool legacyFirst=reverbOn && !reverbWasOn;reverbWasOn = reverbOn;
    if (reverbOn)
    {
        auto validated=[](float value,float lo,float hi,float initial){return juce::jlimit(lo,hi,std::isfinite(value)?value:initial);};
        blockReverbMix=validated(pRevMix->load(),0,100,25)*0.01f;
        const int mode=(int)validated(pRevMode->load(),0,1,0);blockLegacyCalibration=tdEffects::catalogue()[(size_t)(48+mode)].wetGain;
        springVerb.setParameters(validated(pRevTime->load(),0.1f,4,1.5f),validated(pRevTone->load(),1000,12000,6000),mode);
        const float output=juce::Decibels::decibelsToGain(validated(pRevOutput->load(),-24,12,0)),wet=juce::Decibels::decibelsToGain(validated(pRevWetLevel->load(),-24,12,0));
        if(legacyFirst){legacyMix.setCurrentAndTargetValue(blockReverbMix);legacyGain.setCurrentAndTargetValue(output);legacyWetGain.setCurrentAndTargetValue(wet);}
        legacyMix.setTargetValue(blockReverbMix);legacyGain.setTargetValue(output);legacyWetGain.setTargetValue(wet);
    }
}

void TDSpeedVSTAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    juce::ScopedNoDenormals noDenormals;
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0 || rbSize <= 0) return;
    if(isDawMode()){processDaw(buffer,true);return;}
    if (rbSize <= 0 || buffer.getNumChannels() == 0) return;
    if (!wasBypassed) { springVerb.reset(); effects.reset(); }
    wasBypassed = true;
    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getReadPointer(buffer.getNumChannels() > 1 ? 1 : 0);
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        if (pauseReason == 0 && (double)writtenSamples - readIdx >= rbSize - 4)
            beginRecoveryPause(1);
        if (left[n] != 0.0f || right[n] != 0.0f) lastSignalSourceFrame = writtenSamples;
        rbL[(size_t)writeIdx] = left[n];
        rbR[(size_t)writeIdx] = right[n];
        if (++writeIdx == rbSize) writeIdx = 0;
        ++writtenSamples;
        if (pauseReason != 0) advanceRecoveryPause();
    }
    updateBufferedTail();
    backlogSeconds.store(static_cast<float>((writtenSamples - readIdx) / fs));
    bufferStatus.store(pauseReason);
    pauseSeconds.store(static_cast<float>(pauseSamplesRemaining / fs));
    finishOutput(buffer);
}

//==============================================================================

void TDSpeedVSTAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();state.setProperty("rcStateVersion",4,nullptr);state.setProperty("mixLaw","RC-equal-power-v1",nullptr);
    juce::MemoryOutputStream mos(destData, false);
    for (auto child : state)
        if (child.getProperty("id").toString() == "reset")
            child.setProperty("value", 0.0f, nullptr);
    state.writeToStream(mos);
}

void TDSpeedVSTAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree vt = juce::ValueTree::readFromData(data, (size_t)sizeInBytes);
    if (vt.isValid() && vt.hasType(apvts.state.getType()))
    {
        // Reset is an action, never a persistent session setting.
        for (auto child : vt)
            if (child.getProperty("id").toString() == "reset")
                child.setProperty("value", 0.0f, nullptr);
        // A legacy preset has no rack parameters: start those at their defaults,
        // rather than retain effects from the previously loaded modern preset.
        auto complete=apvts.copyState();
        for(int slot=0;slot<3;++slot)complete.removeProperty(tdEffects::slotId(slot,"IR"),nullptr);
        for(auto child:complete)
        {
            const auto id=child.getProperty("id").toString();
            if(id.startsWith("rack"))if(auto* param=apvts.getParameter(id))child.setProperty("value",param->convertFrom0to1(param->getDefaultValue()),nullptr);
        }
        for(auto child:vt)
        {
            auto destination=complete.getChildWithProperty("id",child.getProperty("id"));
            if(destination.isValid())destination.setProperty("value",child.getProperty("value"),nullptr);else complete.addChild(child.createCopy(),-1,nullptr);
        }
        for(int i=0;i<vt.getNumProperties();++i)complete.setProperty(vt.getPropertyName(i),vt.getProperty(vt.getPropertyName(i)),nullptr);
        for(int slot=0;slot<3;++slot){const auto key=tdEffects::slotId(slot,"assigned");if(!vt.hasProperty(key))complete.setProperty(key,vt.getChildWithProperty("id",tdEffects::slotId(slot,"effect")).isValid(),nullptr);}
        for(int slot=0;slot<3;++slot){const float choice=(float)vt.getChildWithProperty("id",tdEffects::slotId(slot,"effect")).getProperty("value",0);
            if(std::isfinite(choice) && (choice<0 || choice>=(float)tdEffects::catalogue().size())){
                complete.setProperty(tdEffects::slotId(slot,"missingEngine"),"Saved effect index "+juce::String(choice),nullptr);complete.setProperty(tdEffects::slotId(slot,"originalEffectIndex"),choice,nullptr);
                complete.getChildWithProperty("id",tdEffects::slotId(slot,"unavailable")).setProperty("value",1,nullptr);
            }
        }
        if((int)vt.getProperty("rcStateVersion",0)<3)complete.setProperty("rcMixNotice",true,nullptr);
        if((int)vt.getProperty("rcStateVersion",0)<4)complete.getChildWithProperty("id","timingMode").setProperty("value",0,nullptr);
        for(const char* key:{"syncGroupId","syncLeader","syncGroupName"})if(!vt.hasProperty(key))complete.removeProperty(key,nullptr);
        apvts.replaceState(complete);
        if(juce::MessageManager::getInstance()->isThisTheMessageThread())handleAsyncUpdate();else triggerAsyncUpdate();
        effects.reloadImpulseResponses();
        // Session recall changes parameters, not the buffered song position.
    }
}

juce::AudioProcessorEditor* TDSpeedVSTAudioProcessor::createEditor()
{
    return new TDSpeedVSTAudioProcessorEditor(*this);
}

juce::AudioProcessorValueTreeState::ParameterLayout TDSpeedVSTAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("semitones", "Semitones",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("reset", "Reset", false));

    // Reverb
    p.push_back(std::make_unique<juce::AudioParameterBool>("revOn", "Reverb On", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revMix", "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revTime", "Reverb Time (s)",
        juce::NormalisableRange<float>(0.1f, 4.0f, 0.01f), 1.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revTone", "Reverb Tone (Hz)",
        juce::NormalisableRange<float>(1000.0f, 12000.0f, 1.0f), 6000.0f));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("revMode", "Reverb Mode",
        juce::StringArray{ "Spring", "PSX" }, 0));

    tdEffects::addParameters(p);
    p.push_back(std::make_unique<juce::AudioParameterFloat>("masterOutput","Final Output",-24,12,0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revOutput","Legacy Output",-24,12,0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("revWetLevel","Legacy Wet Level",-24,12,0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("timingMode","Timing mode",juce::StringArray{"Listening","DAW"},0));
    return { p.begin(), p.end() };
}

//==============================================================================
void TDSpeedVSTAudioProcessor::updateBufferedTail() noexcept
{
    // Silence after a clip must drain this estimate, not create an ever-growing
    // tail. Track actual buffered signal separately from incoming timeline time.
    double remaining = 0.0;
    const double rate = dawRender ? dawRate : (double)lastEffRate;
    if (lastSignalSourceFrame >= 0 && lastSignalSourceFrame >= writtenSamples - rbSize
        && std::isfinite(rate) && rate > 0.0 && fs > 0.0)
        remaining = std::max(0.0, (lastSignalSourceFrame + 2.0 - readIdx) / (fs * rate));
    bufferedTailSeconds.store(remaining, std::memory_order_relaxed);
}

double TDSpeedVSTAudioProcessor::getTailLengthSeconds() const {return (isDawMode()?groupBypassed():pBypass->load()>.5f)?0:getBufferedTailSeconds()+effects.tailSeconds()+(pRevOn->load()>.5f && pEffectsBypass->load()<.5f && !legacyFault.load()?juce::jlimit(.1f,4.0f,std::isfinite(pRevTime->load())?pRevTime->load():1.5f)+.2:0);}
void TDSpeedVSTAudioProcessor::finishOutput(juce::AudioBuffer<float>& buffer)
{
    // A rendered preview already contains pitch, effects and output gain.
    // Audition through the host without applying those controls a second time.
    if (!isNonRealtime() && preview.render(buffer,fs)) {
        outputPeak.store(juce::jmin(32.0f,buffer.getMagnitude(0,buffer.getNumSamples())));
        return;
    }
    float db=pMasterOutput->load();if(!std::isfinite(db))db=0;masterGain.setTargetValue(juce::Decibels::decibelsToGain(juce::jlimit(-24.0f,12.0f,db)));
    float peak=0;const bool protect=pProtect->load()>.5f;
    for(int i=0;i<buffer.getNumSamples();++i){const float gain=masterGain.getNextValue();for(int c=0;c<buffer.getNumChannels();++c){float v=buffer.getSample(c,i)*gain;peak=juce::jmax(peak,std::isfinite(v)?std::abs(v):32.0f);if(protect)v=juce::jlimit(-.98f,.98f,v);buffer.setSample(c,i,v);}}
    outputPeak.store(juce::jmin(32.0f,peak));
}

void TDSpeedVSTAudioProcessor::parameterChanged(const juce::String& id,float value) {
    if(auto* group=syncGroup.load()){
        if(id=="semitones" && syncLeader())group->pendingPitch.store(value);
        if(id=="bypass")group->bypass.store(value>.5f);
    }
}
bool TDSpeedVSTAudioProcessor::syncLeader() const noexcept {
    if(auto* g=syncGroup.load()){uint64_t vacant=0;g->leader.compare_exchange_strong(vacant,memberId);return g->leader.load()==memberId;}return true;
}
void TDSpeedVSTAudioProcessor::createSyncGroup(){joinSyncGroup(juce::Uuid().toString(),true);}
void TDSpeedVSTAudioProcessor::joinSyncGroup(const juce::String& id,bool leader) {
    if(id.isEmpty()){leaveSyncGroup();return;}
    if(syncGroupId()==id)return;
    leaveSyncGroup();auto group=tdDaw::Registry::instance().obtain(id,"Group "+id.substring(0,6));
    if(group->members.fetch_add(1)==0){group->run=0;group->highWater=INT64_MIN;group->capacity=0;group->sampleRate=0;}
    int slot=-1;for(int i=0;i<32;++i){uint64_t vacantKey=0;if(group->memberKeys[i].compare_exchange_strong(vacantKey,memberId)){slot=i;break;}}
    if(slot<0){group->members.fetch_sub(1);return;}group->candidateSerial[slot]=0;group->activeMask.fetch_or(uint32_t(1)<<slot);
    uint64_t vacant=0;if(leader || group->leader.load()==0)group->leader.compare_exchange_strong(vacant,memberId);
    if(group->leader.load()==memberId){group->pendingPitch=pSemitones->load();group->bypass=pBypass->load()>.5f;}
    retainedGroups.push_back(group);syncGroup.store(group.get());
    apvts.state.setProperty("syncGroupId",id,nullptr);apvts.state.setProperty("syncLeader",group->leader.load()==memberId,nullptr);
}
void TDSpeedVSTAudioProcessor::leaveSyncGroup(){
    if(auto* g=syncGroup.exchange(nullptr)){for(int i=0;i<32;++i)if(g->memberKeys[i].load()==memberId){g->activeMask.fetch_and(~(uint32_t(1)<<i));g->memberKeys[i]=0;g->candidateSerial[i]=0;break;}g->members.fetch_sub(1);uint64_t mine=memberId;g->leader.compare_exchange_strong(mine,0);}
    apvts.state.removeProperty("syncGroupId",nullptr);apvts.state.removeProperty("syncLeader",nullptr);
}
juce::String TDSpeedVSTAudioProcessor::syncGroupId() const {if(auto* g=syncGroup.load())return g->id;return {};}
void TDSpeedVSTAudioProcessor::handleAsyncUpdate(){if(!isDawMode()){leaveSyncGroup();return;}const auto id=apvts.state.getProperty("syncGroupId").toString();const bool leader=(bool)apvts.state.getProperty("syncLeader",false);if(id.isEmpty())leaveSyncGroup();else joinSyncGroup(id,leader);}
bool TDSpeedVSTAudioProcessor::groupBypassed() const noexcept {if(auto* g=syncGroup.load())return g->bypass.load();return pBypass->load()>.5f;}
juce::String TDSpeedVSTAudioProcessor::dawStatusText() const {
    if(!isDawMode())return "Listening / independent history";
    switch(dawStatus.load()){
        case 1:return "DAW stopped / history frozen";
        case 4:return "Restart playback to synchronize";
        case 5:return "DAW timing unavailable / use Listening for live input";
        default: {
            const auto* g=syncGroup.load();const float desired=g?g->pendingPitch.load():pSemitones->load();
            return juce::String(g?"Linked / ":"DAW / ")+((groupBypassed() || (g && g->externalBypasses.load()>0))?"dry bypass":"processing")+" / "+juce::String(dawPitch.load(),1)+" st"+(std::abs(desired-dawPitch.load())>.049f?" / Pending - applies after Stop/Play":"");
        }
    }
}
void TDSpeedVSTAudioProcessor::freshDawHistory(){writeIdx=0;writtenSamples=0;lastSignalSourceFrame=-1;bufferedTailSeconds=0;readIdx=-targetBacklog;pauseReason=0;pauseSamplesRemaining=0;springVerb.reset();effects.reset();reverbWasOn=false;backlogSeconds=.25f;bufferStatus=0;pauseSeconds=0;}
void TDSpeedVSTAudioProcessor::reset(){
    if(!isDawMode())return;
    dawHostReset=true;dawStatus=1;auto* g=syncGroup.load();if(!g)g=&solo;
    g->run=0;g->highWater=INT64_MIN;g->capacity=0;g->sampleRate=0;
}
void TDSpeedVSTAudioProcessor::processDaw(juce::AudioBuffer<float>& buffer,bool hostBypass){
    auto* g=syncGroup.load();if(!g)g=&solo;
    if(dawHostReset.exchange(false)){dawWasPlaying=false;dawRun=0;dawExpected=INT64_MIN;pendingSeekSerial=0;dawRestart=false;}
    if(g!=lastDawGroup){if(externalBypass && lastDawGroup)lastDawGroup->externalBypasses.fetch_sub(1);externalBypass=false;lastDawGroup=g;dawRun=0;dawExpected=INT64_MIN;pendingSeekSerial=0;dawMemberSlot=0;for(int i=0;i<32;++i)if(g->memberKeys[i].load()==memberId){dawMemberSlot=i;break;}}
    if(hostBypass!=externalBypass){g->externalBypasses.fetch_add(hostBypass?1:-1);externalBypass=hostBypass;}
    if(g==&solo){g->pendingPitch=pSemitones->load();g->bypass=pBypass->load()>.5f;}
    const auto position=getPlayHead()?getPlayHead()->getPosition():juce::Optional<juce::AudioPlayHead::PositionInfo>();
    if(!position || !position->getTimeInSamples()){dawStatus=5;buffer.clear();finishOutput(buffer);return;}
    const int64_t sample=*position->getTimeInSamples();
    if(!position->getIsPlaying()){g->run=0;g->highWater=INT64_MIN;g->capacity=0;g->sampleRate=0;g->candidateSerial[dawMemberSlot]=0;pendingSeekSerial=0;dawWasPlaying=false;dawRestart=false;dawExpected=INT64_MIN;dawStatus=1;finishOutput(buffer);return;}

    auto run=g->run.load();
    if(!run){uint64_t stopped=0;if(g->run.compare_exchange_strong(stopped,tdDaw::encode(sample,g->pendingPitch.load())))g->highWater=sample;run=g->run.load();}
    bool pending=false;
    if(pendingSeekSerial){
        if(g->seekSerial.load()!=pendingSeekSerial && run==pendingSeekRun){pendingSeekSerial=0;dawRestart=false;}
        else if(run!=dawRun && run!=pendingSeekRun){pendingSeekSerial=0;}
        else {run=pendingSeekRun;pending=true;}
    }
    const bool gap=dawExpected!=INT64_MIN && sample!=dawExpected;
    // A seek is confirmed by every registered member seeing the same jump.
    // Before confirmation, capture into a provisional epoch but stay silent.
    // Its 250 ms reserve avoids losing the first input block when confirmed.
    if(gap && !pendingSeekSerial && run==dawRun){
        const uint64_t serial=g->seekSerial.load();
        const auto next=tdDaw::encode(sample,tdDaw::semi(run));
        g->candidateOrigin[dawMemberSlot]=sample;g->candidateSerial[dawMemberSlot]=serial;
        const auto mask=g->activeMask.load();bool all=true;
        for(int i=0;i<32;++i)if(mask&(uint32_t(1)<<i))all &= g->candidateSerial[i].load()==serial && g->candidateOrigin[i].load()==sample;
        bool unlocked=false;
        if(all && g->publishingSeek.compare_exchange_strong(unlocked,true)){
            if(g->seekSerial.load()==serial){g->run=next;g->highWater=sample;g->seekSerial=serial+1;}
            g->publishingSeek=false;run=g->run.load();
        }else {pendingSeekSerial=serial;pendingSeekRun=next;run=next;pending=true;}
    }
    const int64_t origin=tdDaw::origin(run);
    const bool fresh=run!=dawRun || (!dawWasPlaying && sample==origin) || (gap && sample==origin);
    if(fresh){dawRun=run;dawRestart=false;freshDawHistory();dawExpected=origin;int64_t unset=0;g->capacity.compare_exchange_strong(unset,rbSize);double noRate=0;g->sampleRate.compare_exchange_strong(noRate,fs);}
    if(sample<origin || sample!=dawExpected || g->capacity.load()!=rbSize || g->sampleRate.load()!=fs)dawRestart=true;
    dawWasPlaying=true;dawExpected=sample+buffer.getNumSamples();
    int64_t high=g->highWater.load();if(dawExpected>high)g->highWater.compare_exchange_strong(high,dawExpected);
    if(dawRestart){dawStatus=4;if(!g->bypass.load() && g->externalBypasses.load()==0)buffer.clear();finishOutput(buffer);return;}
    dawStatus=2;dawPitch=tdDaw::semi(run);dawRate=std::pow(2.0,double(dawPitch.load())/12);
    dawPlayLength=dawRate<1?(int64_t)std::ceil((rbSize-4-targetBacklog)/(1-dawRate)):dawRate>1?(int64_t)std::ceil((targetBacklog-1)/(dawRate-1)):INT64_MAX;
    const bool dry=g->bypass.load() || g->externalBypasses.load()>0;
    if(pending)dawStatus=4;
    prepareEffectControls();effects.beginBlock();dawRender=true;
    for(int offset=0;offset<buffer.getNumSamples();offset+=256){const int count=juce::jmin(256,buffer.getNumSamples()-offset);float saved[2][256];float* channels[2];const int nch=buffer.getNumChannels();
        for(int c=0;c<nch;++c){channels[c]=buffer.getWritePointer(c)+offset;if(dry)std::copy_n(channels[c],count,saved[c]);}
        juce::AudioBuffer<float> block(channels,nch,count);dawElapsed=sample-origin+offset;applyPitchVarispeed(block);
        if(dry)for(int c=0;c<nch;++c)std::copy_n(saved[c],count,channels[c]);else if(pending)block.clear();
    }
    dawRender=false;finishOutput(buffer);
}

// JUCE factory
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto* processor=new TDSpeedVSTAudioProcessor();tdEffects::setParameter(processor->apvts,"timingMode",1);return processor;
}

// A private processor is used by the file-render worker. No live history or
// synchronization group is touched, and no full-song RAM allocation is needed.
void TDSpeedVSTAudioProcessor::prepareFileEffects(double rate) {
    fs=rate;masterGain.reset(fs,.02);masterGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(pMasterOutput->load()));
    legacyMix.reset(fs,.02);legacyGain.reset(fs,.02);legacyWetGain.reset(fs,.02);
    reverbWasOn=false;legacyFault=false;springVerb.prepare(fs,512);effects.prepare(fs,true);
    prepareEffectControls();
}
void TDSpeedVSTAudioProcessor::processFileEffects(juce::AudioBuffer<float>& buffer) {
    juce::ScopedNoDenormals noDenormals;prepareEffectControls();effects.beginBlock();
    if(reverbWasOn && pEffectsBypass->load()<.5f) {
        for(int n=0;n<buffer.getNumSamples();++n) {
            float* ch[]{buffer.getWritePointer(0)+n,buffer.getWritePointer(1)+n};
            const float l=*ch[0],r=*ch[1];springVerb.process(ch,2,1,1);
            if(!std::isfinite(*ch[0]) || !std::isfinite(*ch[1]) || std::abs(*ch[0])>32 || std::abs(*ch[1])>32){legacyFault=true;*ch[0]=l;*ch[1]=r;continue;}
            const float mix=legacyMix.getNextValue(),gain=legacyGain.getNextValue(),wet=legacyWetGain.getNextValue()*blockLegacyCalibration;
            const float d=mix==1?0:std::cos(mix*juce::MathConstants<float>::halfPi),w=mix==0?0:wet*std::sin(mix*juce::MathConstants<float>::halfPi);
            *ch[0]=mix==0 && gain==1?l:(l*d+*ch[0]*w)*gain;*ch[1]=mix==0 && gain==1?r:(r*d+*ch[1]*w)*gain;
        }
    }
    effects.process(buffer,0,buffer.getNumSamples());finishOutput(buffer);
}
