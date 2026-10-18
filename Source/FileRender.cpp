#include "FileRender.h"
#include <stdexcept>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#endif
namespace tdFile {
static juce::File executable(const juce::String& name) {
    const auto local=juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(name);
    if(local.existsAsFile())return local;
    juce::StringArray paths;paths.addTokens(juce::SystemStats::getEnvironmentVariable("PATH",{}),";", juce::String::charToString(34));
    for(auto path:paths){auto file=juce::File(path.unquoted()).getChildFile(name);if(file.existsAsFile())return file;}
    return {};
}
static juce::String run(const juce::StringArray& args,const std::function<bool()>& cancelled,bool capture=true) {
    juce::ChildProcess process;if(!process.start(args,capture?(juce::ChildProcess::wantStdOut|juce::ChildProcess::wantStdErr):0))throw std::runtime_error("Could not start FFmpeg/ffprobe");
    juce::String output;char bytes[1024];
    while(process.isRunning()) {
        if(cancelled()){process.kill();process.waitForProcessToFinish(3000);throw std::runtime_error("Render cancelled");}
        const int n=capture?process.readProcessOutput(bytes,1024):0;if(n>0 && output.length()<16384)output+=juce::String::fromUTF8(bytes,n);
        juce::Thread::sleep(10);
    }
    if(capture)output+=process.readAllProcessOutput();
    if(process.getExitCode()!=0)throw std::runtime_error(("Encoder failed: "+output.substring(0,1200)).toStdString());
    return output;
}
Result render(const Request& request,std::atomic<float>& progress,const std::function<bool()>& cancelled) {
    Result result;
#if JUCE_WINDOWS
    struct ComSession {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComSession(){if(SUCCEEDED(hr))CoUninitialize();}} com;
#endif
    try {
        if(request.source==request.destination)throw std::runtime_error("Choose a new filename; the source is never overwritten");
        if(!request.input && !request.source.existsAsFile())throw std::runtime_error("Source file not found");
        if(!std::isfinite(request.semitones) || request.semitones < -12 || request.semitones > 12)throw std::runtime_error("Invalid pitch");
        if(cancelled())throw std::runtime_error("Render cancelled");
        const auto ffmpeg=executable("ffmpeg.exe"),ffprobe=executable("ffprobe.exe");
        juce::TemporaryFile decoded(".wav");
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        auto reader=std::unique_ptr<juce::AudioFormatReader>(request.input?nullptr:formats.createReaderFor(request.source));
        if(!request.input && !reader && ffmpeg.existsAsFile()) {
            tdFile::run({ffmpeg.getFullPathName(),"-v","error","-nostdin","-y","-i",request.source.getFullPathName(),"-map","0:a:0","-c:a","pcm_f32le",decoded.getFile().getFullPathName()},cancelled,false);
            reader.reset(formats.createReaderFor(decoded.getFile()));
        }
        if(!request.input && !reader)throw std::runtime_error("Cannot read this audio file");
        const double sr=request.input?request.input->sampleRate:reader->sampleRate,speed=std::pow(2.,request.semitones/12.);
        const int channels=request.input?request.input->samples.getNumChannels():(int)reader->numChannels;
        const int64_t sourceLength=request.input?request.input->samples.getNumSamples():reader->lengthInSamples;
        const int sourceBits=request.input?request.input->bitsPerSample:(int)reader->bitsPerSample;
        const bool sourceFloating=request.input?request.input->floatingPoint:reader->usesFloatingPointData;
        if(sourceLength<=0 || !std::isfinite(sr) || sr<=0)throw std::runtime_error("Source has no usable audio");
        if(channels<1 || channels>2)throw std::runtime_error("File rendering currently supports mono or stereo sources");
        const auto musicFrames=(int64_t)std::ceil(sourceLength/speed);
        const auto extension=request.destination.getFileExtension().toLowerCase();
        const bool encoded=extension==".mp3" || extension==".m4a" || extension==".aac" || extension==".ogg" || extension==".opus";
        if(encoded && (!ffmpeg.existsAsFile() || !ffprobe.existsAsFile()))throw std::runtime_error("MP3/AAC/OGG/Opus export needs FFmpeg and ffprobe on PATH. Choose FLAC/WAV/AIFF instead, or install FFmpeg");
        int bitrate=0;
        if(encoded) {
            const auto metadata=juce::JSON::parse(tdFile::run({ffprobe.getFullPathName(),"-v","error","-select_streams","a:0","-show_entries","stream=bit_rate:format=bit_rate","-of","json",request.source.getFullPathName()},cancelled));
            const auto streams=metadata["streams"];if(streams.isArray() && streams.size()>0)bitrate=streams[0]["bit_rate"].toString().getIntValue();
            if(bitrate<=0)bitrate=metadata["format"]["bit_rate"].toString().getIntValue();
            if(bitrate<=0)throw std::runtime_error("Could not determine the source bitrate; choose FLAC/WAV/AIFF instead");
        }
        const bool rackBypassed=(float)request.rack.getChildWithProperty("id","rackBypass").getProperty("value",0)>.5f;
        for(int slot=0;slot<3;++slot) {
            const auto on=request.rack.getChildWithProperty("id",tdEffects::slotId(slot,"on"));
            const auto unavailable=request.rack.getChildWithProperty("id",tdEffects::slotId(slot,"unavailable"));
            const int selected=tdEffects::effectIndex((float)request.rack.getChildWithProperty("id",tdEffects::slotId(slot,"effect")).getProperty("value",0));
            if(!rackBypassed && (float)on.getProperty("value",0)>.5f && ((float)unavailable.getProperty("value",0)>.5f || !tdEffects::catalogue()[(size_t)selected].available || request.rack.hasProperty(tdEffects::slotId(slot,"missingEngine"))))
                throw std::runtime_error("An enabled saved effect is unavailable. Replace or bypass it before rendering");
        }
        TDSpeedVSTAudioProcessor processor(2);tdEffects::restoreState(processor.apvts,request.rack);processor.prepareFileEffects(sr);
        const double tail=processor.getTailLengthSeconds();
        if(!std::isfinite(tail) || tail>600)throw std::runtime_error("Disable freeze or infinite feedback before rendering");
        const auto total=musicFrames+(int64_t)std::ceil(std::max(0.,tail)*sr);
        juce::TemporaryFile output(request.destination);
        // Compressed export uses a temporary float WAV only during encoding.
        // It is deleted on completion/cancellation; the final file stays compressed.
        juce::TemporaryFile intermediate(".wav");
        const auto renderFile=encoded?intermediate.getFile():output.getFile();
        std::unique_ptr<juce::AudioFormat> format;
        if(encoded || extension==".wav")format=std::make_unique<juce::WavAudioFormat>();
        else if(extension==".flac")format=std::make_unique<juce::FlacAudioFormat>();
        else if(extension==".aif" || extension==".aiff")format=std::make_unique<juce::AiffAudioFormat>();
        else throw std::runtime_error("Choose WAV, FLAC, AIFF, MP3, M4A, AAC, OGG or Opus");
        const int bits=encoded?32:sourceBits;
        const bool floating=encoded || sourceFloating;
        if(floating && !encoded && extension!=".wav")throw std::runtime_error("Use WAV to preserve floating-point source samples");
        if(!format->getPossibleBitDepths().contains(bits))throw std::runtime_error("This output format cannot preserve the source bit depth. Choose a compatible format");
        std::unique_ptr<juce::OutputStream> stream=renderFile.createOutputStream();
        if(!stream)throw std::runtime_error("Cannot create output file");
        auto options=juce::AudioFormatWriterOptions{}.withSampleRate(sr).withNumChannels(channels).withBitsPerSample(bits)
            .withSampleFormat(floating?juce::AudioFormatWriterOptions::SampleFormat::floatingPoint:juce::AudioFormatWriterOptions::SampleFormat::integral);
        if(extension==".flac")options=options.withQualityOptionIndex(5);
        auto writer=format->createWriterFor(stream,options);if(!writer)throw std::runtime_error("Cannot create writer with the source format/bit depth");
        juce::AudioBuffer<float> source(2,4096),block(2,512);int64_t cacheStart=-4096;
        auto read=[&](int channel,int64_t frame)->float {if(frame<0 || frame>=sourceLength)return 0;return source.getSample(channel,(int)(frame-cacheStart));};
        for(int64_t offset=0;offset<total;offset+=512) {
            if(cancelled())throw std::runtime_error("Render cancelled");
            const int n=(int)std::min<int64_t>(512,total-offset);block.setSize(2,n,false,false,true);block.clear();
            for(int i=0;i<n && offset+i<musicFrames;++i) {
                const double position=(offset+i)*speed;const auto base=(int64_t)std::floor(position);const float frac=(float)(position-base);
                if(base-1<cacheStart || base+2>=cacheStart+4096) {cacheStart=std::max<int64_t>(0,base-1);source.clear();if(request.input) {
                        const int count=(int)std::min<int64_t>(4096,sourceLength-cacheStart);
                        for(int c=0;c<2;++c)source.copyFrom(c,0,request.input->samples,juce::jmin(c,channels-1),(int)cacheStart,count);
                    } else if(!reader->read(&source,0,4096,cacheStart,true,true))throw std::runtime_error("Source read failed");}
                for(int c=0;c<2;++c){const float a=read(c,base-1),b=read(c,base),d=read(c,base+1),e=read(c,base+2);
                    const float c1=.5f*(d-a),c2=a-2.5f*b+2*d-.5f*e,c3=.5f*(e-a)+1.5f*(b-d);
                    block.setSample(c,i,((c3*frac+c2)*frac+c1)*frac+b);}
            }
            processor.processFileEffects(block);
            if(processor.legacyEffectFaulted())throw std::runtime_error("Legacy reverb fault; output was not saved");
            for(int slot=0;slot<3;++slot)if(processor.effects.faulted(slot))throw std::runtime_error("Effect fault; output was not saved");
            for(int c=0;c<2;++c)for(int i=0;i<n;++i)if(!std::isfinite(block.getSample(c,i)) || std::abs(block.getSample(c,i))>32)throw std::runtime_error("Effect output is unstable; output was not saved");
            if(channels==1)for(int i=0;i<n;++i)block.setSample(0,i,.5f*(block.getSample(0,i)+block.getSample(1,i)));
            if(!writer->writeFromAudioSampleBuffer(block,0,n))throw std::runtime_error("Output write failed; check free disk space");
            progress.store((float)((offset+n)/(double)total)*(encoded?.85f:1.f));
        }
        writer.reset();
        if(encoded) {
            juce::String codec,mux;
            if(extension==".mp3"){codec="libmp3lame";mux="mp3";}
            if(extension==".m4a" || extension==".aac"){codec="aac";mux=extension==".m4a"?"ipod":"adts";}
            if(extension==".ogg"){codec="libvorbis";mux="ogg";}
            if(extension==".opus"){codec="libopus";mux="opus";}
            tdFile::run({ffmpeg.getFullPathName(),"-v","error","-nostdin","-y","-i",intermediate.getFile().getFullPathName(),"-i",request.source.getFullPathName(),"-map","0:a:0","-map_metadata","1","-c:a",codec,"-b:a",juce::String(bitrate),"-ar",juce::String((int)sr),"-f",mux,output.getFile().getFullPathName()},cancelled,false);
        }
        if(cancelled())throw std::runtime_error("Render cancelled");
        if(!output.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not commit output file; check permissions");
        result.file=request.destination;result.duration=total/sr;result.encoded=encoded;progress=1;
    } catch(const std::exception& e){result.error=e.what();}catch(...){result.error="File render failed";}
    return result;
}

static std::unique_ptr<tdPreview::Audio> loadAudio(const juce::File& file,const juce::File& original,bool temporary,const std::function<bool()>& cancelled) {
#if JUCE_WINDOWS
    struct ComSession {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComSession(){if(SUCCEEDED(hr))CoUninitialize();}} com;
#endif
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    auto reader=std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));juce::TemporaryFile decoded(".wav");
    if(!reader) {
        const auto ffmpeg=executable("ffmpeg.exe");
        if(ffmpeg.existsAsFile()) {
            tdFile::run({ffmpeg.getFullPathName(),"-v","error","-nostdin","-y","-i",file.getFullPathName(),"-map","0:a:0","-c:a","pcm_f32le",decoded.getFile().getFullPathName()},cancelled,false);
            reader.reset(formats.createReaderFor(decoded.getFile()));
        }
    }
    if(!reader || reader->lengthInSamples<=0 || !std::isfinite(reader->sampleRate) || reader->sampleRate<=0)throw std::runtime_error("Could not load this audio file; try WAV/FLAC/MP3 or install FFmpeg for other formats");
    if(reader->numChannels<1 || reader->numChannels>2)throw std::runtime_error("Load a mono or stereo audio file");
    if(reader->lengthInSamples>1024LL*1024*1024/(4*reader->numChannels))throw std::runtime_error("Song exceeds the 1 GiB audition limit; choose a shorter source file");
    auto prepared=std::make_unique<tdPreview::Audio>();prepared->file=file;prepared->original=original;prepared->sampleRate=reader->sampleRate;prepared->temporary=temporary;prepared->bitsPerSample=(int)reader->bitsPerSample;prepared->floatingPoint=reader->usesFloatingPointData;
    prepared->samples.setSize((int)reader->numChannels,(int)reader->lengthInSamples);
    for(int offset=0;offset<prepared->samples.getNumSamples();offset+=65536) {
        if(cancelled())throw std::runtime_error("Operation cancelled");
        const int n=juce::jmin(65536,prepared->samples.getNumSamples()-offset);
        if(!reader->read(&prepared->samples,offset,n,offset,true,true))throw std::runtime_error("Could not read audio");
        for(int c=0;c<prepared->samples.getNumChannels();++c)for(int i=offset;i<offset+n;++i)if(!std::isfinite(prepared->samples.getSample(c,i)) || std::abs(prepared->samples.getSample(c,i))>32.f)throw std::runtime_error("Audio contains invalid or out-of-range samples");
    }
    prepared->buildPeaks();return prepared;
}
void LoadJob::run() {
    try {audio=loadAudio(source,source,false,[this]{return threadShouldExit();});}
    catch(const std::exception& e){error=e.what();}catch(...){error="Could not load the song";}
    done.store(true,std::memory_order_release);
}
void Job::run() {
    result=render(request,progress,[this]{return threadShouldExit();});
    if(loadAudition && result.error.isEmpty()) {
        try {audio=loadAudio(result.file,request.source,true,[this]{return threadShouldExit();});audio->semitones=request.semitones;audio->rack=request.rack.createCopy();}
        catch(const std::exception& e){result.error=e.what();}catch(...){result.error="Could not allocate preview audio";}
    }
    if(loadAudition && result.error.isNotEmpty() && result.file.existsAsFile())result.file.deleteFile();
    done.store(true,std::memory_order_release);
}

void SaveJob::run() {
    try {
        if(destination==original || destination==rendered)throw std::runtime_error("Choose a new filename; the original and temporary render are preserved");
        if(!rendered.existsAsFile())throw std::runtime_error("Rendered audio is missing; render the source again");
        juce::TemporaryFile output(destination);const auto ext=destination.getFileExtension().toLowerCase();
        if(ext==".wav") {
            if(!rendered.copyFileTo(output.getFile()))throw std::runtime_error("Could not copy WAV; check disk space and permissions");
        } else if(ext==".flac" || ext==".aif" || ext==".aiff") {
            juce::WavAudioFormat wav;auto input=rendered.createInputStream();if(!input)throw std::runtime_error("Could not open rendered WAV");
            auto reader=std::unique_ptr<juce::AudioFormatReader>(wav.createReaderFor(input.release(),true));
            if(!reader)throw std::runtime_error("Could not open rendered WAV");
            std::unique_ptr<juce::AudioFormat> format;
            if(ext==".flac")format=std::make_unique<juce::FlacAudioFormat>();else format=std::make_unique<juce::AiffAudioFormat>();
            if(reader->usesFloatingPointData || !format->getPossibleBitDepths().contains((int)reader->bitsPerSample))throw std::runtime_error("Use WAV to preserve this render's bit depth");
            std::unique_ptr<juce::OutputStream> stream=output.getFile().createOutputStream();
            auto writer=format->createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(reader->sampleRate).withNumChannels((int)reader->numChannels).withBitsPerSample((int)reader->bitsPerSample));
            if(!writer)throw std::runtime_error("Could not create saved file");
            juce::AudioBuffer<float> block((int)reader->numChannels,65536);
            for(int64_t offset=0;offset<reader->lengthInSamples;offset+=65536) {
                if(threadShouldExit())throw std::runtime_error("Save cancelled");
                const int n=(int)juce::jmin<int64_t>(65536,reader->lengthInSamples-offset);
                if(!reader->read(&block,0,n,offset,true,true) || !writer->writeFromAudioSampleBuffer(block,0,n))throw std::runtime_error("Save failed; check disk space");
            }
        } else {
            const auto ffmpeg=executable("ffmpeg.exe"),ffprobe=executable("ffprobe.exe");
            if(!ffmpeg.existsAsFile() || !ffprobe.existsAsFile())throw std::runtime_error("Compressed saving needs FFmpeg and ffprobe on PATH. Save WAV instead, or install FFmpeg");
            auto cancelled=[this]{return threadShouldExit();};
            const auto metadata=juce::JSON::parse(tdFile::run({ffprobe.getFullPathName(),"-v","error","-select_streams","a:0","-show_entries","stream=bit_rate:format=bit_rate","-of","json",original.getFullPathName()},cancelled));
            int bitrate=0;const auto streams=metadata["streams"];if(streams.isArray() && streams.size()>0)bitrate=streams[0]["bit_rate"].toString().getIntValue();
            if(bitrate<=0)bitrate=metadata["format"]["bit_rate"].toString().getIntValue();
            // Changing a PCM source to a compressed format uses a clear, bounded music bitrate.
            bitrate=juce::jlimit(32000,320000,bitrate>0?bitrate:192000);
            juce::String codec,mux;
            if(ext==".mp3"){codec="libmp3lame";mux="mp3";}
            else if(ext==".m4a" || ext==".aac"){codec="aac";mux=ext==".m4a"?"ipod":"adts";}
            else if(ext==".ogg"){codec="libvorbis";mux="ogg";}
            else if(ext==".opus"){codec="libopus";mux="opus";}
            else throw std::runtime_error("Choose WAV, FLAC, AIFF, MP3, M4A, AAC, OGG or Opus");
            tdFile::run({ffmpeg.getFullPathName(),"-v","error","-nostdin","-y","-i",rendered.getFullPathName(),"-i",original.getFullPathName(),"-map","0:a:0","-map_metadata","1","-c:a",codec,"-b:a",juce::String(bitrate),"-f",mux,output.getFile().getFullPathName()},cancelled,false);
        }
        if(threadShouldExit())throw std::runtime_error("Save cancelled");
        if(!output.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not save file; check permissions");
    } catch(const std::exception& e){error=e.what();}catch(...){error="Save failed";}
    done.store(true,std::memory_order_release);
}
}
