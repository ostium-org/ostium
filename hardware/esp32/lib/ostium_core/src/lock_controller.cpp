#include "lock_controller.h"

namespace ostium {

LockController::LockController(LockOutput& output, OneShotTimer& timer, uint32_t unlockMs)
    : output_(output),
      timer_(timer),
      unlockMs_(unlockMs > kMaxUnlockMs ? kMaxUnlockMs : unlockMs) {}

void LockController::begin() {
  std::lock_guard<std::mutex> guard(mutex_);
  output_.write(false);
  timer_.cancel();
  unlocked_ = false;
}

bool LockController::unlock() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (unlocked_) return true;
  // Arm the relock first, so the output is never on without a relock pending.
  if (!timer_.start(unlockMs_, &LockController::onRelockTimer, this)) return false;
  unlocked_ = true;
  output_.write(true);
  return true;
}

void LockController::lock() {
  std::lock_guard<std::mutex> guard(mutex_);
  timer_.cancel();
  unlocked_ = false;
  output_.write(false);
}

void LockController::onRelockTimer(void* context) {
  LockController* self = static_cast<LockController*>(context);
  std::lock_guard<std::mutex> guard(self->mutex_);
  self->unlocked_ = false;
  self->output_.write(false);
}

}  // namespace ostium
