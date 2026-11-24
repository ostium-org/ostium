#include "event_outbox.h"

#include <algorithm>

#include "timing.h"

namespace ostium {
namespace {

enum class Outcome { Delivered, AlreadyReported, Drop, Retry };

Outcome classify(int status, ReportKind kind) {
  if (status >= 200 && status <= 299) return Outcome::Delivered;
  if (status == 409) return Outcome::AlreadyReported;
  if (status == 400 || status == 404 || status == 413 || status == 415 || status == 422) {
    return Outcome::Drop;
  }
  // On qr-events, 403 is a unit binding mismatch, which retrying can't fix.
  if (kind == ReportKind::Qr && status == 403) return Outcome::Drop;
  // 401, 429, 5xx, timeouts, network errors, and any other status.
  return Outcome::Retry;
}

}  // namespace

EventOutbox::EventOutbox(Log& log, ReportKind kind) : log_(log), kind_(kind) {
  entries_.reserve(kCapacity);
}

void EventOutbox::push(const Event& event, uint32_t nowMs) {
  push(event.eventId(), serializeEvent(event), nowMs, false);
}

bool EventOutbox::push(const std::string& eventId, const std::string& body, uint32_t nowMs,
                       bool droppable) {
  if (entries_.size() >= kCapacity) {
    auto dropped = std::find_if(entries_.begin(), entries_.end(),
                                [](const Entry& entry) { return entry.droppable; });
    if (dropped == entries_.end()) {
      if (droppable) {
        log_.write("event queue full, didn't queue " + eventId);
        return false;
      }
      dropped = entries_.begin();
    }
    log_.write("event queue full, dropped " + dropped->eventId);
    entries_.erase(dropped);
  }
  entries_.push_back({eventId, body, nowMs, 0, droppable});
  return true;
}

bool EventOutbox::service(EventSender& sender, const Clock& clock, bool* retrying) {
  if (retrying != nullptr) *retrying = false;
  const uint32_t nowMs = clock.nowMs();
  for (size_t i = 0; i < entries_.size(); ++i) {
    Entry& entry = entries_[i];
    if (!reached(nowMs, entry.nextAttemptMs)) continue;

    const int status = sender.post(entry.body);
    const std::string eventId = entry.eventId;
    const bool droppable = entry.droppable;
    switch (classify(status, kind_)) {
      case Outcome::Delivered:
        log_.write("event " + eventId + " delivered");
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      case Outcome::AlreadyReported:
        log_.write("event " + eventId + " already reported (409)");
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      case Outcome::Drop: {
        std::string message =
            "dropped event " + eventId + " after status " + std::to_string(status);
        if (kind_ == ReportKind::Qr && status == 403) {
          message += "; check that devices.unit_id for this device equals OSTIUM_UNIT_ID";
        }
        log_.write(message);
        // In the QR queue, only grants aren't droppable.
        if (kind_ == ReportKind::Qr && !droppable) {
          log_.write("backend refused a granted QR report; the access log lacks this unlock");
        }
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
      case Outcome::Retry:
        if (retrying != nullptr) *retrying = true;
        ++entry.failures;
        entry.nextAttemptMs = clock.nowMs() + retryBackoffMs(entry.failures);
        log_.write("event " + eventId + " failed with status " + std::to_string(status) +
                   ", retrying");
        break;
    }
    return true;
  }
  return false;
}

bool EventOutbox::contains(const std::string& eventId) const {
  for (const Entry& entry : entries_) {
    if (entry.eventId == eventId) return true;
  }
  return false;
}

ReportScheduler::ReportScheduler(EventOutbox& first, EventSender& firstSender,
                                 EventOutbox& second, EventSender& secondSender)
    : first_(first), firstSender_(firstSender), second_(second), secondSender_(secondSender) {}

bool ReportScheduler::serviceOne(const Clock& clock) {
  if (secondFirst_ && second_.service(secondSender_, clock)) {
    secondFirst_ = false;
    return true;
  }
  bool retrying = false;
  if (first_.service(firstSender_, clock, &retrying)) {
    secondFirst_ = retrying;
    return true;
  }
  // When secondFirst_ is set, the second queue already had nothing due.
  return !secondFirst_ && second_.service(secondSender_, clock);
}

}  // namespace ostium
