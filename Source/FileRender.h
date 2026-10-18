#pragma once
#include "PluginProcessor.h"
#include <functional>
namespace tdFile {
struct Request { juce::File source, destination;juce::ValueTree rack;float semitones=0; };
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
class SaveJob : public juce::Thread {
public:
    SaveJob(juce::File rendered,juce::File original,juce::File destination,std::shared_ptr<const tdPreview::Audio> lease={}):Thread("TDSpeed save rendered audio"),rendered(rendered),original(original),destination(destination),lease(std::move(lease)){}
    ~SaveJob() override {signalThreadShouldExit();stopThread(-1);}
    void run() override;
    juce::File rendered,original,destination;
    std::shared_ptr<const tdPreview::Audio> lease;
    juce::String error;
    std::atomic<bool> done{false};
};
}
