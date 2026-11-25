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

  RUN_TEST(test_ed25519_matches_rfc8032_vectors);
  RUN_TEST(test_ed25519_rejects_non_canonical_signature);
  RUN_TEST(test_ed25519_test_seed_derives_fixture_public_key);

  RUN_TEST(test_base64url_decoding_is_strict);
  RUN_TEST(test_base64url_round_trips);
  RUN_TEST(test_malformed_qr_text_is_rejected);
  RUN_TEST(test_qr_demo_payload_is_rejected);
  RUN_TEST(test_qr_header_must_match_configured_key);
  RUN_TEST(test_forged_qr_is_rejected);
  RUN_TEST(test_qr_signature_matches_backend_fixture);
  RUN_TEST(test_qr_claims_must_parse);
  RUN_TEST(test_qr_times_round_inward);

  RUN_TEST(test_server_clock_bounds_use_request_and_response_times);
  RUN_TEST(test_server_clock_bounds_bracket_true_time);
  RUN_TEST(test_server_clock_combines_polls);
  RUN_TEST(test_server_clock_restarts_when_server_clock_moves);
  RUN_TEST(test_server_clock_stops_after_offline_limit);
  RUN_TEST(test_server_clock_forgets_bounds_before_millis_wraps);
  RUN_TEST(test_server_clock_survives_millis_wraparound);
  RUN_TEST(test_server_clock_boot_time_is_latest_possible);
  RUN_TEST(test_run_poll_feeds_server_clock);

  RUN_TEST(test_valid_qr_unlocks_and_reports_granted);
  RUN_TEST(test_qr_for_other_unit_is_rejected);
  RUN_TEST(test_unverified_qr_never_reports);
  RUN_TEST(test_qr_with_invalid_claims_is_rejected);
  RUN_TEST(test_qr_denied_before_first_sync);
  RUN_TEST(test_qr_offline_validation_stops_at_limit);
  RUN_TEST(test_qr_disabled_without_valid_configuration);
  RUN_TEST(test_test_only_key_disabled_outside_bench);
  RUN_TEST(test_qr_denied_while_backend_refuses_device);
  RUN_TEST(test_qr_self_test_detects_bad_verifier);
  RUN_TEST(test_revoked_qr_is_rejected);
  RUN_TEST(test_qr_is_single_use);
  RUN_TEST(test_used_list_full_denies_new_credentials);
  RUN_TEST(test_used_qr_stays_used_when_server_clock_moves_back);
  RUN_TEST(test_qr_issued_before_boot_is_rejected);
  RUN_TEST(test_power_cycle_cannot_replay_used_qr);
  RUN_TEST(test_expired_qr_is_rejected);
  RUN_TEST(test_fresh_qr_waits_until_issue_time_has_passed);
  RUN_TEST(test_qr_unlock_failure_is_reported);
  RUN_TEST(test_qr_grant_does_not_extend_unlock);
  RUN_TEST(test_qr_checks_report_first_failure_in_order);
  RUN_TEST(test_qr_report_time_is_inside_credential_window);
  RUN_TEST(test_qr_denial_reported_once_per_credential);
  RUN_TEST(test_repeated_decodes_are_ignored);
  RUN_TEST(test_stale_scan_is_ignored);
  RUN_TEST(test_qr_scan_drain_is_bounded);
  RUN_TEST(test_qr_failed_report_does_not_block_scans);
  RUN_TEST(test_qr_log_never_contains_token);

  RUN_TEST(test_qr_report_body_matches_contract);
  RUN_TEST(test_qr_event_responses_follow_table);
  RUN_TEST(test_qr_denial_never_evicts_a_grant_report);
  RUN_TEST(test_one_report_post_per_pass_qr_first);
  RUN_TEST(test_failing_qr_reports_cannot_starve_command_events);

  RUN_TEST(test_serial_scan_lines_are_parsed);

  RUN_TEST(test_camera_decode_is_copied_exactly);
  RUN_TEST(test_qr_signal_follows_decision);
  RUN_TEST(test_qr_drain_shows_each_decision);

  RUN_TEST(test_backend_qr_token_is_granted);
  RUN_TEST(test_backend_accepts_firmware_qr_report_bodies);
  RUN_TEST(test_backend_qr_statuses_follow_response_table);
  RUN_TEST(test_backend_rejects_grants_outside_credential_window);
  RUN_TEST(test_backend_capped_qr_expiry_is_honored);
  RUN_TEST(test_backend_production_qr_token_is_granted);
  RUN_TEST(test_backend_production_qr_window_boundaries);

  return UNITY_END();
}
