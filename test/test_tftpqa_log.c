/**
 * @file test_tftp_log.c
 * @brief Unit tests for tftp_log module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftpqa_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_log_init_without_syslog_sets_min_level(void);
void test_log_init_with_syslog_opens_syslog(void);
void test_log_message_below_min_level_is_suppressed(void);
void test_log_message_at_min_level_is_emitted(void);
void test_log_message_above_min_level_is_emitted(void);
void test_log_shutdown_without_syslog_is_safe(void);
void test_log_shutdown_with_syslog_closes_syslog(void);
void test_log_message_with_syslog_enabled(void);

/*---------------------------------------------------------------------------
 * Helper: capture stderr output from a block of code.
 *
 * Usage:
 *   int pipefd[2];
 *   int saved = stderr_capture_begin(pipefd);
 *   ... code that writes to stderr ...
 *   ssize_t n = stderr_capture_end(pipefd, saved, buf, sizeof buf);
 *
 * Key ordering: restore stderr FIRST (so fd 2 no longer holds the pipe
 * write end), THEN close the original write fd, THEN read.  Otherwise
 * read() blocks because the write end is still open via fd 2.
 *---------------------------------------------------------------------------*/

static int stderr_capture_begin(int pipefd[2])
{
   TEST_ASSERT_EQUAL_INT( 0, pipe( pipefd ) );
   int saved = dup( STDERR_FILENO );
   TEST_ASSERT_NOT_EQUAL( -1, saved );
   dup2( pipefd[1], STDERR_FILENO );
   return saved;
}

static ssize_t stderr_capture_end(int pipefd[2], int saved,
                                  char *buf, size_t bufsz)
{
   fflush( stderr );
   /* Restore stderr BEFORE closing the pipe write end — this drops the
      last reference to the write side so the subsequent read sees EOF. */
   dup2( saved, STDERR_FILENO );
   close( saved );
   close( pipefd[1] );

   ssize_t n = read( pipefd[0], buf, bufsz - 1 );
   close( pipefd[0] );
   if ( n > 0 ) buf[n] = '\0';
   return n;
}

static int stdout_capture_begin(int pipefd[2])
{
   TEST_ASSERT_EQUAL_INT( 0, pipe( pipefd ) );
   int saved = dup( STDOUT_FILENO );
   TEST_ASSERT_NOT_EQUAL( -1, saved );
   dup2( pipefd[1], STDOUT_FILENO );
   return saved;
}

static ssize_t stdout_capture_end(int pipefd[2], int saved,
                                  char *buf, size_t bufsz)
{
   fflush( stdout );
   dup2( saved, STDOUT_FILENO );
   close( saved );
   close( pipefd[1] );

   ssize_t n = read( pipefd[0], buf, bufsz - 1 );
   close( pipefd[0] );
   if ( n > 0 ) buf[n] = '\0';
   return n;
}

/*---------------------------------------------------------------------------
 * tftp_log tests
 *---------------------------------------------------------------------------*/

void test_log_init_without_syslog_sets_min_level(void)
{
   tftpqa_log_init( false, TFTP_LOG_WARN );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftpqa_log( TFTP_LOG_DEBUG, __func__, "should be suppressed" );

   char buf[512];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   // DEBUG < WARN, so nothing should have been written
   TEST_ASSERT_EQUAL_INT( 0, (int)n );

   tftpqa_log_shutdown();
}

void test_log_init_with_syslog_opens_syslog(void)
{
   tftpqa_log_init( true, TFTP_LOG_TRACE );
   tftpqa_log( TFTP_LOG_INFO, NULL, "test syslog integration" );
   tftpqa_log_shutdown();
}

void test_log_message_below_min_level_is_suppressed(void)
{
   tftpqa_log_init( false, TFTP_LOG_ERR );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftpqa_log( TFTP_LOG_TRACE, __func__, "trace msg" );
   tftpqa_log( TFTP_LOG_DEBUG, __func__, "debug msg" );
   tftpqa_log( TFTP_LOG_INFO, NULL, "info msg" );
   tftpqa_log( TFTP_LOG_WARN, __func__, "warn msg" );

   char buf[512];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_EQUAL_INT( 0, (int)n );

   tftpqa_log_shutdown();
}

void test_log_message_at_min_level_is_emitted(void)
{
   tftpqa_log_init( false, TFTP_LOG_TRACE );

   // Capture both stdout and stderr
   int stdout_pipefd[2];
   int stdout_saved = stdout_capture_begin( stdout_pipefd );

   int stderr_pipefd[2];
   int stderr_saved = stderr_capture_begin( stderr_pipefd );

   // Log at TRACE level (minimum, should go to stdout since level <= TFTP_LOG_WARN)
   tftpqa_log( TFTP_LOG_TRACE, __func__, "trace at minimum level" );

   char stdout_buf[1024];
   char stderr_buf[1024];
   ssize_t stdout_n = stdout_capture_end( stdout_pipefd, stdout_saved, stdout_buf, sizeof stdout_buf );
   ssize_t stderr_n = stderr_capture_end( stderr_pipefd, stderr_saved, stderr_buf, sizeof stderr_buf );

   // TRACE should be in stdout (level <= TFTP_LOG_WARN)
   TEST_ASSERT_GREATER_THAN( 0, (int)stdout_n );
   TEST_ASSERT_NOT_NULL( strstr( stdout_buf, "TRACE" ) );
   TEST_ASSERT_NOT_NULL( strstr( stdout_buf, "trace at minimum level" ) );
   // Should not be in stderr
   TEST_ASSERT_EQUAL_INT( 0, (int)stderr_n );

   tftpqa_log_shutdown();
}

void test_log_message_above_min_level_is_emitted(void)
{
   tftpqa_log_init( false, TFTP_LOG_INFO );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftpqa_log( TFTP_LOG_FATAL, __func__, "fatal test message %d", 42 );

   char buf[1024];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_GREATER_THAN( 0, (int)n );
   TEST_ASSERT_NOT_NULL( strstr( buf, "FATAL" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "fatal test message 42" ) );

   tftpqa_log_shutdown();
}

void test_log_shutdown_without_syslog_is_safe(void)
{
   tftpqa_log_init( false, TFTP_LOG_INFO );
   tftpqa_log_shutdown();
   // Second shutdown with syslog already closed — should not crash
   tftpqa_log_shutdown();
}

void test_log_shutdown_with_syslog_closes_syslog(void)
{
   tftpqa_log_init( true, TFTP_LOG_DEBUG );
   tftpqa_log( TFTP_LOG_INFO, NULL, "pre-shutdown syslog message" );
   tftpqa_log_shutdown();
   // Second shutdown — g_syslog_open should now be false
   tftpqa_log_shutdown();
}

void test_log_message_with_syslog_enabled(void)
{
   tftpqa_log_init( true, TFTP_LOG_TRACE );

   // Capture both stdout and stderr
   int stdout_pipefd[2];
   int stdout_saved = stdout_capture_begin( stdout_pipefd );

   int stderr_pipefd[2];
   int stderr_saved = stderr_capture_begin( stderr_pipefd );

   // Log at all levels
   tftpqa_log( TFTP_LOG_TRACE, __func__, "trace with syslog" );
   tftpqa_log( TFTP_LOG_DEBUG, __func__, "debug with syslog" );
   tftpqa_log( TFTP_LOG_INFO, NULL, "info with syslog" );
   tftpqa_log( TFTP_LOG_WARN, __func__, "warn with syslog" );
   tftpqa_log( TFTP_LOG_ERR, __func__, "error with syslog" );
   tftpqa_log( TFTP_LOG_FATAL, __func__, "fatal with syslog" );

   char stdout_buf[4096];
   char stderr_buf[4096];
   ssize_t stdout_n = stdout_capture_end( stdout_pipefd, stdout_saved, stdout_buf, sizeof stdout_buf );
   ssize_t stderr_n = stderr_capture_end( stderr_pipefd, stderr_saved, stderr_buf, sizeof stderr_buf );

   // Low-level messages (TRACE/DEBUG/INFO) go to stdout (level < TFTP_LOG_WARN)
   TEST_ASSERT_GREATER_THAN( 0, (int)stdout_n );
   TEST_ASSERT_NOT_NULL( strstr( stdout_buf, "TRACE" ) );
   TEST_ASSERT_NOT_NULL( strstr( stdout_buf, "DEBUG" ) );
   TEST_ASSERT_NOT_NULL( strstr( stdout_buf, "INFO" ) );

   // High-level messages (WARN/ERR/FATAL) go to stderr (level >= TFTP_LOG_WARN)
   TEST_ASSERT_GREATER_THAN( 0, (int)stderr_n );
   TEST_ASSERT_NOT_NULL( strstr( stderr_buf, "WARN" ) );
   TEST_ASSERT_NOT_NULL( strstr( stderr_buf, "ERROR" ) );
   TEST_ASSERT_NOT_NULL( strstr( stderr_buf, "FATAL" ) );

   tftpqa_log_shutdown();
}
