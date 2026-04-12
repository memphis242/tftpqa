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

