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
void test_fault_lookup_mode_too_short(void);
void test_fault_lookup_mode_too_long(void);
void test_fault_lookup_mode_fault_none(void);
void test_fault_lookup_mode_fault_none_short(void);
void test_fault_lookup_mode_last_mode(void);
void test_fault_lookup_mode_last_mode_short(void);
void test_fault_lookup_mode_partial_match_fails(void);
void test_fault_lookup_mode_middle_modes_full_names(void);
void test_fault_lookup_mode_middle_modes_short_names(void);
void test_fault_lookup_mode_end_modes_both_formats(void);
void test_fault_lookup_mode_alphabetically_before_modes(void);
void test_fault_lookup_mode_alphabetically_after_modes(void);
void test_fault_lookup_mode_multiple_sequential_searches(void);
void test_fault_lookup_mode_mixed_formats_sequential(void);
void test_fault_lookup_mode_all_modes_exhaustive(void);

/*---------------------------------------------------------------------------
 * Local Static Data
 *---------------------------------------------------------------------------*/

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
   int idx = tftptest_fault_name_lookup_mode("FAULT_RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_WRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
}

void test_fault_lookup_mode_short_name_match(void)
{
   // Test short name lookup (without "FAULT_" prefix, e.g., "RRQ_TIMEOUT")
   int idx = tftptest_fault_name_lookup_mode("RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("WRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
}

void test_fault_lookup_mode_case_insensitive(void)
{
   // Lookup should be case-insensitive
   int idx = tftptest_fault_name_lookup_mode("fault_rrq_timeout");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("FaULt_RrQ_TiMeOuT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   // Short names also case-insensitive
   idx = tftptest_fault_name_lookup_mode("rrq_timeout");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("RRQ_TIMEOUT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );

   idx = tftptest_fault_name_lookup_mode("RrQ_TiMeOuT");
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, idx );
}

void test_fault_lookup_mode_nonexistent_returns_negative_one(void)
{
   // Nonexistent modes should return TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND
   int idx = tftptest_fault_name_lookup_mode("NONEXISTENT");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_INVALID");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );
}

void test_fault_lookup_mode_too_short(void)
{
   // Names shorter than the shortest known short-name ("NONE", 4 chars) return TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT
   int idx = tftptest_fault_name_lookup_mode("");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );

   idx = tftptest_fault_name_lookup_mode("N");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );

   idx = tftptest_fault_name_lookup_mode("NO");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );

   idx = tftptest_fault_name_lookup_mode("NON");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );
}

void test_fault_lookup_mode_too_long(void)
{
   // Names longer than the longest known fault name return TFTPTEST_FAULT_LOOKUP_NAME_TOO_LONG
   char long_name[ LONGEST_FAULT_MODE_NAME_LEN + 1 ] = {0};
   memset( long_name, 'a', sizeof(long_name) - 1 );

   int idx = tftptest_fault_name_lookup_mode(long_name);
   TEST_ASSERT_LESS_THAN( FAULT_NONE, idx );
}

void test_fault_lookup_mode_fault_none(void)
{
   // Special case: FAULT_NONE
   int idx = tftptest_fault_name_lookup_mode("FAULT_NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
   TEST_ASSERT_EQUAL_INT( 0, idx );
}

void test_fault_lookup_mode_fault_none_short(void)
{
   // Short name for FAULT_NONE
   int idx = tftptest_fault_name_lookup_mode("NONE");
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, idx );
   TEST_ASSERT_EQUAL_INT( 0, idx );
}

void test_fault_lookup_mode_last_mode(void)
{
   // Test last mode in enum (FAULT_BURST_DATA)
   int idx = tftptest_fault_name_lookup_mode("FAULT_BURST_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx );
   TEST_ASSERT_EQUAL_INT( FAULT_MODE_COUNT - 1, idx );
}

void test_fault_lookup_mode_last_mode_short(void)
{
   // Short name for last mode
   int idx = tftptest_fault_name_lookup_mode("BURST_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx );
   TEST_ASSERT_EQUAL_INT( FAULT_MODE_COUNT - 1, idx );
}

void test_fault_lookup_mode_partial_match_fails(void)
{
   // Partial matches with valid length but no match return TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND
   int idx = tftptest_fault_name_lookup_mode("FAULT_RRQ");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );

   // "RRQ" is 3 chars — too short (< 4), returns TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT
   idx = tftptest_fault_name_lookup_mode("RRQ");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );

   // "TIMEOUT" is 7 chars — valid length, no match → TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND
   idx = tftptest_fault_name_lookup_mode("TIMEOUT");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );
}

void test_fault_lookup_mode_middle_modes_full_names(void)
{
   // Test modes in the middle of the list to exercise loop iteration
   int idx = tftptest_fault_name_lookup_mode("FAULT_MID_TIMEOUT_NO_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_MID_TIMEOUT_NO_DATA, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_DUP_MID_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_SLOW_RESPONSE");
   TEST_ASSERT_EQUAL_INT( FAULT_SLOW_RESPONSE, idx );
}

void test_fault_lookup_mode_middle_modes_short_names(void)
{
   // Test middle modes with short names to exercise loop with short name matches
   int idx = tftptest_fault_name_lookup_mode("MID_TIMEOUT_NO_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_MID_TIMEOUT_NO_DATA, idx );

   idx = tftptest_fault_name_lookup_mode("DUP_MID_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, idx );

   idx = tftptest_fault_name_lookup_mode("SLOW_RESPONSE");
   TEST_ASSERT_EQUAL_INT( FAULT_SLOW_RESPONSE, idx );
}

void test_fault_lookup_mode_end_modes_both_formats(void)
{
   // Test multiple modes near the end to ensure loop iterates fully
   int idx = tftptest_fault_name_lookup_mode("FAULT_CORRUPT_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_CORRUPT_DATA, idx );

   idx = tftptest_fault_name_lookup_mode("TRUNCATED_PKT");
   TEST_ASSERT_EQUAL_INT( FAULT_TRUNCATED_PKT, idx );

   idx = tftptest_fault_name_lookup_mode("CORRUPT_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_CORRUPT_DATA, idx );
}

void test_fault_lookup_mode_alphabetically_before_modes(void)
{
   // Test with names alphabetically before all fault modes to exercise
   // strcasecmp returning negative values
   // "AAAA" and "FAULT_" are valid length but no match → TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND
   int idx = tftptest_fault_name_lookup_mode("AAAA");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );

   idx = tftptest_fault_name_lookup_mode("FAULT_");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );

   // "A" is 1 char — too short → TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT
   idx = tftptest_fault_name_lookup_mode("A");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );
}

void test_fault_lookup_mode_alphabetically_after_modes(void)
{
   // Test with names alphabetically after all fault modes to exercise
   // strcasecmp returning positive values
   // "ZZZZ" and "fault_z" are valid length but no match → TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND
   int idx = tftptest_fault_name_lookup_mode("ZZZZ");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );

   // "zzz" is 3 chars — too short → TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT
   idx = tftptest_fault_name_lookup_mode("zzz");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT, idx );

   idx = tftptest_fault_name_lookup_mode("fault_z");
   TEST_ASSERT_EQUAL_INT( TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND, idx );
}

void test_fault_lookup_mode_multiple_sequential_searches(void)
{
   // Multiple searches to exercise loop iterations and early returns
   // at different positions in the list
   int idx1 = tftptest_fault_name_lookup_mode("FAULT_FILE_NOT_FOUND");
   int idx2 = tftptest_fault_name_lookup_mode("FAULT_DUP_FINAL_DATA");
   int idx3 = tftptest_fault_name_lookup_mode("FAULT_WRONG_TID_READ");

   TEST_ASSERT_EQUAL_INT( FAULT_FILE_NOT_FOUND, idx1 );
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_FINAL_DATA, idx2 );
   TEST_ASSERT_EQUAL_INT( FAULT_WRONG_TID_READ, idx3 );
}

void test_fault_lookup_mode_mixed_formats_sequential(void)
{
   // Sequential searches alternating between full and short names
   // to exercise different code paths
   int idx1 = tftptest_fault_name_lookup_mode("FILE_NOT_FOUND");      // short name
   int idx2 = tftptest_fault_name_lookup_mode("FAULT_FILE_NOT_FOUND"); // full name
   int idx3 = tftptest_fault_name_lookup_mode("DUP_FINAL_DATA");        // short name
   int idx4 = tftptest_fault_name_lookup_mode("FAULT_DUP_FINAL_DATA");  // full name

   TEST_ASSERT_EQUAL_INT( FAULT_FILE_NOT_FOUND, idx1 );
   TEST_ASSERT_EQUAL_INT( FAULT_FILE_NOT_FOUND, idx2 );
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_FINAL_DATA, idx3 );
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_FINAL_DATA, idx4 );
}

void test_fault_lookup_mode_all_modes_exhaustive(void)
{
   // Exhaustively search for all defined modes to ensure loop iteration
   // through the entire array completes and all branches are exercised
   int idx_first = tftptest_fault_name_lookup_mode("FAULT_NONE");
   int idx_mid1 = tftptest_fault_name_lookup_mode("FAULT_MID_TIMEOUT_NO_ACK");
   int idx_mid2 = tftptest_fault_name_lookup_mode("FAULT_SEND_ERROR_WRITE");
   int idx_mid3 = tftptest_fault_name_lookup_mode("FAULT_INVALID_OPCODE_WRITE");
   int idx_last = tftptest_fault_name_lookup_mode("FAULT_BURST_DATA");

   TEST_ASSERT_EQUAL_INT( 0, idx_first );                            // First mode
   TEST_ASSERT_EQUAL_INT( FAULT_MID_TIMEOUT_NO_ACK, idx_mid1 );     // Early-mid mode
   TEST_ASSERT_EQUAL_INT( FAULT_SEND_ERROR_WRITE, idx_mid2 );       // Mid mode
   TEST_ASSERT_EQUAL_INT( FAULT_INVALID_OPCODE_WRITE, idx_mid3 );   // Late-mid mode
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx_last );             // Last mode

   // Also test short names for the last mode to ensure final iteration
   // returns through short name match
   int idx_last_short = tftptest_fault_name_lookup_mode("BURST_DATA");
   TEST_ASSERT_EQUAL_INT( FAULT_BURST_DATA, idx_last_short );
}
