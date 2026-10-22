// Event bodies and the event response table.

#include <unity.h>

#include "fakes.h"
#include "tests.h"

using fakes::FakeClock;
using fakes::FakeLog;
using fakes::FakeSender;
using ostium::Event;
using ostium::EventOutbox;
using ostium::Result;

namespace {

Event doneEvent(const std::string& commandId) {
  Event event;
  event.commandId = commandId;
  event.hasAction = true;
  event.action = "unlock";
  event.result = Result::Done;
  event.occurredAtMs = fakes::kBaseEpochMs;
  return event;
}

}  // namespace

void test_event_body_matches_contract_examples() {
  // Mirrors docs/firmware/examples/event-done.json.
  Event done = doneEvent("6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f");
  done.occurredAtMs = fakes::kBaseEpochMs + 1250;
  TEST_ASSERT_EQUAL_STRING(
      "{\"event_id\":\"evt-6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f\","
      "\"command_id\":\"6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f\",\"action\":\"unlock\","
      "\"result\":\"done\",\"reason\":null,\"occurred_at\":\"2026-10-22T15:00:01.250Z\"}",
      ostium::serializeEvent(done).c_str());

  // Mirrors docs/firmware/examples/event-rejected.json.
  Event rejected = doneEvent("5d7f9b1c-3e5a-4c7e-9f1b-3d5f7a9c1e3b");
  rejected.result = Result::Rejected;
  rejected.reason = ostium::reason::kWrongDoor;
  rejected.occurredAtMs = fakes::kBaseEpochMs + 601250;
  TEST_ASSERT_EQUAL_STRING(
      "{\"event_id\":\"evt-5d7f9b1c-3e5a-4c7e-9f1b-3d5f7a9c1e3b\","
      "\"command_id\":\"5d7f9b1c-3e5a-4c7e-9f1b-3d5f7a9c1e3b\",\"action\":\"unlock\","
      "\"result\":\"rejected\",\"reason\":\"wrong_door\","
      "\"occurred_at\":\"2026-10-22T15:10:01.250Z\"}",
      ostium::serializeEvent(rejected).c_str());

  // A command with no action reports "action": null.
  Event noAction = doneEvent("cmd-a");
  noAction.hasAction = false;
  noAction.result = Result::Rejected;
  noAction.reason = ostium::reason::kMalformedCommand;
  TEST_ASSERT_EQUAL_STRING("null",
                           fakes::parseEventBody(ostium::serializeEvent(noAction)).action.c_str());
}

void test_example_poll_response_parses() {
  // Mirrors docs/firmware/examples/poll-response.json.
  const ostium::PollResult result = ostium::parsePollResponse(
      200,
      "{\"server_time\":\"2026-10-22T15:00:01.200Z\",\"commands\":[{"
      "\"command_id\":\"6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f\",\"action\":\"unlock\","
      "\"door_id\":\"9a1d7c3b-2e4f-4b6a-8c0d-5f3e2b1a7c64\","
      "\"issued_at\":\"2026-10-22T15:00:00.000Z\",\"expires_at\":\"2026-10-22T15:00:10.000Z\"}]}");

  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 1200, result.serverTimeMs);
  TEST_ASSERT_EQUAL_UINT32(1, result.commands.size());
  const ostium::ParsedCommand& command = result.commands[0];
  TEST_ASSERT_TRUE(command.problem == ostium::CommandProblem::None);
  TEST_ASSERT_TRUE(command.action == ostium::Action::Unlock);
  TEST_ASSERT_EQUAL_STRING(fakes::kDoorId, command.doorId.c_str());
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs, command.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 10000, command.expiresAtMs);

  // Mirrors docs/firmware/examples/poll-response-empty.json.
  const ostium::PollResult empty = ostium::parsePollResponse(
      200, "{\"server_time\":\"2026-10-22T15:00:02.700Z\",\"commands\":[]}");
  TEST_ASSERT_TRUE(empty.ok);
  TEST_ASSERT_EQUAL_UINT32(0, empty.commands.size());
}

void test_event_responses_follow_contract_table() {
  struct Case {
    int status;
    bool kept;
  };
  const Case cases[] = {
      {200, false}, {201, false}, {204, false},  // Delivered.
      {409, false},                              // Already reported.
      {400, false}, {404, false}, {413, false}, {415, false}, {422, false},  // Dropped.
      {401, true},  {429, true},  {500, true},  {503, true},  {-1, true},    // Retried.
      {307, true},  {403, true},  {405, true},                               // Any other status.
  };
  for (const Case& responseCase : cases) {
    FakeClock clock;
    FakeLog log;
    EventOutbox outbox(log);
    FakeSender sender;
    sender.fallbackStatus = responseCase.status;
    outbox.push(doneEvent("cmd-a"), clock.nowMs());
    outbox.service(sender, clock);

    const std::string label = "status " + std::to_string(responseCase.status);
    TEST_ASSERT_EQUAL_MESSAGE(responseCase.kept ? 1 : 0, outbox.size(), label.c_str());
    // Every response but a 2xx is logged with its status.
    if (responseCase.status < 200 || responseCase.status > 299) {
      TEST_ASSERT_TRUE_MESSAGE(log.contains(std::to_string(responseCase.status)), label.c_str());
    }
  }
}

void test_event_service_posts_at_most_one_event() {
  FakeClock clock;
  FakeLog log;
  EventOutbox outbox(log);
  FakeSender sender;
  for (int i = 0; i < 3; ++i) outbox.push(doneEvent("cmd-" + std::to_string(i)), clock.nowMs());

  // Each post can block for seconds, so one call posts only one event.
  outbox.service(sender, clock);
  TEST_ASSERT_EQUAL_UINT32(1, sender.bodies.size());
  TEST_ASSERT_EQUAL_UINT32(2, outbox.size());
  TEST_ASSERT_EQUAL_STRING("evt-cmd-0", fakes::parseEventBody(sender.bodies[0]).eventId.c_str());
}

void test_event_retry_backoff_starts_after_slow_post() {
  FakeClock clock;
  FakeLog log;
  EventOutbox outbox(log);
  outbox.push(doneEvent("cmd-a"), clock.nowMs());
  fakes::BlockingSender blocking(clock, 10000);
  outbox.service(blocking, clock);  // Fails at 10,000 ms, so the retry is due at 12,000 ms.

  FakeSender sender;
  outbox.service(sender, clock);
  clock.set(11999);
  outbox.service(sender, clock);
  TEST_ASSERT_EQUAL_UINT32(0, sender.bodies.size());
  clock.set(12000);
  outbox.service(sender, clock);
  TEST_ASSERT_EQUAL_UINT32(1, sender.bodies.size());
}

void test_event_retry_survives_millis_wraparound() {
  FakeClock clock;
  FakeLog log;
  EventOutbox outbox(log);
  const uint32_t nearWrap = 0xFFFFFFFFu - 500;
  clock.set(nearWrap);
  outbox.push(doneEvent("cmd-a"), clock.nowMs());
  FakeSender sender;
  sender.statuses = {503};
  outbox.service(sender, clock);  // The retry is due at nearWrap + 2,000, past zero.

  clock.set(nearWrap + 100);
  outbox.service(sender, clock);
  clock.set(nearWrap + 1999);
  outbox.service(sender, clock);
  TEST_ASSERT_EQUAL_UINT32(1, sender.bodies.size());
  clock.set(nearWrap + 2000);
  outbox.service(sender, clock);
  TEST_ASSERT_EQUAL_UINT32(2, sender.bodies.size());
  TEST_ASSERT_EQUAL_UINT32(0, outbox.size());
}

void test_event_retry_backoff_is_2_4_8_seconds() {
  FakeClock clock;
  FakeLog log;
  EventOutbox outbox(log);
  FakeSender sender;
  sender.fallbackStatus = 503;
  outbox.push(doneEvent("cmd-a"), clock.nowMs());

  // Attempts at 0, 2, 6, 14, and 22 seconds.
  const uint32_t attemptsAt[] = {0, 2000, 6000, 14000, 22000};
  size_t posts = 0;
  for (uint32_t t = 0; t <= 22000; t += 100) {
    clock.set(t);
    outbox.service(sender, clock);
    if (sender.bodies.size() > posts) {
      TEST_ASSERT_EQUAL_UINT32(attemptsAt[posts], t);
      posts = sender.bodies.size();
    }
  }
  TEST_ASSERT_EQUAL_UINT32(5, posts);
}

void test_event_queue_drops_oldest_when_full() {
  FakeClock clock;
  FakeLog log;
  EventOutbox outbox(log);
  for (int i = 0; i < 9; ++i) outbox.push(doneEvent("cmd-" + std::to_string(i)), clock.nowMs());

  TEST_ASSERT_EQUAL_UINT32(EventOutbox::kCapacity, outbox.size());
  TEST_ASSERT_FALSE(outbox.contains("evt-cmd-0"));
  TEST_ASSERT_TRUE(outbox.contains("evt-cmd-8"));
  TEST_ASSERT_TRUE(log.contains("dropped evt-cmd-0"));
}
