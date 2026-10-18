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
    // Loading only decodes the original. Pitch/effects are auditioned live, and
    // the complete render is an independent later command from frame zero.
    const int filesBefore=root.getNumberOfChildFiles(juce::File::findFiles);
    tdFile::LoadJob loaded(request.source);require(loaded.startThread() && loaded.waitForThreadToExit(15000),"Source loading timed out");
    require(loaded.error.isEmpty() && loaded.audio && loaded.audio->samples.getNumSamples()==48000*12,"Loading changed source duration");
    require(root.getNumberOfChildFiles(juce::File::findFiles)==filesBefore,"Loading unexpectedly rendered a file");
    TDSpeedVSTAudioProcessor player(2);player.prepareToPlay(48000,257);player.source.install(std::move(loaded.audio));player.setAuditionMode(1);
    tdEffects::setParameter(player.apvts,"bypass",0);tdEffects::setParameter(player.apvts,"semitones",-12);
    juce::AudioBuffer<float> songBlock(2,257);juce::MidiBuffer songEvents;const float liveHistory=player.getBacklogSeconds();player.source.play();
    songBlock.clear();player.processBlock(songBlock,songEvents);require(std::abs(player.source.position.load()-257./96000)<1e-9,"Slower source audition has wrong speed");
    const auto sourcePos=player.source.position.load();tdEffects::setParameter(player.apvts,"semitones",12);player.processBlock(songBlock,songEvents);
    require(std::abs(player.source.position.load()-sourcePos-514./48000)<1e-9,"Pitch tweak does not update source playback continuously");
    require(player.getBacklogSeconds()==liveHistory,"File audition grows the live pitch backlog");
    player.source.playing=false;const double paused=player.source.position.load();player.processBlock(songBlock,songEvents);require(player.source.position.load()==paused,"Paused source advances");
    player.restartAudition();require(player.source.position.load()==0,"Restart does not reset source cursor");
    tdEffects::applySpaceVariant(player.apvts,0,0);tdEffects::setParameter(player.apvts,"rack1_mix",100);tdEffects::setParameter(player.apvts,"semitones",-5);
    player.source.play();double wetEnergy=0;for(int block=0;block<300;++block){player.processBlock(songBlock,songEvents);for(int i=0;i<257;++i)wetEnergy+=std::pow(songBlock.getSample(0,i),2);}
    require(wetEnergy>1e-6 && !player.effects.faulted(0),"Live effects audition is silent or faulted");
    tdEffects::setParameter(player.apvts,"rack1_on",0);tdEffects::setParameter(player.apvts,"revOn",1);player.restartAudition();
    juce::AudioBuffer<float> mono(1,257);player.processBlock(mono,songEvents);require(std::isfinite(mono.getMagnitude(0,257)),"Mono source/legacy audition is invalid");
    player.source.stop();request.input=player.source.snapshot();
    const auto diskRender=root.getChildFile("disk-reference.wav");request.destination=diskRender;request.input.reset();
    auto diskResult=tdFile::render(request,progress,[]{return false;});require(diskResult.error.isEmpty(),"Disk reference failed");
    request.input=player.source.snapshot();request.destination=root.getChildFile("memory-render.wav");
    auto memoryResult=tdFile::render(request,progress,[]{return false;});require(memoryResult.error.isEmpty() && request.destination.hasIdenticalContentTo(diskRender),"Loaded-memory render differs from disk source");
    const auto sourceLocation=request.source;request.source=root.getChildFile("DAW temporary source no longer exists.wav");request.destination=root.getChildFile("retained-source.wav");
    auto retained=tdFile::render(request,progress,[]{return false;});require(retained.error.isEmpty() && request.destination.hasIdenticalContentTo(diskRender),"Render depends on a DAW temporary input remaining on disk");request.source=sourceLocation;
    request.destination=root.getChildFile("Preview slowed.wav");
    tdFile::Job audition(request,true);require(audition.startThread() && audition.waitForThreadToExit(15000),"Preview worker failed");
    require(audition.result.error.isEmpty() && audition.audio && audition.audio->samples.getNumSamples()>48000*12,"Preview did not load the extended result");
    require(audition.audio->file==request.destination && audition.audio->sampleRate==48000,"Preview uses wrong file or rate");
    const float expected=audition.audio->samples.getSample(0,12345);
    state.prepareToPlay(48000,257);state.preview.install(std::move(audition.audio));state.setAuditionMode(2);
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
    tdFile::SaveJob compactFlac(request.destination,request.source,root.getChildFile("Lossless preset.flac"),{},tdFile::SaveQuality::flac);
    require(compactFlac.startThread() && compactFlac.waitForThreadToExit(5000) && compactFlac.error.isEmpty(),"Lossless preset failed");
    auto lossless=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(compactFlac.destination));juce::AudioBuffer<float> losslessBlock(2,257);lossless->read(&losslessBlock,0,257,12345,true,true);
    require(std::abs(losslessBlock.getSample(0,0)-state.preview.audio()->samples.getSample(0,12345))<1e-8,"FLAC changed source-depth PCM samples");
    const auto floatFile=root.getChildFile("Float source.wav");
    {juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream=floatFile.createOutputStream();auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(2).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));juce::AudioBuffer<float> samples(2,4800);for(int c=0;c<2;++c)for(int i=0;i<4800;++i)samples.setSample(c,i,.23f*std::sin(i*.017f));require(writer && writer->writeFromAudioSampleBuffer(samples,0,4800),"Float fixture failed");}
    tdFile::SaveJob floatFlac(floatFile,request.source,root.getChildFile("Float to PCM.flac"),{},tdFile::SaveQuality::flac);require(floatFlac.startThread() && floatFlac.waitForThreadToExit(5000) && floatFlac.error.isEmpty(),"Float FLAC preset failed");
    auto floatReader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(floatFlac.destination));require(floatReader && floatReader->bitsPerSample==24 && floatReader->sampleRate==48000 && floatReader->lengthInSamples==4800,"Float FLAC conversion metadata wrong");
    if(argc>1) {
        tdFile::SaveJob compressed(request.destination,root.getChildFile("input.mp3"),root.getChildFile("Saved copy.mp3"));
        require(compressed.startThread() && compressed.waitForThreadToExit(15000) && compressed.error.isEmpty(),"Save As compressed failed");
        require(compressed.destination.getSize()<request.destination.getSize()/3,"Compressed Save As unexpectedly large");
        tdFile::SaveJob aac(request.destination,root.getChildFile("missing original.wav"),root.getChildFile("High quality.m4a"),{},tdFile::SaveQuality::aac320);
        require(aac.startThread() && aac.waitForThreadToExit(15000) && aac.error.isEmpty(),"AAC 320 preset needs original metadata or failed");
        require(aac.destination.getSize()<request.destination.getSize()/3,"AAC preset unexpectedly large");
        tdFile::LoadJob decoded(aac.destination);require(decoded.startThread() && decoded.waitForThreadToExit(15000) && decoded.error.isEmpty() && decoded.audio && decoded.audio->sampleRate==48000 && std::abs(decoded.audio->duration()-state.preview.audio()->duration())<.1,"AAC export lost duration or rate");
        std::cout<<"AAC 320 output bytes="<<aac.destination.getSize()<<" duration="<<state.preview.audio()->duration()<<" path="<<aac.destination.getFullPathName()<<"\n";

    }
    {
        TDSpeedVSTAudioProcessor dropTarget(2);auto droppedEditor=std::unique_ptr<juce::AudioProcessorEditor>(dropTarget.createEditor());
        auto* ui=static_cast<TDSpeedVSTAudioProcessorEditor*>(droppedEditor.get());
        require(ui->isInterestedInFileDrag({request.source.getFullPathName()}) && !ui->isInterestedInFileDrag({root.getChildFile("not-audio.txt").getFullPathName()}),"Drop target accepts invalid audio or rejects a valid file");
        ui->filesDropped({request.source.getFullPathName()},200,200);
        bool loadedOnly=false;
        juce::Timer::callAfterDelay(400,[&]{
            loadedOnly=ui->hasSource() && !ui->hasPreview() && !dropTarget.source.playing;
            auto result=std::make_unique<tdPreview::Audio>();result->samples.setSize(2,100);result->samples.clear();result->original=request.source;result->file=request.destination;result->rack=tdEffects::captureState(dropTarget.apvts);result->buildPeaks();
            dropTarget.preview.install(std::move(result));dropTarget.setAuditionMode(2);ui->refreshAuditionView();tdEffects::setParameter(dropTarget.apvts,"semitones",-5);
        });
        juce::Timer::callAfterDelay(800,[]{juce::MessageManager::getInstance()->stopDispatchLoop();});juce::MessageManager::getInstance()->runDispatchLoop();
        require(loadedOnly && ui->hasSource() && !dropTarget.source.playing,"Dropping a song does not load separately from rendering");
        require(dropTarget.auditionMode.load()==1 && ui->hasPreview(),"Editing rendered controls does not return to source audition while retaining the result");
        if(argc>2) {
            tdEffects::setParameter(dropTarget.apvts,"semitones",-5);tdEffects::setParameter(dropTarget.apvts,"bypass",0);ui->setUiScale(1);
            const auto path=juce::File(argv[2]).getSiblingFile("player-ui-source.png");juce::FileOutputStream image(path);image.setPosition(0);image.truncate();juce::PNGImageFormat png;png.writeImageToStream(ui->createComponentSnapshot(ui->getLocalBounds()),image);
        }

    }
    if(argc>2) {
        tdFile::LoadJob forEditor(request.source);require(forEditor.startThread() && forEditor.waitForThreadToExit(15000) && forEditor.audio,"UI source load failed");state.source.install(std::move(forEditor.audio));tdEffects::restoreState(state.apvts,request.rack);tdEffects::setParameter(state.apvts,"semitones",-5);tdEffects::setParameter(state.apvts,"bypass",0);state.setAuditionMode(2);
        auto editor=std::unique_ptr<juce::AudioProcessorEditor>(state.createEditor());
        auto* ui=static_cast<TDSpeedVSTAudioProcessorEditor*>(editor.get());ui->setEffectsExpanded(false);ui->setUiScale(1);
        require(ui->hasPreview(),"Reopened editor lost rendered waveform");
        const int normalHeight=ui->getHeight();ui->setEffectsExpanded(true);require(ui->getHeight()>normalHeight,"Effects expansion does not grow waveform editor");ui->setEffectsExpanded(false);
        for(float scale:{.5f,1.f,1.5f,2.f}){ui->setUiScale(scale);require(ui->effectsFit(),"Scaled preview clips effect controls");}ui->setUiScale(1);
        juce::FileOutputStream image{juce::File(argv[2])};image.setPosition(0);image.truncate();juce::PNGImageFormat png;png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),image);
        ui->setStandaloneMode();const int closed=ui->getHeight();ui->setFilePanelExpanded(true);require(ui->getHeight()==closed+310,"Song panel expansion height wrong");
        state.setAuditionMode(2);state.preview.play();ui->setFilePanelExpanded(false);require(state.auditionMode.load()==0 && !state.preview.playing && ui->hasSource() && ui->hasPreview(),"Closing song panel does not restore live input or preserve audio");ui->setFilePanelExpanded(true);require(state.auditionMode.load()==2 && !state.preview.playing,"Reopening song panel loses last mode or autoplays");
        juce::FileOutputStream standaloneImage{juce::File(argv[2]).getSiblingFile("song-panel-editor.png")};standaloneImage.setPosition(0);standaloneImage.truncate();png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),standaloneImage);

    }
    std::cout<<"PASS file render lengths/final sample, 44.1/48/96 and 16/24/32-bit PCM, wet decay, cancellation, load without render, source pitch/effects/mono/pause/restart, in-memory rendering, audio file drop, preview playback/seek/rate/end, offline isolation, waveform zoom/scaling/reopening and Save As WAV/FLAC/overwrite/failure/source protection; fixtures "<<root.getFullPathName()<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
