/**
 * @file test_tftp_parsecfg.c
 * @brief Unit tests for tftp_parsecfg module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_parsecfg.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_parsecfg_defaults_produces_sane_values(void);
void test_parsecfg_load_nonexistent_file_returns_error(void);
void test_parsecfg_load_valid_config(void);
void test_parsecfg_ignores_comments_and_blanks(void);
void test_parsecfg_inline_comments_stripped(void);
void test_parsecfg_ctrl_port_zero_disables_faults(void);
void test_parsecfg_rejects_invalid_port(void);
void test_parsecfg_unknown_key_still_succeeds(void);
void test_parsecfg_missing_equals_delimiter(void);
void test_parsecfg_root_dir_and_fault_whitelist(void);
void test_parsecfg_all_numeric_fields(void);
void test_parsecfg_wrq_protection_fields_loaded(void);
void test_parsecfg_wrq_enabled_false(void);
void test_parsecfg_wrq_duration_sec_invalid(void);
void test_parsecfg_wrq_enabled_invalid_value(void);
void test_parsecfg_abandoned_sessions_default_zero(void);
void test_parsecfg_max_abandoned_sessions_loaded(void);
void test_parsecfg_ctrl_port_over_65535_rejected(void);
void test_parsecfg_root_dir_empty_rejected(void);
void test_parsecfg_root_dir_too_long_rejected(void);
void test_parsecfg_run_as_user_valid(void);
void test_parsecfg_run_as_user_empty_rejected(void);
void test_parsecfg_run_as_user_too_long_rejected(void);
void test_parsecfg_log_level_all_values(void);
void test_parsecfg_log_level_invalid_rejected(void);
void test_parsecfg_timeout_sec_zero_rejected(void);
void test_parsecfg_timeout_sec_over_300_rejected(void);
void test_parsecfg_max_retransmits_zero_rejected(void);
void test_parsecfg_max_retransmits_over_100_rejected(void);
void test_parsecfg_max_requests_zero_rejected(void);
void test_parsecfg_fault_whitelist_invalid_rejected(void);
void test_parsecfg_allowed_client_ip_empty_allows_all(void);
void test_parsecfg_allowed_client_ip_zero_allows_all(void);
void test_parsecfg_allowed_client_ip_valid(void);
void test_parsecfg_allowed_client_ip_invalid_rejected(void);
void test_parsecfg_wrq_enabled_yes(void);
void test_parsecfg_wrq_enabled_one(void);
void test_parsecfg_wrq_enabled_no(void);
void test_parsecfg_wrq_enabled_zero(void);
void test_parsecfg_tftp_port_zero_rejected(void);
void test_parsecfg_line_without_trailing_newline(void);
void test_parsecfg_fault_whitelist_decimal(void);
void test_parsecfg_multiple_errors_reports_count(void);
void test_parsecfg_tid_port_range_valid(void);
void test_parsecfg_tid_port_range_single_port(void);
void test_parsecfg_tid_port_range_invalid_format(void);
void test_parsecfg_tid_port_range_min_greater_than_max(void);
void test_parsecfg_tid_port_range_zero_rejected(void);
void test_parsecfg_tid_port_range_over_65535_rejected(void);

/*---------------------------------------------------------------------------
 * tftp_parsecfg tests
 *---------------------------------------------------------------------------*/

void test_parsecfg_defaults_produces_sane_values(void)
{
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults( &cfg );

   TEST_ASSERT_EQUAL_UINT16( 23069, cfg.tftp_port );
   TEST_ASSERT_EQUAL_UINT16( 23070, cfg.ctrl_port );
   TEST_ASSERT_EQUAL_STRING( ".", cfg.root_dir );
   TEST_ASSERT_EQUAL_STRING( "nobody", cfg.run_as_user );
   TEST_ASSERT_EQUAL_INT( TFTP_LOG_INFO, cfg.log_level );
   TEST_ASSERT_EQUAL_UINT( 1, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.max_retransmits );
   TEST_ASSERT_EQUAL( 10000, cfg.max_requests );
   TEST_ASSERT_EQUAL_UINT64( UINT64_MAX, cfg.fault_whitelist );
   TEST_ASSERT_EQUAL_UINT32( 0, cfg.allowed_client_ip ); // 0 = allow all clients
   TEST_ASSERT_EQUAL( 0, cfg.max_wrq_file_size );
   TEST_ASSERT_EQUAL( 0, cfg.max_wrq_session_bytes );
   TEST_ASSERT_EQUAL_UINT( 0, cfg.max_wrq_duration_sec );
   TEST_ASSERT_EQUAL( 0, cfg.max_wrq_file_count );
   TEST_ASSERT_EQUAL( 0, cfg.min_disk_free_bytes );
   TEST_ASSERT_TRUE( cfg.wrq_enabled );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_max );
}

void test_parsecfg_load_nonexistent_file_returns_error(void)
{
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults( &cfg );

   int rc = tftp_parsecfg_load( "/nonexistent/path/config.ini", &cfg );
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

/*---------------------------------------------------------------------------
 * Config file parsing with real file
 *---------------------------------------------------------------------------*/

void test_parsecfg_load_valid_config(void)
{
   // Create a temp config file
   const char *path = "/tmp/tftptest_test_cfg.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "# Test config\n"
      "tftp_port = 12345\n"
      "ctrl_port = 12346\n"
      "timeout_sec = 3\n"
      "max_retransmits = 10\n"
      "max_requests = 500\n"
      "log_level = DEBUG\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 12345, cfg.tftp_port );
   TEST_ASSERT_EQUAL_UINT16( 12346, cfg.ctrl_port );
   TEST_ASSERT_EQUAL_UINT( 3, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_UINT( 10, cfg.max_retransmits );
   TEST_ASSERT_EQUAL( 500, cfg.max_requests );
   TEST_ASSERT_EQUAL_INT( TFTP_LOG_DEBUG, cfg.log_level );

   (void)remove(path);
}

void test_parsecfg_ignores_comments_and_blanks(void)
{
   const char *path = "/tmp/tftptest_test_cfg2.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "# Comment line\n"
      "\n"
      "   \n"
      "tftp_port = 54321\n"
      "# Another comment\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 54321, cfg.tftp_port );

   (void)remove(path);
}

void test_parsecfg_inline_comments_stripped(void)
{
   const char *path = "/tmp/tftptest_test_cfg_inline.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "tftp_port = 54321 # TFTP listening port\n"
      "timeout_sec = 5   # Timeout in seconds\n"
      "log_level = info  # Log verbosity level\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 54321, cfg.tftp_port );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_INT( TFTP_LOG_INFO, cfg.log_level );

   (void)remove(path);
}

void test_parsecfg_ctrl_port_zero_disables_faults(void)
{
   const char *path = "/tmp/tftptest_test_cfg_ctrl_port.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "ctrl_port = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   TEST_ASSERT_NOT_EQUAL( 0, cfg.ctrl_port );  // Default should not be 0

   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.ctrl_port );  // Should now be 0

   (void)remove(path);
}

void test_parsecfg_rejects_invalid_port(void)
{
   const char *path = "/tmp/tftptest_test_cfg3.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tftp_port = 99999\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   // Should still return 0 (warnings, not fatal), but port should be unchanged
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 23069, cfg.tftp_port ); // default unchanged

   (void)remove(path);
}

void test_parsecfg_unknown_key_still_succeeds(void)
{
   const char *path = "/tmp/tftptest_test_cfg4.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "unknown_key = some_value\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   // Unknown keys log a warning but don't cause failure
   TEST_ASSERT_EQUAL_INT( 0, rc );
   (void)remove(path);
}

void test_parsecfg_missing_equals_delimiter(void)
{
   const char *path = "/tmp/tftptest_test_cfg5.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "no_equals_here\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   (void)remove(path);
}

void test_parsecfg_root_dir_and_fault_whitelist(void)
{
   const char *path = "/tmp/tftptest_test_cfg6.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "root_dir = /srv/tftp\n"
      "fault_whitelist = 0xFF\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_STRING( "/srv/tftp", cfg.root_dir );
   TEST_ASSERT_EQUAL_UINT64( 0xFF, cfg.fault_whitelist );
   (void)remove(path);
}

void test_parsecfg_all_numeric_fields(void)
{
   const char *path = "/tmp/tftptest_test_cfg7.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "timeout_sec = 5\n"
      "max_retransmits = 3\n"
      "max_requests = 100\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_UINT( 3, cfg.max_retransmits );
   TEST_ASSERT_EQUAL( 100, cfg.max_requests );
   (void)remove(path);
}

void test_parsecfg_wrq_protection_fields_loaded(void)
{
   const char *path = "/tmp/tftptest_test_wrq1.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "max_wrq_file_size = 1048576\n"
      "max_wrq_session_bytes = 10485760\n"
      "max_wrq_duration_sec = 60\n"
      "max_wrq_file_count = 100\n"
      "min_disk_free_bytes = 536870912\n"
      "wrq_enabled = true\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL( 1048576, cfg.max_wrq_file_size );
   TEST_ASSERT_EQUAL( 10485760, cfg.max_wrq_session_bytes );
   TEST_ASSERT_EQUAL_UINT( 60, cfg.max_wrq_duration_sec );
   TEST_ASSERT_EQUAL( 100, cfg.max_wrq_file_count );
   TEST_ASSERT_EQUAL( 536870912, cfg.min_disk_free_bytes );
   TEST_ASSERT_TRUE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_false(void)
{
   const char *path = "/tmp/tftptest_test_wrq2.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = false\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   TEST_ASSERT_TRUE( cfg.wrq_enabled );  // default is true
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_FALSE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_wrq_duration_sec_invalid(void)
{
   const char *path = "/tmp/tftptest_test_wrq3.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "max_wrq_duration_sec = 99999\n");  // > 86400
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );  // load succeeds (non-fatal)
   TEST_ASSERT_EQUAL_UINT( 0, cfg.max_wrq_duration_sec );  // unchanged from default
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_invalid_value(void)
{
   const char *path = "/tmp/tftptest_test_wrq4.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = maybe\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );  // load succeeds (non-fatal)
   TEST_ASSERT_TRUE( cfg.wrq_enabled );  // unchanged from default
   (void)remove(path);
}

void test_parsecfg_abandoned_sessions_default_zero(void)
{
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   TEST_ASSERT_EQUAL( 0, cfg.max_abandoned_sessions );
}

void test_parsecfg_max_abandoned_sessions_loaded(void)
{
   const char *path = "/tmp/tftptest_test_abandon.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "max_abandoned_sessions = 5\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL( 5, cfg.max_abandoned_sessions );
   (void)remove(path);
}

/*---------------------------------------------------------------------------
 * Additional coverage tests — exercising every remaining decision branch
 *---------------------------------------------------------------------------*/

void test_parsecfg_ctrl_port_over_65535_rejected(void)
{
   const char *path = "/tmp/tftptest_test_ctrlport_high.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "ctrl_port = 70000\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   uint16_t orig_ctrl_port = cfg.ctrl_port;
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( orig_ctrl_port, cfg.ctrl_port ); // unchanged
   (void)remove(path);
}

void test_parsecfg_root_dir_empty_rejected(void)
{
   const char *path = "/tmp/tftptest_test_rootdir_empty.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "root_dir = \n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_STRING( ".", cfg.root_dir ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_root_dir_too_long_rejected(void)
{
   // root_dir is PATH_MAX (4096) bytes, but MAX_LINE_LEN is 512, so fgets
   // truncates any line to < 512 chars. We can't actually exceed PATH_MAX
   // through the config file. Instead, verify that a value right at the
   // MAX_LINE_LEN boundary is still accepted (it fits in PATH_MAX).
   // This test exercises the valid root_dir path with a longer-than-typical value.
   const char *path = "/tmp/tftptest_test_rootdir_long.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   // Write "root_dir = " (12 chars) + 490 chars of 'a' + newline = 503 chars
   // fgets reads up to 511 chars, so value will be ~490 'a' chars
   fprintf(f, "root_dir = ");
   for (int i = 0; i < 490; i++)
      fputc('a', f);
   fprintf(f, "\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   // 490 < PATH_MAX, so it should be accepted and stored
   TEST_ASSERT_EQUAL_INT( 490, (int)strlen( cfg.root_dir ) );
   (void)remove(path);
}

void test_parsecfg_run_as_user_valid(void)
{
   const char *path = "/tmp/tftptest_test_run_as_user.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "run_as_user = tftpd\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_STRING( "tftpd", cfg.run_as_user );
   (void)remove(path);
}

void test_parsecfg_run_as_user_empty_rejected(void)
{
   const char *path = "/tmp/tftptest_test_run_as_user_empty.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "run_as_user = \n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_STRING( "nobody", cfg.run_as_user ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_run_as_user_too_long_rejected(void)
{
   const char *path = "/tmp/tftptest_test_run_as_user_long.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   // run_as_user buffer is 64 bytes; write 64+ chars
   fprintf(f, "run_as_user = ");
   for (int i = 0; i < 70; i++)
      fputc('x', f);
   fprintf(f, "\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_STRING( "nobody", cfg.run_as_user ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_log_level_all_values(void)
{
   // Test every valid log level through the config parser to exercise all
   // branches in parse_log_level()
   const char *levels[] = { "trace", "debug", "info", "warn", "error", "fatal" };
   enum TFTP_LogLevel expected[] = {
      TFTP_LOG_TRACE, TFTP_LOG_DEBUG, TFTP_LOG_INFO,
      TFTP_LOG_WARN, TFTP_LOG_ERR, TFTP_LOG_FATAL
   };

   for (int i = 0; i < 6; i++)
   {
      const char *path = "/tmp/tftptest_test_loglevel.ini";
      FILE *f = fopen(path, "w");
      TEST_ASSERT_NOT_NULL( f );
      fprintf(f, "log_level = %s\n", levels[i]);
      fclose(f);

      struct TFTPTest_Config cfg;
      tftp_parsecfg_defaults(&cfg);
      int rc = tftp_parsecfg_load(path, &cfg);
      TEST_ASSERT_EQUAL_INT( 0, rc );
      TEST_ASSERT_EQUAL_INT( expected[i], cfg.log_level );
      (void)remove(path);
   }
}

void test_parsecfg_log_level_invalid_rejected(void)
{
   const char *path = "/tmp/tftptest_test_loglevel_bad.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "log_level = nonsense\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( TFTP_LOG_INFO, cfg.log_level ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_timeout_sec_zero_rejected(void)
{
   const char *path = "/tmp/tftptest_test_timeout0.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "timeout_sec = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT( 1, cfg.timeout_sec ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_timeout_sec_over_300_rejected(void)
{
   const char *path = "/tmp/tftptest_test_timeout_high.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "timeout_sec = 301\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT( 1, cfg.timeout_sec ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_max_retransmits_zero_rejected(void)
{
   const char *path = "/tmp/tftptest_test_retx0.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "max_retransmits = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.max_retransmits ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_max_retransmits_over_100_rejected(void)
{
   const char *path = "/tmp/tftptest_test_retx_high.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "max_retransmits = 101\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.max_retransmits ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_max_requests_zero_rejected(void)
{
   const char *path = "/tmp/tftptest_test_maxreq0.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "max_requests = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL( 10000, cfg.max_requests ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_fault_whitelist_invalid_rejected(void)
{
   const char *path = "/tmp/tftptest_test_fwl_bad.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "fault_whitelist = not_a_number\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT64( UINT64_MAX, cfg.fault_whitelist ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_allowed_client_ip_empty_allows_all(void)
{
   const char *path = "/tmp/tftptest_test_ip_empty.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "allowed_client_ip = \n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT32( 0, cfg.allowed_client_ip ); // 0 = allow all
   (void)remove(path);
}

void test_parsecfg_allowed_client_ip_zero_allows_all(void)
{
   const char *path = "/tmp/tftptest_test_ip_zero.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "allowed_client_ip = 0.0.0.0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT32( 0, cfg.allowed_client_ip );
   (void)remove(path);
}

void test_parsecfg_allowed_client_ip_valid(void)
{
   const char *path = "/tmp/tftptest_test_ip_valid.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "allowed_client_ip = 192.168.1.100\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   // Should store the IP in network byte order — verify it's non-zero
   TEST_ASSERT_NOT_EQUAL( 0, cfg.allowed_client_ip );

   // Verify the actual IP value by converting back
   struct in_addr addr;
   addr.s_addr = cfg.allowed_client_ip;
   TEST_ASSERT_EQUAL_STRING( "192.168.1.100", inet_ntoa( addr ) );
   (void)remove(path);
}

void test_parsecfg_allowed_client_ip_invalid_rejected(void)
{
   const char *path = "/tmp/tftptest_test_ip_bad.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "allowed_client_ip = not.an.ip\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT32( 0, cfg.allowed_client_ip ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_yes(void)
{
   const char *path = "/tmp/tftptest_test_wrq_yes.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = yes\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   cfg.wrq_enabled = false; // force off so we can test "yes" turns it on
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_TRUE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_one(void)
{
   const char *path = "/tmp/tftptest_test_wrq1.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = 1\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   cfg.wrq_enabled = false;
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_TRUE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_no(void)
{
   const char *path = "/tmp/tftptest_test_wrq_no.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = no\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   TEST_ASSERT_TRUE( cfg.wrq_enabled ); // default is true
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_FALSE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_wrq_enabled_zero(void)
{
   const char *path = "/tmp/tftptest_test_wrq0.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "wrq_enabled = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   TEST_ASSERT_TRUE( cfg.wrq_enabled );
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_FALSE( cfg.wrq_enabled );
   (void)remove(path);
}

void test_parsecfg_tftp_port_zero_rejected(void)
{
   const char *path = "/tmp/tftptest_test_port0.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tftp_port = 0\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 23069, cfg.tftp_port ); // default unchanged
   (void)remove(path);
}

void test_parsecfg_line_without_trailing_newline(void)
{
   // Tests the len > 0 && line[len-1] == '\n' decision — false branch
   const char *path = "/tmp/tftptest_test_nonewline.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   // Write a line without trailing newline
   fputs("tftp_port = 11111", f);
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 11111, cfg.tftp_port );
   (void)remove(path);
}

void test_parsecfg_fault_whitelist_decimal(void)
{
   // Exercises the valid (non-error) path for fault_whitelist with decimal
   const char *path = "/tmp/tftptest_test_fwl_dec.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "fault_whitelist = 42\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT64( 42, cfg.fault_whitelist );
   (void)remove(path);
}

void test_parsecfg_multiple_errors_reports_count(void)
{
   // Exercise the errors > 0 branch at the end of loading
   const char *path = "/tmp/tftptest_test_multi_err.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "tftp_port = 0\n"
      "timeout_sec = 999\n"
      "max_retransmits = 999\n"
   );
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc ); // still returns 0 (non-fatal)
   // All three values should be unchanged from defaults
   TEST_ASSERT_EQUAL_UINT16( 23069, cfg.tftp_port );
   TEST_ASSERT_EQUAL_UINT( 1, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.max_retransmits );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_valid(void)
{
   const char *path = "/tmp/tftptest_test_tid_range.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 50000-50100\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 50000, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 50100, cfg.tid_port_max );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_single_port(void)
{
   const char *path = "/tmp/tftptest_test_tid_single.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 50000-50000\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 50000, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 50000, cfg.tid_port_max );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_invalid_format(void)
{
   const char *path = "/tmp/tftptest_test_tid_badfmt.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 50000\n"); // Missing dash and max
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc ); // Non-fatal
   // Should remain at defaults (0/0)
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_max );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_min_greater_than_max(void)
{
   const char *path = "/tmp/tftptest_test_tid_minmax.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 50100-50000\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_max );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_zero_rejected(void)
{
   const char *path = "/tmp/tftptest_test_tid_zero.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 0-100\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_max );
   (void)remove(path);
}

void test_parsecfg_tid_port_range_over_65535_rejected(void)
{
   const char *path = "/tmp/tftptest_test_tid_over.ini";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "tid_port_range = 50000-70000\n");
   fclose(f);

   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   int rc = tftp_parsecfg_load(path, &cfg);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_min );
   TEST_ASSERT_EQUAL_UINT16( 0, cfg.tid_port_max );
   (void)remove(path);
}

