// Lock output rules: R1, R9 to R12.

#include <unity.h>

#include "fakes.h"
#include "tests.h"

using fakes::FakeClock;
using fakes::FakeLockOutput;
using fakes::FakeTimer;
using fakes::Harness;
using ostium::LockController;

void test_lock_is_locked_after_startup() {
  FakeClock clock;
  FakeTimer timer(clock);
  FakeLockOutput output(clock);  // Starts on, like a floating pin.
  LockController lock(output, timer);

  TEST_ASSERT_TRUE(output.writes().empty());  // The constructor doesn't touch the pin.
  lock.begin();

  TEST_ASSERT_EQUAL_UINT32(1, output.writes().size());
  TEST_ASSERT_FALSE(output.writes()[0].unlocked);
  TEST_ASSERT_FALSE(output.unlocked());
  TEST_ASSERT_FALSE(lock.isUnlocked());
  TEST_ASSERT_FALSE(timer.armed());
}

void test_unlock_pulses_for_configured_time() {
  Harness door;
  door.lock.unlock();
  TEST_ASSERT_TRUE(door.output.unlocked());

  door.clock.advance(2999);
  TEST_ASSERT_TRUE(door.output.unlocked());
  door.clock.advance(1);
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_FALSE(door.lock.isUnlocked());

  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_TRUE(writes[0].unlocked);
  TEST_ASSERT_EQUAL_UINT32(10000, writes[0].atMs);
  TEST_ASSERT_FALSE(writes[1].unlocked);
  TEST_ASSERT_EQUAL_UINT32(13000, writes[1].atMs);

  // A configured unlock time is used as is.
  Harness shorter(2000);
  shorter.lock.unlock();
  shorter.clock.advance(1999);
  TEST_ASSERT_TRUE(shorter.output.unlocked());
  shorter.clock.advance(1);
  TEST_ASSERT_FALSE(shorter.output.unlocked());
  TEST_ASSERT_EQUAL_UINT32(12000, shorter.writesAfterBegin()[1].atMs);
}

void test_unlock_never_exceeds_maximum() {
  Harness door(10000);
  TEST_ASSERT_EQUAL_UINT32(LockController::kMaxUnlockMs, door.lock.unlockMs());

  door.lock.unlock();
  door.clock.advance(10000);

  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_FALSE(writes[1].unlocked);
  TEST_ASSERT_EQUAL_UINT32(10000 + 5000, writes[1].atMs);

  // The clamp starts just above the maximum.
  TEST_ASSERT_EQUAL_UINT32(3001, Harness(3001).lock.unlockMs());
  TEST_ASSERT_EQUAL_UINT32(4999, Harness(4999).lock.unlockMs());
  TEST_ASSERT_EQUAL_UINT32(5000, Harness(5000).lock.unlockMs());
  Harness justOver(5001);
  TEST_ASSERT_EQUAL_UINT32(5000, justOver.lock.unlockMs());
  justOver.lock.unlock();
  justOver.clock.advance(6000);
  TEST_ASSERT_EQUAL_UINT32(10000 + 5000, justOver.writesAfterBegin()[1].atMs);
}

void test_unlock_stays_locked_when_timer_fails() {
  Harness door;
  door.timer.failStart = true;
  door.poll(fakes::pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)}));

  // With no relock armed, the door never turns the output on.
  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_FALSE(door.lock.isUnlocked());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(1, events.size());
  TEST_ASSERT_EQUAL_STRING("rejected", events[0].result.c_str());
  TEST_ASSERT_EQUAL_STRING("unlock_failed", events[0].reason.c_str());
}

void test_second_unlock_does_not_extend() {
  Harness door;
  door.poll(fakes::pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)}));
  door.clock.advance(1000);
  door.poll(fakes::pollJson(21000, {fakes::commandJson("cmd-b", "unlock", 20500, 30500)}));
  door.clock.advance(5000);

  TEST_ASSERT_EQUAL_INT(1, door.timer.starts());
  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_EQUAL_UINT32(13000, writes[1].atMs);  // Still 3 s after the first unlock.

  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
  TEST_ASSERT_EQUAL_STRING("done", events[1].result.c_str());
}

void test_lock_during_unlock_relocks_immediately() {
  Harness door;
  door.poll(fakes::pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)}));
  door.clock.advance(1000);
  door.poll(fakes::pollJson(21000, {fakes::commandJson("cmd-b", "lock", 20500, 30500)}));

  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_FALSE(door.timer.armed());
  auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_EQUAL_UINT32(11000, writes[1].atMs);

  door.clock.advance(5000);
  TEST_ASSERT_EQUAL_UINT32(2, door.writesAfterBegin().size());

  // Lock on a locked door still reports done.
  door.poll(fakes::pollJson(26000, {fakes::commandJson("cmd-c", "lock", 25500, 35500)}));
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto events = door.flushEvents();
  TEST_ASSERT_EQUAL_UINT32(3, events.size());
  for (const auto& event : events) TEST_ASSERT_EQUAL_STRING("done", event.result.c_str());
}

void test_network_error_keeps_door_locked() {
  Harness door;
  const std::string unlock =
      fakes::pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)});

  // A failed request, a server error, and a timeout never unlock the door,
  // even when the body happens to hold an unlock command.
  door.poll(unlock, -1);
  door.poll(unlock, 503);
  door.poll(unlock, 0);
  TEST_ASSERT_TRUE(door.writesAfterBegin().empty());
  TEST_ASSERT_FALSE(door.output.unlocked());
  TEST_ASSERT_EQUAL_UINT32(0, door.outbox.size());

  // Failing event posts don't move the lock either.
  door.poll(fakes::pollJson(20000, {fakes::commandJson("cmd-b", "lock", 19000, 29000)}));
  fakes::FakeSender failing;
  failing.fallbackStatus = -1;
  for (int i = 0; i < 5; ++i) {
    door.outbox.service(failing, door.clock);
    door.clock.advance(8000);
  }
  TEST_ASSERT_FALSE(door.output.unlocked());
}

void test_relock_fires_while_request_blocks() {
  Harness door;
  door.poll(fakes::pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)}));
  TEST_ASSERT_TRUE(door.output.unlocked());

  // Posting the event blocks for 10 s against an unreachable host.
  fakes::BlockingSender blocking(door.clock, 10000);
  door.outbox.service(blocking, door.clock);

  TEST_ASSERT_EQUAL_UINT32(20000, door.clock.nowMs());
  TEST_ASSERT_FALSE(door.output.unlocked());
  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_FALSE(writes[1].unlocked);
  TEST_ASSERT_EQUAL_UINT32(13000, writes[1].atMs);  // On time, mid-request.
}
