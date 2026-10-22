#pragma once

#include <cstdint>

namespace ostium {

// True once nowMs has reached deadlineMs. Safe across millis() wraparound as
// long as the two values are less than about 24.8 days apart.
inline bool reached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

// Wait before the next attempt after this many consecutive failures:
// 2, 4, then 8 seconds, for both polls and event posts.
inline uint32_t retryBackoffMs(uint32_t failures) {
  if (failures == 0) return 0;
  if (failures == 1) return 2000;
  if (failures == 2) return 4000;
  return 8000;
}

}  // namespace ostium
