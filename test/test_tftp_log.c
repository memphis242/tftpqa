/**
 * @file test_tftp_log.c
 * @brief Unit tests for tftp_log module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_log.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_log_level_str_returns_expected_names(void);
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

/*---------------------------------------------------------------------------
 * tftp_log tests
 *---------------------------------------------------------------------------*/

void test_log_level_str_returns_expected_names(void)
{
   TEST_ASSERT_EQUAL_STRING( "TRACE", tftp_log_level_str( TFTP_LOG_TRACE ) );
   TEST_ASSERT_EQUAL_STRING( "DEBUG", tftp_log_level_str( TFTP_LOG_DEBUG ) );
   TEST_ASSERT_EQUAL_STRING( "INFO",  tftp_log_level_str( TFTP_LOG_INFO ) );
   TEST_ASSERT_EQUAL_STRING( "WARN",  tftp_log_level_str( TFTP_LOG_WARN ) );
   TEST_ASSERT_EQUAL_STRING( "ERROR", tftp_log_level_str( TFTP_LOG_ERR ) );
   TEST_ASSERT_EQUAL_STRING( "FATAL", tftp_log_level_str( TFTP_LOG_FATAL ) );
}

void test_log_init_without_syslog_sets_min_level(void)
{
   tftp_log_init( false, TFTP_LOG_WARN );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftp_log( TFTP_LOG_DEBUG, "should be suppressed" );

   char buf[512];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   // DEBUG < WARN, so nothing should have been written
   TEST_ASSERT_EQUAL_INT( 0, (int)n );

   tftp_log_shutdown();
}

void test_log_init_with_syslog_opens_syslog(void)
{
   tftp_log_init( true, TFTP_LOG_TRACE );
   tftp_log( TFTP_LOG_INFO, "test syslog integration" );
   tftp_log_shutdown();
}

void test_log_message_below_min_level_is_suppressed(void)
{
   tftp_log_init( false, TFTP_LOG_ERR );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftp_log( TFTP_LOG_TRACE, "trace msg" );
   tftp_log( TFTP_LOG_DEBUG, "debug msg" );
   tftp_log( TFTP_LOG_INFO, "info msg" );
   tftp_log( TFTP_LOG_WARN, "warn msg" );

   char buf[512];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_EQUAL_INT( 0, (int)n );

   tftp_log_shutdown();
}

void test_log_message_at_min_level_is_emitted(void)
{
   tftp_log_init( false, TFTP_LOG_WARN );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftp_log( TFTP_LOG_WARN, "expected warning message" );

   char buf[1024];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_GREATER_THAN( 0, (int)n );
   TEST_ASSERT_NOT_NULL( strstr( buf, "WARN" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "expected warning message" ) );

   tftp_log_shutdown();
}

void test_log_message_above_min_level_is_emitted(void)
{
   tftp_log_init( false, TFTP_LOG_INFO );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftp_log( TFTP_LOG_FATAL, "fatal test message %d", 42 );

   char buf[1024];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_GREATER_THAN( 0, (int)n );
   TEST_ASSERT_NOT_NULL( strstr( buf, "FATAL" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "fatal test message 42" ) );

   tftp_log_shutdown();
}

void test_log_shutdown_without_syslog_is_safe(void)
{
   tftp_log_init( false, TFTP_LOG_INFO );
   tftp_log_shutdown();
   // Second shutdown with syslog already closed — should not crash
   tftp_log_shutdown();
}

void test_log_shutdown_with_syslog_closes_syslog(void)
{
   tftp_log_init( true, TFTP_LOG_DEBUG );
   tftp_log( TFTP_LOG_INFO, "pre-shutdown syslog message" );
   tftp_log_shutdown();
   // Second shutdown — g_syslog_open should now be false
   tftp_log_shutdown();
}

void test_log_message_with_syslog_enabled(void)
{
   tftp_log_init( true, TFTP_LOG_TRACE );

   int pipefd[2];
   int saved = stderr_capture_begin( pipefd );

   tftp_log( TFTP_LOG_TRACE, "trace with syslog" );
   tftp_log( TFTP_LOG_DEBUG, "debug with syslog" );
   tftp_log( TFTP_LOG_INFO, "info with syslog" );
   tftp_log( TFTP_LOG_WARN, "warn with syslog" );
   tftp_log( TFTP_LOG_ERR, "error with syslog" );
   tftp_log( TFTP_LOG_FATAL, "fatal with syslog" );

   char buf[4096];
   ssize_t n = stderr_capture_end( pipefd, saved, buf, sizeof buf );

   TEST_ASSERT_GREATER_THAN( 0, (int)n );

   TEST_ASSERT_NOT_NULL( strstr( buf, "TRACE" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "DEBUG" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "INFO" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "WARN" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "ERROR" ) );
   TEST_ASSERT_NOT_NULL( strstr( buf, "FATAL" ) );

   tftp_log_shutdown();
}
