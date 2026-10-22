#include "event_outbox.h"

#include "timing.h"

namespace ostium {
namespace {

enum class Outcome { Delivered, AlreadyReported, Drop, Retry };

Outcome classify(int status) {
  if (status >= 200 && status <= 299) return Outcome::Delivered;
  if (status == 409) return Outcome::AlreadyReported;
  if (status == 400 || status == 404 || status == 413 || status == 415 || status == 422) {
    return Outcome::Drop;
  }
  // 401, 429, 5xx, timeouts, network errors, and any other status.
  return Outcome::Retry;
}

}  // namespace

EventOutbox::EventOutbox(Log& log) : log_(log) { entries_.reserve(kCapacity); }

void EventOutbox::push(const Event& event, uint32_t nowMs) {
  if (entries_.size() >= kCapacity) {
    log_.write("event queue full, dropped " + entries_.front().eventId);
    entries_.erase(entries_.begin());
  }
  entries_.push_back({event.eventId(), serializeEvent(event), nowMs, 0});
}

void EventOutbox::service(EventSender& sender, const Clock& clock) {
  const uint32_t nowMs = clock.nowMs();
  for (size_t i = 0; i < entries_.size(); ++i) {
    Entry& entry = entries_[i];
    if (!reached(nowMs, entry.nextAttemptMs)) continue;

    const int status = sender.post(entry.body);
    const std::string eventId = entry.eventId;
    switch (classify(status)) {
      case Outcome::Delivered:
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      case Outcome::AlreadyReported:
        log_.write("event " + eventId + " already reported (409)");
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      case Outcome::Drop:
        log_.write("dropped event " + eventId + " after status " + std::to_string(status));
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      case Outcome::Retry:
        ++entry.failures;
        entry.nextAttemptMs = clock.nowMs() + retryBackoffMs(entry.failures);
        log_.write("event " + eventId + " failed with status " + std::to_string(status) +
                   ", retrying");
        break;
    }
    return;
  }
}

bool EventOutbox::contains(const std::string& eventId) const {
  for (const Entry& entry : entries_) {
    if (entry.eventId == eventId) return true;
  }
  return false;
}

}  // namespace ostium
