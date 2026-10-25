# Door unit firmware: Assignment 1 report

This is the firmware module's section of the Assignment 1 report. It covers the test output, the CI stages, the MVP subset, the design changes from project plan v2, the challenges, and each firmware team member's contribution. For the architecture, the UML diagrams, and the full design rationale, see [Door unit design](design.md). For the door's exact behavior, see the [lock command contract](lock-command-v0.md), whose rules R1 to R16 this report refers to by number.

## Summary

- **What the door unit does:** it polls the backend over HTTPS for Lock and Unlock commands, checks each command, drives the lock relay, relocks on a timer, and reports each result back to the backend.
- **Where the code is:** the core logic is in `hardware/esp32/lib/ostium_core/`, the board firmware is in `hardware/esp32/src/`, and the tests are in `hardware/esp32/test/test_core/`. Pull request #9, the lock command contract, is merged into `main`. Pull requests #11, #12, #14, and #15 merged into each other's branches instead, so this code, the CI workflow, and the firmware docs reach `main` with one more pull request, from the `scrum-58-firmware-fixes` branch.
- **Test status:** all 57 firmware unit tests pass on a laptop. CI ran the first 54, before the last 3 were added.
- **Board status:** the firmware compiles for the ESP32-S3 in CI. The bench test on the board is still to come (SCRUM-52).

## Test output

The firmware has 57 Unity tests. They run on a laptop or a CI runner with no board, because the core logic talks to the hardware only through small interfaces, and the tests use fakes for them. Every rule from R1 to R16 has at least one test.

To run them from the repository root:

```sh
pio test -d hardware/esp32/native-tests
```

The output ends with this summary:

```text
native         test_core  PASSED    00:00:01.551
================= 57 test cases: 57 succeeded in 00:00:01.551 =================
```

| Area | Tests | What they check |
|---|---|---|
| Lock control | 8 | The door is locked at startup, an unlock lasts the configured time and never more than 5 seconds, a second unlock doesn't extend the first, a Lock command ends an unlock right away, the relock fires while the main loop is blocked, and the door stays locked after a network error or a timer failure. |
| Command checks | 12 | Commands run in order. Expired commands, commands issued before the door started, commands for another door, and malformed or unknown commands are rejected, and the first failed check is the one reported. A response that isn't valid JSON, including one with a raw control character in a string, runs nothing. |
| Duplicates | 7 | A command that's delivered twice runs once, a repeated unlock doesn't restart the timer, and each command ID is kept until the backend's clock passes its expiry. |
| Poll parsing and timestamps | 5 | A bad poll response runs nothing, a poll is capped at 32 commands, and timestamps parse and format as UTC. |
| Events | 9 | Each result produces one event with a stable ID and the exact body the contract specifies. A failed post doesn't block lock control, and retries back off at 2, 4, and 8 seconds, even across a `millis()` wraparound. |
| Poll schedule | 4 | Polls start every 1.5 seconds, measured start to start, even across a `millis()` wraparound, and back off at 2, 4, and 8 seconds after a failure. |
| Response bodies | 3 | A response body that stops arriving ends after 3 seconds with no byte, or 6 seconds in all, so a stalled server can't block the door's loop. Bodies end at their `Content-Length` or last chunk, nothing past them is read, and bodies over 8 KiB or with bad chunked encoding are rejected. |
| Backend replay | 9 | Responses recorded from the real FastAPI door routes run through the same core code that the board runs. The tests check that the backend accepts the firmware's exact event bodies, that a rejected token runs nothing, and that expired commands stop being served. |

Each test builds the core with fakes, drives it, and checks what comes out, such as the writes to the lock output or the event bodies. For example, this test from `hardware/esp32/test/test_core/test_lock_controller.cpp` checks R10: an unlock configured for 10 seconds still relocks after 5 seconds. The test harness starts the fake clock at 10,000 ms, so the relock is expected at 10,000 + 5,000 ms.

```cpp
void test_unlock_never_exceeds_maximum() {
  Harness door(10000);
  TEST_ASSERT_EQUAL_UINT32(LockController::kMaxUnlockMs, door.lock.unlockMs());

  door.lock.unlock();
  door.clock.advance(10000);

  const auto writes = door.writesAfterBegin();
  TEST_ASSERT_EQUAL_UINT32(2, writes.size());
  TEST_ASSERT_FALSE(writes[1].unlocked);
  TEST_ASSERT_EQUAL_UINT32(10000 + 5000, writes[1].atMs);
  // The full test also checks the clamp at 3,001, 4,999, 5,000, and 5,001 ms.
}
```

The replay tests use responses that `hardware/esp32/tools/capture_backend_responses.py` recorded from the backend on `main`. If the backend's door routes change, you record the responses again and the tests show what broke.

During review, bugs were planted in a copy of the core one at a time, and each one made at least one test fail.

A code review of the firmware pull requests with Codex found two problems that the team fixed: the parser accepted a raw control character inside a JSON string, and the board's HTTP client could wait forever for a stalled response body. The fixes added the control-character cases and the 3 response-body tests, and each new test fails when its fix is removed. A second Codex review, of the fixes themselves, found five more gaps, such as the time limit not applying while bytes kept arriving, and the team fixed those too. The first review's other findings are recorded in [Known limits](design.md#known-limits).

The CI workflow also runs the other modules' unit tests: 24 for the backend and 141 for the frontend.

### Bench results

The bench test runs on the ESP32-S3 board (SCRUM-52). Its results go here when it's done.

| Test | Runs | Result |
|---|---|---|
| The relay stays off through power-on | 5 | To be measured |
| The relay stays off through a reset-button press | 5 | To be measured |
| The relay stays off through a forced crash | 5 | To be measured |
| Unlock duration, median and maximum, against the 3,000 ms setting | 20 | To be measured |
| Time for a Lock command to end an unlock | 5 | To be measured |
| Time from command to relay, end to end | 5 | To be measured |

## CI stages

The team's CI is GitHub Actions, in `.github/workflows/ci.yml`. The instructor confirmed that it can replace GitLab CI. The workflow passed on pull requests #15 and #14, and it reaches `main` with the firmware pull request described in [Summary](#summary). `auto-merge` hasn't merged a pull request yet, because neither of those targeted `main`. Once the workflow is on `main`, a push to a pull request starts the build jobs. If every build passes, the test jobs run. If every test passes in a pull request into `main`, the `auto-merge` job merges the pull request. For the setup steps, see [Continuous integration](../ci.md).

| Stage | Job | What it runs | Time in the first run |
|---|---|---|---|
| Build | `firmware-build` | Both ESP32-S3 board builds (`pio run`) | 1 min 32 s |
| Build | `backend-build` | Installs the backend, compiles it, and imports the FastAPI app | 14 s |
| Build | `frontend-build` | Installs the Expo app's packages and type-checks it | 18 s |
| Test | `firmware-test` | The firmware unit tests (54 at the time) | 39 s |
| Test | `backend-test` | The backend's unit tests | 18 s |
| Test | `frontend-test` | The frontend's type check and unit tests | 13 s |
| Merge | `auto-merge` | `gh pr merge --merge --auto` for a pull request into `main` | Skipped, because the base wasn't `main` |

The first run was [run 40138315867](https://github.com/ostium-org/ostium/actions/runs/40138315867), on [pull request #15](https://github.com/ostium-org/ostium/pull/15), and it took 2 minutes 16 seconds. Every build and test job passed.

Screenshots to add before you submit:

- The run's graph, with the three stages.
- The `auto-merge` job's log from a pull request into `main`.
- The pull request that `auto-merge` merged.

## The MVP subset and why

**The MVP is remote Lock and Unlock.** A manager sends a command, the backend stores it, the door polls for it and drives the relay, and the door reports the result. The MVP spans all three modules: the app, the backend, and the door. The app's Lock and Unlock buttons aren't built yet, so until they are, a manager sends commands through the backend API, as in the [API walkthrough](../api.md#thursday-walkthrough). The end-to-end run on the board waits for the bench test (SCRUM-52).

The team chose this subset on October 19, 2026, for these reasons:

- **It could be built and tested end to end by October 25.** The backend's door routes were merged on October 21 (pull request #10), and the firmware's replay tests check against them.
- **The QR path depended on work that wasn't ready.** That work was the camera decode bench test, which is plan v2's decision gate for the credential format (section 5.7), a scheme for distributing keys, and backend credential routes.
- **It exercises the safety rules that matter most.** The door is locked at boot, unlocks for a set time, relocks on its own, rejects stale and repeated commands, and stays locked on any error.

**Deferred to the final product:** QR codes and camera scanning, credential checks on the door (SCRUM-38), offline unlocking and the offline event log, configuration and revocation sync, the doorbell, and the RTC module. For details, see [The MVP subset and what's deferred](design.md#the-mvp-subset-and-whats-deferred).

## Design changes from plan v2

The full table, with the plan sections, is in [Changes from project plan v2](design.md#changes-from-project-plan-v2). The main changes are these, plus the CI change, which that table doesn't cover:

| Area | Plan v2 | Assignment 1 MVP | Why |
|---|---|---|---|
| What opens the door | A tenant's signed QR code, checked offline by the door | A manager's Lock or Unlock command, through the backend | The scope decision of October 19 |
| Door to backend | MQTT with TLS | HTTPS polling of the REST door routes every 1.5 seconds | One protocol and server, outbound-only requests, and no broker to host |
| Remote unlock security | Commands signed with Ed25519, checked against an RTC | A per-device token over verified HTTPS, backend role checks, a 10-second expiry, duplicate checks, and a check against commands issued before the door started | No RTC or key distribution yet, and the MVP is online-only |
| Board | AI-Thinker ESP32-CAM | ESP32-S3 with an onboard camera | The team didn't have an ESP32-CAM. The S3 also has more GPIO. |
| Timekeeping | DS3231 RTC | The backend's `server_time` (R4, R5) | The MVP is online-only |
| Offline behavior | 72 hours of QR unlocks and 1,000 buffered events | Online-only. The door stays locked while offline, and up to 8 events wait in RAM. | Deferred with the QR path |
| Door secrets | Flash encryption and secure boot | A device token in plain flash | The MVP runs on a bench. Flash encryption and secure boot are deferred. |
| CI | Every pull request builds the firmware and runs all unit tests, and a merge into `main` needs a green build and one review (section 8.1) | GitHub Actions builds and tests every pull request, and merges a pull request that targets `main` when every test passes | The Assignment 1 brief asks for an automatic merge in GitLab CI. The code is on GitHub, and the instructor accepted GitHub Actions. |

## Challenges

- **QR codes moved out of the MVP.** On October 19, the team moved QR codes and camera scanning to the final product to meet the deadline. The firmware tickets (SCRUM-48, 51, 52, and 54 to 57) were rewritten for relay commands, and SCRUM-58 was added for polling the backend.
- **The hardware changed, and the board isn't confirmed yet.** Plan v2's ESP32-CAM wasn't available, and a standard ESP32 can't use a USB webcam, so Cameron ordered an ESP32-S3 board with an onboard camera. The exact model isn't recorded yet, so the build uses a generic ESP32-S3 board ID, and the lock pin and relay polarity are placeholders. On October 24, the other board on hand turned out to be an ESP32-C6. That chip needs a newer Arduino core than the firmware uses, and it has no camera interface, so the bench test waits for the S3.
- **The backend API changed during the sprint.** At first the door API existed only in a Jira ticket, and the repository's API docs still described the older QR-era device API. The firmware team wrote the [lock command contract](lock-command-v0.md) first, then aligned it with the FastAPI door routes when they merged on October 21. If the door routes change later, recording their responses again and rerunning the replay tests shows what broke. A planned move from FastAPI to Supabase Edge Functions (SCRUM-62) would have to keep the same paths, bodies, and status codes.
- **Testing without the board.** The firmware was written without the board on hand, and none of it has run on a board yet. Keeping the logic in a core library with interfaces let all 57 tests run on a laptop, and the replay tests checked the core against the real backend.
- **Clock handling without an RTC.** The door has no real-time clock, so it estimates time from the backend's `server_time`. Review found that measuring from the end of the request, instead of its start, could let a slow response re-run a command from before a reset. The core now measures from the request start, and two tests cover it.
- **Moving CI from GitLab to GitHub.** The brief asked for GitLab CI, but the team's code is on GitHub. With the instructor's approval, the team used GitHub Actions. A clean checkout also exposed a gap in the frontend: its type check needs `expo-env.d.ts`, which Expo generates and Git ignores, so the CI job writes it first.
- **Working remotely.** One firmware team member was travelling, so firmware work was split by location: laptop-only work, such as the core, tests, CI, and docs, on one side, and hardware and bench work on the other.

## Contributions

| Member | Work |
|---|---|
| Abdulwaheed Abdulmujeeb | The lock command contract (SCRUM-48, [pull request #9](https://github.com/ostium-org/ostium/pull/9)). The core library and its 57 unit tests, including the backend replay tests (SCRUM-51, [pull request #11](https://github.com/ostium-org/ostium/pull/11)). The ESP32-S3 board firmware that polls for commands (SCRUM-58, [pull request #12](https://github.com/ostium-org/ostium/pull/12)). The UML class and use case diagrams and the design rationale (SCRUM-54, 55, and 56, [pull request #14](https://github.com/ostium-org/ostium/pull/14)). The GitHub Actions CI for the whole repository (SCRUM-46 and 53, [pull request #15](https://github.com/ostium-org/ostium/pull/15)). This report, which covers the test output and write-up parts of SCRUM-57. |
| Cameron Shea | Sourcing the ESP32-S3 camera board. Jira shows these of his tickets as done: the initial ESP32 setup (SCRUM-15), relay unlock and automatic relock (SCRUM-39), the hardware inventory (SCRUM-49), and the single-QR proof of concept on the ESP32-S3 camera (SCRUM-59). Their code and records aren't in the repository yet. Still to do: the relay bench test (SCRUM-52), and the bench photos, video, and serial logs for SCRUM-57. |

The firmware also depends on the backend team's door routes: Jaiveer Matta's pull request #10 (SCRUM-60), built on Jack Ryan's FastAPI backend (pull request #7).
