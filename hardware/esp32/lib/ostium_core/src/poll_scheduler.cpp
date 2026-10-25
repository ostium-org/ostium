#include "poll_scheduler.h"

#include "timing.h"

namespace ostium {

bool PollScheduler::due(uint32_t nowMs) const {
  return !hasStarted_ || reached(nowMs, nextDueMs_);
}

void PollScheduler::started(uint32_t nowMs) {
  hasStarted_ = true;
  // Start to start. If the poll takes longer than the interval, the next one
  // is due as soon as it finishes.
  nextDueMs_ = nowMs + kIntervalMs;
}

void PollScheduler::succeeded() { failures_ = 0; }

void PollScheduler::failed(uint32_t nowMs) {
  ++failures_;
  nextDueMs_ = nowMs + retryBackoffMs(failures_);
}

}  // namespace ostium
