#pragma once

#include <cstdint>

namespace ostium {

// Decides when the next poll starts. Successful polls start 1,500 ms apart,
// measured start to start. After a failed poll, the door waits 2, 4, then
// 8 seconds, and returns to 1,500 ms after the next success.
class PollScheduler {
 public:
  static constexpr uint32_t kIntervalMs = 1500;

  // True when a poll should start. The first poll is due right away.
  bool due(uint32_t nowMs) const;

  void started(uint32_t nowMs);
  void succeeded();
  void failed(uint32_t nowMs);

  uint32_t failures() const { return failures_; }

 private:
  bool hasStarted_ = false;
  uint32_t nextDueMs_ = 0;
  uint32_t failures_ = 0;
};

}  // namespace ostium
