#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void set(TDSpeedVSTAudioProcessor& p, const char* id, float v)
{
    auto* param = p.apvts.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(v));
}
static void run(TDSpeedVSTAudioProcessor& p, int samples, double fs, int channels = 2)
{
    juce::MidiBuffer midi;
    int offset = 0;
    while (offset < samples)
    {
        const int n = juce::jmin(257, samples-offset);
        juce::AudioBuffer<float> block(channels, n);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                block.setSample(ch, i, 0.2f*std::sin((offset+i)*440.0*juce::MathConstants<double>::twoPi/fs));
        p.processBlock(block, midi);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < n; ++i)
                require(std::isfinite(block.getSample(ch, i)), "Non-finite audio");
        offset += n;
    }
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            TDSpeedVSTAudioProcessor p(12.0);
            p.prepareToPlay(fs, 257);
            run(p, (int)fs, fs);
            require(std::abs(p.getBacklogSeconds()-0.25f)<0.001f, "Unity delay drift");
            require(p.getBufferStatus()==0, "Unity buffer stopped");
            set(p, "semitones", -12);
            run(p, (int)fs, fs);
            require(std::abs(p.getBacklogSeconds()-0.75f)<0.003f, "Pitch down rate drift");
            set(p, "reset", 1);
            run(p, (int)fs, fs);
            require(std::abs(p.getBacklogSeconds()-0.75f)<0.003f, "Held reset triggers repeatedly");
            set(p, "reset", 0);
            p.requestBacklogReset();
            run(p, 257, fs);
            require(p.getBacklogSeconds()<0.26f, "UI reset ignored");
            set(p, "bypass", 1);
            juce::AudioBuffer<float> bypass(2, 257); bypass.clear(); bypass.setSample(0, 11, 0.731f);
            juce::MidiBuffer midi; p.processBlock(bypass, midi);
            require(bypass.getSample(0, 11)==0.731f, "Bypass changes input");
            run(p, (int)(fs*3), fs);
            set(p, "bypass", 0); run(p, 257, fs);
            require(p.getBacklogSeconds()>3.0f && p.getBacklogSeconds()<3.3f, "Bypass resume discarded song history");
            set(p, "semitones", 12); run(p, (int)(fs*5), fs);
            require(p.getBufferStatus()==2, "Live-input exhaustion not detected");
            require(p.getPauseSeconds()>0.0f && p.getPauseSeconds()<3.0f, "Underrun pause countdown missing");
            set(p, "revOn", 1); set(p, "revMix", 100); set(p, "revTime", 4);
            run(p, (int)fs, fs);
            p.prepareToPlay(fs, 257);
            juce::AudioBuffer<float> silence(2, 257); silence.clear(); p.processBlock(silence, midi);
            require(silence.getMagnitude(0,257)==0.0f, "Prepare retains reverb history");
            set(p, "reset", 1);
            juce::MemoryBlock state; p.getStateInformation(state);
            p.setStateInformation(state.getData(), (int)state.getSize());
            require(p.apvts.getRawParameterValue("reset")->load()<0.5f, "Reset persisted in session");
            require(p.apvts.getRawParameterValue("semitones")->load()==12.0f, "Session pitch lost");
        }
        // Reproduce long pitch-down playback beyond the old two-second limit.
        // The source is a ramp, so every output sample identifies its exact song
        // position. A skip or repeat cannot hide behind a periodic test tone.
        TDSpeedVSTAudioProcessor continuous(120.0);
        set(continuous, "semitones", -12);
        continuous.prepareToPlay(48000, 257);
        juce::MidiBuffer timelineMidi;
        constexpr int duration = 180 * 48000;
        for (int offset = 0; offset < duration; offset += 257)
        {
            const int n = juce::jmin(257, duration-offset);
            juce::AudioBuffer<float> block(2,n);
            for (int i=0; i<n; ++i)
            {
                block.setSample(0,i,(float)(offset+i)/duration);
                block.setSample(1,i,-0.5f*(float)(offset+i)/duration);
            }
            continuous.processBlock(block,timelineMidi);
            for (int i=0; i<n; ++i)
            {
                const double sourcePosition = 0.5*(offset+i)-12000.0;
                if (sourcePosition > 2.0)
                {
                    const float expected = (float)(sourcePosition/duration);
                    require(std::abs(block.getSample(0,i)-expected)<0.000001f, "Pitch-down song position jumped");
                    require(std::abs(block.getSample(1,i)+0.5f*expected)<0.000001f, "Stereo song position changed");
                }
            }
        }
        require(std::abs(continuous.getBacklogSeconds()-90.25f)<0.001f, "Accumulated backlog lost");
        require(continuous.getBufferStatus()==0, "Continuous playback stopped before capacity");
        juce::MemoryBlock continuityState; continuous.getStateInformation(continuityState);
        continuous.setStateInformation(continuityState.getData(), (int)continuityState.getSize());
        run(continuous,257,48000);
        require(continuous.getBacklogSeconds()>90.25f, "Session recall reset history");

        {
            TDSpeedVSTAudioProcessor listening;
            set(listening,"semitones",-12); listening.prepareToPlay(48000,257);
            require(std::abs(listening.getHistoryCapacitySeconds() - (float)(TDSpeedVSTAudioProcessor::maxHistoryFrames / 48000.0)) < 0.001f,
                    "Default history does not use full 2 GiB budget");
            run(listening,48000*5,48000);
            require(std::abs(listening.getBacklogSeconds()-2.75f)<0.001f && listening.getBufferStatus()==0,
                    "Default build still resets at two seconds");
        }
        // Sample-exact recovery across arbitrary callback boundaries. Constant
        // input distinguishes the silent pause from both normal and fresh audio.
        for (double fs : {44100.0,48000.0,96000.0})
            for (float semi : {-12.0f,12.0f})
            {
                TDSpeedVSTAudioProcessor recovery(2.0);
                set(recovery,"semitones",semi);
                set(recovery,"revOn",1); set(recovery,"revMix",25);
                recovery.prepareToPlay(fs,257);
                juce::MidiBuffer midi;
                const int length = (int)(fs*8.0);
                int silentRun = 0, pauseCount = 0;
                bool heardAudio = false;
                for (int offset=0;offset<length;offset+=257)
                {
                    const int n = juce::jmin(257,length-offset);
                    juce::AudioBuffer<float> block(2,n);
                    for(int ch=0;ch<2;++ch) for(int i=0;i<n;++i) block.setSample(ch,i,0.2f);
                    recovery.processBlock(block,midi);
                    for(int i=0;i<n;++i)
                    {
                        const bool silent = block.getSample(0,i)==0.0f && block.getSample(1,i)==0.0f;
                        if (!heardAudio && !silent) heardAudio=true;
                        if (heardAudio && silent) ++silentRun;
                        if (heardAudio && !silent && silentRun>0)
                        {
                            require(silentRun==(int)std::llround(3*fs), "Recovery not exactly three seconds or reverb leaked");
                            ++pauseCount; silentRun=0;
                        }
                    }
                }
                require(pauseCount>0,"Buffer did not automatically resume");
                recovery.requestBacklogReset(); run(recovery,257,fs);
                require(recovery.getBufferStatus()==0 && recovery.getBacklogSeconds()<0.26f,"Manual reset failed");
            }

        // Wet signal must have an audible tail in both modes, and the input
        // high-pass must reject DC without attenuating the audio band by 30 dB.
        OnePoleHPF hpf; hpf.prepare(48000,180);
        double inputEnergy=0,outputEnergy=0;
        for(int i=0;i<48000;++i)
        {
            const float x=std::sin((float)(i*1000.0*juce::MathConstants<double>::twoPi/48000));
            const float y=hpf.process(x);
            if(i>1000){ inputEnergy+=x*x; outputEnergy+=y*y; }
        }
        require(outputEnergy/inputEnergy>0.8,"Reverb HPF attenuates audible frequencies");
        for(int mode : {0,1})
        {
            SpringVerb verb; verb.prepare(48000,257); verb.setParameters(1.5f,6000,mode);
            juce::AudioBuffer<float> wet(2,48000);wet.clear();wet.setSample(0,0,0.5f);wet.setSample(1,0,0.5f);
            float* channels[2]{wet.getWritePointer(0),wet.getWritePointer(1)};
            verb.process(channels,2,48000,1.0f);
            require(wet.getMagnitude(1000,23000)>0.01f,"Reverb tail inaudible");
            require(wet.getMagnitude(12000,12000)>0.00001f,"Reverb has no sustained decay");
            verb.reset();wet.clear();verb.process(channels,2,48000,1.0f);
            require(wet.getMagnitude(0,48000)==0.0f,"Reverb reset retains a tail");
        }

        TDSpeedVSTAudioProcessor unity(2.0);
        unity.prepareToPlay(48000, 257);
        juce::AudioBuffer<float> impulse(2, 14000); impulse.clear();
        impulse.setSample(0, 0, 0.5f); impulse.setSample(1, 0, -0.25f);
        juce::MidiBuffer impulseMidi; unity.processBlock(impulse, impulseMidi);
        require(impulse.getSample(0,12000)==0.5f && impulse.getSample(1,12000)==-0.25f,
                "Unity interpolation changes audio or stereo channels");
        require(impulse.getMagnitude(0,12000)==0.0f, "Startup reads uninitialised history");
        juce::AudioBuffer<float> empty(2,0); unity.processBlock(empty, impulseMidi);

        TDSpeedVSTAudioProcessor mono(2.0);
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(juce::AudioChannelSet::mono()); layout.outputBuses.add(juce::AudioChannelSet::mono());
        require(mono.setBusesLayout(layout), "Mono layout rejected");
        mono.prepareToPlay(48000, 257); set(mono, "revOn", 1); set(mono, "revMix", 50); run(mono, 48000, 48000, 1);

        TDSpeedVSTAudioProcessor p;
        auto editor = std::unique_ptr<juce::AudioProcessorEditor>(p.createEditor());
        auto* locked=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());require(!locked->isResizable(),"Editor resize corner still enabled");
        for(float scale:{1.0f,.9f,.8f,.7f}){
            locked->setUiScale(scale);require(locked->getWidth()==juce::roundToInt(820*scale),"Whole UI scale wrong");
            if(argc>1){juce::FileOutputStream stream(juce::File(argv[1]).getChildFile("UI-"+juce::String((int)(scale*100))+".png"));stream.setPosition(0);stream.truncate();juce::PNGImageFormat png;require(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),stream),"Screenshot failed");}
        }
        std::cout << "PASS: rate/delay, one-shot reset, bypass continuity, long song-position continuity, three-second automatic recovery, audible reverb and reset, state, mono, UI sizes\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
