#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "interfaces.h"

namespace ostium {

// Decides when the lock output is on. Unlock arms a one-shot relock timer and
// then turns the output on. The timer callback turns the output off by itself,
// so a blocked main loop can't hold the door unlocked (R12).
//
// On the board, the relock callback runs in the esp_timer task, on the other
// core. begin(), unlock(), lock(), and the callback take one mutex, so they
// never interleave. Keep esp_timer's default ESP_TIMER_TASK dispatch, because
// the callback takes a mutex.
class LockController {
 public:
  static constexpr uint32_t kDefaultUnlockMs = 3000;
  static constexpr uint32_t kMaxUnlockMs = 5000;

  // Unlock times above kMaxUnlockMs are clamped to kMaxUnlockMs (R10).
  LockController(LockOutput& output, OneShotTimer& timer,
                 uint32_t unlockMs = kDefaultUnlockMs);

  // Writes the locked level. Call it as the first statement of setup() (R1).
  void begin();

  // Arms the relock timer, then turns the output on (R9). Does nothing while
  // the door is already unlocked, so no input extends an unlock (R10).
  // Returns false, and leaves the door locked, if the timer can't be armed.
  bool unlock();

  // Turns the output off right away and cancels the relock timer (R11).
  void lock();

  bool isUnlocked() const { return unlocked_.load(); }
  uint32_t unlockMs() const { return unlockMs_; }

 private:
  static void onRelockTimer(void* context);

  LockOutput& output_;
  OneShotTimer& timer_;
  const uint32_t unlockMs_;
  std::mutex mutex_;
  std::atomic<bool> unlocked_{false};
};

}  // namespace ostium
