#pragma once

#include <cstdint>

#include "interfaces.h"

namespace ostium {

// Conservative bounds on the server's clock, in ms since the Unix epoch.
struct ServerTimeBounds {
  int64_t earliestMs = 0;  // never later than the server's true time
  int64_t latestMs = 0;    // never earlier than the server's true time
};

// What QR access knows about the server's clock (Q10, Q11, Q14). Each good
// poll brackets the server_time stamp between the request start and the
// response arrival, and intersects that window with the bounds carried from
// earlier polls. Between polls, the bounds widen by 50 ppm of the local time
// that passed, so they hold for a millis() rate within 50 ppm of the server's.
// docs/firmware/qr-access-v0.md, Time model, gives the arithmetic.
class ServerClock {
 public:
  static constexpr uint32_t kDriftPpm = 50;
  static constexpr uint32_t kMaxOfflineMs = 259200000;  // 72 h since the last good poll
  static constexpr int64_t kMaxWidthMs = 30000;         // latest - earliest
  static constexpr int64_t kBootMarginMs = 1000;

  explicit ServerClock(Log& log);

  // A poll that returned 200 and parsed. requestStartedMs is the Clock time just
  // before fetcher.get(). responseArrivedMs is the Clock time just after get()
  // returns, after the whole body is read. Clears deviceRefused().
  void onPollSucceeded(int64_t serverTimeMs, uint32_t requestStartedMs, uint32_t responseArrivedMs);

  // A poll answered 401: the backend refuses this device's token or status.
  void onPollRefused();

  // Call on every loop pass. Forgets the bounds once kMaxOfflineMs has passed
  // since the last good poll, long before 32-bit millis() can wrap.
  void tick(uint32_t nowMs);

  // False before the first good poll, after tick() forgot the bounds, when more
  // than kMaxOfflineMs has passed (this includes nowMs earlier than the anchor),
  // or when latest - earliest > kMaxWidthMs. Sets *out only when it returns true.
  bool bounds(uint32_t nowMs, ServerTimeBounds* out) const;

  bool hasBootTime() const { return hasBootTime_; }
  // Latest possible server time at millis() == 0, plus kBootMarginMs. Set by
  // the first good poll, and only ever raised after that.
  int64_t bootTimeLatestMs() const { return bootTimeLatestMs_; }
  bool deviceRefused() const { return deviceRefused_; }

  // ceil(elapsedMs * 50 / 1,000,000), in 64-bit arithmetic.
  static int64_t driftMs(uint32_t elapsedMs);

 private:
  Log& log_;
  bool has_ = false;
  uint32_t anchorMs_ = 0;  // local time of the last good poll's arrival
  int64_t lowMs_ = 0;      // bounds at anchorMs_
  int64_t highMs_ = 0;
  bool hasBootTime_ = false;
  int64_t bootTimeLatestMs_ = 0;
  bool deviceRefused_ = false;
};

}  // namespace ostium
