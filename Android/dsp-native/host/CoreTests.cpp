#include "VarispeedEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace {
thread_local bool countAllocations = false;
thread_local std::size_t allocations = 0;
void* allocate(std::size_t size) {
    if (countAllocations) ++allocations;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
constexpr double pi = 3.14159265358979323846;
}
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment) {
    if (countAllocations) ++allocations;
    const auto a = static_cast<std::size_t>(alignment);
#ifdef _MSC_VER
    if (auto* p = _aligned_malloc(n ? n : 1, a)) return p;
#else
    if (auto* p = std::aligned_alloc(a, ((n + a - 1) / a) * a)) return p;
#endif
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

static void frequencyAndTimeline() {
    for (int fs : {44100, 48000, 96000}) for (float semi : {0.0f, -12.0f, -4.0f, 4.0f}) {
        tdspeed::VarispeedEngine engine;
        engine.prepare(fs, fs * 8, semi);
        float stereo[514];
        int crossings = 0, observed = 0; float previous = 0;
        const float rate = std::pow(2.0f, semi / 12.0f);
        // +4 catches input around 0.96 s; measure a window before recovery.
        const int start = static_cast<int>(fs * 0.6), end = static_cast<int>(fs * 0.9);
        for (int offset = 0; offset < fs; offset += 257) {
            const int n = std::min(257, fs - offset);
            for (int i = 0; i < n; ++i) {
                stereo[2*i] = static_cast<float>(0.25 * std::sin(2 * pi * 440 * (offset+i) / fs));
                stereo[2*i+1] = -0.5f * stereo[2*i];
            }
            require(engine.processInterleaved(stereo, n, {semi}), "process failed");
            for (int i = 0; i < n; ++i) {
                require(std::isfinite(stereo[2*i]), "nonfinite output");
                require(stereo[2*i+1] == -0.5f * stereo[2*i], "stereo relationship changed");
                if (offset+i >= start && offset+i < end) {
                    if (observed && previous <= 0 && stereo[2*i] > 0) ++crossings;
                    previous = stereo[2*i]; ++observed;
                }
            }
        }
        require(std::abs(crossings * static_cast<double>(fs) / observed - 440 * rate) < 5.0,
                "measured frequency does not follow coupled speed");
        if (semi <= 0) require(std::abs(engine.state().readPosition - (-std::round(fs*.25) + fs*static_cast<double>(rate))) < 1e-5,
                               "cursor advance incorrect");
    }
}

static void exactRecovery() {
    for (int fs : {44100, 48000, 96000}) for (float semi : {-12.0f, 12.0f}) {
        tdspeed::VarispeedEngine engine; engine.prepare(fs, fs*2, semi);
        float stereo[514]; bool heard = false; int silent = 0, completed = 0;
        for (int offset = 0; offset < fs*9; offset += 257) {
            const int n = std::min(257, fs*9-offset);
            std::fill_n(stereo, n*2, 0.2f);
            engine.processInterleaved(stereo, n, {semi});
            for (int i=0; i<n; ++i) {
                if (stereo[2*i] == 0 && heard) ++silent;
                else if (stereo[2*i] != 0) {
                    if (silent) { require(silent == fs*3, "recovery silence is not exactly three seconds"); ++completed; silent=0; }
                    heard=true;
                }
            }
        }
        require(completed > 0, "did not resume after recovery");
        std::fill_n(stereo, 514, 0.2f); engine.processInterleaved(stereo, 257, {semi, true});
        require(engine.state().recovery == tdspeed::Recovery::none && engine.state().backlogSeconds < .26,
                "manual recenter did not cancel recovery");
    }
}

static void longTimelineAndReset() {
    tdspeed::VarispeedEngine engine; engine.prepare(48000, 48000*120, -12);
    constexpr int length = 180 * 48000; float stereo[514];
    for (int offset=0; offset<length; offset+=257) {
        const int n=std::min(257,length-offset);
        for(int i=0;i<n;++i) { stereo[2*i]=static_cast<float>(offset+i)/length; stereo[2*i+1]=-0.5f*stereo[2*i]; }
        engine.processInterleaved(stereo,n,{-12});
        for(int i=0;i<n;++i) {
            const double source=.5*(offset+i)-12000;
            if(source>2) require(std::abs(stereo[2*i]-static_cast<float>(source/length))<1e-6f,"long timeline skipped or repeated");
        }
    }
    require(std::abs(engine.state().backlogSeconds-90.25)<1e-9,"long history lost");
    std::fill_n(stereo,514,0.2f); engine.processInterleaved(stereo,257,{0});
    const double held=engine.state().backlogSeconds;
    for(int i=0;i<1000;++i) { std::fill_n(stereo,514,0.2f); engine.processInterleaved(stereo,257,{0}); }
    require(engine.state().backlogSeconds>90 && std::abs(engine.state().backlogSeconds-held)<.001,"zero pitch discarded backlog");
    engine.processInterleaved(stereo,257,{0,true});
    require(std::abs(engine.state().backlogSeconds-.25)<1e-6,"recenter did not restore 250 ms offset");
}

static void wrapAndPartitionAndAllocation() {
    tdspeed::VarispeedEngine a,b; a.prepare(48000,96000); b.prepare(48000,96000);
    float interleaved[514], left[257], right[257];
    allocations=0; countAllocations=true;
    bool same=true;
    for(int offset=0;offset<48000*12;offset+=257) {
        const int n=std::min(257,48000*12-offset);
        for(int i=0;i<n;++i) {
            interleaved[2*i]=left[i]=static_cast<float>((offset+i)%10007)/20014;
            interleaved[2*i+1]=right[i]=-left[i];
        }
        a.processInterleaved(interleaved,n);
        // Process the same timeline one frame at a time through the planar API.
        for(int i=0;i<n;++i) b.processPlanar(left+i,right+i,1);
        for(int i=0;i<n;++i) same = same && interleaved[2*i]==left[i] && interleaved[2*i+1]==right[i];
    }
    countAllocations=false;
    require(allocations==0,"callback allocated memory");
    require(same,"callback partition or layout changes samples");
    require(a.state().recoveryCount==0 && std::abs(a.state().backlogSeconds-.25)<1e-9,"ordinary ring wrap caused recovery");
    std::cout << "callback allocations=" << allocations << "; 12-second ring wrap and layout/partition checks passed\n";
}

static void validation() {
    tdspeed::VarispeedEngine engine; float pcm[2]{};
    require(!engine.processInterleaved(pcm,1),"unprepared process accepted");
    for(double rate : {0.0, 1.0, std::numeric_limits<double>::quiet_NaN(), 192001.0}) {
        bool threw=false; try { engine.prepare(rate,96000); } catch(const std::invalid_argument&) { threw=true; }
        require(threw,"invalid sample rate accepted");
    }
    engine.prepare(48000,96000);
    require(!engine.processInterleaved(nullptr,1),"null input accepted");
    require(engine.processInterleaved(nullptr,0,{0,true}) && engine.state().writtenFrames==0,"empty block changed state");
    for(int i=0;i<96000;++i) {
        pcm[0]=std::numeric_limits<float>::quiet_NaN(); pcm[1]=std::numeric_limits<float>::infinity();
        engine.processInterleaved(pcm,1,{std::numeric_limits<float>::quiet_NaN()});
        require(std::isfinite(pcm[0]) && std::isfinite(pcm[1]),"invalid PCM escaped");
    }
    engine.prepare(48000,96000); require(engine.state().writtenFrames==0 && engine.state().readPosition==-12000,"prepare retained history");
}

int main() {
    try {
        validation(); frequencyAndTimeline(); exactRecovery(); longTimelineAndReset(); wrapAndPartitionAndAllocation();
        std::cout << "PASS: 44.1/48/96 kHz; unity/-12/-4/+4; exact recovery; 180-second timeline; recenter; zero return; validation\n";
        return 0;
    } catch(const std::exception& error) { countAllocations=false; std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
