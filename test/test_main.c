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

// tftp_err
extern void test_err_str_returns_non_null_for_all_codes(void);
extern void test_err_str_none_is_no_error(void);

// tftp_log
extern void test_log_level_str_returns_expected_names(void);

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

// tftptest_ctrl
extern void test_ctrl_set_fault_and_get(void);
extern void test_ctrl_set_fault_with_param(void);
extern void test_ctrl_unknown_command(void);
extern void test_ctrl_unknown_fault_mode(void);
extern void test_ctrl_whitelist_rejects_disallowed_mode(void);
extern void test_ctrl_set_fault_missing_mode_name(void);

// tftptest_faultmode
extern void test_fault_mode_names_all_present(void);
extern void test_fault_lookup_mode_full_name_match(void);
extern void test_fault_lookup_mode_short_name_match(void);
extern void test_fault_lookup_mode_case_insensitive(void);
extern void test_fault_lookup_mode_nonexistent_returns_negative_one(void);
extern void test_fault_lookup_mode_fault_none(void);
extern void test_fault_lookup_mode_fault_none_short(void);
extern void test_fault_lookup_mode_last_mode(void);
extern void test_fault_lookup_mode_last_mode_short(void);
extern void test_fault_lookup_mode_partial_match_fails(void);

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

// tftptest_seq
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

/*---------------------------------------------------------------------------
 * Main test runner
 *---------------------------------------------------------------------------*/

int main(void)
{
   UNITY_BEGIN();

   // tftp_err
   RUN_TEST( test_err_str_returns_non_null_for_all_codes );
   RUN_TEST( test_err_str_none_is_no_error );

   // tftp_log
   RUN_TEST( test_log_level_str_returns_expected_names );

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

   // packet edge cases
   RUN_TEST( test_pkt_reject_filename_too_long );
   RUN_TEST( test_pkt_parse_data_rejects_wrong_opcode );
   RUN_TEST( test_pkt_build_error_returns_zero_when_buffer_too_small );
   RUN_TEST( test_pkt_build_error_succeeds_with_adequate_buffer );
   RUN_TEST( test_pkt_valid_rrq_octet_mixed_case );
   RUN_TEST( test_pkt_ack_block_zero );

   // chroot_and_drop
   RUN_TEST( test_chroot_and_drop_non_root_succeeds );
   RUN_TEST( test_chroot_and_drop_bad_dir_fails );

   // fault mode names and lookup
   RUN_TEST( test_fault_mode_names_all_present );
   RUN_TEST( test_fault_lookup_mode_full_name_match );
   RUN_TEST( test_fault_lookup_mode_short_name_match );
   RUN_TEST( test_fault_lookup_mode_case_insensitive );
   RUN_TEST( test_fault_lookup_mode_nonexistent_returns_negative_one );
   RUN_TEST( test_fault_lookup_mode_fault_none );
   RUN_TEST( test_fault_lookup_mode_fault_none_short );
   RUN_TEST( test_fault_lookup_mode_last_mode );
   RUN_TEST( test_fault_lookup_mode_last_mode_short );
   RUN_TEST( test_fault_lookup_mode_partial_match_fails );

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

   return UNITY_END();
}
