#include "server_clock.h"

#include <algorithm>
#include <string>

namespace ostium {

ServerClock::ServerClock(Log& log) : log_(log) {}

int64_t ServerClock::driftMs(uint32_t elapsedMs) {
  return static_cast<int64_t>((static_cast<uint64_t>(elapsedMs) * kDriftPpm + 999999) / 1000000);
}

void ServerClock::onPollSucceeded(int64_t serverTimeMs, uint32_t requestStartedMs,
                                  uint32_t responseArrivedMs) {
  // The backend stamps server_time once, after the request leaves and before
  // the response arrives, and the stamp is truncated to whole ms. So at the
  // arrival, the server's clock is at least S and less than S + 1 plus the
  // round trip, measured on a clock that may run up to 50 ppm slow.
  const uint32_t roundTripMs = responseArrivedMs - requestStartedMs;
  const int64_t candidateLow = serverTimeMs;
  const int64_t candidateHigh = serverTimeMs + 1 + roundTripMs + driftMs(roundTripMs);
  // At least requestStartedMs - drift of server time passed between
  // millis() == 0 and the stamp, so this is later than the server's time at boot.
  const int64_t candidateBoot =
      serverTimeMs + 1 - requestStartedMs + driftMs(requestStartedMs) + kBootMarginMs;

  int64_t low = candidateLow;
  int64_t high = candidateHigh;
  const uint32_t elapsedMs = responseArrivedMs - anchorMs_;
  if (has_ && elapsedMs <= kMaxOfflineMs) {
    // Both windows hold the server's true time, so it's in their intersection.
    const int64_t carriedLow = lowMs_ + elapsedMs - driftMs(elapsedMs);
    const int64_t carriedHigh = highMs_ + elapsedMs + driftMs(elapsedMs);
    low = std::max(carriedLow, candidateLow);
    high = std::min(carriedHigh, candidateHigh);
    if (low > high) {
      // They can't both hold it, so the server's clock was set to another
      // time. Trust only this poll, and keep the stricter boot time.
      log_.write("server clock moved by about " + std::to_string(candidateLow - carriedLow) +
                 " ms; QR time bounds restarted from this poll");
      low = candidateLow;
      high = candidateHigh;
      bootTimeLatestMs_ = std::max(bootTimeLatestMs_, candidateBoot);
    }
  } else {
    // The first good poll, or the first after the bounds lapsed.
    if (!hasBootTime_) {
      bootTimeLatestMs_ = candidateBoot;
      hasBootTime_ = true;
    } else {
      bootTimeLatestMs_ = std::max(bootTimeLatestMs_, candidateBoot);
    }
    log_.write("server clock set (window " + std::to_string(high - low) + " ms)");
  }

  anchorMs_ = responseArrivedMs;
  lowMs_ = low;
  highMs_ = high;
  has_ = true;
  if (deviceRefused_) {
    deviceRefused_ = false;
    log_.write("backend accepts this device again; QR access resumed");
  }
}

void ServerClock::onPollRefused() {
  if (deviceRefused_) return;
  deviceRefused_ = true;
  log_.write("backend refused this device (401); QR access paused until a poll succeeds");
}

void ServerClock::tick(uint32_t nowMs) {
  if (has_ && nowMs - anchorMs_ > kMaxOfflineMs) {
    has_ = false;
    log_.write("no successful poll for 72 hours; QR access waits for the next poll");
  }
}

bool ServerClock::bounds(uint32_t nowMs, ServerTimeBounds* out) const {
  if (!has_) return false;
  // Unsigned, so a nowMs before the anchor is a huge elapsed time.
  const uint32_t elapsedMs = nowMs - anchorMs_;
  if (elapsedMs > kMaxOfflineMs) return false;
  const int64_t earliest = lowMs_ + elapsedMs - driftMs(elapsedMs);
  const int64_t latest = highMs_ + elapsedMs + driftMs(elapsedMs);
  if (latest - earliest > kMaxWidthMs) return false;
  if (out != nullptr) {
    out->earliestMs = earliest;
    out->latestMs = latest;
  }
  return true;
}

}  // namespace ostium
