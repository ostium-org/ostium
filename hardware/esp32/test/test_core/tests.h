#pragma once

// Each test name in a rule comment matches docs/firmware/lock-command-v0.md,
// Rules the door follows. The other tests cover the same rules more closely.

// test_lock_controller.cpp
void test_lock_is_locked_after_startup();            // R1
void test_unlock_pulses_for_configured_time();       // R9
void test_unlock_never_exceeds_maximum();            // R10
void test_second_unlock_does_not_extend();           // R10
void test_lock_during_unlock_relocks_immediately();  // R11
void test_network_error_keeps_door_locked();         // R12
void test_relock_fires_while_request_blocks();       // R12
void test_unlock_stays_locked_when_timer_fails();

// test_command_processor.cpp
void test_commands_run_in_response_order();                 // R2
void test_unlock_then_lock_in_one_poll_ends_locked();       // R2
void test_duplicate_command_is_not_run_again();             // R3
void test_redelivered_unlock_does_not_restart_timer();      // R3
void test_duplicate_list_never_drops_unexpired_id();        // R3
void test_expired_command_is_rejected();                    // R4
void test_command_issued_before_boot_is_rejected();         // R5
void test_boot_time_is_set_once_from_first_poll();          // R5
void test_command_for_other_door_is_rejected();             // R6
void test_malformed_command_is_rejected();                  // R7
void test_unknown_action_is_rejected();                     // R8
void test_event_id_derives_from_command_id();               // R13
void test_failed_event_post_does_not_block_lock_control();  // R14
void test_bad_poll_response_runs_nothing();                 // R16
void test_checks_report_first_failure_in_contract_order();
void test_cache_full_rejection_is_final();
void test_cache_full_rejection_is_queued_once();
void test_duplicate_id_kept_until_its_own_expiry();
void test_boot_time_uses_response_arrival_time();
void test_long_action_is_malformed_and_event_stays_small();
void test_poll_command_count_is_capped();

// test_timestamp.cpp
void test_timestamp_parser_accepts_utc_formats();  // R15
void test_timestamp_formats_as_utc_with_milliseconds();

// test_event_outbox.cpp
void test_event_body_matches_contract_examples();
void test_example_poll_response_parses();
void test_event_responses_follow_contract_table();
void test_event_retry_backoff_is_2_4_8_seconds();
void test_event_queue_drops_oldest_when_full();
void test_event_service_posts_at_most_one_event();
void test_event_retry_backoff_starts_after_slow_post();
void test_event_retry_survives_millis_wraparound();

// test_poll_scheduler.cpp
void test_poll_interval_is_start_to_start();
void test_poll_backoff_is_2_4_8_seconds();
void test_poll_scheduler_survives_millis_wraparound();
