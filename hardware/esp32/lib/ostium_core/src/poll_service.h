#pragma once

#include <cstdint>
#include <string>

#include "command_processor.h"
#include "interfaces.h"
#include "poll_parser.h"
#include "poll_scheduler.h"

namespace ostium {

// Fetches one GET /devices/{device_id}/commands response. Returns the HTTP
// status, or a negative value for a timeout or network error, and puts the
// response body in body.
class PollFetcher {
 public:
  virtual ~PollFetcher() = default;
  virtual int get(std::string& body) = 0;
};

// Runs one poll: fetches it, runs its commands, and tells the scheduler how it
// went. Records the request start before the network call, so R4 and R5 never
// measure from the response's arrival. Returns whether the poll succeeded.
inline bool runPoll(PollFetcher& fetcher, const Clock& clock, PollScheduler& scheduler,
                    CommandProcessor& processor) {
  const uint32_t startedAtMs = clock.nowMs();
  scheduler.started(startedAtMs);
  std::string body;
  const int status = fetcher.get(body);
  const PollResult result = parsePollResponse(status, body);
  processor.handlePoll(result, startedAtMs);
  if (result.ok) {
    scheduler.succeeded();
  } else {
    scheduler.failed(clock.nowMs());
  }
  return result.ok;
}

}  // namespace ostium
