/**
 * @file test_tftp_log.c
 * @brief Unit tests for tftp_log module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_log.h"

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_log_level_str_returns_expected_names(void);

void test_log_level_str_returns_expected_names(void)
{
   TEST_ASSERT_EQUAL_STRING( "TRACE", tftp_log_level_str( TFTP_LOG_TRACE ) );
   TEST_ASSERT_EQUAL_STRING( "DEBUG", tftp_log_level_str( TFTP_LOG_DEBUG ) );
   TEST_ASSERT_EQUAL_STRING( "INFO",  tftp_log_level_str( TFTP_LOG_INFO ) );
   TEST_ASSERT_EQUAL_STRING( "WARN",  tftp_log_level_str( TFTP_LOG_WARN ) );
   TEST_ASSERT_EQUAL_STRING( "ERROR", tftp_log_level_str( TFTP_LOG_ERR ) );
   TEST_ASSERT_EQUAL_STRING( "FATAL", tftp_log_level_str( TFTP_LOG_FATAL ) );
}
