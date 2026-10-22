// Command rules: R2 to R8, R13, R14, and R16.

#include <unity.h>

#include "fakes.h"
#include "tests.h"

using fakes::commandJson;
using fakes::Harness;
using fakes::pollJson;

void test_commands_run_in_response_order() {
  Harness door;
  door.poll(pollJson(20000, {commandJson("cmd-lock", "lock", 18000, 28000),
                             commandJson("cmd-unlock", "unlock", 19000, 29000)}));

  // A later unlock doesn't cancel the earlier lock: both run, in order.
  TEST_ASSERT_TRUE(door.output.unlocked());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("cmd-lock", events[0].commandId.c_str());
  TEST_ASSERT_EQUAL_STRING("cmd-unlock", events[1].commandId.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[1].result.c_str());
}

void test_unlock_then_lock_in_one_poll_ends_locked() {
  Harness door;
  door.poll(pollJson(20000, {commandJson("cmd-unlock", "unlock", 19000, 29000),
                             commandJson("cmd-lock", "lock", 19500, 29500)}));

  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_FALSE(door.timer.armed());
  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_TRUE(writes[0].unlocked);
  TEST_ASSERT_FALSE(writes[1].unlocked);

  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("unlock", events[0].action.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING("lock", events[1].action.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[1].result.c_str());
}

void test_duplicate_command_is_not_run_again() {
  Harness door;
  const std::string unlock = commandJson("cmd-a", "unlock", 19000, 29000);
  door.poll(pollJson(20000, {unlock}));
  door.clock.advance(4000);  // Relocked.
  TEST_ASSERT_FALSE(door.output.unlocked());

  // The backend redelivers the command because its event hasn't landed yet.
  door.poll(pollJson(24000, {unlock}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_EQUAL_UINT32(2, door.writesAfterBegin().size());
  TEST_ASSERT_EQUAL_UINT32(1, door.flushEvents().size());

  // A rejected command isn't rejected again either.
  const std::string wrongDoor = commandJson("cmd-b", "unlock", 23000, 33000, "other-door");
  door.poll(pollJson(24500, {wrongDoor}));
  door.poll(pollJson(25000, {wrongDoor}));
  TEST_ASSERT_EQUAL_UINT32(1, door.flushEvents().size());
}

void test_redelivered_unlock_does_not_restart_timer() {
  Harness door;
  const std::string unlock = commandJson("cmd-a", "unlock", 19000, 29000);
  door.poll(pollJson(20000, {unlock}));
  door.clock.advance(1500);
  door.poll(pollJson(21500, {unlock}));
  door.clock.advance(5000);

  TEST_ASSERT_EQUAL_INT(1, door.timer.starts());
  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_EQUAL_UINT32(13000, writes[1].atMs);
}

void test_duplicate_list_never_drops_unexpired_id() {
  Harness door;
  std::vector<std::string> commands;
  for (int i = 0; i < 32; ++i) {
    commands.push_back(commandJson("cmd-" + std::to_string(i), "lock", 19000, 29000));
  }
  door.poll(pollJson(20000, commands));
  TEST_ASSERT_FALSE(door.log.contains("duplicate_cache_full"));

  // The list is full of unexpired IDs, so a 33rd command is rejected...
  door.poll(pollJson(21000, {commandJson("cmd-32", "unlock", 20500, 30500)}));
  TEST_ASSERT_TRUE(door.log.contains("rejected command cmd-32: duplicate_cache_full"));
  TEST_ASSERT_FALSE(door.output.unlocked());

  // ...and the oldest ID is still remembered, so its command doesn't run again.
  const size_t messagesBefore = door.log.messages.size();
  door.poll(pollJson(21500, {commandJson("cmd-0", "unlock", 19000, 29000)}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_EQUAL_UINT32(messagesBefore, door.log.messages.size());

  // Once those commands expire, their IDs are released and new commands run.
  door.clock.advance(9000);
  door.poll(pollJson(30500, {commandJson("cmd-33", "unlock", 30000, 40000)}));
  TEST_ASSERT_TRUE(door.output.unlocked());
}

void test_expired_command_is_rejected() {
  Harness door;
  door.poll(pollJson(20000, {}));  // Sets the start time.

  // The poll request started 6 s ago, so by the R4 estimate the command
  // expired 1 s ago.
  door.clock.advance(7000);
  door.poll(pollJson(21000, {commandJson("cmd-late", "unlock", 20000, 26000)}), 200, 6000);

  // A command that expires exactly at the estimated time is also expired.
  door.poll(pollJson(27000, {commandJson("cmd-edge", "unlock", 25000, 27000)}));

  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("rejected", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING("expired", events[0].reason.c_str());
  TEST_ASSERT_EQUAL_STRING(fakes::at(27000).c_str(), events[0].occurredAt.c_str());
  TEST_ASSERT_EQUAL_STRING("expired", events[1].reason.c_str());
}

void test_command_issued_before_boot_is_rejected() {
  Harness door;
  // The start time is offset 12,000 ms. A command from before then may have
  // run before a reset, so the door doesn't risk a second unlock.
  door.poll(pollJson(20000, {commandJson("cmd-old", "unlock", 11999, 21999)}));
  TEST_ASSERT_FALSE(door.output.unlocked());

  door.poll(pollJson(20000, {commandJson("cmd-new", "unlock", 12000, 22000)}));
  TEST_ASSERT_TRUE(door.output.unlocked());

  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("rejected", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING("issued_before_boot", events[0].reason.c_str());
  TEST_ASSERT_EQUAL_STRING("done", events[1].result.c_str());
}

void test_boot_time_is_set_once_from_first_poll() {
  Harness door;
  // A failed poll doesn't set the start time.
  door.poll("not json");
  TEST_ASSERT_FALSE(door.processor.hasStartTime());

  door.poll(pollJson(20000, {}));
  TEST_ASSERT_TRUE(door.processor.hasStartTime());
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + Harness::kStartOffsetMs,
                          door.processor.startTimeMs());

  // A later poll whose server time jumped ahead doesn't move it.
  door.clock.advance(1000);
  door.poll(pollJson(60000, {commandJson("cmd-a", "unlock", 13000, 70000)}));
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + Harness::kStartOffsetMs,
                          door.processor.startTimeMs());
  TEST_ASSERT_TRUE(door.output.unlocked());
}

void test_command_for_other_door_is_rejected() {
  Harness door;
  const std::string thisDoor = fakes::kDeviceId;
  door.poll(pollJson(20000, {commandJson("cmd-a", "unlock", 19000, 29000, "other-door"),
                             // An empty door_id isn't a wildcard.
                             commandJson("cmd-b", "unlock", 19000, 29000, ""),
                             // A door_id that only starts with this door's ID.
                             commandJson("cmd-c", "unlock", 19000, 29000, thisDoor + "-2")}));

  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(3, events.size());
  for (const auto& event : events) {
    TEST_ASSERT_EQUAL_STRING("rejected", event.result.c_str());
    TEST_ASSERT_EQUAL_STRING("wrong_door", event.reason.c_str());
  }

  // A door_id with this door's ID, a NUL character, and more text is invalid,
  // not a match.
  door.poll(pollJson(20000, {commandJson("cmd-d", "unlock", 19000, 29000,
                                         thisDoor + "\\u0000other-door")}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto nul = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, nul.size());
  TEST_ASSERT_EQUAL_STRING("malformed_command", nul[0].reason.c_str());
}

void test_malformed_command_is_rejected() {
  Harness door;
  const std::string door_id = fakes::kDeviceId;
  const std::string issued = fakes::at(19000);
  const std::string expires = fakes::at(29000);
  door.poll(pollJson(
      20000,
      {
          // Missing door_id.
          "{\"command_id\":\"m-1\",\"action\":\"unlock\",\"issued_at\":\"" + issued +
              "\",\"expires_at\":\"" + expires + "\"}",
          // issued_at that isn't a timestamp.
          "{\"command_id\":\"m-2\",\"action\":\"unlock\",\"door_id\":\"" + door_id +
              "\",\"issued_at\":\"yesterday\",\"expires_at\":\"" + expires + "\"}",
          // expires_at with no UTC offset.
          "{\"command_id\":\"m-3\",\"action\":\"unlock\",\"door_id\":\"" + door_id +
              "\",\"issued_at\":\"" + issued + "\",\"expires_at\":\"2026-10-22T15:00:29\"}",
          // Missing action.
          "{\"command_id\":\"m-4\",\"door_id\":\"" + door_id + "\",\"issued_at\":\"" + issued +
              "\",\"expires_at\":\"" + expires + "\"}",
          // Both malformed and an unknown action: malformed comes first.
          "{\"command_id\":\"m-5\",\"action\":\"open\",\"issued_at\":\"" + issued +
              "\",\"expires_at\":\"" + expires + "\"}",
          // A valid timestamp followed by a NUL character and more text.
          "{\"command_id\":\"m-6\",\"action\":\"unlock\",\"door_id\":\"" + door_id +
              "\",\"issued_at\":\"" + issued + "\\u0000junk\",\"expires_at\":\"" + expires +
              "\"}",
          // No usable command_id: logged and skipped, with no event.
          "{\"action\":\"unlock\",\"door_id\":\"" + door_id + "\",\"issued_at\":\"" + issued +
              "\",\"expires_at\":\"" + expires + "\"}",
          "{\"command_id\":\"abc\\u0000one\",\"action\":\"unlock\",\"door_id\":\"" + door_id +
              "\",\"issued_at\":\"" + issued + "\",\"expires_at\":\"" + expires + "\"}",
          "42",
      }));

  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  TEST_ASSERT_TRUE(door.log.contains(
      "skipped command with no usable command_id (missing or unusable command_id): "
      "{\"action\":\"unlock\""));
  TEST_ASSERT_TRUE(door.log.contains(
      "skipped command with no usable command_id (command isn't a JSON object): 42"));
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(6, events.size());
  for (const auto& event : events) {
    TEST_ASSERT_EQUAL_STRING("rejected", event.result.c_str());
    TEST_ASSERT_EQUAL_STRING("malformed_command", event.reason.c_str());
  }
  TEST_ASSERT_EQUAL_STRING("null", events[3].action.c_str());
  TEST_ASSERT_EQUAL_STRING("open", events[4].action.c_str());
}

void test_unknown_action_is_rejected() {
  Harness door;
  door.poll(pollJson(20000, {commandJson("cmd-a", "open", 19000, 29000),
                             // invalid_action is checked before wrong_door.
                             commandJson("cmd-b", "open", 19000, 29000, "other-door")}));

  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  for (const auto& event : events) {
    TEST_ASSERT_EQUAL_STRING("rejected", event.result.c_str());
    TEST_ASSERT_EQUAL_STRING("invalid_action", event.reason.c_str());
    TEST_ASSERT_EQUAL_STRING("open", event.action.c_str());
  }
}

void test_event_id_derives_from_command_id() {
  Harness door;
  const std::string unlock =
      commandJson("6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f", "unlock", 19000, 29000);
  door.poll(pollJson(20000, {unlock}));

  // The first post fails, and the retry sends the same event ID and body.
  fakes::FakeSender sender;
  sender.statuses = {503, 200};
  door.outbox.service(sender, door.clock);
  door.clock.advance(2000);
  door.outbox.service(sender, door.clock);

  TEST_ASSERT_EQUAL_UINT32(2, sender.bodies.size());
  TEST_ASSERT_EQUAL_STRING(sender.bodies[0].c_str(), sender.bodies[1].c_str());
  const auto event = fakes::parseEventBody(sender.bodies[0]);
  TEST_ASSERT_EQUAL_STRING("evt-6f1e2d3c-4b5a-4c7d-8e9f-0a1b2c3d4e5f", event.eventId.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());

  // A redelivered command gets no second event.
  door.poll(pollJson(22000, {unlock}));
  TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());
}

void test_failed_event_post_does_not_block_lock_control() {
  Harness door;
  fakes::FakeSender sender;
  sender.statuses = {503};  // Only the first post fails.

  door.poll(pollJson(20000, {commandJson("cmd-a", "unlock", 19000, 29000)}));
  door.outbox.service(sender, door.clock);  // cmd-a fails, retry in 2 s.
  TEST_ASSERT_TRUE(door.outbox.contains("evt-cmd-a"));

  // While cmd-a waits to retry, a Lock still runs at once...
  door.clock.advance(500);
  door.poll(pollJson(20500, {commandJson("cmd-b", "lock", 20200, 30200)}));
  TEST_ASSERT_FALSE(door.output.unlocked());

  // ...and its event goes out first, without waiting for cmd-a.
  door.outbox.service(sender, door.clock);
  TEST_ASSERT_EQUAL_UINT32(2, sender.bodies.size());
  TEST_ASSERT_EQUAL_STRING("evt-cmd-b", fakes::parseEventBody(sender.bodies[1]).eventId.c_str());
  TEST_ASSERT_TRUE(door.outbox.contains("evt-cmd-a"));
  TEST_ASSERT_FALSE(door.outbox.contains("evt-cmd-b"));

  // cmd-a is retried once its backoff ends.
  door.clock.advance(1500);
  door.outbox.service(sender, door.clock);
  TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());
}

void test_bad_poll_response_runs_nothing() {
  const std::string unlock = commandJson("cmd-a", "unlock", 19000, 29000);
  const std::string good = pollJson(20000, {unlock});
  std::string oversized = pollJson(20000, {unlock});
  oversized.insert(oversized.size() - 1, ",\"padding\":\"" + std::string(8200, 'x') + "\"");

  struct Case {
    int status;
    std::string body;
  };
  const Case cases[] = {
      {500, good},
      {201, good},
      {200, "{\"server_time\":\"" + fakes::at(20000) + "\",\"commands\":["},
      {200, oversized},
      {200, "{\"commands\":[" + unlock + "]}"},
      {200, "{\"server_time\":\"soon\",\"commands\":[" + unlock + "]}"},
      {200, "{\"server_time\":\"2026-10-22T15:00:20\",\"commands\":[" + unlock + "]}"},
      {200, "{\"server_time\":\"" + fakes::at(20000) + "\",\"commands\":" + unlock + "}"},
      {200, "[" + good + "]"},
      // A good response followed by anything but whitespace.
      {200, good + "x"},
      {200, good + "{\"server_time\":1}"},
  };

  for (const Case& badCase : cases) {
    Harness door;
    const ostium::PollResult result = door.poll(badCase.body, badCase.status);
    TEST_ASSERT_FALSE_MESSAGE(result.ok, badCase.body.substr(0, 60).c_str());
    TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
    TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());
    TEST_ASSERT_FALSE(door.processor.hasStartTime());
  }

  // The same command in a good response runs, with or without trailing
  // whitespace.
  Harness door;
  TEST_ASSERT_TRUE(door.poll(good + "\r\n ").ok);
  TEST_ASSERT_TRUE(door.output.unlocked());
}

void test_checks_report_first_failure_in_contract_order() {
  Harness door;  // Start time is offset 12,000 ms.
  // o-1 fails wrong_door, issued_before_boot, and expired: wrong_door wins.
  // o-2 fails issued_before_boot and expired: issued_before_boot wins.
  door.poll(pollJson(20000, {commandJson("o-1", "unlock", 11000, 19000, "other-door"),
                             commandJson("o-2", "unlock", 11000, 19000)}));
  std::vector<std::string> fill;
  for (int i = 0; i < 32; ++i) {
    fill.push_back(commandJson("f-" + std::to_string(i), "lock", 19000, 29000));
  }
  door.poll(pollJson(20000, fill));
  // o-3 is malformed while the list is full: duplicate_cache_full wins.
  door.poll(pollJson(20000, {"{\"command_id\":\"o-3\",\"action\":\"unlock\",\"issued_at\":\"" +
                             fakes::at(19000) + "\",\"expires_at\":\"" + fakes::at(29000) +
                             "\"}"}));

  TEST_ASSERT_TRUE(door.log.contains("rejected command o-1: wrong_door"));
  TEST_ASSERT_TRUE(door.log.contains("rejected command o-2: issued_before_boot"));
  TEST_ASSERT_TRUE(door.log.contains("rejected command o-3: duplicate_cache_full"));
  TEST_ASSERT_FALSE(door.output.unlocked());
}

void test_cache_full_rejection_is_final() {
  Harness door;
  std::vector<std::string> commands;
  for (int i = 0; i < 32; ++i) {
    commands.push_back(commandJson("cmd-" + std::to_string(i), "lock", 19000, 29000));
  }
  door.poll(pollJson(20000, commands));
  const std::string late = commandJson("cmd-32", "unlock", 20500, 30500);
  door.poll(pollJson(21000, {late}));

  // The other IDs expire, but cmd-32's rejected event is still queued, so the
  // redelivered command doesn't run.
  door.clock.advance(8500);
  door.poll(pollJson(29500, {late}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  for (const auto& event : door.flushEvents()) {
    if (event.commandId == "cmd-32") {
      TEST_ASSERT_EQUAL_STRING("duplicate_cache_full", event.reason.c_str());
    }
  }
}

void test_cache_full_rejection_is_queued_once() {
  Harness door;
  std::vector<std::string> commands;
  for (int i = 0; i < 32; ++i) {
    commands.push_back(commandJson("cmd-" + std::to_string(i), "lock", 19000, 29000));
  }
  door.poll(pollJson(20000, commands));
  door.flushEvents();

  // cmd-32 is rejected and not remembered, so the backend redelivers it
  // before its event is posted. It's still queued only once.
  const std::string extra = commandJson("cmd-32", "unlock", 20500, 30500);
  door.poll(pollJson(21000, {extra}));
  door.poll(pollJson(22500, {extra}));
  TEST_ASSERT_EQUAL_UINT32(1, door.outbox.size());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("duplicate_cache_full", events[0].reason.c_str());
}

void test_duplicate_id_kept_until_its_own_expiry() {
  Harness door;
  const std::string longLived = commandJson("cmd-long", "unlock", 19000, 60000);
  door.poll(pollJson(20000, {longLived}));
  door.flushEvents();  // Only the duplicate list can stop a second run now.
  door.clock.advance(15000);  // Relocked at 13,000 ms.

  // Another command goes first, which runs the list's eviction. cmd-long
  // hasn't expired, so it's still remembered and doesn't run again.
  door.poll(pollJson(35000, {commandJson("cmd-other", "lock", 34000, 44000), longLived}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_EQUAL_INT(1, door.timer.starts());
}

void test_boot_time_uses_poll_start_time() {
  Harness door;
  // The first poll request starts at uptime 10,000 ms, and its response takes
  // 3,000 ms. runPoll records the start before the request, so the start
  // time comes from 10,000 ms, not from the arrival at 13,000 ms.
  fakes::SlowFetcher fetcher(door.clock, 3000, 200,
                             pollJson(20000, {commandJson("cmd-old", "unlock", 11000, 24000)}));
  TEST_ASSERT_TRUE(ostium::runPoll(fetcher, door.clock, door.scheduler, door.processor));

  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + Harness::kStartOffsetMs,
                          door.processor.startTimeMs());
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("issued_before_boot", events[0].reason.c_str());
}

void test_long_action_is_malformed_and_event_stays_small() {
  Harness door;
  // The longest command_id, made of quotes that each escape to 2 bytes.
  const std::string escapedId = [] {
    std::string id;
    for (size_t i = 0; i < ostium::kMaxCommandIdLength; ++i) id += "\\\"";
    return id;
  }();
  const std::string longAction(900, 'x');
  door.poll(pollJson(20000, {"{\"command_id\":\"" + escapedId + "\",\"action\":\"" + longAction +
                             "\",\"door_id\":\"" + std::string(fakes::kDeviceId) +
                             "\",\"issued_at\":\"" + fakes::at(19000) + "\",\"expires_at\":\"" +
                             fakes::at(29000) + "\"}"}));

  fakes::FakeSender sender;
  door.outbox.service(sender, door.clock);
  TEST_ASSERT_EQUAL_UINT32(1, sender.bodies.size());
  TEST_ASSERT_LESS_THAN_UINT32(1024, sender.bodies[0].size());
  const auto event = fakes::parseEventBody(sender.bodies[0]);
  TEST_ASSERT_EQUAL_STRING("malformed_command", event.reason.c_str());
  TEST_ASSERT_EQUAL_STRING("null", event.action.c_str());

  // An action of exactly 16 characters is kept, and it's just unknown.
  Harness edge;
  edge.poll(pollJson(20000, {commandJson("cmd-a", std::string(16, 'x'), 19000, 29000)}));
  const auto edgeEvents = edge.flushEvents();
  TEST_ASSERT_EQUAL_STRING("invalid_action", edgeEvents[0].reason.c_str());
}

void test_poll_command_count_is_capped() {
  // A broken backend sends an 8 KiB array of zeros. Only 32 are handled.
  std::string body = "{\"server_time\":\"" + fakes::at(20000) + "\",\"commands\":[0";
  while (body.size() < ostium::kMaxPollBodyBytes - 4) body += ",0";
  body += "]}";

  Harness door;
  const ostium::PollResult result = door.poll(body);
  TEST_ASSERT_TRUE(result.ok);
  TEST_ASSERT_EQUAL_UINT32(ostium::kMaxCommandsPerPoll, result.commands.size());
  TEST_ASSERT_GREATER_THAN_UINT32(3000, result.deferred);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(ostium::kMaxCommandsPerPoll + 1, door.log.messages.size());
  TEST_ASSERT_TRUE(door.log.contains("commands for a later poll"));
  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
}

void test_slow_response_cannot_rerun_command_from_before_boot() {
  // The door started at server offset 15,000 ms. Its first poll request
  // starts at uptime 5,000 ms, the backend stamps server_time 100 ms later,
  // and the response takes 2,600 ms to arrive. The command was issued 300 ms
  // before the door started, so it may already have run before a reset.
  Harness door;
  door.clock.set(5000);
  fakes::SlowFetcher fetcher(door.clock, 2600, 200,
                             pollJson(20100, {commandJson("cmd-old", "unlock", 14700, 24700)}));
  TEST_ASSERT_TRUE(ostium::runPoll(fetcher, door.clock, door.scheduler, door.processor));

  // Measured from the request start, the start time is 20,100 - 5,000 +
  // 2,000 = 17,100 ms, so the command is rejected instead of run again.
  // Measured from the arrival, it would be 14,500 ms, and the command would run.
  TEST_ASSERT_EQUAL_INT64(fakes::kBaseEpochMs + 17100, door.processor.startTimeMs());
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("issued_before_boot", events[0].reason.c_str());
  TEST_ASSERT_EQUAL_STRING(fakes::at(22700).c_str(), events[0].occurredAt.c_str());
}

void test_run_poll_backs_off_after_a_failed_poll() {
  Harness door;
  fakes::SlowFetcher failing(door.clock, 3000, -1, "");
  TEST_ASSERT_FALSE(ostium::runPoll(failing, door.clock, door.scheduler, door.processor));
  TEST_ASSERT_EQUAL_UINT32(1, door.scheduler.failures());
  // The 2 s backoff starts when the failed request returns, at 13,000 ms.
  TEST_ASSERT_FALSE(door.scheduler.due(14999));
  TEST_ASSERT_TRUE(door.scheduler.due(15000));
  TEST_ASSERT_FALSE(door.processor.hasStartTime());

  door.clock.set(15000);
  fakes::SlowFetcher good(door.clock, 100, 200, pollJson(25000, {}));
  TEST_ASSERT_TRUE(ostium::runPoll(good, door.clock, door.scheduler, door.processor));
  TEST_ASSERT_EQUAL_UINT32(0, door.scheduler.failures());
  TEST_ASSERT_TRUE(door.scheduler.due(16500));  // 1.5 s after the request started.
}

void test_duplicate_id_kept_while_backend_still_serves() {
  // The R4 estimate can run ahead of the backend's clock after a slow poll,
  // but the backend keeps serving a command until its own clock passes
  // expires_at. The door must not forget the ID before then.
  Harness door;
  const std::string unlock = commandJson("cmd-a", "unlock", 20000, 30500);
  door.poll(pollJson(20000, {unlock}));
  fakes::FakeSender dropping;
  dropping.fallbackStatus = 422;
  door.outbox.service(dropping, door.clock);  // The event is dropped, so it's never recorded.
  TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());

  // A slow poll: the request starts at uptime 18,500 ms, the backend stamps
  // 29,900 ms, and the response arrives 1,500 ms after the start, so the R4
  // estimate is 31,400 ms, past cmd-a's expiry. A new command is added in the
  // same poll.
  door.clock.advance(10000);
  door.poll(pollJson(29900, {unlock, commandJson("cmd-b", "lock", 29000, 39000)}), 200, 1500);

  // The next poll is fast: stamped 30,000 ms, 200 ms in flight. The backend
  // still serves cmd-a, and the door still remembers it.
  door.clock.advance(200);
  door.poll(pollJson(30000, {unlock, commandJson("cmd-b", "lock", 29000, 39000)}), 200, 200);
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_EQUAL_INT(1, door.timer.starts());
}
