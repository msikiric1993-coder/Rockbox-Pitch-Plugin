#include "FileRender.h"
#include "PluginEditor.h"
#include <iostream>
#include <stdexcept>
static void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI init;try{
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("TDSpeed-render-test-"+juce::Uuid().toString());root.createDirectory();
    juce::AudioFormatManager formats;formats.registerBasicFormats();TDSpeedVSTAudioProcessor state(2);
    tdFile::Request request;request.rack=tdEffects::captureState(state.apvts);std::atomic<float> progress{0};
    for(double sr:{44100.,48000.,96000.})for(int bits:{16,24,32}) {
        request.source=root.getChildFile("input.wav");request.source.deleteFile();request.destination=root.getChildFile("output.wav");request.semitones=-12;
        juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> out=request.source.createOutputStream();
        auto writer=wav.createWriterFor(out,juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(2).withBitsPerSample(bits).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::integral));
        juce::AudioBuffer<float> source(2,(int)sr);source.clear();for(int c=0;c<2;++c){source.setSample(c,0,.25f);source.setSample(c,(int)sr-1,.25f);}require(writer->writeFromAudioSampleBuffer(source,0,source.getNumSamples()),"Fixture write");writer.reset();
        auto result=tdFile::render(request,progress,[]{return false;});if(result.error.isNotEmpty())throw std::runtime_error(result.error.toStdString());
        auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(request.destination));require(reader && reader->lengthInSamples==(int64_t)sr*2,"Slowed output length incorrect");require(reader->sampleRate==sr && reader->bitsPerSample==bits && !reader->usesFloatingPointData,"Source rate/depth not preserved");
        juce::AudioBuffer<float> ending(2,20);reader->read(&ending,0,20,reader->lengthInSamples-20,true,true);require(ending.getMagnitude(0,20)>.1f,"Last source sample cut off");
    }
    request.destination=root.getChildFile("fast.wav");request.semitones=7;
    auto fast=tdFile::render(request,progress,[]{return false;});require(fast.error.isEmpty(),"Faster file render failed");
    auto faster=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(request.destination));require(faster && faster->lengthInSamples==(int64_t)std::ceil(96000/std::pow(2.,7./12)),"Faster file has incorrect duration");
    request.destination=root.getChildFile("output.flac");request.semitones=7;
    auto result=tdFile::render(request,progress,[]{return false;});
    // The last fixture is 32-bit PCM, which FLAC cannot preserve: fail clearly.
    require(result.error.isNotEmpty(),"Unsupported depth silently changed");
    request.destination=root.getChildFile("keep.wav");request.destination.replaceWithText("original sentinel");
    int cancellationChecks=0;result=tdFile::render(request,progress,[&]{return ++cancellationChecks>4;});require(result.error.isNotEmpty() && request.destination.loadFileAsString()=="original sentinel","Cancelled render overwrites target");
    request.destination=request.source;result=tdFile::render(request,progress,[]{return false;});require(result.error.isNotEmpty(),"Source overwrite allowed");
    request.destination=root.getChildFile("wet.wav");request.semitones=-3;tdEffects::applySpaceVariant(state.apvts,0,0);tdEffects::setParameter(state.apvts,"rack1_mix",100);request.rack=tdEffects::captureState(state.apvts);
    result=tdFile::render(request,progress,[]{return false;});if(result.error.isNotEmpty())throw std::runtime_error(result.error.toStdString());
    auto wet=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(request.destination));juce::AudioBuffer<float> ambience(2,96000);wet->read(&ambience,0,96000,96000*2,true,true);require(ambience.getMagnitude(0,96000)>1e-6,"Rendered reverb has no decay");
    if(argc>1){const juce::String ffmpeg(argv[1]);juce::ChildProcess encoder;const auto mp3=root.getChildFile("input.mp3");require(encoder.start(juce::StringArray{ffmpeg,"-v","error","-y","-i",request.source.getFullPathName(),"-c:a","libmp3lame","-b:a","192k",mp3.getFullPathName()}),"Encoder start");require(encoder.waitForProcessToFinish(10000) && encoder.getExitCode()==0,"MP3 fixture encode");
        request.source=mp3;request.destination=root.getChildFile("output.mp3");request.semitones=-12;request.rack=tdEffects::captureState(TDSpeedVSTAudioProcessor(2).apvts);
        result=tdFile::render(request,progress,[]{return false;});if(result.error.isNotEmpty())throw std::runtime_error(result.error.toStdString());require(request.destination.getSize()<mp3.getSize()*3,"Compressed render became uncompressed or overinflated");
        std::cout<<"MP3 input bytes="<<mp3.getSize()<<" output bytes="<<request.destination.getSize()<<"\n";
        tdFile::Job worker(request);require(worker.startThread(),"Render worker cannot start");require(worker.waitForThreadToExit(15000),"Render worker timed out");if(worker.result.error.isNotEmpty())throw std::runtime_error(worker.result.error.toStdString());
        const juce::String extensions[]{".m4a",".ogg",".opus"},codecs[]{"aac","libvorbis","libopus"};
        for(int type=0;type<3;++type) {
            const auto source=root.getChildFile("compressed"+extensions[type]);
            require(encoder.start(juce::StringArray{ffmpeg,"-v","error","-y","-i",mp3.getFullPathName(),"-c:a",codecs[type],"-b:a","128k",source.getFullPathName()}),"Compressed fixture start");
            require(encoder.waitForProcessToFinish(10000) && encoder.getExitCode()==0,"Compressed fixture encode");
            request.source=source;request.destination=root.getChildFile("rendered"+extensions[type]);result=tdFile::render(request,progress,[]{return false;});
            if(result.error.isNotEmpty())throw std::runtime_error(result.error.toStdString());require(result.encoded && request.destination.getSize()>0,"Compressed output missing");
            std::cout<<extensions[type]<<" source bytes="<<source.getSize()<<" output bytes="<<request.destination.getSize()<<"\n";
        }

    }
    if(argc>2) {
        auto editor=std::unique_ptr<juce::AudioProcessorEditor>(state.createEditor());
        auto* ui=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());ui->setEffectsExpanded(true);ui->setAdvanced(true);ui->setUiScale(1);
        juce::FileOutputStream image{juce::File(argv[2])};juce::PNGImageFormat png;png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),image);
    }
    std::cout<<"PASS file render lengths/final sample, 44.1/48/96 and 16/24/32-bit PCM, wet decay, cancellation and source protection; fixtures "<<root.getFullPathName()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
