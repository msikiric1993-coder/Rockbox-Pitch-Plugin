#pragma once
#include "PluginProcessor.h"
#include <functional>
namespace tdFile {
class DragButton : public juce::TextButton {
public:
    DragButton():juce::TextButton("Drag file into DAW") {setTooltip("Drag the rendered file into your DAW, or click to show it in Explorer");}
    juce::File file;bool dragged=false;
    void mouseDown(const juce::MouseEvent& e) override {dragged=false;juce::TextButton::mouseDown(e);}
    void mouseDrag(const juce::MouseEvent& e) override {
        if(!dragged && e.getDistanceFromDragStart()>6 && file.existsAsFile()) {
            dragged=true;juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()},false,this);
        }
    }
};
struct Request { juce::File source, destination;juce::ValueTree rack;float semitones=0; };
struct Result { juce::String error;juce::File file;double duration=0;bool encoded=false; };
Result render(const Request&,std::atomic<float>& progress,const std::function<bool()>& cancelled);
class Job : public juce::Thread {
public:
    explicit Job(Request r):Thread("TDSpeed file render"),request(std::move(r)){}
    ~Job() override {signalThreadShouldExit();stopThread(-1);}
    void run() override {result=render(request,progress,[this]{return threadShouldExit();});done.store(true,std::memory_order_release);}
    Request request;Result result;std::atomic<float> progress{0};std::atomic<bool> done{false};
};
}
