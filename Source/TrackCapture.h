#pragma once
#include <JuceHeader.h>
// Disk I/O happens only in explicitly offline host callbacks. Real-time
// callbacks never open/write/close files, allocate, or wait for capture.
class TrackCapture {
public:
    enum State {idle,armed,capturing,finishing,ready,failed,complete};
    ~TrackCapture(){writer.reset();if(file!=juce::File())file.deleteFile();}
    bool arm(double sr,int channels,const juce::ValueTree& settings,float pitch,const juce::File& output) {
        bool expected=false;if(!busy.compare_exchange_strong(expected,true))return false;
        const int old=state.load();if(old==armed || old==capturing || old==finishing){busy=false;return false;}
        state=idle;writer.reset();if(file!=juce::File())file.deleteFile();
        file=juce::File::createTempFile(".wav");rack=settings.createCopy();semitones=pitch;destination=output;frames=0;error=0;haveTimeline=false;
        const bool ok=open(sr,channels);state=ok?armed:failed;busy=false;return ok;
    }
    void prepare(double sr,int channels) {
        if(state.load()!=armed)return;
        bool expected=false;if(!busy.compare_exchange_strong(expected,true)){error=4;state=failed;return;}
        if(rate!=sr || numChannels!=channels){writer.reset();file.deleteFile();if(!open(sr,channels)){error=1;state=failed;}}
        busy=false;
    }
    // True means that this offline callback was captured and should pass dry
    // to the DAW's throwaway bounce file, without changing live pitch history.
    bool push(const juce::AudioBuffer<float>& input,bool offline,double sr,
              juce::Optional<int64_t> samplePosition={}) noexcept {
        const int before=state.load(std::memory_order_acquire);
        if(before!=armed && before!=capturing)return false;
        if(!offline){if(before==capturing)requestFinish();return false;}
        if(input.getNumSamples()==0)return true;
        bool expected=false;if(!busy.compare_exchange_strong(expected,true)){error=4;state=failed;return false;}
        bool accepted=false;
        try {
            int now=state.load();
            if(now==armed || now==capturing) {
                if(sr!=rate || input.getNumChannels()!=numChannels){error=2;state=failed;}
                else if(samplePosition.hasValue() && haveTimeline && *samplePosition!=nextSample){error=5;state=failed;}
                else {
                    if(samplePosition.hasValue()){haveTimeline=true;nextSample=*samplePosition+input.getNumSamples();}
                    if(writer && writer->writeFromAudioSampleBuffer(input,0,input.getNumSamples())){frames.fetch_add(input.getNumSamples());state.compare_exchange_strong(now,capturing);accepted=true;}
                    else {error=1;state=failed;}
                }
            }
        } catch(...){error=3;state=failed;}
        busy.store(false,std::memory_order_release);return accepted;
    }
    void requestFinish() noexcept {int expected=capturing;state.compare_exchange_strong(expected,finishing);}
    void finishManually() noexcept {
        int expected=armed;if(state.compare_exchange_strong(expected,failed)){error=6;return;}requestFinish();
    }
    void cancel() noexcept {error=7;state=failed;}
    // Message-thread polling closes the writer only after the last callback.
    void poll() {
        const int current=state.load();if(current!=finishing && current!=failed)return;
        bool expected=false;if(!busy.compare_exchange_strong(expected,true))return;
        writer.reset();if(state.load()==finishing)state=frames.load()>0?ready:failed;busy=false;
    }
    State status() const noexcept {return (State)state.load();}
    int64_t capturedFrames() const noexcept {return frames.load();}
    double sampleRate() const noexcept {return rate;}
    void markComplete(){state=complete;}
    juce::String problem() const {
        switch(error.load()) {
            case 1:return "Capture write failed; check free disk space";
            case 2:return "Capture audio format changed; re-arm and bounce again";
            case 3:return "Capture failed; re-arm and bounce again";
            case 4:return "Capture callbacks overlapped; re-arm and bounce again";
            case 5:return "Timeline jumped during capture; bounce one continuous range";
            case 6:return "No offline audio captured. Use the DAW's Fast Bounce/offline export";
            case 7:return "Track capture cancelled";
            default:return "Capture failed";
        }
    }
    juce::File file,destination;juce::ValueTree rack;float semitones=0;
private:
    bool open(double sr,int channels) {
        if(sr<=0 || channels<1 || channels>2)return false;
        rate=sr;numChannels=channels;juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream=file.createOutputStream();if(!stream)return false;
        writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(channels).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        return writer!=nullptr;
    }
    std::unique_ptr<juce::AudioFormatWriter> writer;
    std::atomic<int> state{idle},error{0};std::atomic<bool> busy{false};std::atomic<int64_t> frames{0};
    double rate=0;int numChannels=2;bool haveTimeline=false;int64_t nextSample=0;
};
