// Replays responses recorded from the real Deno QR and device routes
// (qr_backend_responses.h) through QR access. The tests fail if the firmware
// stops matching those responses: the tokens it must grant, the report bodies
// the backend accepts, and the statuses it answers. The production-clock set
// has whole-millisecond times, as real tokens do. After a backend change, run
// hardware/esp32/tools/capture_qr_backend.ts again. CI fails while the
// generated headers are stale.

#include <ArduinoJson.h>
#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "qr_fakes.h"
#include "tests.h"

using fakes::FakeClock;
using fakes::FakeLog;
using fakes::FakeSender;
using ostium::EventOutbox;
using ostium::QrDecision;
using ostium::QrOutcome;
using ostium::QrToken;
using ostium::QrTokenProblem;
using ostium::ReportKind;
using qr_backend::Capture;
using qrfakes::parseQrReportBody;
using qrfakes::QrHarness;
using qrfakes::SentQrReport;

namespace qr_reason = ostium::qr_reason;

// The tool computed the production-clock set's Q6 values with a copy of this
// ArduinoJson release's parse. Another release can read a claim one double
// differently, which would fail these tests 1 ms off, so the build stops here
// instead.
static_assert(ARDUINOJSON_VERSION_MAJOR == qr_backend::kArduinoJsonMajor &&
                  ARDUINOJSON_VERSION_MINOR == qr_backend::kArduinoJsonMinor &&
                  ARDUINOJSON_VERSION_REVISION == qr_backend::kArduinoJsonRevision,
              "ArduinoJson isn't the release whose parse capture_qr_backend.ts copies. Pin "
              "that release, or update the tool's copy and run the tool again.");

namespace {

// The server time of a recorded poll, as the door parses it.
int64_t serverTimeOf(const Capture& poll) {
  TEST_ASSERT_EQUAL_INT(200, poll.status);
  const ostium::PollResult result = ostium::parsePollResponse(poll.status, poll.body);
  TEST_ASSERT_TRUE(result.ok);
  return result.serverTimeMs;
}

// A recorded report's occurred_at, in ms since the epoch.
int64_t occurredAtOf(const Capture& report) {
  int64_t epochMs = 0;
  const std::string text = parseQrReportBody(report.request).occurredAt;
  TEST_ASSERT_TRUE_MESSAGE(ostium::parseTimestamp(text.c_str(), &epochMs), text.c_str());
  return epochMs;
}

// A timestamp from a recorded response body, such as the issued expires_at.
int64_t timestampIn(const Capture& capture, const char* name) {
  JsonDocument document;
  TEST_ASSERT_FALSE(deserializeJson(document, capture.body));
  int64_t epochMs = 0;
  TEST_ASSERT_TRUE_MESSAGE(ostium::parseTimestamp(document[name].as<const char*>(), &epochMs),
                           name);
  return epochMs;
}

// A timestamp the backend wrote, such as a stored issued_at.
int64_t epochMsOf(const char* text) {
  int64_t epochMs = 0;
  TEST_ASSERT_TRUE_MESSAGE(ostium::parseTimestamp(text, &epochMs), text);
  return epochMs;
}

// The backend's token, checked with the fixture's key under ostium-qr-1.
QrToken verified(const char* token) {
  ostium::QrKey key;
  const char* error = "";
  TEST_ASSERT_TRUE_MESSAGE(
      ostium::makeQrKey(ostium::kQrSelfTestKeyId, ostium::kTestOnlyQrPublicKey, &key, &error),
      error);
  const QrToken checked = ostium::verifyQrToken(token, std::strlen(token), key);
  TEST_ASSERT_TRUE_MESSAGE(checked.problem == QrTokenProblem::None, checked.detail);
  return checked;
}

// Checks an issue response: 201, and the token for this credential and unit.
void expectIssued(const Capture& issued, const char* token, const char* credentialId,
                  const char* unitId) {
  TEST_ASSERT_EQUAL_INT(201, issued.status);
  JsonDocument document;
  TEST_ASSERT_FALSE(deserializeJson(document, issued.body));
  TEST_ASSERT_EQUAL_STRING(token, document["payload"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(credentialId, document["credential_id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(unitId, document["house_id"].as<const char*>());
}

void expectDecision(const QrDecision& decision, QrOutcome outcome, const char* reason,
                    bool reported, const char* label) {
  TEST_ASSERT_TRUE_MESSAGE(decision.outcome == outcome, label);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(reason, decision.reason, label);
  TEST_ASSERT_TRUE_MESSAGE(decision.reported == reported, label);
}

// Checks a 200: the backend recorded the report and names it.
void expectRecorded(const Capture& accepted) {
  TEST_ASSERT_EQUAL_INT(200, accepted.status);
  JsonDocument response;
  TEST_ASSERT_FALSE(deserializeJson(response, accepted.body));
  const SentQrReport sent = parseQrReportBody(accepted.request);
  TEST_ASSERT_EQUAL_STRING(sent.eventId.c_str(), response["event_id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING(sent.credentialId.c_str(), response["credential_id"].as<const char*>());
  TEST_ASSERT_TRUE(response["recorded"].as<bool>());
}

// Checks that the backend refused a grant because the credential wasn't
// valid at its occurred_at, not because occurred_at was in the future.
void expectOutsideWindow(const Capture& refused) {
  TEST_ASSERT_EQUAL_INT(422, refused.status);
  TEST_ASSERT_EQUAL_STRING(R"({"detail":"Credential was not valid at scan time."})", refused.body);
  TEST_ASSERT_EQUAL_STRING("granted", parseQrReportBody(refused.request).result.c_str());
}

// A recorded report's body with the event ID the door writes for it, for a
// report recorded under its own event ID.
std::string withDoorEventId(const Capture& report) {
  const SentQrReport sent = parseQrReportBody(report.request);
  const std::string recorded = "{\"event_id\":\"" + sent.eventId + "\",";
  std::string body = report.request;
  TEST_ASSERT_EQUAL_UINT32(0, body.find(recorded));
  body.replace(0, recorded.size(),
               "{\"event_id\":\"" +
                   ostium::qrEventIdFor(sent.credentialId, sent.result == "granted") + "\",");
  return body;
}

// Posts the one report the door queued and returns the body it sent.
std::string onlyQueuedBody(QrHarness& h) {
  TEST_ASSERT_EQUAL_UINT32(1, h.reports.size());
  FakeSender sender;
  TEST_ASSERT_TRUE(h.reports.service(sender, h.clock));
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_EQUAL_UINT32(1, sender.bodies.size());
  return sender.bodies[0];
}

}  // namespace

void test_backend_qr_token_is_granted() {
  expectIssued(qr_backend::kQrIssued, qr_backend::kIssuedToken, qr_backend::kCredentialId,
               qr_backend::kUnitId);

  // The door accepts the backend's signature and claims. Q6 gives the values
  // the tool computed exactly from the signed claim text, and the door's
  // expiry is before the backend's expires_at.
  const QrToken token = verified(qr_backend::kIssuedToken);
  TEST_ASSERT_EQUAL_STRING(qr_backend::kUnitId, token.claims.unitId.c_str());
  TEST_ASSERT_EQUAL_STRING(qr_backend::kCredentialId, token.claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_INT64(qr_backend::kIssuedIatMs, token.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(qr_backend::kIssuedExpMs, token.claims.expiresAtMs);
  TEST_ASSERT_TRUE(token.claims.expiresAtMs < timestampIn(qr_backend::kQrIssued, "expires_at"));
  TEST_ASSERT_FALSE(token.claims.hasOtherClaims);

  // A door synced to the recorded poll, with a 0 ms round trip, grants it and
  // queues byte for byte the report the backend accepted.
  QrHarness h;
  h.sync(serverTimeOf(qr_backend::kQrPoll));
  const QrDecision decision = h.scan(qr_backend::kIssuedToken);
  expectDecision(decision, QrOutcome::Granted, "", true, "issued token");
  TEST_ASSERT_EQUAL_STRING(qr_backend::kCredentialId, decision.credentialId.c_str());
  TEST_ASSERT_TRUE(h.output.unlocked());
  TEST_ASSERT_EQUAL_STRING(qr_backend::kQrGranted.request, onlyQueuedBody(h).c_str());
}

void test_backend_accepts_firmware_qr_report_bodies() {
  // An exact retry of the granted report gets 200 again. Each 200 names the
  // report it recorded.
  TEST_ASSERT_EQUAL_INT(200, qr_backend::kQrGranted.status);
  TEST_ASSERT_EQUAL_INT(200, qr_backend::kQrGrantedRetry.status);
  TEST_ASSERT_EQUAL_STRING(qr_backend::kQrGranted.request, qr_backend::kQrGrantedRetry.request);
  for (const Capture& accepted : {qr_backend::kQrGranted, qr_backend::kQrGrantedRetry,
                                  qr_backend::kQrDeniedExpired}) {
    JsonDocument response;
    TEST_ASSERT_FALSE(deserializeJson(response, accepted.body));
    const SentQrReport sent = parseQrReportBody(accepted.request);
    TEST_ASSERT_EQUAL_STRING(sent.eventId.c_str(), response["event_id"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(sent.credentialId.c_str(),
                             response["credential_id"].as<const char*>());
    TEST_ASSERT_TRUE(response["recorded"].as<bool>());
  }

  // The same event ID with an occurred_at 1 ms later gets 409, which the door
  // treats as already reported (Q19).
  TEST_ASSERT_EQUAL_INT(409, qr_backend::kQrConflict.status);
  const SentQrReport granted = parseQrReportBody(qr_backend::kQrGranted.request);
  const SentQrReport conflict = parseQrReportBody(qr_backend::kQrConflict.request);
  TEST_ASSERT_EQUAL_STRING(granted.eventId.c_str(), conflict.eventId.c_str());
  TEST_ASSERT_EQUAL_STRING(granted.credentialId.c_str(), conflict.credentialId.c_str());
  TEST_ASSERT_EQUAL_STRING(granted.result.c_str(), conflict.result.c_str());
  TEST_ASSERT_EQUAL_STRING(granted.reason.c_str(), conflict.reason.c_str());
  TEST_ASSERT_EQUAL_INT64(occurredAtOf(qr_backend::kQrGranted) + 1,
                          occurredAtOf(qr_backend::kQrConflict));

  // A door synced to the late poll denies the capped token as expired, and
  // queues byte for byte the denial the backend accepted.
  TEST_ASSERT_EQUAL_INT(200, qr_backend::kQrDeniedExpired.status);
  QrHarness h;
  h.sync(serverTimeOf(qr_backend::kQrPollLate));
  const QrDecision decision = h.scan(qr_backend::kCappedToken);
  expectDecision(decision, QrOutcome::Denied, qr_reason::kExpired, true, "capped token, late");
  TEST_ASSERT_EQUAL_STRING(qr_backend::kCappedCredentialId, decision.credentialId.c_str());
  TEST_ASSERT_FALSE(h.output.unlocked());
  TEST_ASSERT_EQUAL_STRING(qr_backend::kQrDeniedExpired.request, onlyQueuedBody(h).c_str());
}

void test_backend_qr_statuses_follow_response_table() {
  struct Case {
    const char* name;
    Capture capture;
    bool kept;
  };
  const Case cases[] = {
      {"granted", qr_backend::kQrGranted, false},
      {"exact retry", qr_backend::kQrGrantedRetry, false},
      {"same event ID, different report", qr_backend::kQrConflict, false},
      {"late denial", qr_backend::kQrDeniedExpired, false},
      {"granted before issue", qr_backend::kQrGrantedBeforeIssue, false},
      {"granted after expiry", qr_backend::kQrGrantedAfterExpiry, false},
      {"granted in the future", qr_backend::kQrGrantedInFuture, false},
      {"unknown credential", qr_backend::kQrUnknownCredential, false},
      {"another unit's credential", qr_backend::kQrOtherUnitReport, false},
      {"rejected device token", qr_backend::kQrBadToken, true},
      {"production grant at the earliest", qr_backend::kQrProdGranted, false},
      {"production grant before issue", qr_backend::kQrProdGrantedBeforeIssue, false},
      {"production grant at the second token's earliest",
       qr_backend::kQrProdSecondGrantedEarliest, false},
      {"production grant at the latest", qr_backend::kQrProdGrantedLatest, false},
      {"production grant at expiry", qr_backend::kQrProdGrantedAtExpiry, false},
  };
  for (const Case& replay : cases) {
    FakeClock clock;
    FakeLog log;
    EventOutbox outbox(log, ReportKind::Qr);
    FakeSender sender;
    sender.fallbackStatus = replay.capture.status;
    // Grants aren't droppable, and denials are, as QrAccess queues them.
    const SentQrReport report = parseQrReportBody(replay.capture.request);
    TEST_ASSERT_TRUE_MESSAGE(outbox.push(report.eventId, replay.capture.request, clock.nowMs(),
                                         report.result != "granted"),
                             replay.name);
    TEST_ASSERT_TRUE_MESSAGE(outbox.service(sender, clock), replay.name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(replay.capture.request, sender.bodies[0].c_str(),
                                     replay.name);
    TEST_ASSERT_EQUAL_MESSAGE(replay.kept ? 1 : 0, outbox.size(), replay.name);
    TEST_ASSERT_TRUE_MESSAGE(log.contains("devices.unit_id") == (replay.capture.status == 403),
                             replay.name);
    if (!replay.kept) continue;
    // A kept report is posted again after 2 s.
    clock.set(2000);
    TEST_ASSERT_TRUE_MESSAGE(outbox.service(sender, clock), replay.name);
    TEST_ASSERT_EQUAL_MESSAGE(2, sender.bodies.size(), replay.name);
  }
  TEST_ASSERT_EQUAL_INT(404, qr_backend::kQrUnknownCredential.status);
  TEST_ASSERT_EQUAL_INT(403, qr_backend::kQrOtherUnitReport.status);
  TEST_ASSERT_EQUAL_INT(401, qr_backend::kQrBadToken.status);

  // The door never sends the other unit's report: at this door, that unit's
  // token is wrong_unit, which isn't reported (Q7, Q8).
  expectIssued(qr_backend::kQrOtherUnitIssued, qr_backend::kOtherUnitToken,
               qr_backend::kOtherUnitCredentialId, qr_backend::kOtherUnitId);
  QrHarness h;
  h.sync(serverTimeOf(qr_backend::kQrPoll));
  const QrDecision decision = h.scan(qr_backend::kOtherUnitToken);
  expectDecision(decision, QrOutcome::Denied, qr_reason::kWrongUnit, false, "other unit's token");
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
}

void test_backend_rejects_grants_outside_credential_window() {
  // The backend answers 422 to a grant at the truncated iat, at exp rounded
  // up to whole ms, and 1 ms after its own clock (12:00:03.123456 then).
  TEST_ASSERT_EQUAL_INT(422, qr_backend::kQrGrantedBeforeIssue.status);
  TEST_ASSERT_EQUAL_INT(422, qr_backend::kQrGrantedAfterExpiry.status);
  TEST_ASSERT_EQUAL_INT(422, qr_backend::kQrGrantedInFuture.status);

  // Q6 rounds inward, past both boundaries, and the door's own grant lies
  // between them.
  TEST_ASSERT_TRUE(qr_backend::kIssuedIatMs > occurredAtOf(qr_backend::kQrGrantedBeforeIssue));
  TEST_ASSERT_TRUE(qr_backend::kCappedExpMs < occurredAtOf(qr_backend::kQrGrantedAfterExpiry));
  TEST_ASSERT_TRUE(occurredAtOf(qr_backend::kQrGranted) >= qr_backend::kIssuedIatMs);
  TEST_ASSERT_TRUE(occurredAtOf(qr_backend::kQrGranted) < qr_backend::kIssuedExpMs);

  // The door never writes those grants. Bounds that would put occurred_at at
  // the truncated iat make the issued token wait (Q16).
  {
    QrHarness h;
    h.sync(occurredAtOf(qr_backend::kQrGrantedBeforeIssue) +
           ostium::QrAccess::kOccurredAtMarginMs);
    const QrDecision decision = h.scan(qr_backend::kIssuedToken);
    expectDecision(decision, QrOutcome::Denied, qr_reason::kNotYetValid, false,
                   "occurred_at at the truncated iat");
    TEST_ASSERT_FALSE(decision.isFinal);
    TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  }
  // Bounds that would put occurred_at at the rounded-up exp make the capped
  // token expired, so the door reports a denial instead (Q15).
  {
    QrHarness h;
    h.sync(occurredAtOf(qr_backend::kQrGrantedAfterExpiry) +
           ostium::QrAccess::kOccurredAtMarginMs);
    const QrDecision decision = h.scan(qr_backend::kCappedToken);
    expectDecision(decision, QrOutcome::Denied, qr_reason::kExpired, true,
                   "occurred_at at the rounded-up exp");
    TEST_ASSERT_EQUAL_STRING("denied", parseQrReportBody(onlyQueuedBody(h)).result.c_str());
  }
}

void test_backend_capped_qr_expiry_is_honored() {
  // The tenant's grant ends 20.5 s after issue, so the backend capped the
  // token's exp, and the door's expiry is before the backend's expires_at.
  expectIssued(qr_backend::kQrCappedIssued, qr_backend::kCappedToken,
               qr_backend::kCappedCredentialId, qr_backend::kUnitId);
  const QrToken capped = verified(qr_backend::kCappedToken);
  TEST_ASSERT_EQUAL_STRING(qr_backend::kCappedCredentialId, capped.claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_INT64(qr_backend::kIssuedIatMs, capped.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(qr_backend::kCappedExpMs, capped.claims.expiresAtMs);
  TEST_ASSERT_TRUE(capped.claims.expiresAtMs <
                   timestampIn(qr_backend::kQrCappedIssued, "expires_at"));

  // Synced to the recorded poll, the door grants it 18 s later, while latest
  // is 12:00:19.125, before the capped expiry.
  {
    QrHarness h;
    h.sync(serverTimeOf(qr_backend::kQrPoll));
    h.clock.advance(18000);
    ostium::ServerTimeBounds bounds;
    TEST_ASSERT_TRUE(h.serverClock.bounds(h.clock.nowMs(), &bounds));
    TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:19.125Z",
                             ostium::formatTimestamp(bounds.latestMs).c_str());
    expectDecision(h.scan(qr_backend::kCappedToken), QrOutcome::Granted, "", true,
                   "capped token, 18 s after the poll");
    int64_t occurredAtMs = 0;
    const std::string body = onlyQueuedBody(h);
    TEST_ASSERT_TRUE(
        ostium::parseTimestamp(parseQrReportBody(body).occurredAt.c_str(), &occurredAtMs));
    TEST_ASSERT_TRUE(occurredAtMs < qr_backend::kCappedExpMs);
  }
  // Synced to the late poll, it's expired.
  {
    QrHarness h;
    h.sync(serverTimeOf(qr_backend::kQrPollLate));
    expectDecision(h.scan(qr_backend::kCappedToken), QrOutcome::Denied, qr_reason::kExpired, true,
                   "capped token, late");
    TEST_ASSERT_FALSE(h.output.unlocked());
  }
}

void test_backend_production_qr_token_is_granted() {
  // The production-clock set: createHandler's default clock has whole
  // milliseconds, so iat and exp fall on exact milliseconds, such as
  // 1792670400.123.
  expectIssued(qr_backend::kQrProdIssued, qr_backend::kProdToken, qr_backend::kProdCredentialId,
               qr_backend::kUnitId);
  expectIssued(qr_backend::kQrProdSecondIssued, qr_backend::kProdSecondToken,
               qr_backend::kProdSecondCredentialId, qr_backend::kUnitId);

  // Q6 gives the values the tool computed in the firmware's double
  // arithmetic. ArduinoJson reads the first iat, 1792670400.123, as a little
  // more than the exact value, so iatMs is 2 ms after issued_at. It reads the
  // second iat, 2 ms later, exactly, so iatMs is 1 ms after it. Either way,
  // expMs is 1 ms before expires_at. If ArduinoJson's parse changes, these
  // fail until the tool's copy of it changes too.
  const QrToken first = verified(qr_backend::kProdToken);
  TEST_ASSERT_EQUAL_STRING(qr_backend::kUnitId, first.claims.unitId.c_str());
  TEST_ASSERT_EQUAL_STRING(qr_backend::kProdCredentialId, first.claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdIatMs, first.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdExpMs, first.claims.expiresAtMs);
  TEST_ASSERT_EQUAL_INT64(epochMsOf(qr_backend::kProdIssuedAt) + 2, first.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(timestampIn(qr_backend::kQrProdIssued, "expires_at") - 1,
                          first.claims.expiresAtMs);
  TEST_ASSERT_FALSE(first.claims.hasOtherClaims);
  const QrToken second = verified(qr_backend::kProdSecondToken);
  TEST_ASSERT_EQUAL_STRING(qr_backend::kProdSecondCredentialId, second.claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdSecondIatMs, second.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdSecondExpMs, second.claims.expiresAtMs);
  TEST_ASSERT_EQUAL_INT64(epochMsOf(qr_backend::kProdSecondIssuedAt) + 1,
                          second.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(timestampIn(qr_backend::kQrProdSecondIssued, "expires_at") - 1,
                          second.claims.expiresAtMs);

  // A door synced to the recorded poll, with a 0 ms round trip, grants the
  // first token at the earliest occurred_at it writes, iatMs, and queues byte
  // for byte the report the backend accepted.
  QrHarness h;
  h.sync(serverTimeOf(qr_backend::kQrProdPoll));
  const QrDecision decision = h.scan(qr_backend::kProdToken);
  expectDecision(decision, QrOutcome::Granted, "", true, "production token");
  TEST_ASSERT_EQUAL_STRING(qr_backend::kProdCredentialId, decision.credentialId.c_str());
  TEST_ASSERT_TRUE(h.output.unlocked());
  TEST_ASSERT_EQUAL_STRING(qr_backend::kQrProdGranted.request, onlyQueuedBody(h).c_str());
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdIatMs, occurredAtOf(qr_backend::kQrProdGranted));
  expectRecorded(qr_backend::kQrProdGranted);
}

void test_backend_production_qr_window_boundaries() {
  // The backend refuses a grant 1 ms before the issued_at it stored, and
  // accepts the door's earliest occurred_at, iatMs, for both tokens: 2 ms
  // after issued_at for the first, and 1 ms after it for the second, the
  // closest any door grant gets.
  const int64_t issuedAtMs = epochMsOf(qr_backend::kProdIssuedAt);
  expectOutsideWindow(qr_backend::kQrProdGrantedBeforeIssue);
  TEST_ASSERT_EQUAL_INT64(issuedAtMs - 1, occurredAtOf(qr_backend::kQrProdGrantedBeforeIssue));
  expectRecorded(qr_backend::kQrProdGranted);
  TEST_ASSERT_EQUAL_INT64(issuedAtMs + 2, occurredAtOf(qr_backend::kQrProdGranted));
  expectRecorded(qr_backend::kQrProdSecondGrantedEarliest);
  TEST_ASSERT_EQUAL_INT64(epochMsOf(qr_backend::kProdSecondIssuedAt) + 1,
                          occurredAtOf(qr_backend::kQrProdSecondGrantedEarliest));

  // Bounds 1 ms earlier than the recorded poll's would put occurred_at
  // before iatMs, so the door waits (Q16).
  {
    QrHarness h;
    h.sync(serverTimeOf(qr_backend::kQrProdPoll) - 1);
    const QrDecision decision = h.scan(qr_backend::kProdToken);
    expectDecision(decision, QrOutcome::Denied, qr_reason::kNotYetValid, false,
                   "1 ms before the earliest grant");
    TEST_ASSERT_FALSE(decision.isFinal);
    TEST_ASSERT_FALSE(h.output.unlocked());
    TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  }

  // A door whose bounds put occurred_at at the second token's iatMs grants
  // it, and queues the recorded report under the door's event ID. Bounds 1 ms
  // earlier make it wait.
  {
    QrHarness h;
    h.sync(qr_backend::kProdSecondIatMs + ostium::QrAccess::kOccurredAtMarginMs);
    const QrDecision decision = h.scan(qr_backend::kProdSecondToken);
    expectDecision(decision, QrOutcome::Granted, "", true, "the second token's earliest grant");
    TEST_ASSERT_EQUAL_STRING(withDoorEventId(qr_backend::kQrProdSecondGrantedEarliest).c_str(),
                             onlyQueuedBody(h).c_str());
  }
  {
    QrHarness h;
    h.sync(qr_backend::kProdSecondIatMs + ostium::QrAccess::kOccurredAtMarginMs - 1);
    const QrDecision decision = h.scan(qr_backend::kProdSecondToken);
    expectDecision(decision, QrOutcome::Denied, qr_reason::kNotYetValid, false,
                   "1 ms before the second token's earliest grant");
    TEST_ASSERT_FALSE(decision.isFinal);
    TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  }

  // The backend refuses a grant at its expires_at, and accepts the door's
  // latest occurred_at.
  const int64_t expiresAtMs = timestampIn(qr_backend::kQrProdSecondIssued, "expires_at");
  expectOutsideWindow(qr_backend::kQrProdGrantedAtExpiry);
  TEST_ASSERT_EQUAL_INT64(expiresAtMs, occurredAtOf(qr_backend::kQrProdGrantedAtExpiry));
  expectRecorded(qr_backend::kQrProdGrantedLatest);
  TEST_ASSERT_EQUAL_INT64(qr_backend::kProdSecondExpMs - 502,
                          occurredAtOf(qr_backend::kQrProdGrantedLatest));
  TEST_ASSERT_TRUE(occurredAtOf(qr_backend::kQrProdGrantedLatest) < expiresAtMs);

  // A door synced to the late poll, with a 0 ms round trip, has latest at
  // expMs - 1, the last time Q15 allows. It grants the second token and
  // queues byte for byte the report the backend accepted.
  {
    QrHarness h;
    h.sync(serverTimeOf(qr_backend::kQrProdPollLate));
    ostium::ServerTimeBounds bounds;
    TEST_ASSERT_TRUE(h.serverClock.bounds(h.clock.nowMs(), &bounds));
    TEST_ASSERT_EQUAL_INT64(qr_backend::kProdSecondExpMs - 1, bounds.latestMs);
    const QrDecision decision = h.scan(qr_backend::kProdSecondToken);
    expectDecision(decision, QrOutcome::Granted, "", true, "at the latest grant");
    TEST_ASSERT_EQUAL_STRING(qr_backend::kQrProdGrantedLatest.request, onlyQueuedBody(h).c_str());
  }
  // 1 ms later, latest reaches expMs, so the door denies the token as expired
  // and reports the denial instead (Q15).
  {
    QrHarness h;
    h.sync(serverTimeOf(qr_backend::kQrProdPollLate) + 1);
    const QrDecision decision = h.scan(qr_backend::kProdSecondToken);
    expectDecision(decision, QrOutcome::Denied, qr_reason::kExpired, true,
                   "1 ms after the latest grant");
    TEST_ASSERT_FALSE(h.output.unlocked());
    const SentQrReport denial = parseQrReportBody(onlyQueuedBody(h));
    TEST_ASSERT_EQUAL_STRING("denied", denial.result.c_str());
    TEST_ASSERT_EQUAL_STRING(qr_reason::kExpired, denial.reason.c_str());
  }
}
