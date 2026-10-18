#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#include <memory>

namespace tdDaw {
// One atomic publication includes both origin and latched pitch. Zero means
// stopped. No mutex, allocation or waiting is required by the audio callback.
inline uint64_t encode(int64_t origin,float semi) noexcept {
    constexpr int64_t offset=int64_t(1)<<55;
    return (uint64_t(origin+offset)<<8)|uint64_t(juce::jlimit(0,240,juce::roundToInt((semi+12)*10))+1);
}
inline int64_t origin(uint64_t state) noexcept {return int64_t(state>>8)-(int64_t(1)<<55);}
inline float semi(uint64_t state) noexcept {return (int(state&255)-1)*.1f-12;}
struct Group {
    juce::String id,name;
    std::atomic<uint64_t> run{0};
    std::atomic<int64_t> highWater{INT64_MIN};
    std::atomic<float> pendingPitch{0};
    std::atomic<bool> bypass{false};
    std::atomic<int> externalBypasses{0};
    std::atomic<int64_t> capacity{0};
    std::atomic<double> sampleRate{0};
    std::atomic<uint64_t> leader{0};
    std::atomic<int> members{0};
    std::atomic<uint32_t> activeMask{0};
    std::array<std::atomic<uint64_t>,32> memberKeys{},candidateSerial{};
    std::array<std::atomic<int64_t>,32> candidateOrigin{};
    std::atomic<uint64_t> seekSerial{1};
    std::atomic<bool> publishingSeek{false};
};
struct Registry {
    std::mutex mutex;std::vector<std::weak_ptr<Group>> groups;std::atomic<uint64_t> nextMember{1};
    static Registry& instance(){static Registry r;return r;}
    std::shared_ptr<Group> obtain(const juce::String& id,const juce::String& name) {
        std::lock_guard<std::mutex> lock(mutex);
        for(auto it=groups.begin();it!=groups.end();)if(auto g=it->lock()){if(g->id==id)return g;++it;}else it=groups.erase(it);
        auto g=std::make_shared<Group>();g->id=id;g->name=name.isEmpty()?"Linked tracks":name;groups.push_back(g);return g;
    }
    std::vector<std::pair<juce::String,juce::String>> list(){std::lock_guard<std::mutex> lock(mutex);std::vector<std::pair<juce::String,juce::String>> result;for(auto& w:groups)if(auto g=w.lock())if(g->members.load()>0)result.emplace_back(g->id,g->name);return result;}
};
}
