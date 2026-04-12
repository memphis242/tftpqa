/**
 * @file test_tftp_err.c
 * @brief Unit tests for tftp_err module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_err.h"

#include <string.h>

void test_err_str_returns_non_null_for_all_codes(void)
{
   for ( int i = 0; i < TFTP_ERR_COUNT; i++ )
   {
      const char *s = tftp_err_str( (enum TFTP_Err)i );
      TEST_ASSERT_NOT_NULL( s );
      TEST_ASSERT_TRUE( strlen(s) > 0 );
   }
}

void test_err_str_none_is_no_error(void)
{
   const char *s = tftp_err_str( TFTP_ERR_NONE );
   TEST_ASSERT_EQUAL_STRING( "No error", s );
}
