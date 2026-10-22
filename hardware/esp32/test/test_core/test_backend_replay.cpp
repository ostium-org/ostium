// Replays responses recorded from the real FastAPI door routes
// (backend_responses.h) through the firmware core. The tests fail if the
// firmware stops matching those responses. After a backend change, record them
// again (hardware/esp32/tools/capture_backend_responses.py) so the tests check
// the new behavior.

#include <unity.h>

#include "backend_responses.h"
#include "fakes.h"
#include "tests.h"

using fakes::FakeClock;
using fakes::FakeLog;
using fakes::FakeSender;
using fakes::Harness;

namespace {

ostium::Event event(const char* commandId, const char* action, ostium::Result result,
                    const char* reason, int64_t occurredOffsetMs) {
  ostium::Event built;
  built.commandId = commandId;
  built.hasAction = true;
  built.action = action;
  built.result = result;
  if (reason != nullptr) built.reason = reason;
  built.occurredAtMs = fakes::kBaseEpochMs + occurredOffsetMs;
  return built;
}

}  // namespace

void test_backend_poll_runs_unlock_then_lock() {
  Harness door(ostium::LockController::kDefaultUnlockMs, backend::kDeviceKey);

  // The first poll holds the unlock.
  TEST_ASSERT_EQUAL_INT(200, backend::kPollOne.status);
  TEST_ASSERT_TRUE(door.poll(backend::kPollOne.body).ok);
  TEST_ASSERT_TRUE(door.output.unlocked());

  // The next poll still holds the unlock, because its event isn't posted yet,
  // and adds a lock. The door skips the unlock and runs the lock.
  door.clock.advance(800);
  TEST_ASSERT_TRUE(door.poll(backend::kPollTwo.body).ok);
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_EQUAL_UINT32(10800, writes[1].atMs);

  // Later polls repeat the lock, then nothing. Neither runs anything new.
  TEST_ASSERT_TRUE(door.poll(backend::kPollAfterReport.body).ok);
  const ostium::PollResult empty = door.poll(backend::kPollEmpty.body);
  TEST_ASSERT_TRUE(empty.ok);
  TEST_ASSERT_EQUAL_UINT32(0, empty.commands.size());
  TEST_ASSERT_EQUAL_UINT32(2, door.writesAfterBegin().size());

  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING((std::string("evt-") + backend::kUnlockCommandId).c_str(),
                           events[0].eventId.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING((std::string("evt-") + backend::kLockCommandId).c_str(),
                           events[1].eventId.c_str());
  TEST_ASSERT_EQUAL_STRING("lock", events[1].action.c_str());
}

void test_backend_accepts_firmware_event_bodies() {
  // The firmware writes byte for byte the bodies the backend accepted.
  using ostium::Result;
  TEST_ASSERT_EQUAL_STRING(
      backend::kEventDone.request,
      ostium::serializeEvent(event(backend::kUnlockCommandId, "unlock", Result::Done, nullptr, 1250))
          .c_str());
  TEST_ASSERT_EQUAL_INT(200, backend::kEventDone.status);
  TEST_ASSERT_EQUAL_INT(200, backend::kEventRepeat.status);

  TEST_ASSERT_EQUAL_STRING(
      backend::kEventLateRejected.request,
      ostium::serializeEvent(event(backend::kLateCommandId, "unlock", Result::Rejected,
                                   ostium::reason::kExpired, 14050))
          .c_str());
  TEST_ASSERT_EQUAL_INT(200, backend::kEventLateRejected.status);  // Late reports are accepted.

  // A different report under an event ID the backend already holds gets 409,
  // and the backend keeps the first report.
  TEST_ASSERT_EQUAL_STRING(
      backend::kEventConflict.request,
      ostium::serializeEvent(event(backend::kUnlockCommandId, "unlock", Result::Rejected,
                                   ostium::reason::kIssuedBeforeBoot, 3050))
          .c_str());
  TEST_ASSERT_EQUAL_INT(409, backend::kEventConflict.status);
}

void test_backend_event_statuses_follow_response_table() {
  struct Case {
    const char* name;
    backend::Capture capture;
    bool kept;
  };
  const Case cases[] = {
      {"first report", backend::kEventDone, false},
      {"exact retry", backend::kEventRepeat, false},
      {"same event ID, different report", backend::kEventConflict, false},
      {"action mismatch", backend::kEventWrongAction, false},
      {"unknown command", backend::kEventUnknownCommand, false},
      {"invalid body", backend::kEventInvalid, false},
  };
  for (const Case& replay : cases) {
    FakeClock clock;
    FakeLog log;
    ostium::EventOutbox outbox(log);
    FakeSender sender;
    sender.fallbackStatus = replay.capture.status;
    outbox.push(event(backend::kUnlockCommandId, "unlock", ostium::Result::Done, nullptr, 0),
                clock.nowMs());
    outbox.service(sender, clock);
    TEST_ASSERT_EQUAL_MESSAGE(replay.kept ? 1 : 0, outbox.size(), replay.name);
  }
  TEST_ASSERT_EQUAL_INT(409, backend::kEventWrongAction.status);
  TEST_ASSERT_EQUAL_INT(404, backend::kEventUnknownCommand.status);
  TEST_ASSERT_EQUAL_INT(422, backend::kEventInvalid.status);
}

void test_backend_rejected_token_runs_nothing() {
  Harness door(ostium::LockController::kDefaultUnlockMs, backend::kDeviceKey);
  TEST_ASSERT_EQUAL_INT(401, backend::kPollBadToken.status);
  TEST_ASSERT_FALSE(door.poll(backend::kPollBadToken.body, backend::kPollBadToken.status).ok);
  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  TEST_ASSERT_FALSE(door.processor.hasStartTime());
}

void test_backend_timestamps_parse() {
  // The backend writes 6 fractional digits, or none on a whole second.
  const ostium::PollResult six = ostium::parsePollResponse(200, backend::kPollOne.body);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 1200, six.serverTimeMs);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 1000, six.commands[0].issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 11000, six.commands[0].expiresAtMs);

  const ostium::PollResult whole = ostium::parsePollResponse(200, backend::kPollTwo.body);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 2000, whole.serverTimeMs);
  TEST_ASSERT_EQUAL_UINT32(2, whole.commands.size());
}

void test_backend_door_id_is_the_device_key() {
  // Each command's door_id is the device key from the app's request, so a door
  // configured with any other value rejects every command.
  const ostium::PollResult poll = ostium::parsePollResponse(200, backend::kPollOne.body);
  TEST_ASSERT_EQUAL_STRING(backend::kDeviceKey, poll.commands[0].doorId.c_str());

  Harness wrongKey;  // Configured with fakes::kDeviceId, not the backend's key.
  wrongKey.poll(backend::kPollOne.body);
  TEST_ASSERT_FALSE(wrongKey.output.unlocked());
  const auto events = wrongKey.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("wrong_door", events[0].reason.c_str());
}

void test_backend_command_matches_poll() {
  // The app's 201 response describes the same command the door then polls.
  JsonDocument created;
  TEST_ASSERT_FALSE(deserializeJson(created, backend::kCommandCreated.body));
  TEST_ASSERT_EQUAL_INT(201, backend::kCommandCreated.status);
  const ostium::PollResult poll = ostium::parsePollResponse(200, backend::kPollOne.body);
  const ostium::ParsedCommand& command = poll.commands[0];
  TEST_ASSERT_EQUAL_STRING(created["command_id"].as<const char*>(), command.commandId.c_str());
  TEST_ASSERT_EQUAL_STRING(created["door_id"].as<const char*>(), command.doorId.c_str());
  TEST_ASSERT_EQUAL_STRING(created["action"].as<const char*>(), command.actionText.c_str());
  TEST_ASSERT_EQUAL_INT64(10000, command.expiresAtMs - command.issuedAtMs);
}

void test_backend_stops_serving_expired_commands() {
  // R3 forgets an ID once its command expires, so the backend must stop
  // serving the command then. The late unlock was never reported, and it
  // expired at 13.2 s.
  TEST_ASSERT_EQUAL_INT(200, backend::kPollAfterExpiry.status);
  const ostium::PollResult poll = ostium::parsePollResponse(200, backend::kPollAfterExpiry.body);
  TEST_ASSERT_TRUE(poll.ok);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 14000, poll.serverTimeMs);
  TEST_ASSERT_EQUAL_UINT32(0, poll.commands.size());
}

void test_backend_accepts_report_after_reset() {
  // A reset lost an unlock's done event. After the reset, the door rejects the
  // command as issued_before_boot (R5). That's the first report the backend
  // sees, so it answers 200, records the outcome as unknown, and stops
  // serving the command.
  TEST_ASSERT_EQUAL_STRING(
      backend::kEventAfterReset.request,
      ostium::serializeEvent(event(backend::kResetCommandId, "unlock", ostium::Result::Rejected,
                                   ostium::reason::kIssuedBeforeBoot, 4450))
          .c_str());
  TEST_ASSERT_EQUAL_INT(200, backend::kEventAfterReset.status);

  const ostium::PollResult poll =
      ostium::parsePollResponse(200, backend::kPollAfterResetReport.body);
  TEST_ASSERT_TRUE(poll.ok);
  for (const ostium::ParsedCommand& command : poll.commands) {
    TEST_ASSERT_TRUE(command.commandId != backend::kResetCommandId);
  }
}
