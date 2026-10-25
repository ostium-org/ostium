#include <unity.h>

#include "tests.h"

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_lock_is_locked_after_startup);
  RUN_TEST(test_commands_run_in_response_order);
  RUN_TEST(test_unlock_then_lock_in_one_poll_ends_locked);
  RUN_TEST(test_duplicate_command_is_not_run_again);
  RUN_TEST(test_redelivered_unlock_does_not_restart_timer);
  RUN_TEST(test_duplicate_list_never_drops_unexpired_id);
  RUN_TEST(test_expired_command_is_rejected);
  RUN_TEST(test_command_issued_before_boot_is_rejected);
  RUN_TEST(test_boot_time_is_set_once_from_first_poll);
  RUN_TEST(test_command_for_other_door_is_rejected);
  RUN_TEST(test_malformed_command_is_rejected);
  RUN_TEST(test_unknown_action_is_rejected);
  RUN_TEST(test_unlock_pulses_for_configured_time);
  RUN_TEST(test_unlock_never_exceeds_maximum);
  RUN_TEST(test_second_unlock_does_not_extend);
  RUN_TEST(test_lock_during_unlock_relocks_immediately);
  RUN_TEST(test_network_error_keeps_door_locked);
  RUN_TEST(test_relock_fires_while_request_blocks);
  RUN_TEST(test_event_id_derives_from_command_id);
  RUN_TEST(test_failed_event_post_does_not_block_lock_control);
  RUN_TEST(test_timestamp_parser_accepts_utc_formats);
  RUN_TEST(test_bad_poll_response_runs_nothing);

  RUN_TEST(test_unlock_stays_locked_when_timer_fails);
  RUN_TEST(test_checks_report_first_failure_in_contract_order);
  RUN_TEST(test_cache_full_rejection_is_final);
  RUN_TEST(test_cache_full_rejection_is_queued_once);
  RUN_TEST(test_duplicate_id_kept_until_its_own_expiry);
  RUN_TEST(test_boot_time_uses_poll_start_time);
  RUN_TEST(test_slow_response_cannot_rerun_command_from_before_boot);
  RUN_TEST(test_run_poll_backs_off_after_a_failed_poll);
  RUN_TEST(test_duplicate_id_kept_while_backend_still_serves);
  RUN_TEST(test_long_action_is_malformed_and_event_stays_small);
  RUN_TEST(test_poll_command_count_is_capped);
  RUN_TEST(test_timestamp_formats_as_utc_with_milliseconds);
  RUN_TEST(test_event_body_matches_contract_examples);
  RUN_TEST(test_example_poll_response_parses);
  RUN_TEST(test_event_responses_follow_contract_table);
  RUN_TEST(test_event_retry_backoff_is_2_4_8_seconds);
  RUN_TEST(test_event_queue_drops_oldest_when_full);
  RUN_TEST(test_event_service_posts_at_most_one_event);
  RUN_TEST(test_event_retry_backoff_starts_after_slow_post);
  RUN_TEST(test_event_retry_survives_millis_wraparound);
  RUN_TEST(test_poll_interval_is_start_to_start);
  RUN_TEST(test_poll_backoff_is_2_4_8_seconds);
  RUN_TEST(test_poll_scheduler_survives_millis_wraparound);
  RUN_TEST(test_body_read_stops_when_server_stalls);
  RUN_TEST(test_body_read_follows_content_length_and_chunks);
  RUN_TEST(test_body_read_rejects_bad_or_large_bodies);

  RUN_TEST(test_backend_poll_runs_unlock_then_lock);
  RUN_TEST(test_backend_accepts_firmware_event_bodies);
  RUN_TEST(test_backend_event_statuses_follow_response_table);
  RUN_TEST(test_backend_rejected_token_runs_nothing);
  RUN_TEST(test_backend_timestamps_parse);
  RUN_TEST(test_backend_door_id_is_the_device_key);
  RUN_TEST(test_backend_command_matches_poll);
  RUN_TEST(test_backend_stops_serving_expired_commands);
  RUN_TEST(test_backend_accepts_report_after_reset);

  return UNITY_END();
}
