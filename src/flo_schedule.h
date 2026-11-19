#pragma once
#include <cstdint>

namespace flo_schedule {
constexpr int64_t kDailyInterval = 86400;
constexpr int64_t kRetryInterval = 300;

inline bool due(int64_t now, int64_t lastRefresh, int64_t anchor, int64_t maxSpan,
                int64_t retryAt, bool catchingUp) {
    if (now <= 0 || now < retryAt) return false;
    return catchingUp || lastRefresh <= 0 || now >= lastRefresh + kDailyInterval || now - anchor > maxSpan;
}
}  // namespace flo_schedule
