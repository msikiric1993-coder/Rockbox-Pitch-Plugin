#pragma once
#include "PluginProcessor.h"
#include <functional>
namespace tdFile {
struct Request { juce::File source, destination;juce::ValueTree rack;float semitones=0;std::shared_ptr<const tdPreview::Audio> input; };
struct Result { juce::String error;juce::File file;double duration=0;bool encoded=false; };
Result render(const Request&,std::atomic<float>& progress,const std::function<bool()>& cancelled);
class Job : public juce::Thread {
public:
    explicit Job(Request r,bool audition=false):Thread("TDSpeed file render"),request(std::move(r)),loadAudition(audition){}
    ~Job() override {signalThreadShouldExit();stopThread(-1);}
    void run() override;
    std::unique_ptr<tdPreview::Audio> audio;
    bool loadAudition=false;
    Request request;Result result;std::atomic<float> progress{0};std::atomic<bool> done{false};
};
// Loading decodes and draws a source; it never invokes pitch/effect rendering.
class LoadJob : public juce::Thread {
public:
    explicit LoadJob(juce::File source):Thread("TDSpeed load source"),source(source){}
    ~LoadJob() override {signalThreadShouldExit();stopThread(-1);}
    void run() override;
    juce::File source;std::unique_ptr<tdPreview::Audio> audio;juce::String error;
    std::atomic<bool> done{false};
};
enum class SaveQuality { original, aac320, flac };
bool aacAvailable();
class SaveJob : public juce::Thread {
public:
    SaveJob(juce::File rendered,juce::File original,juce::File destination,std::shared_ptr<const tdPreview::Audio> lease={},SaveQuality quality=SaveQuality::original):Thread("TDSpeed save rendered audio"),rendered(rendered),original(original),destination(destination),lease(std::move(lease)),quality(quality){}
    ~SaveJob() override {signalThreadShouldExit();stopThread(-1);}
    void run() override;
    juce::File rendered,original,destination;
    std::shared_ptr<const tdPreview::Audio> lease;
    SaveQuality quality;
    juce::String error;
    std::atomic<bool> done{false};
};
}
