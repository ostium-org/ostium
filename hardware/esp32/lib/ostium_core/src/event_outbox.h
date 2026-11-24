#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "event.h"
#include "interfaces.h"

namespace ostium {

// Which route an outbox's reports go to. The response tables differ only in
// 403: on qr-events it means this device's unit doesn't match the
// credential's, which retrying can't fix (Q19).
enum class ReportKind { Command, Qr };

// Queue of reports waiting to be posted. Each report has its own retry
// backoff, so a failing report never holds up later reports, polls, QR scans,
// or lock control (R14). The door keeps one queue for command events and a
// separate one for QR reports, so QR traffic never evicts a command event.
class EventOutbox {
 public:
  static constexpr size_t kCapacity = 8;

  explicit EventOutbox(Log& log, ReportKind kind = ReportKind::Command);

  // Queues a command event, which isn't droppable. When the queue is full,
  // drops and logs the oldest event.
  void push(const Event& event, uint32_t nowMs);

  // Queues a prepared body, and returns whether it queued it. When the queue
  // is full, it first drops and logs the oldest droppable report. With none
  // queued, a droppable report isn't queued, and any other report drops and
  // logs the oldest one. So a QR denial (droppable) never evicts a grant.
  bool push(const std::string& eventId, const std::string& body, uint32_t nowMs, bool droppable);

  // Posts at most one report that's due, and handles the response as the
  // contract's response table says. Call it from the main loop between polls.
  // The retry backoff starts when the post returns, so a slow request doesn't
  // shorten it. Returns true when it called sender.post(). When retrying is
  // set, *retrying says whether the report stayed queued for a retry.
  bool service(EventSender& sender, const Clock& clock, bool* retrying = nullptr);

  size_t size() const { return entries_.size(); }
  bool contains(const std::string& eventId) const;

 private:
  struct Entry {
    std::string eventId;
    std::string body;
    uint32_t nextAttemptMs;
    uint32_t failures;
    bool droppable;
  };

  Log& log_;
  ReportKind kind_;
  std::vector<Entry> entries_;
};

// Posts at most one report per loop pass across two queues (R14, Q19). The
// door passes the QR queue first, because the app waits on grants. A queue
// with nothing due gives the pass to the other. After a post from the first
// queue that stays queued for a retry, the second queue goes first until it
// posts, and after a post from the second queue, the first goes first again.
// So while reports in one queue keep failing, however slowly, the other queue
// still gets at least every other post.
class ReportScheduler {
 public:
  ReportScheduler(EventOutbox& first, EventSender& firstSender, EventOutbox& second,
                  EventSender& secondSender);

  // Posts at most one due report. Returns whether it posted.
  bool serviceOne(const Clock& clock);

 private:
  EventOutbox& first_;
  EventSender& firstSender_;
  EventOutbox& second_;
  EventSender& secondSender_;
  bool secondFirst_ = false;
};

}  // namespace ostium
