#pragma once

#include <cstdint>
#include <string>

namespace ostium {

// Drives the GPIO pin that controls the relay or lock. On the board, an
// adapter writes the pin. In tests, a fake records each write.
class LockOutput {
 public:
  virtual ~LockOutput() = default;
  // true turns the output on (unlocked). false is the fail-secure locked level.
  virtual void write(bool unlocked) = 0;
};

// Milliseconds since startup, like millis(). Wraps after about 49.7 days.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t nowMs() const = 0;
};

// A one-shot timer whose callback runs independently of the main loop. On the
// board it wraps esp_timer, so the relock fires on time while the loop is
// blocked in network code (R12).
class OneShotTimer {
 public:
  using Callback = void (*)(void* context);
  virtual ~OneShotTimer() = default;
  // Stops any pending timer, then arms it for delayMs. Returns false, with no
  // timer armed, if it can't arm it. With esp_timer: call esp_timer_stop and
  // ignore ESP_ERR_INVALID_STATE, then call esp_timer_start_once.
  virtual bool start(uint32_t delayMs, Callback callback, void* context) = 0;
  // Stops the timer. Does nothing if it isn't armed.
  virtual void cancel() = 0;
};

class Log {
 public:
  virtual ~Log() = default;
  virtual void write(const std::string& message) = 0;
};

// Posts one event body to the backend. Returns the HTTP status, or a negative
// value for a timeout or network error.
class EventSender {
 public:
  virtual ~EventSender() = default;
  virtual int post(const std::string& body) = 0;
};

}  // namespace ostium
