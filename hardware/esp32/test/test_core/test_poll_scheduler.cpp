// Poll timing: start-to-start interval and backoff.

#include <unity.h>

#include "fakes.h"
#include "tests.h"

using ostium::PollScheduler;

void test_poll_interval_is_start_to_start() {
  PollScheduler scheduler;
  TEST_ASSERT_TRUE(scheduler.due(0));  // The first poll starts right away.

  scheduler.started(1000);
  TEST_ASSERT_FALSE(scheduler.due(1000));
  scheduler.succeeded();  // Finished at, say, 1,400 ms.
  TEST_ASSERT_FALSE(scheduler.due(2499));
  TEST_ASSERT_TRUE(scheduler.due(2500));

  // A poll that takes longer than the interval makes the next one due at once.
  scheduler.started(2500);
  scheduler.succeeded();  // Finished at 4,200 ms.
  TEST_ASSERT_TRUE(scheduler.due(4200));
}

void test_poll_backoff_is_2_4_8_seconds() {
  PollScheduler scheduler;
  scheduler.started(0);
  scheduler.failed(3000);  // A timeout.
  TEST_ASSERT_FALSE(scheduler.due(4999));
  TEST_ASSERT_TRUE(scheduler.due(5000));

  scheduler.started(5000);
  scheduler.failed(5100);
  TEST_ASSERT_FALSE(scheduler.due(9099));
  TEST_ASSERT_TRUE(scheduler.due(9100));

  scheduler.started(9100);
  scheduler.failed(9200);
  TEST_ASSERT_TRUE(scheduler.due(17200));
  TEST_ASSERT_FALSE(scheduler.due(17199));

  scheduler.started(17200);
  scheduler.failed(17300);  // Stays at 8 s.
  TEST_ASSERT_FALSE(scheduler.due(25299));
  TEST_ASSERT_TRUE(scheduler.due(25300));

  // A success returns to the normal interval.
  scheduler.started(25300);
  scheduler.succeeded();
  TEST_ASSERT_EQUAL_UINT32(0, scheduler.failures());
  TEST_ASSERT_FALSE(scheduler.due(26799));
  TEST_ASSERT_TRUE(scheduler.due(26800));
}

void test_poll_scheduler_survives_millis_wraparound() {
  PollScheduler scheduler;
  const uint32_t nearWrap = 0xFFFFFFFFu - 500;
  scheduler.started(nearWrap);
  scheduler.succeeded();
  TEST_ASSERT_FALSE(scheduler.due(nearWrap + 1499));
  TEST_ASSERT_TRUE(scheduler.due(nearWrap + 1500));  // Wrapped past zero.
}
