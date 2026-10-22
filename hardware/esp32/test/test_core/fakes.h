#pragma once

// Fakes and helpers for the ostium_core tests. Nothing here touches hardware
// or the network.

#include <ArduinoJson.h>
#include <ostium_core.h>

#include <cstdint>
#include <string>
#include <vector>

namespace fakes {

class FakeTimer;

// Manual clock. advance() fires the fake timer at its exact deadline when the
// deadline falls inside the step, the way esp_timer fires while the main loop
// is blocked.
class FakeClock : public ostium::Clock {
 public:
  uint32_t nowMs() const override { return now_; }
  void set(uint32_t nowMs) { now_ = nowMs; }
  void advance(uint32_t ms);
  void attach(FakeTimer* timer) { timer_ = timer; }

 private:
  uint32_t now_ = 0;
  FakeTimer* timer_ = nullptr;
};

class FakeTimer : public ostium::OneShotTimer {
 public:
  explicit FakeTimer(FakeClock& clock) : clock_(clock) { clock.attach(this); }

  bool start(uint32_t delayMs, Callback callback, void* context) override {
    if (failStart) return false;
    armed_ = true;
    deadlineMs_ = clock_.nowMs() + delayMs;
    callback_ = callback;
    context_ = context;
    ++starts_;
    return true;
  }
  void cancel() override { armed_ = false; }

  // Makes start() fail, like esp_timer with no timer handle.
  bool failStart = false;

  void fire() {
    armed_ = false;
    callback_(context_);
  }

  bool armed() const { return armed_; }
  uint32_t deadlineMs() const { return deadlineMs_; }
  int starts() const { return starts_; }

 private:
  FakeClock& clock_;
  bool armed_ = false;
  uint32_t deadlineMs_ = 0;
  Callback callback_ = nullptr;
  void* context_ = nullptr;
  int starts_ = 0;
};

inline void FakeClock::advance(uint32_t ms) {
  const uint32_t target = now_ + ms;
  while (timer_ != nullptr && timer_->armed() && timer_->deadlineMs() <= target) {
    now_ = timer_->deadlineMs();
    timer_->fire();
  }
  now_ = target;
}

struct OutputWrite {
  uint32_t atMs;
  bool unlocked;
};

// Records every write with the fake clock's time. Starts in the "on" state to
// stand in for a pin whose level is unknown before setup() runs.
class FakeLockOutput : public ostium::LockOutput {
 public:
  explicit FakeLockOutput(const FakeClock& clock) : clock_(clock) {}

  void write(bool unlocked) override {
    writes_.push_back({clock_.nowMs(), unlocked});
    unlocked_ = unlocked;
  }

  bool unlocked() const { return unlocked_; }
  const std::vector<OutputWrite>& writes() const { return writes_; }

 private:
  const FakeClock& clock_;
  bool unlocked_ = true;
  std::vector<OutputWrite> writes_;
};

class FakeLog : public ostium::Log {
 public:
  void write(const std::string& message) override { messages.push_back(message); }

  bool contains(const std::string& text) const {
    for (const std::string& message : messages) {
      if (message.find(text) != std::string::npos) return true;
    }
    return false;
  }

  std::vector<std::string> messages;
};

// Returns scripted statuses in order, then fallbackStatus. Records each body.
class FakeSender : public ostium::EventSender {
 public:
  int post(const std::string& body) override {
    bodies.push_back(body);
    return next < statuses.size() ? statuses[next++] : fallbackStatus;
  }

  std::vector<int> statuses;
  size_t next = 0;
  int fallbackStatus = 200;
  std::vector<std::string> bodies;
};

// A sender whose post blocks for blockMs of fake time, then fails, like a
// request to an unreachable host.
class BlockingSender : public ostium::EventSender {
 public:
  BlockingSender(FakeClock& clock, uint32_t blockMs) : clock_(clock), blockMs_(blockMs) {}

  int post(const std::string&) override {
    clock_.advance(blockMs_);
    return -1;
  }

 private:
  FakeClock& clock_;
  uint32_t blockMs_;
};

// Times are offsets in ms from this base, 2026-10-22T15:00:00.000Z.
constexpr int64_t kBaseEpochMs = 1792681200000LL;
constexpr const char* kDoorId = "9a1d7c3b-2e4f-4b6a-8c0d-5f3e2b1a7c64";

inline std::string at(int64_t offsetMs) { return ostium::formatTimestamp(kBaseEpochMs + offsetMs); }

inline std::string commandJson(const std::string& commandId, const std::string& action,
                               int64_t issuedOffsetMs, int64_t expiresOffsetMs,
                               const std::string& doorId = kDoorId) {
  return "{\"command_id\":\"" + commandId + "\",\"action\":\"" + action + "\",\"door_id\":\"" +
         doorId + "\",\"issued_at\":\"" + at(issuedOffsetMs) + "\",\"expires_at\":\"" +
         at(expiresOffsetMs) + "\"}";
}

inline std::string pollJson(int64_t serverOffsetMs, const std::vector<std::string>& commands) {
  std::string body = "{\"server_time\":\"" + at(serverOffsetMs) + "\",\"commands\":[";
  for (size_t i = 0; i < commands.size(); ++i) {
    if (i > 0) body += ",";
    body += commands[i];
  }
  return body + "]}";
}

struct SentEvent {
  std::string eventId;
  std::string commandId;
  std::string action;  // "null" when the action is null.
  std::string result;
  std::string reason;  // "null" when the reason is null.
  std::string occurredAt;
};

inline std::string stringOrNull(JsonVariantConst value) {
  return value.isNull() ? "null" : value.as<std::string>();
}

inline SentEvent parseEventBody(const std::string& body) {
  JsonDocument document;
  deserializeJson(document, body);
  JsonObjectConst root = document.as<JsonObjectConst>();
  return {stringOrNull(root["event_id"]), stringOrNull(root["command_id"]),
          stringOrNull(root["action"]),   stringOrNull(root["result"]),
          stringOrNull(root["reason"]),   stringOrNull(root["occurred_at"])};
}

// The door unit's core, wired to fakes. The door started 10 s before the
// first poll, and the first poll is at server offset 20,000 ms, so the R5
// start time is offset 20,000 - 10,000 + 2,000 = 12,000 ms.
struct Harness {
  static constexpr uint32_t kUptimeAtFirstPollMs = 10000;
  static constexpr int64_t kFirstPollOffsetMs = 20000;
  static constexpr int64_t kStartOffsetMs = 12000;

  explicit Harness(uint32_t unlockMs = ostium::LockController::kDefaultUnlockMs)
      : lock(output, timer, unlockMs), processor(kDoorId, lock, outbox, clock, log) {
    clock.set(kUptimeAtFirstPollMs);
    lock.begin();
  }

  // Parses and handles a poll response that arrived receivedAgoMs ago.
  ostium::PollResult poll(const std::string& body, int status = 200, uint32_t receivedAgoMs = 0) {
    ostium::PollResult result = ostium::parsePollResponse(status, body);
    processor.handlePoll(result, clock.nowMs() - receivedAgoMs);
    return result;
  }

  // Posts every queued event with a 200 response and returns them in order.
  std::vector<SentEvent> flushEvents() {
    FakeSender sender;
    for (int guard = 0; outbox.size() > 0 && guard < 100; ++guard) {
      outbox.service(sender, clock);
    }
    std::vector<SentEvent> events;
    for (const std::string& body : sender.bodies) events.push_back(parseEventBody(body));
    return events;
  }

  // The lock output's writes after the locked write from begin().
  std::vector<OutputWrite> writesAfterBegin() const {
    return std::vector<OutputWrite>(output.writes().begin() + 1, output.writes().end());
  }

  FakeClock clock;
  FakeTimer timer{clock};
  FakeLockOutput output{clock};
  FakeLog log;
  ostium::EventOutbox outbox{log};
  ostium::LockController lock;
  ostium::CommandProcessor processor;
};

}  // namespace fakes
