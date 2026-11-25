// Camera decodes and door feedback for QR scans: Q1.

#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "qr_backend_responses.h"
#include "qr_fakes.h"
#include "tests.h"

using ostium::QrDecision;
using ostium::QrOutcome;
using ostium::QrScan;
using ostium::QrSignal;
using ostium::scanFromCameraDecode;
using qrfakes::jtiFor;
using qrfakes::kIatFloorMs;
using qrfakes::kIatUs;
using qrfakes::kJti;
using qrfakes::QrHarness;
using qrfakes::token;

namespace qr_reason = ostium::qr_reason;

namespace {

// The payload buffer in ESP32QRCodeReader's QRCodeData.
constexpr size_t kPayloadBytes = 1024;
constexpr size_t kPayloadCapacity = kPayloadBytes - 1;

// A poll 1 s after token T's iat, as in test_qr_access.cpp.
constexpr int64_t kSyncMs = kIatFloorMs + 1000;

// Records each signal the door shows, and how many scans were still waiting in
// the source when it showed it. drainScans takes one scan at a time, so a
// signal that shows right after its own decision sees only the later scans
// still waiting. A signal shown late, such as after the whole batch, sees fewer.
class FakeFeedback : public ostium::QrFeedback {
 public:
  explicit FakeFeedback(const qrfakes::FakeScanSource& source) : source_(source) {}

  void show(QrSignal signal) override {
    signals.push_back(signal);
    waitingWhenShown.push_back(source_.scans.size());
  }

  std::vector<QrSignal> signals;
  std::vector<size_t> waitingWhenShown;

 private:
  const qrfakes::FakeScanSource& source_;
};

QrDecision decisionOf(QrOutcome outcome, bool isFinal) {
  QrDecision decision;
  decision.outcome = outcome;
  decision.isFinal = isFinal;
  return decision;
}

}  // namespace

void test_camera_decode_is_copied_exactly() {
  uint8_t payload[kPayloadBytes] = {};

  // A payload that holds a 0 byte keeps every byte, because the copy uses the
  // decoded length, never strlen.
  const char withNul[] = {'a', 'b', '\0', 'c', 'd'};
  std::memcpy(payload, withNul, sizeof(withNul));
  QrScan scan;
  TEST_ASSERT_TRUE(scanFromCameraDecode(true, payload, 5, kPayloadCapacity, 4321, &scan));
  TEST_ASSERT_EQUAL_UINT32(5, scan.text.size());
  TEST_ASSERT_EQUAL_MEMORY(withNul, scan.text.data(), 5);
  TEST_ASSERT_EQUAL_UINT32(4321, scan.decodedAtMs);

  // A backend token arrives whole.
  const std::string issued = qr_backend::kIssuedToken;
  std::memcpy(payload, issued.data(), issued.size());
  TEST_ASSERT_TRUE(scanFromCameraDecode(true, payload, static_cast<int>(issued.size()),
                                        kPayloadCapacity, 99, &scan));
  TEST_ASSERT_EQUAL_STRING(issued.c_str(), scan.text.c_str());
  TEST_ASSERT_EQUAL_UINT32(issued.size(), scan.text.size());

  // An empty payload is a scan too. QrAccess then denies it as malformed_qr.
  TEST_ASSERT_TRUE(scanFromCameraDecode(true, payload, 0, kPayloadCapacity, 7, &scan));
  TEST_ASSERT_EQUAL_UINT32(0, scan.text.size());
  TEST_ASSERT_EQUAL_UINT32(7, scan.decodedAtMs);

  // The longest payload the buffer can hold passes; one byte more doesn't.
  std::memset(payload, 'x', sizeof(payload));
  TEST_ASSERT_TRUE(scanFromCameraDecode(true, payload, static_cast<int>(kPayloadCapacity),
                                        kPayloadCapacity, 8, &scan));
  TEST_ASSERT_EQUAL_UINT32(kPayloadCapacity, scan.text.size());

  // Everything else is skipped and leaves the scan as it was.
  scan.text = "keep";
  scan.decodedAtMs = 5;
  const char* error = "ECC failure";  // A failed decode carries the decoder's error text.
  std::memcpy(payload, error, std::strlen(error));
  TEST_ASSERT_FALSE(scanFromCameraDecode(false, payload, static_cast<int>(std::strlen(error)),
                                         kPayloadCapacity, 9, &scan));
  TEST_ASSERT_FALSE(scanFromCameraDecode(true, payload, static_cast<int>(kPayloadCapacity) + 1,
                                         kPayloadCapacity, 9, &scan));
  TEST_ASSERT_FALSE(scanFromCameraDecode(true, payload, -1, kPayloadCapacity, 9, &scan));
  TEST_ASSERT_FALSE(scanFromCameraDecode(true, nullptr, 3, kPayloadCapacity, 9, &scan));
  TEST_ASSERT_EQUAL_STRING("keep", scan.text.c_str());
  TEST_ASSERT_EQUAL_UINT32(5, scan.decodedAtMs);
  TEST_ASSERT_FALSE(scanFromCameraDecode(true, payload, 3, kPayloadCapacity, 9, nullptr));
}

void test_qr_signal_follows_decision() {
  // Every outcome and finality.
  TEST_ASSERT_TRUE(QrSignal::Granted == ostium::qrSignalFor(decisionOf(QrOutcome::Granted, true)));
  TEST_ASSERT_TRUE(QrSignal::Denied == ostium::qrSignalFor(decisionOf(QrOutcome::Denied, true)));
  TEST_ASSERT_TRUE(QrSignal::Wait == ostium::qrSignalFor(decisionOf(QrOutcome::Denied, false)));
  TEST_ASSERT_TRUE(QrSignal::None == ostium::qrSignalFor(decisionOf(QrOutcome::Ignored, false)));
  TEST_ASSERT_TRUE(QrSignal::None == ostium::qrSignalFor(QrDecision{}));

  // The decisions QrAccess really makes.
  QrHarness h;
  QrDecision decision = h.scan(token(kJti, kIatUs));
  TEST_ASSERT_EQUAL_STRING(qr_reason::kClockUnsynced, decision.reason);
  TEST_ASSERT_TRUE(QrSignal::Wait == ostium::qrSignalFor(decision));

  // A transient denial isn't remembered, so the same code is checked again.
  h.sync(kSyncMs);
  decision = h.scan(token(kJti, kIatUs));
  TEST_ASSERT_TRUE(decision.outcome == QrOutcome::Granted);
  TEST_ASSERT_TRUE(QrSignal::Granted == ostium::qrSignalFor(decision));

  decision = h.scan(token(kJti, kIatUs));
  TEST_ASSERT_EQUAL_STRING(qr_reason::kRepeat, decision.reason);
  TEST_ASSERT_TRUE(QrSignal::None == ostium::qrSignalFor(decision));

  decision = h.scan("not a token");
  TEST_ASSERT_EQUAL_STRING(qr_reason::kMalformedQr, decision.reason);
  TEST_ASSERT_TRUE(QrSignal::Denied == ostium::qrSignalFor(decision));

  // A code issued after the door's latest server time isn't valid yet.
  decision = h.scan(token(jtiFor(2), kIatUs + 60000000));
  TEST_ASSERT_EQUAL_STRING(qr_reason::kNotYetValid, decision.reason);
  TEST_ASSERT_TRUE(QrSignal::Wait == ostium::qrSignalFor(decision));

  decision = h.scan(token(jtiFor(3), kIatUs - 61000000));
  TEST_ASSERT_EQUAL_STRING(qr_reason::kExpired, decision.reason);
  TEST_ASSERT_TRUE(QrSignal::Denied == ostium::qrSignalFor(decision));
}

void test_qr_drain_shows_each_decision() {
  QrHarness h;
  qrfakes::FakeScanSource source;
  FakeFeedback feedback(source);
  const uint32_t now = h.clock.nowMs();

  // Before the door's first good poll, a valid code is a transient denial
  // (clock_unsynced), so it shows Wait. A final denial shows Denied.
  source.scans.push_back({token(kJti, kIatUs), now});  // clock_unsynced: Wait
  source.scans.push_back({"not a token", now});        // malformed_qr: Denied
  TEST_ASSERT_EQUAL_UINT32(2, ostium::drainScans(source, h.qr, 4, &feedback));
  TEST_ASSERT_EQUAL_UINT32(2, feedback.signals.size());
  TEST_ASSERT_TRUE(QrSignal::Wait == feedback.signals[0]);
  TEST_ASSERT_TRUE(QrSignal::Denied == feedback.signals[1]);
  // Each signal shows right after its own decision, while the later scans
  // still wait.
  TEST_ASSERT_EQUAL_UINT32(1, feedback.waitingWhenShown[0]);
  TEST_ASSERT_EQUAL_UINT32(0, feedback.waitingWhenShown[1]);

  // The transient denial wasn't remembered, so the same code is granted once
  // the door has server time.
  h.sync(kSyncMs);
  source.scans.push_back({token(kJti, kIatUs), now});                  // Granted
  source.scans.push_back({token(kJti, kIatUs), now});                  // repeat: shows nothing
  source.scans.push_back({"not a token", now});                        // malformed_qr: Denied
  source.scans.push_back({token(jtiFor(4), kIatUs), now - 2001});      // stale: shows nothing
  source.scans.push_back({token(jtiFor(3), kIatUs - 61000000), now});  // expired: Denied
  // Issued after the door's latest server time: not_yet_valid, so Wait.
  source.scans.push_back({token(jtiFor(2), kIatUs + 60000000), now});

  // The drain limit still holds, and an ignored decode shows nothing.
  TEST_ASSERT_EQUAL_UINT32(4, ostium::drainScans(source, h.qr, 4, &feedback));
  TEST_ASSERT_EQUAL_UINT32(4, feedback.signals.size());
  TEST_ASSERT_TRUE(QrSignal::Granted == feedback.signals[2]);
  TEST_ASSERT_TRUE(QrSignal::Denied == feedback.signals[3]);
  TEST_ASSERT_EQUAL_UINT32(5, feedback.waitingWhenShown[2]);
  TEST_ASSERT_EQUAL_UINT32(3, feedback.waitingWhenShown[3]);

  TEST_ASSERT_EQUAL_UINT32(2, ostium::drainScans(source, h.qr, 4, &feedback));
  TEST_ASSERT_EQUAL_UINT32(6, feedback.signals.size());
  TEST_ASSERT_TRUE(QrSignal::Denied == feedback.signals[4]);
  TEST_ASSERT_TRUE(QrSignal::Wait == feedback.signals[5]);
  TEST_ASSERT_EQUAL_UINT32(1, feedback.waitingWhenShown[4]);
  TEST_ASSERT_EQUAL_UINT32(0, feedback.waitingWhenShown[5]);

  TEST_ASSERT_EQUAL_UINT32(0, ostium::drainScans(source, h.qr, 4, &feedback));
  TEST_ASSERT_EQUAL_UINT32(6, feedback.signals.size());
}
