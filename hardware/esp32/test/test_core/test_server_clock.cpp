// Server time bounds for QR access: Q10, the boot watermark (Q14), and runPoll
// feeding them (Q11).

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "fakes.h"
#include "tests.h"

using fakes::FakeLog;
using fakes::Harness;
using fakes::pollJson;
using fakes::SlowFetcher;
using ostium::ServerClock;
using ostium::ServerTimeBounds;

namespace {

// A server_time as the door parses it: 2026-10-22T12:00:01.123Z.
constexpr int64_t kS = 1792670401123LL;
// Another server time, K, for the multi-poll tests.
constexpr int64_t kK = 1792670500000LL;

bool hasBounds(const ServerClock& clock, uint32_t nowMs) {
  ServerTimeBounds bounds;
  return clock.bounds(nowMs, &bounds);
}

ServerTimeBounds boundsAt(const ServerClock& clock, uint32_t nowMs) {
  ServerTimeBounds bounds;
  const std::string label = "no bounds at " + std::to_string(nowMs);
  TEST_ASSERT_TRUE_MESSAGE(clock.bounds(nowMs, &bounds), label.c_str());
  return bounds;
}

void expectBounds(const ServerClock& clock, uint32_t nowMs, int64_t earliestMs, int64_t latestMs) {
  const ServerTimeBounds bounds = boundsAt(clock, nowMs);
  TEST_ASSERT_EQUAL_INT64(earliestMs, bounds.earliestMs);
  TEST_ASSERT_EQUAL_INT64(latestMs, bounds.latestMs);
}

size_t countLines(const FakeLog& log, const std::string& text) {
  return static_cast<size_t>(
      std::count_if(log.messages.begin(), log.messages.end(), [&](const std::string& message) {
        return message.find(text) != std::string::npos;
      }));
}

// A server whose clock runs ratePpm faster than millis(), or slower when
// negative. Times are whole ns, so the checks are exact: at local time
// localMs, the server's true time is startNs + localMs x (1,000,000 + ratePpm).
struct SimulatedServer {
  int64_t startNs;  // The server's true time at millis() == 0.
  int64_t ratePpm;

  int64_t trueNsAt(int64_t localMs) const { return startNs + localMs * (1000000 + ratePpm); }
  // server_time for a stamp at localMs, truncated to whole ms like parseTimestamp.
  int64_t serverTimeMsAt(int64_t localMs) const { return trueNsAt(localMs) / 1000000; }
};

// The fixture's iat, 2026-10-22T12:00:00.123456Z, in ns.
constexpr int64_t kStartNs = 1792670400123456000LL;
// The fastest, an exact, and the slowest clock that the 50 ppm allowance covers.
constexpr int64_t kRatesPpm[] = {-50, 0, 50};

// Runs 200 polls 1.5 s apart, start to start, or back to back when a response
// takes longer. The round trips cycle through 0, 50, 300, and 3,000 ms, and
// the backend stamps server_time at the request start, the midpoint, or the
// arrival, so every round trip meets every stamp position. Calls
// check(arrivedMs) after each poll, and returns the last arrival.
template <typename Check>
int64_t runPolls(ServerClock& clock, const SimulatedServer& server, Check check) {
  const int64_t kRoundTripsMs[] = {0, 50, 300, 3000};
  int64_t startedMs = 5000;
  int64_t arrivedMs = startedMs;
  for (int i = 0; i < 200; ++i) {
    const int64_t roundTripMs = kRoundTripsMs[i % 4];
    arrivedMs = startedMs + roundTripMs;
    const int64_t stampedMs = startedMs + roundTripMs * (i % 3) / 2;
    clock.onPollSucceeded(server.serverTimeMsAt(stampedMs), static_cast<uint32_t>(startedMs),
                          static_cast<uint32_t>(arrivedMs));
    check(arrivedMs);
    startedMs = std::max(startedMs + 1500, arrivedMs);
  }
  return arrivedMs;
}

// Checks earliest <= the server's true time <= latest at localMs.
void expectBracketed(const ServerClock& clock, const SimulatedServer& server, int64_t localMs) {
  const std::string label =
      std::to_string(server.ratePpm) + " ppm at local " + std::to_string(localMs) + " ms";
  ServerTimeBounds bounds;
  TEST_ASSERT_TRUE_MESSAGE(clock.bounds(static_cast<uint32_t>(localMs), &bounds), label.c_str());
  const int64_t trueNs = server.trueNsAt(localMs);
  TEST_ASSERT_TRUE_MESSAGE(bounds.earliestMs * 1000000 <= trueNs, label.c_str());
  TEST_ASSERT_TRUE_MESSAGE(trueNs <= bounds.latestMs * 1000000, label.c_str());
}

}  // namespace

void test_server_clock_bounds_use_request_and_response_times() {
  FakeLog log;
  ServerClock clock(log);
  TEST_ASSERT_FALSE(hasBounds(clock, 1000));  // No good poll yet.

  // The request left at 1,000 ms and the response arrived at 1,300 ms. The
  // stamp is somewhere in between, and truncated, so the server's clock at
  // the arrival is S to S + 1 + 300 + 1 ms of drift.
  clock.onPollSucceeded(kS, 1000, 1300);
  expectBounds(clock, 1300, kS, kS + 302);
  TEST_ASSERT_TRUE(log.contains("server clock set (window 302 ms)"));
  // 1,000 ms later, each side moves by 1,000 ms and widens by 1 ms of drift.
  expectBounds(clock, 2300, kS + 999, kS + 1303);
  // A time before the arrival has no bounds.
  TEST_ASSERT_FALSE(hasBounds(clock, 1299));
  TEST_ASSERT_FALSE(clock.deviceRefused());

  TEST_ASSERT_EQUAL_INT64(0, ServerClock::driftMs(0));
  TEST_ASSERT_EQUAL_INT64(1, ServerClock::driftMs(1));
  TEST_ASSERT_EQUAL_INT64(1, ServerClock::driftMs(20000));
  TEST_ASSERT_EQUAL_INT64(2, ServerClock::driftMs(20001));
  TEST_ASSERT_EQUAL_INT64(12960, ServerClock::driftMs(ServerClock::kMaxOfflineMs));
  TEST_ASSERT_EQUAL_INT64(214749, ServerClock::driftMs(0xFFFFFFFFu));
}

void test_server_clock_bounds_bracket_true_time() {
  for (const int64_t ratePpm : kRatesPpm) {
    const SimulatedServer server{kStartNs, ratePpm};
    FakeLog log;
    ServerClock clock(log);
    const int64_t lastMs = runPolls(clock, server, [&](int64_t arrivedMs) {
      expectBracketed(clock, server, arrivedMs);
    });

    // A steady server never contradicts the bounds, and intersecting the polls
    // keeps the window a few ms wide, far below the last poll's 3,000 ms.
    TEST_ASSERT_FALSE(log.contains("moved"));
    const ServerTimeBounds last = boundsAt(clock, static_cast<uint32_t>(lastMs));
    TEST_ASSERT_TRUE(last.latestMs - last.earliestMs <= 10);

    // Offline, the bounds still hold the true time, up to the 72 h limit.
    const int64_t kOfflineMs[] = {0, 1000, 3600000, ServerClock::kMaxOfflineMs};
    for (const int64_t offlineMs : kOfflineMs) {
      expectBracketed(clock, server, lastMs + offlineMs);
    }
  }
}

void test_server_clock_combines_polls() {
  FakeLog log;
  ServerClock clock(log);
  clock.onPollSucceeded(kK + 1000, 1000, 1400);
  expectBounds(clock, 1400, kK + 1000, kK + 1402);
  // At 3,400 ms, the first poll gives [K + 2999, K + 3403], and the second
  // gives [K + 3400, K + 3802]. The server's time is in both, so it's in
  // their intersection, which is narrower than either round trip.
  clock.onPollSucceeded(kK + 3400, 3000, 3400);
  expectBounds(clock, 3400, kK + 3400, kK + 3403);
  TEST_ASSERT_FALSE(log.contains("moved"));
  TEST_ASSERT_EQUAL_UINT32(1, countLines(log, "server clock set"));

  // A later poll with a wide window can't widen the bounds again.
  clock.onPollSucceeded(kK + 5000, 4000, 7000);
  expectBounds(clock, 7000, kK + 6999, kK + 7004);
}

void test_server_clock_restarts_when_server_clock_moves() {
  // The server's clock is set back 5 s between two polls.
  FakeLog log;
  ServerClock clock(log);
  clock.onPollSucceeded(kK + 1000, 1000, 1400);
  const int64_t bootTimeMs = clock.bootTimeLatestMs();
  clock.onPollSucceeded(kK + 3400 - 5000, 3000, 3400);
  // The windows don't overlap, so the door trusts only the new poll.
  expectBounds(clock, 3400, kK - 1600, kK - 1198);
  TEST_ASSERT_TRUE(log.contains("server clock moved by about -4599 ms"));
  TEST_ASSERT_TRUE(log.contains("QR time bounds restarted from this poll"));
  // The boot watermark never moves earlier.
  TEST_ASSERT_EQUAL_INT64(bootTimeMs, clock.bootTimeLatestMs());

  // The server's clock is set forward 5 s instead. The boot watermark rises
  // to this poll's, so a token issued before the boot on the new clock fails.
  FakeLog forwardLog;
  ServerClock forward(forwardLog);
  forward.onPollSucceeded(kK + 1000, 1000, 1400);
  const int64_t forwardBootTimeMs = forward.bootTimeLatestMs();
  forward.onPollSucceeded(kK + 3400 + 5000, 3000, 3400);
  expectBounds(forward, 3400, kK + 8400, kK + 8802);
  TEST_ASSERT_TRUE(forwardLog.contains("server clock moved by about 5401 ms"));
  TEST_ASSERT_EQUAL_INT64(kK + 8400 + 1 - 3000 + 1 + 1000, forward.bootTimeLatestMs());
  TEST_ASSERT_TRUE(forward.bootTimeLatestMs() > forwardBootTimeMs);
}

void test_server_clock_stops_after_offline_limit() {
  // A 100 ms round trip gives a 102 ms window. 72 h later, each side has
  // widened by 12,960 ms of drift, so the width is 26,022 ms.
  FakeLog log;
  ServerClock clock(log);
  clock.onPollSucceeded(kS, 1000, 1100);
  const uint32_t limitMs = 1100 + ServerClock::kMaxOfflineMs;
  expectBounds(clock, limitMs, kS + ServerClock::kMaxOfflineMs - 12960,
               kS + 102 + ServerClock::kMaxOfflineMs + 12960);
  const ServerTimeBounds atLimit = boundsAt(clock, limitMs);
  TEST_ASSERT_EQUAL_INT64(26022, atLimit.latestMs - atLimit.earliestMs);
  TEST_ASSERT_FALSE(hasBounds(clock, limitMs + 1));

  // A 5,000 ms round trip gives a 5,002 ms window, so the 30,000 ms width cap
  // ends the bounds first: 12,499 ms of drift on each side is the most it allows.
  FakeLog slowLog;
  ServerClock slow(slowLog);
  slow.onPollSucceeded(kS, 1000, 6000);
  expectBounds(slow, 6000, kS, kS + 5002);
  const ServerTimeBounds atCap = boundsAt(slow, 6000 + 249980000);
  TEST_ASSERT_EQUAL_INT64(30000, atCap.latestMs - atCap.earliestMs);
  TEST_ASSERT_FALSE(hasBounds(slow, 6000 + 249980001));
  TEST_ASSERT_FALSE(hasBounds(slow, 6000 + ServerClock::kMaxOfflineMs));
}

void test_server_clock_forgets_bounds_before_millis_wraps() {
  FakeLog log;
  ServerClock clock(log);
  clock.onPollSucceeded(kS, 1000, 1000);
  const uint32_t limitMs = 1000 + ServerClock::kMaxOfflineMs;
  clock.tick(limitMs);  // Exactly 72 h keeps the bounds.
  TEST_ASSERT_TRUE(hasBounds(clock, limitMs));
  TEST_ASSERT_FALSE(log.contains("no successful poll"));

  clock.tick(limitMs + 1);
  TEST_ASSERT_FALSE(hasBounds(clock, limitMs + 1));
  TEST_ASSERT_TRUE(
      log.contains("no successful poll for 72 hours; QR access waits for the next poll"));
  // 1,050 ms is also the time just after millis() next wraps, 50 ms after the
  // anchor as 32-bit arithmetic sees it. The forgotten bounds stay forgotten.
  TEST_ASSERT_FALSE(hasBounds(clock, 1050));
  clock.tick(1050);
  TEST_ASSERT_EQUAL_UINT32(1, countLines(log, "no successful poll"));

  // Without tick(), the same clock would trust the old poll again.
  FakeLog untickedLog;
  ServerClock unticked(untickedLog);
  unticked.onPollSucceeded(kS, 1000, 1000);
  TEST_ASSERT_TRUE(hasBounds(unticked, 1050));

  // A new poll sets the bounds again. The first poll's boot watermark was
  // S + 1 - 1,000 + 1 ms of drift + 1,000. This poll's own estimate is later,
  // so the watermark rises to it, with 1 ms of drift on 2,000 ms.
  TEST_ASSERT_EQUAL_INT64(kS + 1 - 1000 + 1 + 1000, clock.bootTimeLatestMs());
  const int64_t wrapMs = 4294967296LL;  // 2^32 ms of local time.
  const int64_t restartMs = kS - 1000 + wrapMs + 2000;
  clock.onPollSucceeded(restartMs, 2000, 2100);
  expectBounds(clock, 2100, restartMs, restartMs + 102);
  TEST_ASSERT_EQUAL_UINT32(2, countLines(log, "server clock set"));
  const int64_t bootTimeMs = restartMs + 1 - 2000 + 1 + 1000;
  TEST_ASSERT_EQUAL_INT64(bootTimeMs, clock.bootTimeLatestMs());

  // The bounds lapse again, and the next poll comes from a server whose clock
  // was set back about 10 minutes, so its own boot estimate is about 589 s
  // earlier. The boot watermark never moves earlier, even with no bounds to
  // compare the poll against.
  const uint32_t lapsedMs = 2100 + ServerClock::kMaxOfflineMs + 1;
  clock.tick(lapsedMs);
  TEST_ASSERT_FALSE(hasBounds(clock, lapsedMs));
  const int64_t steppedBackMs = restartMs + ServerClock::kMaxOfflineMs - 600000;
  clock.onPollSucceeded(steppedBackMs, lapsedMs + 1000, lapsedMs + 1100);
  expectBounds(clock, lapsedMs + 1100, steppedBackMs, steppedBackMs + 102);
  TEST_ASSERT_EQUAL_UINT32(3, countLines(log, "server clock set"));
  TEST_ASSERT_FALSE(log.contains("moved"));
  TEST_ASSERT_EQUAL_INT64(bootTimeMs, clock.bootTimeLatestMs());
}

void test_server_clock_survives_millis_wraparound() {
  // The request leaves 4,352 ms before millis() wraps, and the response
  // arrives 256 ms later. The next scan is 7,936 ms after the arrival.
  FakeLog log;
  ServerClock clock(log);
  clock.onPollSucceeded(kS, 0xFFFFEF00u, 0xFFFFF000u);
  expectBounds(clock, 0xFFFFF000u, kS, kS + 258);
  clock.tick(0x00000F00u);
  expectBounds(clock, 0x00000F00u, kS + 7935, kS + 8195);
  TEST_ASSERT_FALSE(log.contains("no successful poll"));

  // A poll after the wrap combines with the one before it.
  clock.onPollSucceeded(kS + 8000, 0x00000E00u, 0x00000F00u);
  expectBounds(clock, 0x00000F00u, kS + 8000, kS + 8195);
}

void test_server_clock_boot_time_is_latest_possible() {
  // The request left at uptime 10,000 ms, and the stamp came after that. At
  // least 10,000 ms less 1 ms of drift passed on the server's clock between
  // millis() == 0 and a stamp below S + 1, so the server's time at boot was
  // before S + 1 - 10,000 + 1. The margin adds 1,000 ms.
  FakeLog log;
  ServerClock clock(log);
  TEST_ASSERT_FALSE(clock.hasBootTime());
  clock.onPollSucceeded(kS, 10000, 10000);
  TEST_ASSERT_TRUE(clock.hasBootTime());
  TEST_ASSERT_EQUAL_INT64(kS - 8998, clock.bootTimeLatestMs());

  // An overlapping poll leaves it, even one whose own estimate is later.
  clock.onPollSucceeded(kS + 950, 10900, 11000);
  expectBounds(clock, 11000, kS + 999, kS + 1002);
  TEST_ASSERT_EQUAL_INT64(kS - 8998, clock.bootTimeLatestMs());

  // A poll that moves the server's clock 5 s forward raises it, and one that
  // moves it back never lowers it.
  clock.onPollSucceeded(kS + 6000, 12000, 12000);
  TEST_ASSERT_TRUE(log.contains("moved"));
  TEST_ASSERT_EQUAL_INT64(kS + 6000 + 1 - 12000 + 1 + 1000, clock.bootTimeLatestMs());
  clock.onPollSucceeded(kS + 2000, 13000, 13000);
  TEST_ASSERT_EQUAL_UINT32(2, countLines(log, "moved"));
  TEST_ASSERT_EQUAL_INT64(kS - 4998, clock.bootTimeLatestMs());

  // Against the simulated servers, the watermark is always more than the
  // margin after the server's true time at boot.
  for (const int64_t ratePpm : kRatesPpm) {
    const SimulatedServer server{kStartNs, ratePpm};
    FakeLog simulatedLog;
    ServerClock simulated(simulatedLog);
    runPolls(simulated, server, [&](int64_t) {
      TEST_ASSERT_TRUE(simulated.hasBootTime());
      TEST_ASSERT_TRUE(simulated.bootTimeLatestMs() * 1000000 > server.startNs + 1000000000);
    });
  }
}

void test_run_poll_feeds_server_clock() {
  const int64_t base = fakes::kBaseEpochMs;
  Harness door;
  ServerClock serverClock(door.log);

  // The request starts at uptime 10,000 ms, and the response arrives 300 ms
  // later. runPoll passes both times: the window is the round trip plus 2 ms,
  // and the boot watermark counts from the request start.
  SlowFetcher first(door.clock, 300, 200, pollJson(20000, {}));
  TEST_ASSERT_TRUE(
      ostium::runPoll(first, door.clock, door.scheduler, door.processor, &serverClock));
  expectBounds(serverClock, 10300, base + 20000, base + 20302);
  TEST_ASSERT_EQUAL_INT64(base + 20000 + 1 - 10000 + 1 + 1000, serverClock.bootTimeLatestMs());

  // A failed poll that isn't a 401 changes nothing: the bounds still come
  // from the first poll.
  door.clock.advance(1500);
  SlowFetcher serverError(door.clock, 100, 500, "");
  TEST_ASSERT_FALSE(
      ostium::runPoll(serverError, door.clock, door.scheduler, door.processor, &serverClock));
  door.clock.advance(1500);
  SlowFetcher badBody(door.clock, 100, 200, "{\"server_time\":\"not a time\",\"commands\":[]}");
  TEST_ASSERT_FALSE(
      ostium::runPoll(badBody, door.clock, door.scheduler, door.processor, &serverClock));
  TEST_ASSERT_FALSE(serverClock.deviceRefused());
  expectBounds(serverClock, 10300, base + 20000, base + 20302);

  // A 401 means the backend refuses this device. The door logs it once.
  SlowFetcher refused(door.clock, 100, 401, "{\"detail\":\"invalid device token\"}");
  for (int i = 0; i < 2; ++i) {
    door.clock.advance(1500);
    TEST_ASSERT_FALSE(
        ostium::runPoll(refused, door.clock, door.scheduler, door.processor, &serverClock));
    TEST_ASSERT_TRUE(serverClock.deviceRefused());
  }
  TEST_ASSERT_EQUAL_UINT32(1, countLines(door.log, "backend refused this device (401)"));

  // The next good poll clears it, and narrows the bounds with the first poll's.
  door.clock.advance(1500);
  SlowFetcher second(door.clock, 200, 200, pollJson(28250, {}));
  TEST_ASSERT_TRUE(
      ostium::runPoll(second, door.clock, door.scheduler, door.processor, &serverClock));
  TEST_ASSERT_FALSE(serverClock.deviceRefused());
  TEST_ASSERT_TRUE(door.log.contains("backend accepts this device again; QR access resumed"));
  TEST_ASSERT_EQUAL_UINT32(18400, door.clock.nowMs());
  expectBounds(serverClock, 18400, base + 28250, base + 28403);

  // Without the pointer, runPoll runs the commands and the scheduler exactly
  // as it does with one.
  const auto pollAll = [](Harness& h, ServerClock* clock) {
    const std::pair<int, std::string> responses[] = {
        {200, pollJson(20000, {fakes::commandJson("cmd-a", "unlock", 19000, 29000)})},
        {500, ""},
        {401, ""},
        {200, pollJson(24800, {fakes::commandJson("cmd-b", "lock", 24000, 34000)})},
    };
    std::vector<bool> results;
    for (const auto& response : responses) {
      SlowFetcher fetcher(h.clock, 100, response.first, response.second);
      results.push_back(
          clock == nullptr
              ? ostium::runPoll(fetcher, h.clock, h.scheduler, h.processor)
              : ostium::runPoll(fetcher, h.clock, h.scheduler, h.processor, clock));
      h.clock.advance(1500);
    }
    return results;
  };
  Harness withClock;
  ServerClock withClockServer(withClock.log);
  Harness plain;
  TEST_ASSERT_TRUE(pollAll(withClock, &withClockServer) == pollAll(plain, nullptr));
  TEST_ASSERT_TRUE(plain.processor.hasStartTime());
  TEST_ASSERT_EQUAL_INT64(withClock.processor.startTimeMs(), plain.processor.startTimeMs());
  TEST_ASSERT_EQUAL_UINT32(withClock.scheduler.failures(), plain.scheduler.failures());
  TEST_ASSERT_EQUAL_UINT32(withClock.output.writes().size(), plain.output.writes().size());
  TEST_ASSERT_EQUAL_UINT32(withClock.outbox.size(), plain.outbox.size());
  TEST_ASSERT_EQUAL_UINT32(2, plain.outbox.size());
  TEST_ASSERT_FALSE(plain.output.unlocked());
}
