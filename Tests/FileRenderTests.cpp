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
    // Render-to-preview is a worker job, not a save-first workflow. The saved
    // file and in-memory audition contain the same already-processed samples.
    request.source=root.getChildFile("Preview music.wav");request.destination=root.getChildFile("Preview slowed.wav");request.semitones=-5;
    request.rack=tdEffects::captureState(TDSpeedVSTAudioProcessor(2).apvts);
    {
        juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=request.source.createOutputStream();
        auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(24));
        juce::AudioBuffer<float> music(2,48000*12);
        for(int c=0;c<2;++c)for(int i=0;i<music.getNumSamples();++i){const double t=i/48000.;const double envelope=(.4+.6*std::pow(std::sin(t*3.5),2))*std::sin(juce::MathConstants<double>::pi*t/12);music.setSample(c,i,(float)(envelope*(.18*std::sin(t*(1382.3+c*2))+.07*std::sin(t*2073.45)+.04*std::sin(t*3455.75))));}
        require(writer && writer->writeFromAudioSampleBuffer(music,0,music.getNumSamples()),"Preview fixture write");
    }
    tdFile::Job audition(request,true);require(audition.startThread() && audition.waitForThreadToExit(15000),"Preview worker failed");
    require(audition.result.error.isEmpty() && audition.audio && audition.audio->samples.getNumSamples()>48000*12,"Preview did not load the extended result");
    require(audition.audio->file==request.destination && audition.audio->sampleRate==48000,"Preview uses wrong file or rate");
    const float expected=audition.audio->samples.getSample(0,12345);
    state.prepareToPlay(48000,257);state.preview.install(std::move(audition.audio));
    juce::AudioBuffer<float> previewBlock(2,257);previewBlock.clear();juce::MidiBuffer midi;
    state.preview.seek(12345/48000.);state.preview.play();
    tdEffects::setParameter(state.apvts,"masterOutput",-24);tdEffects::setParameter(state.apvts,"bypass",1);state.processBlock(previewBlock,midi);
    require(std::abs(previewBlock.getSample(0,0)-expected)<1e-6,"Preview reapplies live pitch, effects or output gain");
    const auto held=state.preview.position.load();state.preview.playing=false;previewBlock.clear();
    require(!state.preview.render(previewBlock,48000) && state.preview.position.load()==held,"Pause advances preview or replaces live sound");
    state.preview.seek(1);state.preview.play();require(state.preview.render(previewBlock,96000),"Preview sample-rate conversion failed");
    require(std::abs(state.preview.position.load()-(1.+257./96000))<1e-8,"Preview rate conversion changes speed");
    state.preview.stop();require(!state.preview.playing && state.preview.position.load()==0,"Preview Stop does not rewind");
    state.preview.play();state.setNonRealtime(true);const auto offlineHeld=state.preview.position.load();previewBlock.clear();state.processBlock(previewBlock,midi);
    require(previewBlock.getMagnitude(0,257)==0 && state.preview.position.load()==offlineHeld,"Audition contaminates host offline export");state.setNonRealtime(false);state.preview.stop();
    state.preview.seek(state.preview.audio()->duration()-.00001);state.preview.play();state.preview.render(previewBlock,48000);require(!state.preview.playing,"Preview does not stop at end");
    state.preview.stop();
    RenderedWaveform waveform(state.preview);waveform.setSize(748,162);waveform.resetView();const double full=waveform.visibleDuration();
    waveform.zoom(4,.5);require(std::abs(waveform.visibleDuration()-full/4)<1e-8 && waveform.startTime()>0,"Waveform zoom does not preserve anchor");
    waveform.zoom(1e10,1);require(waveform.visibleDuration()>=.01999 && waveform.startTime()+waveform.visibleDuration()<=full+.0001,"Waveform zoom bounds invalid");waveform.resetView();
    tdFile::SaveJob save(request.destination,request.source,root.getChildFile("Saved copy.wav"));require(save.startThread() && save.waitForThreadToExit(5000) && save.error.isEmpty(),"Save As WAV failed");
    require(save.destination.hasIdenticalContentTo(request.destination),"Save As WAV changes rendered audio");
    save.destination.replaceWithText("old output");tdFile::SaveJob overwrite(request.destination,request.source,save.destination);require(overwrite.startThread() && overwrite.waitForThreadToExit(5000) && overwrite.error.isEmpty() && overwrite.destination.hasIdenticalContentTo(request.destination),"Save As overwrite fails");
    tdFile::SaveJob missing(request.destination,request.source,root.getChildFile("missing folder/output.wav"));require(missing.startThread() && missing.waitForThreadToExit(5000) && missing.error.isNotEmpty() && request.destination.existsAsFile(),"Failed Save As loses render or falsely succeeds");
    tdFile::SaveJob protect(request.destination,request.source,request.source);require(protect.startThread() && protect.waitForThreadToExit(5000) && protect.error.isNotEmpty(),"Save As overwrites original source");
    tdFile::SaveJob flac(request.destination,request.source,root.getChildFile("Saved copy.flac"));require(flac.startThread() && flac.waitForThreadToExit(5000) && flac.error.isEmpty(),"Save As FLAC failed");
    auto savedFlac=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(flac.destination));require(savedFlac && savedFlac->lengthInSamples==state.preview.audio()->samples.getNumSamples() && savedFlac->sampleRate==48000 && savedFlac->bitsPerSample==24,"Save As FLAC loses rate/depth/ending");
    if(argc>1) {
        tdFile::SaveJob compressed(request.destination,root.getChildFile("input.mp3"),root.getChildFile("Saved copy.mp3"));
        require(compressed.startThread() && compressed.waitForThreadToExit(15000) && compressed.error.isEmpty(),"Save As compressed failed");
        require(compressed.destination.getSize()<request.destination.getSize()/3,"Compressed Save As unexpectedly large");
    }
    if(argc>2) {
        auto editor=std::unique_ptr<juce::AudioProcessorEditor>(state.createEditor());
        auto* ui=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());ui->setEffectsExpanded(false);ui->setUiScale(1);
        require(ui->hasPreview(),"Reopened editor lost rendered waveform");
        const int normalHeight=ui->getHeight();ui->setEffectsExpanded(true);require(ui->getHeight()>normalHeight,"Effects expansion does not grow waveform editor");ui->setEffectsExpanded(false);
        for(float scale:{.5f,1.f,1.5f,2.f}){ui->setUiScale(scale);require(ui->effectsFit(),"Scaled preview clips effect controls");}ui->setUiScale(1);
        juce::FileOutputStream image{juce::File(argv[2])};image.setPosition(0);image.truncate();juce::PNGImageFormat png;png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),image);
    }
    std::cout<<"PASS file render lengths/final sample, 44.1/48/96 and 16/24/32-bit PCM, wet decay, cancellation, preview playback/seek/rate/end, offline isolation, waveform zoom/scaling/reopening and Save As WAV/FLAC/overwrite/failure/source protection; fixtures "<<root.getFullPathName()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
