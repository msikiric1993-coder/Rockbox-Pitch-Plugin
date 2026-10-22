#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tdspeed {

struct Controls {
    float semitones = 0.0f;
    // One command at a block boundary. Returning pitch to zero never recentres.
    bool recenter = false;
};

enum class Recovery : int { none = 0, historyFull = 1, caughtLiveInput = 2 };

struct EngineState {
    std::int64_t writtenFrames = 0;
    double readPosition = 0.0;
    double backlogSeconds = 0.25;
    float effectiveRate = 1.0f;
    Recovery recovery = Recovery::none;
    std::int64_t recoveryFramesRemaining = 0;
    std::uint64_t recoveryCount = 0;
    std::size_t capacityFrames = 0;
};

// Dry stereo listening algorithm extracted from the hashed Windows 0.4.2-rc.1
// PluginProcessor.cpp. No JUCE, Android, filesystem or routing dependencies.
// One owner thread: prepare allocates; process/state must be serialized with it.
class VarispeedEngine final {
public:
    static constexpr std::size_t maxHistoryFrames = (2ULL * 1024 * 1024 * 1024) / (2 * sizeof(float));

    void prepare(double sampleRate, std::size_t historyFrames, float initialSemitones = 0.0f);
    bool processInterleaved(float* stereo, std::size_t frames, Controls controls = {}) noexcept;
    bool processPlanar(float* left, float* right, std::size_t frames, Controls controls = {}) noexcept;
    EngineState state() const noexcept;

private:
    static float rateFor(float semitones) noexcept;
    static float interpolate(const std::vector<float>& buffer, double position) noexcept;
    void recenter() noexcept;
    void beginRecovery(Recovery reason) noexcept;
    void frame(float& left, float& right, float targetRate) noexcept;

    std::vector<float> left_, right_;
    double sampleRate_ = 0.0;
    std::size_t writeIndex_ = 0;
    std::int64_t written_ = 0;
    std::int64_t targetBacklog_ = 0;
    double read_ = 0.0;
    float rate_ = 1.0f, smoothing_ = 0.0f;
    Recovery recovery_ = Recovery::none;
    std::int64_t remaining_ = 0;
    std::uint64_t recoveryCount_ = 0;
};

} // namespace tdspeed
