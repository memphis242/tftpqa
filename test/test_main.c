/**
 * @file test_main.c
 * @brief Test runner and common setup/teardown.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"

/*---------------------------------------------------------------------------
 * Common test setup and teardown
 *---------------------------------------------------------------------------*/

void setUp(void)
{
   // Common setup for all tests
}

void tearDown(void)
{
   // Common teardown for all tests
}

/*---------------------------------------------------------------------------
 * Forward declarations of all test functions
 *---------------------------------------------------------------------------*/

// tftp_log
extern void test_log_init_without_syslog_sets_min_level(void);
extern void test_log_init_with_syslog_opens_syslog(void);
extern void test_log_message_below_min_level_is_suppressed(void);
extern void test_log_message_at_min_level_is_emitted(void);
extern void test_log_message_above_min_level_is_emitted(void);
extern void test_log_shutdown_without_syslog_is_safe(void);
extern void test_log_shutdown_with_syslog_closes_syslog(void);
extern void test_log_message_with_syslog_enabled(void);

// tftp_parsecfg
extern void test_parsecfg_defaults_produces_sane_values(void);
extern void test_parsecfg_load_nonexistent_file_returns_error(void);
extern void test_parsecfg_load_valid_config(void);
extern void test_parsecfg_ignores_comments_and_blanks(void);
extern void test_parsecfg_inline_comments_stripped(void);
extern void test_parsecfg_ctrl_port_zero_disables_faults(void);
extern void test_parsecfg_rejects_invalid_port(void);
extern void test_parsecfg_unknown_key_still_succeeds(void);
extern void test_parsecfg_missing_equals_delimiter(void);
extern void test_parsecfg_root_dir_and_fault_whitelist(void);
extern void test_parsecfg_all_numeric_fields(void);
extern void test_parsecfg_wrq_protection_fields_loaded(void);
extern void test_parsecfg_wrq_enabled_false(void);
extern void test_parsecfg_wrq_duration_sec_invalid(void);
extern void test_parsecfg_wrq_enabled_invalid_value(void);
extern void test_parsecfg_abandoned_sessions_default_zero(void);
extern void test_parsecfg_max_abandoned_sessions_loaded(void);
extern void test_parsecfg_ctrl_port_over_65535_rejected(void);
extern void test_parsecfg_root_dir_empty_rejected(void);
extern void test_parsecfg_root_dir_too_long_rejected(void);
extern void test_parsecfg_run_as_user_valid(void);
extern void test_parsecfg_run_as_user_empty_rejected(void);
extern void test_parsecfg_run_as_user_too_long_rejected(void);
extern void test_parsecfg_log_level_all_values(void);
extern void test_parsecfg_log_level_invalid_rejected(void);
extern void test_parsecfg_timeout_sec_zero_rejected(void);
extern void test_parsecfg_timeout_sec_over_300_rejected(void);
extern void test_parsecfg_max_retransmits_zero_rejected(void);
extern void test_parsecfg_max_retransmits_over_100_rejected(void);
extern void test_parsecfg_max_requests_zero_rejected(void);
extern void test_parsecfg_fault_whitelist_invalid_rejected(void);
extern void test_parsecfg_ip_whitelist_empty_is_deny_all(void);
extern void test_parsecfg_ip_whitelist_slash_zero_allows_all(void);
extern void test_parsecfg_ip_whitelist_valid(void);
extern void test_parsecfg_ip_whitelist_invalid_rejected(void);
extern void test_parsecfg_ip_whitelist_plural_with_cidr(void);
extern void test_parsecfg_missing_whitelist_key_is_fatal(void);
extern void test_parsecfg_missing_whitelist_key_ok_when_external(void);
extern void test_parsecfg_wrq_enabled_yes(void);
extern void test_parsecfg_wrq_enabled_one(void);
extern void test_parsecfg_wrq_enabled_no(void);
extern void test_parsecfg_wrq_enabled_zero(void);
extern void test_parsecfg_tftp_port_zero_rejected(void);
extern void test_parsecfg_line_without_trailing_newline(void);
extern void test_parsecfg_fault_whitelist_decimal(void);
extern void test_parsecfg_multiple_errors_reports_count(void);
extern void test_parsecfg_tid_port_range_valid(void);
extern void test_parsecfg_tid_port_range_single_port(void);
extern void test_parsecfg_tid_port_range_invalid_format(void);
extern void test_parsecfg_tid_port_range_min_greater_than_max(void);
extern void test_parsecfg_tid_port_range_zero_rejected(void);
extern void test_parsecfg_tid_port_range_over_65535_rejected(void);
extern void test_parsecfg_new_file_mode_default_is_0666(void);
extern void test_parsecfg_new_file_mode_octal(void);
extern void test_parsecfg_new_file_mode_rejects_setuid(void);
extern void test_parsecfg_new_file_mode_rejects_sticky(void);
extern void test_parsecfg_new_file_mode_rejects_trailing_garbage(void);

// tftp_pkt (validation, parsing, round-trip, rejection, edge cases)
extern void test_pkt_valid_rrq_octet(void);
extern void test_pkt_valid_wrq_netascii(void);
extern void test_pkt_valid_rrq_netascii_case_insensitive(void);
extern void test_pkt_reject_wrong_opcode(void);
extern void test_pkt_reject_missing_filename_nul(void);
extern void test_pkt_reject_empty_filename(void);
extern void test_pkt_reject_filename_with_slash(void);
extern void test_pkt_reject_filename_with_backslash(void);
extern void test_pkt_reject_filename_only_dots(void);
extern void test_pkt_reject_unsupported_mode_mail(void);
extern void test_pkt_reject_missing_mode_nul(void);
extern void test_pkt_reject_packet_too_short(void);
extern void test_pkt_reject_nonprintable_in_filename(void);
extern void test_pkt_parse_request_rrq(void);
extern void test_pkt_parse_request_wrq(void);
extern void test_pkt_data_round_trip(void);
extern void test_pkt_data_empty_payload(void);
extern void test_pkt_data_full_512(void);
extern void test_pkt_ack_round_trip(void);
extern void test_pkt_error_round_trip(void);
extern void test_pkt_parse_ack_rejects_short(void);
extern void test_pkt_parse_ack_rejects_wrong_opcode(void);
extern void test_pkt_parse_data_rejects_short(void);
extern void test_pkt_parse_error_rejects_no_nul(void);
extern void test_pkt_reject_filename_too_long(void);
extern void test_pkt_parse_data_rejects_wrong_opcode(void);
extern void test_pkt_build_error_returns_zero_when_buffer_too_small(void);
extern void test_pkt_build_error_succeeds_with_adequate_buffer(void);
extern void test_pkt_valid_rrq_octet_mixed_case(void);
extern void test_pkt_ack_block_zero(void);

// tftp_util (basic, netascii, chroot_and_drop)
extern void test_util_is_valid_filename_char_accepts_alphanumeric(void);
extern void test_util_is_valid_filename_char_rejects_separators(void);
extern void test_util_is_valid_filename_char_rejects_control_chars(void);
extern void test_util_create_ephemeral_socket_succeeds(void);
extern void test_util_set_recv_timeout_succeeds(void);
extern void test_util_netascii_bare_lf_becomes_crlf(void);
extern void test_util_netascii_bare_cr_becomes_cr_nul(void);
extern void test_util_netascii_crlf_passes_through(void);
extern void test_util_netascii_pending_cr_across_boundary(void);
extern void test_util_netascii_pending_cr_followed_by_non_lf(void);
extern void test_util_netascii_no_special_chars(void);
extern void test_util_netascii_empty_input(void);
extern void test_util_netascii_to_octet_crlf_becomes_lf(void);
extern void test_util_netascii_to_octet_cr_nul_becomes_cr(void);
extern void test_util_netascii_to_octet_pending_cr_boundary(void);
extern void test_util_netascii_to_octet_no_special(void);
extern void test_chroot_and_drop_non_root_succeeds(void);
extern void test_chroot_and_drop_bad_dir_fails(void);
extern void test_util_create_udp_socket_in_range_succeeds(void);
extern void test_util_create_udp_socket_in_range_single_port(void);
extern void test_util_create_udp_socket_in_range_all_busy(void);
extern void test_util_suspicious_text_clean_ascii(void);
extern void test_util_suspicious_text_allowed_controls(void);
extern void test_util_suspicious_text_cr_nul_allowed(void);
extern void test_util_suspicious_text_standalone_nul(void);
extern void test_util_suspicious_text_leading_nul(void);
extern void test_util_suspicious_text_bell_char(void);
extern void test_util_suspicious_text_del_char(void);
extern void test_util_suspicious_text_high_byte(void);
extern void test_util_suspicious_text_empty_buffer(void);
extern void test_util_text_check_valid_utf8_2byte(void);
extern void test_util_text_check_valid_utf8_3byte(void);
extern void test_util_text_check_valid_utf8_4byte(void);
extern void test_util_text_check_utf8_mixed_with_ascii(void);
extern void test_util_text_check_lone_continuation_byte(void);
extern void test_util_text_check_overlong_2byte(void);
extern void test_util_text_check_truncated_sequence(void);
extern void test_util_text_check_overlong_3byte(void);
extern void test_util_text_check_above_max_codepoint(void);
extern void test_util_check_read_perms_world_readable(void);
extern void test_util_check_read_perms_not_world_readable(void);
extern void test_util_check_read_perms_setuid_rejected(void);
extern void test_util_check_read_perms_directory_rejected(void);
extern void test_util_check_write_perms_world_writable(void);
extern void test_util_check_write_perms_not_world_writable(void);
extern void test_util_open_for_read_rejects_symlink(void);
extern void test_util_open_for_write_creates_with_mode(void);
extern void test_util_open_for_write_overwrites_existing(void);
extern void test_util_open_for_write_create_mode_stripped_by_umask(void);

// tftpqa_ctrl
extern void test_ctrl_set_fault_and_get(void);
extern void test_ctrl_set_fault_with_param(void);
extern void test_ctrl_unknown_command(void);
extern void test_ctrl_unknown_fault_mode(void);
extern void test_ctrl_whitelist_rejects_disallowed_mode(void);
extern void test_ctrl_set_fault_missing_mode_name(void);
extern void test_ctrl_param_present_false_when_no_param(void);
extern void test_ctrl_param_zero_is_distinct_from_no_param(void);
extern void test_ctrl_reset_clears_mode_and_param_present(void);
extern void test_ctrl_set_fault_reply_no_param(void);
extern void test_ctrl_set_fault_reply_with_param(void);
extern void test_ctrl_get_fault_reply_no_param(void);
extern void test_ctrl_get_fault_reply_with_param(void);
extern void test_ctrl_reset_reply(void);
extern void test_ctrl_unknown_cmd_reply(void);
extern void test_ctrl_whitelist_reject_reply(void);
extern void test_ctrl_set_fault_invalid_param_nonnumeric(void);
extern void test_ctrl_set_fault_invalid_param_overflow(void);
extern void test_ctrl_set_fault_mode_name_too_long(void);
extern void test_ctrl_case_insensitive_command(void);
extern void test_ctrl_leading_whitespace_stripped(void);
extern void test_ctrl_crlf_stripped(void);
extern void test_ctrl_whitelisted_client_ip_accepts_loopback(void);
extern void test_ctrl_whitelisted_client_ip_blocks_other_sender(void);
extern void test_ctrl_init_null_cfg(void);
extern void test_ctrl_init_bind_failure(void);
extern void test_ctrl_no_packet_poll_noop(void);
extern void test_ctrl_empty_packet_ignored(void);
extern void test_ctrl_set_fault_bare_no_args_reply(void);
extern void test_ctrl_set_fault_whitespace_only_after_command(void);
extern void test_ctrl_set_fault_param_with_trailing_garbage(void);
extern void test_ctrl_set_fault_param_just_above_uint32_max(void);
extern void test_ctrl_set_fault_mode_name_too_long_inner(void);

// tftpqa_faultmode
extern void test_fault_mode_names_all_present(void);
extern void test_fault_lookup_mode_full_name_match(void);
extern void test_fault_lookup_mode_short_name_match(void);
extern void test_fault_lookup_mode_case_insensitive(void);
extern void test_fault_lookup_mode_nonexistent_returns_negative_one(void);
extern void test_fault_lookup_mode_too_short(void);
extern void test_fault_lookup_mode_too_long(void);
extern void test_fault_lookup_mode_fault_none(void);
extern void test_fault_lookup_mode_fault_none_short(void);
extern void test_fault_lookup_mode_last_mode(void);
extern void test_fault_lookup_mode_last_mode_short(void);
extern void test_fault_lookup_mode_partial_match_fails(void);
extern void test_fault_lookup_mode_middle_modes_full_names(void);
extern void test_fault_lookup_mode_middle_modes_short_names(void);
extern void test_fault_lookup_mode_end_modes_both_formats(void);
extern void test_fault_lookup_mode_alphabetically_before_modes(void);
extern void test_fault_lookup_mode_alphabetically_after_modes(void);
extern void test_fault_lookup_mode_multiple_sequential_searches(void);
extern void test_fault_lookup_mode_mixed_formats_sequential(void);
extern void test_fault_lookup_mode_all_modes_exhaustive(void);

// tftp_ipwhitelist
extern void test_ipwhitelist_empty_is_deny_all(void);
extern void test_ipwhitelist_is_deny_all_true(void);
extern void test_ipwhitelist_is_deny_all_false(void);
extern void test_ipwhitelist_single_bare_ip(void);
extern void test_ipwhitelist_single_cidr_24(void);
extern void test_ipwhitelist_host_bits_normalized(void);
extern void test_ipwhitelist_mixed_list_with_whitespace(void);
extern void test_ipwhitelist_prefix_zero_matches_all(void);
extern void test_ipwhitelist_prefix_32_explicit_vs_bare(void);
extern void test_ipwhitelist_overflow_rejected(void);
extern void test_ipwhitelist_malformed_trailing_comma(void);
extern void test_ipwhitelist_malformed_empty_middle_token(void);
extern void test_ipwhitelist_malformed_non_numeric_prefix(void);
extern void test_ipwhitelist_malformed_prefix_33(void);
extern void test_ipwhitelist_malformed_negative_prefix(void);
extern void test_ipwhitelist_malformed_bad_octet(void);
extern void test_ipwhitelist_malformed_trailing_garbage(void);
extern void test_ipwhitelist_malformed_trailing_slash(void);
extern void test_ipwhitelist_malformed_double_slash(void);
extern void test_ipwhitelist_matcher_slash_32(void);
extern void test_ipwhitelist_matcher_slash_24_boundary(void);
extern void test_ipwhitelist_matcher_multiple_entries(void);
extern void test_ipwhitelist_malformed_results_in_deny_all(void);
extern void test_ipwhitelist_null_input_is_deny_all(void);
extern void test_ipwhitelist_init_resets_singleton(void);
extern void test_ipwhitelist_block_whitelisted_ip_excluded(void);
extern void test_ipwhitelist_block_non_whitelisted_ip_stays_false(void);
extern void test_ipwhitelist_block_duplicate_is_noop(void);
extern void test_ipwhitelist_block_invalid_inaddr_any(void);
extern void test_ipwhitelist_block_invalid_broadcast(void);
extern void test_ipwhitelist_block_one_from_subnet(void);
extern void test_ipwhitelist_block_multiple_from_subnet(void);
extern void test_ipwhitelist_block_subnet_boundary_ips(void);
extern void test_ipwhitelist_clear_resets_to_deny_all(void);
extern void test_ipwhitelist_clear_resets_blacklist(void);
extern void test_ipwhitelist_clear_twice_is_safe(void);
extern void test_ipwhitelist_init_does_not_reset_blacklist(void);
extern void test_ipwhitelist_block_forces_growth(void);
extern void test_ipwhitelist_block_with_allow_all_whitelist(void);
extern void test_ipwhitelist_is_deny_all_when_only_host_blocked(void);
extern void test_ipwhitelist_is_deny_all_allow_all_with_blocked_ips(void);
extern void test_ipwhitelist_is_deny_all_subnet_fully_shadowed(void);
extern void test_ipwhitelist_is_deny_all_subnet_partially_shadowed(void);
extern void test_ipwhitelist_is_deny_all_multi_entry_all_shadowed(void);
extern void test_ipwhitelist_is_deny_all_multi_entry_one_live(void);
extern void test_ipwhitelist_clear_allows_reblock(void);
extern void test_ipwhitelist_clear_then_reblock_is_deny_all(void);

// tftp_fsm
extern void test_fsm_kickoff_rejects_null_rqbuf(void);
extern void test_fsm_kickoff_rejects_null_peer_addr(void);
extern void test_fsm_kickoff_rejects_null_cfg(void);
extern void test_fsm_kickoff_rejects_null_fault(void);
extern void test_fsm_kickoff_rejects_zero_rqsz(void);
extern void test_fsm_kickoff_unparseable_request_returns_protocol_err(void);
extern void test_fsm_kickoff_rrq_timeout_returns_fine(void);
extern void test_fsm_kickoff_wrq_timeout_returns_fine(void);
extern void test_fsm_kickoff_rrq_file_not_found_fault_returns_fine(void);
extern void test_fsm_kickoff_wrq_access_violation_returns_fine(void);
extern void test_fsm_kickoff_rrq_access_violation_fault_returns_fine(void);
extern void test_fsm_kickoff_file_not_found_returns_file_err(void);
extern void test_fsm_kickoff_wrq_disabled_returns_wrq_disabled(void);
extern void test_fsm_kickoff_wrq_disk_check_fails_returns_disk_check(void);
extern void test_fsm_kickoff_wrq_file_creation_fails_returns_file_err(void);
extern void test_fsm_kickoff_socket_creation_fails_returns_socket_err(void);
extern void test_fsm_kickoff_set_recv_timeout_fails_returns_setsockopt_err(void);
extern void test_fsm_clean_exit_cleans_resources(void);
extern void test_fsm_clean_exit_closes_file_and_socket(void);
extern void test_fsm_kickoff_sets_transfer_mode_octet(void);
extern void test_fsm_kickoff_sets_transfer_mode_netascii(void);
extern void test_fsm_kickoff_wrq_bytes_written_null_handled(void);
extern void test_fsm_kickoff_wrq_with_session_budget(void);
extern void test_fsm_kickoff_rrq_fault_none(void);
extern void test_fsm_kickoff_wrq_fault_none(void);

// tftpqa_seq
extern void test_seq_load_valid_single_entry_defaults(void);
extern void test_seq_load_valid_multiple_entries_with_params(void);
extern void test_seq_load_valid_comments_and_blanks(void);
extern void test_seq_load_valid_case_insensitive_mode(void);
extern void test_seq_load_valid_short_mode_names(void);
extern void test_seq_load_valid_field_order_doesnt_matter(void);
extern void test_seq_load_nonexistent_file_returns_error(void);
extern void test_seq_load_empty_file_returns_error(void);
extern void test_seq_load_unknown_fault_mode_returns_error(void);
extern void test_seq_load_invalid_param_non_numeric_returns_error(void);
extern void test_seq_load_invalid_count_zero_returns_error(void);
extern void test_seq_load_invalid_count_non_numeric_returns_error(void);
extern void test_seq_load_missing_required_mode_field_returns_error(void);
extern void test_seq_load_unknown_key_returns_error(void);
extern void test_seq_load_partial_file_on_first_error(void);
extern void test_seq_advance_increments_sessions_in_step(void);
extern void test_seq_advance_transitions_to_next_entry(void);
extern void test_seq_advance_returns_false_when_exhausted(void);
extern void test_seq_advance_updates_param_on_transition(void);
extern void test_seq_advance_multi_session_entries(void);
extern void test_seq_free_zeros_struct(void);
extern void test_seq_integration_real_file_good(void);
extern void test_seq_integration_real_file_bad_mode(void);
extern void test_seq_load_inline_comment_makes_empty_line(void);
extern void test_seq_load_field_with_inline_comment(void);
extern void test_seq_load_various_param_values(void);
extern void test_seq_load_various_count_values(void);
extern void test_seq_advance_single_session_entry(void);
extern void test_seq_advance_large_count_entry(void);
extern void test_seq_load_only_comments_and_blanks(void);
extern void test_seq_load_trailing_spaces_before_comment(void);
extern void test_seq_load_all_whitespace_line_mixed(void);
extern void test_seq_advance_exact_boundary(void);
extern void test_seq_load_count_boundary_values(void);
extern void test_seq_advance_multiple_transitions(void);
extern void test_seq_load_comment_only_line_with_tabs(void);
extern void test_seq_load_spaces_then_comment_then_spaces(void);

/*---------------------------------------------------------------------------
 * Main test runner
 *---------------------------------------------------------------------------*/

int main(void)
{
   UNITY_BEGIN();

   // tftp_log
   RUN_TEST( test_log_init_without_syslog_sets_min_level );
   RUN_TEST( test_log_init_with_syslog_opens_syslog );
   RUN_TEST( test_log_message_below_min_level_is_suppressed );
   RUN_TEST( test_log_message_at_min_level_is_emitted );
   RUN_TEST( test_log_message_above_min_level_is_emitted );
   RUN_TEST( test_log_shutdown_without_syslog_is_safe );
   RUN_TEST( test_log_shutdown_with_syslog_closes_syslog );
   RUN_TEST( test_log_message_with_syslog_enabled );

   // tftp_parsecfg
   RUN_TEST( test_parsecfg_defaults_produces_sane_values );
   RUN_TEST( test_parsecfg_load_nonexistent_file_returns_error );

   // tftp_pkt: validation
   RUN_TEST( test_pkt_valid_rrq_octet );
   RUN_TEST( test_pkt_valid_wrq_netascii );
   RUN_TEST( test_pkt_valid_rrq_netascii_case_insensitive );
   RUN_TEST( test_pkt_reject_wrong_opcode );
   RUN_TEST( test_pkt_reject_missing_filename_nul );
   RUN_TEST( test_pkt_reject_empty_filename );
   RUN_TEST( test_pkt_reject_filename_with_slash );
   RUN_TEST( test_pkt_reject_filename_with_backslash );
   RUN_TEST( test_pkt_reject_filename_only_dots );
   RUN_TEST( test_pkt_reject_unsupported_mode_mail );
   RUN_TEST( test_pkt_reject_missing_mode_nul );
   RUN_TEST( test_pkt_reject_packet_too_short );
   RUN_TEST( test_pkt_reject_nonprintable_in_filename );

   // tftp_pkt: parsing
   RUN_TEST( test_pkt_parse_request_rrq );
   RUN_TEST( test_pkt_parse_request_wrq );

   // tftp_pkt: build/parse round-trips
   RUN_TEST( test_pkt_data_round_trip );
   RUN_TEST( test_pkt_data_empty_payload );
   RUN_TEST( test_pkt_data_full_512 );
   RUN_TEST( test_pkt_ack_round_trip );
   RUN_TEST( test_pkt_error_round_trip );

   // tftp_pkt: parse rejection
   RUN_TEST( test_pkt_parse_ack_rejects_short );
   RUN_TEST( test_pkt_parse_ack_rejects_wrong_opcode );
   RUN_TEST( test_pkt_parse_data_rejects_short );
   RUN_TEST( test_pkt_parse_error_rejects_no_nul );

   // tftp_util
   RUN_TEST( test_util_is_valid_filename_char_accepts_alphanumeric );
   RUN_TEST( test_util_is_valid_filename_char_rejects_separators );
   RUN_TEST( test_util_is_valid_filename_char_rejects_control_chars );
   RUN_TEST( test_util_create_ephemeral_socket_succeeds );
   RUN_TEST( test_util_set_recv_timeout_succeeds );

   // netascii translation
   RUN_TEST( test_util_netascii_bare_lf_becomes_crlf );
   RUN_TEST( test_util_netascii_bare_cr_becomes_cr_nul );
   RUN_TEST( test_util_netascii_crlf_passes_through );
   RUN_TEST( test_util_netascii_pending_cr_across_boundary );
   RUN_TEST( test_util_netascii_pending_cr_followed_by_non_lf );
   RUN_TEST( test_util_netascii_no_special_chars );
   RUN_TEST( test_util_netascii_empty_input );

   // reverse netascii (netascii -> octet)
   RUN_TEST( test_util_netascii_to_octet_crlf_becomes_lf );
   RUN_TEST( test_util_netascii_to_octet_cr_nul_becomes_cr );
   RUN_TEST( test_util_netascii_to_octet_pending_cr_boundary );
   RUN_TEST( test_util_netascii_to_octet_no_special );

   // control channel
   RUN_TEST( test_ctrl_set_fault_and_get );
   RUN_TEST( test_ctrl_set_fault_with_param );
   RUN_TEST( test_ctrl_unknown_command );
   RUN_TEST( test_ctrl_unknown_fault_mode );
   RUN_TEST( test_ctrl_whitelist_rejects_disallowed_mode );
   RUN_TEST( test_ctrl_set_fault_missing_mode_name );
   // param_present semantics
   RUN_TEST( test_ctrl_param_present_false_when_no_param );
   RUN_TEST( test_ctrl_param_zero_is_distinct_from_no_param );
   RUN_TEST( test_ctrl_reset_clears_mode_and_param_present );
   // reply content
   RUN_TEST( test_ctrl_set_fault_reply_no_param );
   RUN_TEST( test_ctrl_set_fault_reply_with_param );
   RUN_TEST( test_ctrl_get_fault_reply_no_param );
   RUN_TEST( test_ctrl_get_fault_reply_with_param );
   RUN_TEST( test_ctrl_reset_reply );
   RUN_TEST( test_ctrl_unknown_cmd_reply );
   RUN_TEST( test_ctrl_whitelist_reject_reply );
   // bad input
   RUN_TEST( test_ctrl_set_fault_invalid_param_nonnumeric );
   RUN_TEST( test_ctrl_set_fault_invalid_param_overflow );
   RUN_TEST( test_ctrl_set_fault_mode_name_too_long );
   // protocol robustness
   RUN_TEST( test_ctrl_case_insensitive_command );
   RUN_TEST( test_ctrl_leading_whitespace_stripped );
   RUN_TEST( test_ctrl_crlf_stripped );
   // IP whitelist
   RUN_TEST( test_ctrl_whitelisted_client_ip_accepts_loopback );
   RUN_TEST( test_ctrl_whitelisted_client_ip_blocks_other_sender );
   // init errors and poll-loop behavior
   RUN_TEST( test_ctrl_init_null_cfg );
   RUN_TEST( test_ctrl_init_bind_failure );
   RUN_TEST( test_ctrl_no_packet_poll_noop );
   RUN_TEST( test_ctrl_empty_packet_ignored );
   // SET_FAULT argument validation
   RUN_TEST( test_ctrl_set_fault_bare_no_args_reply );
   RUN_TEST( test_ctrl_set_fault_whitespace_only_after_command );
   RUN_TEST( test_ctrl_set_fault_param_with_trailing_garbage );
   RUN_TEST( test_ctrl_set_fault_param_just_above_uint32_max );
   RUN_TEST( test_ctrl_set_fault_mode_name_too_long_inner );

   // config file parsing (with real files)
   RUN_TEST( test_parsecfg_load_valid_config );
   RUN_TEST( test_parsecfg_ignores_comments_and_blanks );
   RUN_TEST( test_parsecfg_inline_comments_stripped );
   RUN_TEST( test_parsecfg_ctrl_port_zero_disables_faults );
   RUN_TEST( test_parsecfg_rejects_invalid_port );
   RUN_TEST( test_parsecfg_unknown_key_still_succeeds );
   RUN_TEST( test_parsecfg_missing_equals_delimiter );
   RUN_TEST( test_parsecfg_root_dir_and_fault_whitelist );
   RUN_TEST( test_parsecfg_all_numeric_fields );
   RUN_TEST( test_parsecfg_wrq_protection_fields_loaded );
   RUN_TEST( test_parsecfg_wrq_enabled_false );
   RUN_TEST( test_parsecfg_wrq_duration_sec_invalid );
   RUN_TEST( test_parsecfg_wrq_enabled_invalid_value );
   RUN_TEST( test_parsecfg_abandoned_sessions_default_zero );
   RUN_TEST( test_parsecfg_max_abandoned_sessions_loaded );
   RUN_TEST( test_parsecfg_ctrl_port_over_65535_rejected );
   RUN_TEST( test_parsecfg_root_dir_empty_rejected );
   RUN_TEST( test_parsecfg_root_dir_too_long_rejected );
   RUN_TEST( test_parsecfg_run_as_user_valid );
   RUN_TEST( test_parsecfg_run_as_user_empty_rejected );
   RUN_TEST( test_parsecfg_run_as_user_too_long_rejected );
   RUN_TEST( test_parsecfg_log_level_all_values );
   RUN_TEST( test_parsecfg_log_level_invalid_rejected );
   RUN_TEST( test_parsecfg_timeout_sec_zero_rejected );
   RUN_TEST( test_parsecfg_timeout_sec_over_300_rejected );
   RUN_TEST( test_parsecfg_max_retransmits_zero_rejected );
   RUN_TEST( test_parsecfg_max_retransmits_over_100_rejected );
   RUN_TEST( test_parsecfg_max_requests_zero_rejected );
   RUN_TEST( test_parsecfg_fault_whitelist_invalid_rejected );
   RUN_TEST( test_parsecfg_ip_whitelist_empty_is_deny_all );
   RUN_TEST( test_parsecfg_ip_whitelist_slash_zero_allows_all );
   RUN_TEST( test_parsecfg_ip_whitelist_valid );
   RUN_TEST( test_parsecfg_ip_whitelist_invalid_rejected );
   RUN_TEST( test_parsecfg_ip_whitelist_plural_with_cidr );
   RUN_TEST( test_parsecfg_missing_whitelist_key_is_fatal );
   RUN_TEST( test_parsecfg_missing_whitelist_key_ok_when_external );
   RUN_TEST( test_parsecfg_wrq_enabled_yes );
   RUN_TEST( test_parsecfg_wrq_enabled_one );
   RUN_TEST( test_parsecfg_wrq_enabled_no );
   RUN_TEST( test_parsecfg_wrq_enabled_zero );
   RUN_TEST( test_parsecfg_tftp_port_zero_rejected );
   RUN_TEST( test_parsecfg_line_without_trailing_newline );
   RUN_TEST( test_parsecfg_fault_whitelist_decimal );
   RUN_TEST( test_parsecfg_multiple_errors_reports_count );
   RUN_TEST( test_parsecfg_tid_port_range_valid );
   RUN_TEST( test_parsecfg_tid_port_range_single_port );
   RUN_TEST( test_parsecfg_tid_port_range_invalid_format );
   RUN_TEST( test_parsecfg_tid_port_range_min_greater_than_max );
   RUN_TEST( test_parsecfg_tid_port_range_zero_rejected );
   RUN_TEST( test_parsecfg_tid_port_range_over_65535_rejected );
   RUN_TEST( test_parsecfg_new_file_mode_default_is_0666 );
   RUN_TEST( test_parsecfg_new_file_mode_octal );
   RUN_TEST( test_parsecfg_new_file_mode_rejects_setuid );
   RUN_TEST( test_parsecfg_new_file_mode_rejects_sticky );
   RUN_TEST( test_parsecfg_new_file_mode_rejects_trailing_garbage );

   // packet edge cases
   RUN_TEST( test_pkt_reject_filename_too_long );
   RUN_TEST( test_pkt_parse_data_rejects_wrong_opcode );
   RUN_TEST( test_pkt_build_error_returns_zero_when_buffer_too_small );
   RUN_TEST( test_pkt_build_error_succeeds_with_adequate_buffer );
   RUN_TEST( test_pkt_valid_rrq_octet_mixed_case );
   RUN_TEST( test_pkt_ack_block_zero );

   // create_udp_socket_in_range
   RUN_TEST( test_util_create_udp_socket_in_range_succeeds );
   RUN_TEST( test_util_create_udp_socket_in_range_single_port );
   RUN_TEST( test_util_create_udp_socket_in_range_all_busy );

   // suspicious text byte detection
   RUN_TEST( test_util_suspicious_text_clean_ascii );
   RUN_TEST( test_util_suspicious_text_allowed_controls );
   RUN_TEST( test_util_suspicious_text_cr_nul_allowed );
   RUN_TEST( test_util_suspicious_text_standalone_nul );
   RUN_TEST( test_util_suspicious_text_leading_nul );
   RUN_TEST( test_util_suspicious_text_bell_char );
   RUN_TEST( test_util_suspicious_text_del_char );
   RUN_TEST( test_util_suspicious_text_high_byte );
   RUN_TEST( test_util_suspicious_text_empty_buffer );

   // UTF-8 awareness
   RUN_TEST( test_util_text_check_valid_utf8_2byte );
   RUN_TEST( test_util_text_check_valid_utf8_3byte );
   RUN_TEST( test_util_text_check_valid_utf8_4byte );
   RUN_TEST( test_util_text_check_utf8_mixed_with_ascii );
   RUN_TEST( test_util_text_check_lone_continuation_byte );
   RUN_TEST( test_util_text_check_overlong_2byte );
   RUN_TEST( test_util_text_check_truncated_sequence );
   RUN_TEST( test_util_text_check_overlong_3byte );
   RUN_TEST( test_util_text_check_above_max_codepoint );

   // file-permission helpers
   RUN_TEST( test_util_check_read_perms_world_readable );
   RUN_TEST( test_util_check_read_perms_not_world_readable );
   RUN_TEST( test_util_check_read_perms_setuid_rejected );
   RUN_TEST( test_util_check_read_perms_directory_rejected );
   RUN_TEST( test_util_check_write_perms_world_writable );
   RUN_TEST( test_util_check_write_perms_not_world_writable );
   RUN_TEST( test_util_open_for_read_rejects_symlink );
   RUN_TEST( test_util_open_for_write_creates_with_mode );
   RUN_TEST( test_util_open_for_write_overwrites_existing );
   RUN_TEST( test_util_open_for_write_create_mode_stripped_by_umask );

   // chroot_and_drop
   RUN_TEST( test_chroot_and_drop_non_root_succeeds );
   RUN_TEST( test_chroot_and_drop_bad_dir_fails );

   // fault mode names and lookup
   RUN_TEST( test_fault_mode_names_all_present );
   RUN_TEST( test_fault_lookup_mode_full_name_match );
   RUN_TEST( test_fault_lookup_mode_short_name_match );
   RUN_TEST( test_fault_lookup_mode_case_insensitive );
   RUN_TEST( test_fault_lookup_mode_nonexistent_returns_negative_one );
   RUN_TEST( test_fault_lookup_mode_too_short );
   RUN_TEST( test_fault_lookup_mode_too_long );
   RUN_TEST( test_fault_lookup_mode_fault_none );
   RUN_TEST( test_fault_lookup_mode_fault_none_short );
   RUN_TEST( test_fault_lookup_mode_last_mode );
   RUN_TEST( test_fault_lookup_mode_last_mode_short );
   RUN_TEST( test_fault_lookup_mode_partial_match_fails );
   RUN_TEST( test_fault_lookup_mode_middle_modes_full_names );
   RUN_TEST( test_fault_lookup_mode_middle_modes_short_names );
   RUN_TEST( test_fault_lookup_mode_end_modes_both_formats );
   RUN_TEST( test_fault_lookup_mode_alphabetically_before_modes );
   RUN_TEST( test_fault_lookup_mode_alphabetically_after_modes );
   RUN_TEST( test_fault_lookup_mode_multiple_sequential_searches );
   RUN_TEST( test_fault_lookup_mode_mixed_formats_sequential );
   RUN_TEST( test_fault_lookup_mode_all_modes_exhaustive );

   // IP whitelist tests
   RUN_TEST( test_ipwhitelist_empty_is_deny_all );
   RUN_TEST( test_ipwhitelist_is_deny_all_true );
   RUN_TEST( test_ipwhitelist_is_deny_all_false );
   RUN_TEST( test_ipwhitelist_single_bare_ip );
   RUN_TEST( test_ipwhitelist_single_cidr_24 );
   RUN_TEST( test_ipwhitelist_host_bits_normalized );
   RUN_TEST( test_ipwhitelist_mixed_list_with_whitespace );
   RUN_TEST( test_ipwhitelist_prefix_zero_matches_all );
   RUN_TEST( test_ipwhitelist_prefix_32_explicit_vs_bare );
   RUN_TEST( test_ipwhitelist_overflow_rejected );
   RUN_TEST( test_ipwhitelist_malformed_trailing_comma );
   RUN_TEST( test_ipwhitelist_malformed_empty_middle_token );
   RUN_TEST( test_ipwhitelist_malformed_non_numeric_prefix );
   RUN_TEST( test_ipwhitelist_malformed_prefix_33 );
   RUN_TEST( test_ipwhitelist_malformed_negative_prefix );
   RUN_TEST( test_ipwhitelist_malformed_bad_octet );
   RUN_TEST( test_ipwhitelist_malformed_trailing_garbage );
   RUN_TEST( test_ipwhitelist_malformed_trailing_slash );
   RUN_TEST( test_ipwhitelist_malformed_double_slash );
   RUN_TEST( test_ipwhitelist_matcher_slash_32 );
   RUN_TEST( test_ipwhitelist_matcher_slash_24_boundary );
   RUN_TEST( test_ipwhitelist_matcher_multiple_entries );
   RUN_TEST( test_ipwhitelist_malformed_results_in_deny_all );
   RUN_TEST( test_ipwhitelist_null_input_is_deny_all );
   RUN_TEST( test_ipwhitelist_init_resets_singleton );
   // blacklist & combined whitelist+blacklist
   RUN_TEST( test_ipwhitelist_block_whitelisted_ip_excluded );
   RUN_TEST( test_ipwhitelist_block_non_whitelisted_ip_stays_false );
   RUN_TEST( test_ipwhitelist_block_duplicate_is_noop );
   RUN_TEST( test_ipwhitelist_block_invalid_inaddr_any );
   RUN_TEST( test_ipwhitelist_block_invalid_broadcast );
   RUN_TEST( test_ipwhitelist_block_one_from_subnet );
   RUN_TEST( test_ipwhitelist_block_multiple_from_subnet );
   RUN_TEST( test_ipwhitelist_block_subnet_boundary_ips );
   RUN_TEST( test_ipwhitelist_clear_resets_to_deny_all );
   RUN_TEST( test_ipwhitelist_clear_resets_blacklist );
   RUN_TEST( test_ipwhitelist_clear_twice_is_safe );
   RUN_TEST( test_ipwhitelist_init_does_not_reset_blacklist );
   RUN_TEST( test_ipwhitelist_block_forces_growth );
   RUN_TEST( test_ipwhitelist_block_with_allow_all_whitelist );
   // is_deny_all() blacklist-shadowing
   RUN_TEST( test_ipwhitelist_is_deny_all_when_only_host_blocked );
   RUN_TEST( test_ipwhitelist_is_deny_all_allow_all_with_blocked_ips );
   RUN_TEST( test_ipwhitelist_is_deny_all_subnet_fully_shadowed );
   RUN_TEST( test_ipwhitelist_is_deny_all_subnet_partially_shadowed );
   RUN_TEST( test_ipwhitelist_is_deny_all_multi_entry_all_shadowed );
   RUN_TEST( test_ipwhitelist_is_deny_all_multi_entry_one_live );
   // additional clear() coverage
   RUN_TEST( test_ipwhitelist_clear_allows_reblock );
   RUN_TEST( test_ipwhitelist_clear_then_reblock_is_deny_all );

   // FSM state machine tests
   RUN_TEST( test_fsm_kickoff_rejects_null_rqbuf );
   RUN_TEST( test_fsm_kickoff_rejects_null_peer_addr );
   RUN_TEST( test_fsm_kickoff_rejects_null_cfg );
   RUN_TEST( test_fsm_kickoff_rejects_null_fault );
   RUN_TEST( test_fsm_kickoff_rejects_zero_rqsz );
   RUN_TEST( test_fsm_kickoff_unparseable_request_returns_protocol_err );
   RUN_TEST( test_fsm_kickoff_rrq_timeout_returns_fine );
   RUN_TEST( test_fsm_kickoff_wrq_timeout_returns_fine );
   RUN_TEST( test_fsm_kickoff_rrq_file_not_found_fault_returns_fine );
   RUN_TEST( test_fsm_kickoff_wrq_access_violation_returns_fine );
   RUN_TEST( test_fsm_kickoff_rrq_access_violation_fault_returns_fine );
   RUN_TEST( test_fsm_kickoff_file_not_found_returns_file_err );
   RUN_TEST( test_fsm_kickoff_wrq_disabled_returns_wrq_disabled );
   RUN_TEST( test_fsm_kickoff_wrq_disk_check_fails_returns_disk_check );
   RUN_TEST( test_fsm_kickoff_wrq_file_creation_fails_returns_file_err );
   RUN_TEST( test_fsm_kickoff_socket_creation_fails_returns_socket_err );
   RUN_TEST( test_fsm_kickoff_set_recv_timeout_fails_returns_setsockopt_err );
   RUN_TEST( test_fsm_clean_exit_cleans_resources );
   RUN_TEST( test_fsm_clean_exit_closes_file_and_socket );
   RUN_TEST( test_fsm_kickoff_sets_transfer_mode_octet );
   RUN_TEST( test_fsm_kickoff_sets_transfer_mode_netascii );
   RUN_TEST( test_fsm_kickoff_wrq_bytes_written_null_handled );
   RUN_TEST( test_fsm_kickoff_wrq_with_session_budget );
   RUN_TEST( test_fsm_kickoff_rrq_fault_none );
   RUN_TEST( test_fsm_kickoff_wrq_fault_none );

   // sequence file loading and advancement
   RUN_TEST( test_seq_load_valid_single_entry_defaults );
   RUN_TEST( test_seq_load_valid_multiple_entries_with_params );
   RUN_TEST( test_seq_load_valid_comments_and_blanks );
   RUN_TEST( test_seq_load_valid_case_insensitive_mode );
   RUN_TEST( test_seq_load_valid_short_mode_names );
   RUN_TEST( test_seq_load_valid_field_order_doesnt_matter );
   RUN_TEST( test_seq_load_nonexistent_file_returns_error );
   RUN_TEST( test_seq_load_empty_file_returns_error );
   RUN_TEST( test_seq_load_unknown_fault_mode_returns_error );
   RUN_TEST( test_seq_load_invalid_param_non_numeric_returns_error );
   RUN_TEST( test_seq_load_invalid_count_zero_returns_error );
   RUN_TEST( test_seq_load_invalid_count_non_numeric_returns_error );
   RUN_TEST( test_seq_load_missing_required_mode_field_returns_error );
   RUN_TEST( test_seq_load_unknown_key_returns_error );
   RUN_TEST( test_seq_load_partial_file_on_first_error );
   RUN_TEST( test_seq_advance_increments_sessions_in_step );
   RUN_TEST( test_seq_advance_transitions_to_next_entry );
   RUN_TEST( test_seq_advance_returns_false_when_exhausted );
   RUN_TEST( test_seq_advance_updates_param_on_transition );
   RUN_TEST( test_seq_advance_multi_session_entries );
   RUN_TEST( test_seq_free_zeros_struct );
   RUN_TEST( test_seq_integration_real_file_good );
   RUN_TEST( test_seq_integration_real_file_bad_mode );
   RUN_TEST( test_seq_load_inline_comment_makes_empty_line );
   RUN_TEST( test_seq_load_field_with_inline_comment );
   RUN_TEST( test_seq_load_various_param_values );
   RUN_TEST( test_seq_load_various_count_values );
   RUN_TEST( test_seq_advance_single_session_entry );
   RUN_TEST( test_seq_advance_large_count_entry );
   RUN_TEST( test_seq_load_only_comments_and_blanks );
   RUN_TEST( test_seq_load_trailing_spaces_before_comment );
   RUN_TEST( test_seq_load_all_whitespace_line_mixed );
   RUN_TEST( test_seq_advance_exact_boundary );
   RUN_TEST( test_seq_load_count_boundary_values );
   RUN_TEST( test_seq_advance_multiple_transitions );
   RUN_TEST( test_seq_load_comment_only_line_with_tabs );
   RUN_TEST( test_seq_load_spaces_then_comment_then_spaces );

   return UNITY_END();
}
