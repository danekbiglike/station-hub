#pragma once
#include <algorithm>
#include <chrono>
#include <optional>

// The Station acknowledges a voice request before reporting the resulting level.
// One request may be in flight; retain the latest user choice until that request
// is observed. This avoids displaying stale state and preserves a quick reversal.
class VolumeIntent {
public:
    using Clock = std::chrono::steady_clock;
    std::optional<int> select(int observed, int requested, Clock::time_point now) {
        if (observed < 0 || observed > 10 || requested < 1 || requested > 10) return {};
        requested = std::min(requested, std::min(10, observed + 1));
        if (inFlight_ >= 0) { desired_ = requested; return {}; }
        if (requested == observed) return {};
        desired_ = requested;
        return begin(requested, now);
    }
    std::optional<int> observe(bool connected, int observed, Clock::time_point now) {
        if (inFlight_ < 0) return {};
        if (!connected || observed < 0 || observed > 10 || now >= deadline_) { clear(); return {}; }
        if (observed != inFlight_) return {};
        inFlight_ = -1;
        if (desired_ >= 1 && desired_ != observed)
            return begin(std::min(desired_, std::min(10, observed + 1)), now);
        desired_ = -1;
        return {};
    }
    int desired() const { return desired_; }
private:
    std::optional<int> begin(int value, Clock::time_point now) {
        inFlight_ = value; deadline_ = now + std::chrono::seconds(8); return value;
    }
    void clear() { inFlight_ = desired_ = -1; }
    int inFlight_ = -1, desired_ = -1;
    Clock::time_point deadline_{};
};
