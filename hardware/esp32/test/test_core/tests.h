#pragma once

// Each test name in a rule comment matches Rules the door follows, in
// docs/firmware/lock-command-v0.md (R1 to R16) or docs/firmware/qr-access-v0.md
// (Q1 to Q20). The other tests cover the same rules more closely.

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
void test_boot_time_uses_poll_start_time();
void test_slow_response_cannot_rerun_command_from_before_boot();
void test_run_poll_backs_off_after_a_failed_poll();
void test_duplicate_id_kept_while_backend_still_serves();
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

// test_backend_replay.cpp, against responses from the real backend
void test_backend_poll_runs_unlock_then_lock();
void test_backend_accepts_firmware_event_bodies();
void test_backend_event_statuses_follow_response_table();
void test_backend_rejected_token_runs_nothing();
void test_backend_timestamps_parse();
void test_backend_door_id_is_the_device_key();
void test_backend_command_matches_poll();
void test_backend_stops_serving_expired_commands();
void test_backend_accepts_report_after_reset();

// test_poll_scheduler.cpp
void test_poll_interval_is_start_to_start();
void test_poll_backoff_is_2_4_8_seconds();
void test_poll_scheduler_survives_millis_wraparound();

// test_body_reader.cpp
void test_body_read_stops_when_server_stalls();
void test_body_read_follows_content_length_and_chunks();
void test_body_read_rejects_bad_or_large_bodies();

// test_ed25519.cpp
void test_ed25519_matches_rfc8032_vectors();               // Q4
void test_ed25519_rejects_non_canonical_signature();       // Q4
void test_ed25519_test_seed_derives_fixture_public_key();  // Q4

// test_qr_token.cpp
void test_base64url_decoding_is_strict();          // Q2
void test_base64url_round_trips();                 // Q2
void test_malformed_qr_text_is_rejected();         // Q2
void test_qr_demo_payload_is_rejected();           // Q2
void test_qr_header_must_match_configured_key();   // Q3
void test_forged_qr_is_rejected();                 // Q4
void test_qr_signature_matches_backend_fixture();  // Q4
void test_qr_claims_must_parse();                  // Q5
void test_qr_times_round_inward();                 // Q6

// test_server_clock.cpp
void test_server_clock_bounds_use_request_and_response_times();  // Q10
void test_server_clock_bounds_bracket_true_time();               // Q10
void test_server_clock_combines_polls();                         // Q10
void test_server_clock_restarts_when_server_clock_moves();       // Q10
void test_server_clock_stops_after_offline_limit();              // Q10
void test_server_clock_forgets_bounds_before_millis_wraps();     // Q10
void test_server_clock_survives_millis_wraparound();             // Q10
void test_server_clock_boot_time_is_latest_possible();           // Q14
void test_run_poll_feeds_server_clock();                         // Q10, Q11

// test_qr_access.cpp
void test_valid_qr_unlocks_and_reports_granted();             // Q17
void test_qr_for_other_unit_is_rejected();                    // Q7
void test_unverified_qr_never_reports();                      // Q8
void test_qr_with_invalid_claims_is_rejected();               // Q9
void test_qr_denied_before_first_sync();                      // Q10
void test_qr_offline_validation_stops_at_limit();             // Q10
void test_qr_disabled_without_valid_configuration();          // Q11
void test_test_only_key_disabled_outside_bench();             // Q11
void test_qr_denied_while_backend_refuses_device();           // Q11
void test_qr_self_test_detects_bad_verifier();                // Q11
void test_revoked_qr_is_rejected();                           // Q12
void test_qr_is_single_use();                                 // Q13
void test_used_list_full_denies_new_credentials();            // Q13
void test_used_qr_stays_used_when_server_clock_moves_back();  // Q13
void test_qr_issued_before_boot_is_rejected();                // Q14
void test_power_cycle_cannot_replay_used_qr();                // Q14
void test_expired_qr_is_rejected();                           // Q15
void test_fresh_qr_waits_until_issue_time_has_passed();       // Q16
void test_qr_unlock_failure_is_reported();                    // Q17
void test_qr_grant_does_not_extend_unlock();                  // Q17
void test_qr_checks_report_first_failure_in_order();          // Q17
void test_qr_report_time_is_inside_credential_window();       // Q18
void test_qr_denial_reported_once_per_credential();           // Q18
void test_repeated_decodes_are_ignored();                     // Q1
void test_stale_scan_is_ignored();                            // Q1
void test_qr_scan_drain_is_bounded();                         // Q1
void test_qr_failed_report_does_not_block_scans();            // Q19
void test_qr_log_never_contains_token();                      // Q20

// test_event_outbox.cpp, QR reports
void test_qr_report_body_matches_contract();                  // Q18
void test_qr_event_responses_follow_table();                  // Q19
void test_qr_denial_never_evicts_a_grant_report();            // Q19
void test_one_report_post_per_pass_qr_first();                // Q19
void test_failing_qr_reports_cannot_starve_command_events();  // R14, Q19

// test_serial_scan.cpp
void test_serial_scan_lines_are_parsed();  // Q1

// test_camera_scan.cpp
void test_camera_decode_is_copied_exactly();  // Q1
void test_qr_signal_follows_decision();       // Q1
void test_qr_drain_shows_each_decision();     // Q1

// test_qr_backend_replay.cpp, against responses from the real Deno backend
void test_backend_qr_token_is_granted();                       // Q4
void test_backend_accepts_firmware_qr_report_bodies();         // Q18
void test_backend_qr_statuses_follow_response_table();         // Q19
void test_backend_rejects_grants_outside_credential_window();  // Q6
void test_backend_capped_qr_expiry_is_honored();               // Q15
void test_backend_production_qr_token_is_granted();            // Q6
void test_backend_production_qr_window_boundaries();           // Q15, Q16
