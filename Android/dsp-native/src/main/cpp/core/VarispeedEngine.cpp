#include "VarispeedEngine.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tdspeed {

float VarispeedEngine::rateFor(float semitones) noexcept {
    const float finite = std::isfinite(semitones) ? semitones : 0.0f;
    return std::pow(2.0f, std::clamp(finite, -12.0f, 12.0f) / 12.0f);
}

void VarispeedEngine::prepare(double sampleRate, std::size_t historyFrames, float initialSemitones) {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 192000.0
        || historyFrames < static_cast<std::size_t>(std::ceil(sampleRate * 2.0))
        || historyFrames > maxHistoryFrames) {
        throw std::invalid_argument("Sample rate must be 8-192 kHz; history must be 2 seconds to 2 GiB stereo float");
    }
    // Commit only after both allocations succeed. Never called by the callback.
    std::vector<float> newLeft(historyFrames, 0.0f), newRight(historyFrames, 0.0f);
    left_.swap(newLeft); right_.swap(newRight);
    sampleRate_ = sampleRate;
    writeIndex_ = 0; written_ = 0;
    rate_ = rateFor(initialSemitones);
    smoothing_ = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * 0.001));
    targetBacklog_ = static_cast<std::int64_t>(std::round(0.25 * sampleRate));
    read_ = -static_cast<double>(targetBacklog_);
    recovery_ = Recovery::none; remaining_ = 0; recoveryCount_ = 0;
}

float VarispeedEngine::interpolate(const std::vector<float>& buffer, double position) noexcept {
    const int size = static_cast<int>(buffer.size());
    const int i1 = static_cast<int>(std::floor(position));
    const float fraction = static_cast<float>(position - static_cast<double>(i1));
    const auto at = [&buffer, size](int index) {
        index %= size; if (index < 0) index += size;
        return buffer[static_cast<std::size_t>(index)];
    };
    const float xm1 = at(i1 - 1), x0 = at(i1), x1 = at(i1 + 1), x2 = at(i1 + 2);
    // Preserve the reference's float precision and arithmetic order.
    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

void VarispeedEngine::recenter() noexcept {
    read_ = static_cast<double>(written_) - static_cast<double>(targetBacklog_);
    recovery_ = Recovery::none; remaining_ = 0;
}

void VarispeedEngine::beginRecovery(Recovery reason) noexcept {
    recovery_ = reason;
    remaining_ = static_cast<std::int64_t>(std::llround(3.0 * sampleRate_));
    ++recoveryCount_;
}

void VarispeedEngine::frame(float& left, float& right, float targetRate) noexcept {
    if (recovery_ == Recovery::none
        && static_cast<double>(written_) - read_ >= static_cast<double>(left_.size() - 4))
        beginRecovery(Recovery::historyFull);

    left_[writeIndex_] = std::isfinite(left) ? left : 0.0f;
    right_[writeIndex_] = std::isfinite(right) ? right : 0.0f;
    if (++writeIndex_ == left_.size()) writeIndex_ = 0;
    ++written_;
    rate_ += smoothing_ * (targetRate - rate_);

    if (recovery_ == Recovery::none && read_ >= 0.0 && std::floor(read_) + 2 >= written_)
        beginRecovery(Recovery::caughtLiveInput);
    if (recovery_ != Recovery::none) {
        left = right = 0.0f;
        if (remaining_ > 0 && --remaining_ == 0) recenter();
        return;
    }
    if (read_ < 0.0) left = right = 0.0f;
    else {
        const double position = std::fmod(read_, static_cast<double>(left_.size()));
        left = interpolate(left_, position); right = interpolate(right_, position);
        // Invalid external PCM cannot poison history or reach the phone output.
        if (!std::isfinite(left)) left = 0.0f;
        if (!std::isfinite(right)) right = 0.0f;
    }
    read_ += rate_;
}

bool VarispeedEngine::processInterleaved(float* stereo, std::size_t frames, Controls controls) noexcept {
    if (left_.empty() || (!stereo && frames != 0)) return false;
    if (frames == 0) return true;
    if (controls.recenter) recenter();
    const float targetRate = rateFor(controls.semitones);
    for (std::size_t i = 0; i < frames; ++i) frame(stereo[2*i], stereo[2*i+1], targetRate);
    return true;
}

bool VarispeedEngine::processPlanar(float* left, float* right, std::size_t frames, Controls controls) noexcept {
    if (left_.empty() || (frames != 0 && (!left || !right || left == right))) return false;
    if (frames == 0) return true;
    if (controls.recenter) recenter();
    const float targetRate = rateFor(controls.semitones);
    for (std::size_t i = 0; i < frames; ++i) frame(left[i], right[i], targetRate);
    return true;
}

EngineState VarispeedEngine::state() const noexcept {
    return {written_, read_, sampleRate_ > 0 ? (written_ - read_) / sampleRate_ : 0.0,
            rate_, recovery_, remaining_, recoveryCount_, left_.size()};
}

} // namespace tdspeed
