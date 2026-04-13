/**
 * @file test_tftptest_faultmode.c
 * @brief Unit tests for tftptest_faultmode module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftptest_faultmode.h"

#include <string.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_fault_mode_names_all_present(void);
void test_fault_lookup_mode_full_name_match(void);
void test_fault_lookup_mode_short_name_match(void);
void test_fault_lookup_mode_case_insensitive(void);
void test_fault_lookup_mode_nonexistent_returns_negative_one(void);
void test_fault_lookup_mode_fault_none(void);
void test_fault_lookup_mode_fault_none_short(void);
void test_fault_lookup_mode_last_mode(void);
void test_fault_lookup_mode_last_mode_short(void);
void test_fault_lookup_mode_partial_match_fails(void);

/*---------------------------------------------------------------------------
 * tftptest_faultmode tests
 *---------------------------------------------------------------------------*/

void test_fault_mode_names_all_present(void)
{
   // Verify all fault modes have names
   for ( int i = 0; i < FAULT_MODE_COUNT; i++ )
   {
      TEST_ASSERT_NOT_NULL( tftptest_fault_mode_names[i] );
      TEST_ASSERT_TRUE( strlen(tftptest_fault_mode_names[i]) > 0 );
      // All names should start with "FAULT_"
      TEST_ASSERT_TRUE( strncmp(tftptest_fault_mode_names[i], "FAULT_", 6) == 0 );
   }
}

void test_fault_lookup_mode_full_name_match(void)
{
   // Test full name lookup (e.g., "FAULT_RRQ_TIMEOUT")
   int idx = tftptest_fault_lookup_mode("FAULT_RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("FAULT_WRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("FAULT_NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
}

void test_fault_lookup_mode_short_name_match(void)
{
   // Test short name lookup (without "FAULT_" prefix, e.g., "RRQ_TIMEOUT")
   int idx = tftptest_fault_lookup_mode("RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("WRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
}

void test_fault_lookup_mode_case_insensitive(void)
{
   // Lookup should be case-insensitive
   int idx = tftptest_fault_lookup_mode("fault_rrq_timeout");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("FAULT_RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("FaULt_RrQ_TiMeOuT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   // Short names also case-insensitive
   idx = tftptest_fault_lookup_mode("rrq_timeout");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_lookup_mode("RrQ_TiMeOuT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );
}

void test_fault_lookup_mode_nonexistent_returns_negative_one(void)
{
   // Nonexistent modes should return -1
   int idx = tftptest_fault_lookup_mode("NONEXISTENT");
   TEST_ASSERT_EQUAL_INT( -1, idx );

   idx = tftptest_fault_lookup_mode("FAULT_INVALID");
   TEST_ASSERT_EQUAL_INT( -1, idx );

   idx = tftptest_fault_lookup_mode("");
   TEST_ASSERT_EQUAL_INT( -1, idx );

   idx = tftptest_fault_lookup_mode("TIMEOUT");
   TEST_ASSERT_EQUAL_INT( -1, idx );
}

void test_fault_lookup_mode_fault_none(void)
{
   // Special case: FAULT_NONE
   int idx = tftptest_fault_lookup_mode("FAULT_NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
   TEST_ASSERT_EQUAL_INT( 0, idx );
}

void test_fault_lookup_mode_fault_none_short(void)
{
   // Short name for FAULT_NONE
   int idx = tftptest_fault_lookup_mode("NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
   TEST_ASSERT_EQUAL_INT( 0, idx );
}

void test_fault_lookup_mode_last_mode(void)
{
   // Test last mode in enum (FAULT_BURST_DATA)
   int idx = tftptest_fault_lookup_mode("FAULT_BURST_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx );
   TEST_ASSERT_EQUAL_INT( FAULT_MODE_COUNT - 1, idx );
}

void test_fault_lookup_mode_last_mode_short(void)
{
   // Short name for last mode
   int idx = tftptest_fault_lookup_mode("BURST_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx );
   TEST_ASSERT_EQUAL_INT( FAULT_MODE_COUNT - 1, idx );
}

void test_fault_lookup_mode_partial_match_fails(void)
{
   // Partial matches should fail
   int idx = tftptest_fault_lookup_mode("FAULT_RRQ");
   TEST_ASSERT_EQUAL_INT( -1, idx );

   idx = tftptest_fault_lookup_mode("RRQ");
   TEST_ASSERT_EQUAL_INT( -1, idx );

   idx = tftptest_fault_lookup_mode("TIMEOUT");
   TEST_ASSERT_EQUAL_INT( -1, idx );
}
