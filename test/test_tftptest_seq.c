/**
 * @file test_tftptest_seq.c
 * @brief Unit tests for tftptest_seq module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftptest_seq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * tftptest_seq: sequence file loading and advancement
 *---------------------------------------------------------------------------*/

void test_seq_load_valid_single_entry_defaults(void)
{
   const char *path = "/tmp/tftptest_seq_single.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=FAULT_RRQ_TIMEOUT\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 1, seq.n_entries );
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_UINT32( 0, seq.entries[0].param );
   TEST_ASSERT_EQUAL_INT( 1, seq.entries[0].count );
   TEST_ASSERT_EQUAL_INT( 0, seq.current );
   TEST_ASSERT_EQUAL_INT( 0, seq.sessions_in_step );

   tftptest_seq_free(&seq);
}

void test_seq_load_valid_multiple_entries_with_params(void)
{
   const char *path = "/tmp/tftptest_seq_multi.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "# Sequence test\n"
      "mode=FAULT_NONE count=2\n"
      "mode=FAULT_RRQ_TIMEOUT count=1\n"
      "mode=FAULT_CORRUPT_DATA param=5 count=3\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 3, seq.n_entries );

   // Entry 0
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_UINT32( 0, seq.entries[0].param );
   TEST_ASSERT_EQUAL_INT( 2, seq.entries[0].count );

   // Entry 1
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, seq.entries[1].mode );
   TEST_ASSERT_EQUAL_UINT32( 0, seq.entries[1].param );
   TEST_ASSERT_EQUAL_INT( 1, seq.entries[1].count );

   // Entry 2
   TEST_ASSERT_EQUAL_INT( FAULT_CORRUPT_DATA, seq.entries[2].mode );
   TEST_ASSERT_EQUAL_UINT32( 5, seq.entries[2].param );
   TEST_ASSERT_EQUAL_INT( 3, seq.entries[2].count );

   tftptest_seq_free(&seq);
}

void test_seq_load_valid_comments_and_blanks(void)
{
   const char *path = "/tmp/tftptest_seq_comments.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "# Header comment\n"
      "\n"
      "mode=FAULT_NONE # inline comment\n"
      "  \n"
      "mode=FAULT_RRQ_TIMEOUT count=1 # another comment\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 2, seq.n_entries );
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, seq.entries[1].mode );

   tftptest_seq_free(&seq);
}

void test_seq_load_valid_case_insensitive_mode(void)
{
   const char *path = "/tmp/tftptest_seq_case.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "mode=fault_none\n"
      "mode=FAULT_RRQ_TIMEOUT\n"
      "mode=FaULt_WrQ_TiMeOuT\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 3, seq.n_entries );
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, seq.entries[1].mode );
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, seq.entries[2].mode );

   tftptest_seq_free(&seq);
}

void test_seq_load_valid_short_mode_names(void)
{
   const char *path = "/tmp/tftptest_seq_short.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "mode=RRQ_TIMEOUT\n"
      "mode=WRQ_TIMEOUT\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 2, seq.n_entries );
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_INT( FAULT_WRQ_TIMEOUT, seq.entries[1].mode );

   tftptest_seq_free(&seq);
}

void test_seq_load_valid_field_order_doesnt_matter(void)
{
   const char *path = "/tmp/tftptest_seq_order.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "count=2 param=10 mode=FAULT_SLOW_RESPONSE\n"
      "param=5 mode=FAULT_CORRUPT_DATA count=3\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 2, seq.n_entries );

   TEST_ASSERT_EQUAL_INT( FAULT_SLOW_RESPONSE, seq.entries[0].mode );
   TEST_ASSERT_EQUAL_UINT32( 10, seq.entries[0].param );
   TEST_ASSERT_EQUAL_INT( 2, seq.entries[0].count );

   TEST_ASSERT_EQUAL_INT( FAULT_CORRUPT_DATA, seq.entries[1].mode );
   TEST_ASSERT_EQUAL_UINT32( 5, seq.entries[1].param );
   TEST_ASSERT_EQUAL_INT( 3, seq.entries[1].count );

   tftptest_seq_free(&seq);
}

void test_seq_load_nonexistent_file_returns_error(void)
{
   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load("/nonexistent/path/seq.txt", &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_empty_file_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_empty.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "# Just a comment\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_unknown_fault_mode_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_bad_mode.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=NONEXISTENT_FAULT\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_invalid_param_non_numeric_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_bad_param.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=FAULT_CORRUPT_DATA param=notanumber\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_invalid_count_zero_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_bad_count_zero.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=FAULT_NONE count=0\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_invalid_count_non_numeric_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_bad_count_str.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=FAULT_NONE count=notanumber\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_missing_required_mode_field_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_no_mode.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "param=5 count=1\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_unknown_key_returns_error(void)
{
   const char *path = "/tmp/tftptest_seq_bad_key.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f, "mode=FAULT_NONE unknown_key=value\n");
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_seq_load_partial_file_on_first_error(void)
{
   // If there are errors, the entire file is rejected and entries are freed
   const char *path = "/tmp/tftptest_seq_partial.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "mode=FAULT_NONE count=2\n"
      "mode=INVALID_MODE count=1\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
   // seq should not be populated on error
   TEST_ASSERT_NULL( seq.entries );
}

void test_seq_advance_increments_sessions_in_step(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 3 },
      { .mode = FAULT_RRQ_TIMEOUT, .param = 0, .count = 1 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 2,
      .current = 0,
      .sessions_in_step = 0,
   };
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // First session: increments but doesn't transition
   bool keep_going = tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_TRUE( keep_going );
   TEST_ASSERT_EQUAL_INT( 1, seq.sessions_in_step );
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode ); // unchanged
   TEST_ASSERT_EQUAL_INT( 0, seq.current ); // still at first entry
}

void test_seq_advance_transitions_to_next_entry(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 2 },
      { .mode = FAULT_RRQ_TIMEOUT, .param = 0, .count = 1 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 2,
      .current = 0,
      .sessions_in_step = 1, // already processed 1 session
   };
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Advance past the count for entry 0
   bool keep_going = tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_TRUE( keep_going );
   TEST_ASSERT_EQUAL_INT( 0, seq.sessions_in_step ); // reset
   TEST_ASSERT_EQUAL_INT( 1, seq.current ); // advanced to next entry
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode ); // updated from new entry
}

void test_seq_advance_returns_false_when_exhausted(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 1 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 1,
      .current = 0,
      .sessions_in_step = 0, // about to complete the only entry
   };
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Advance past the last entry
   bool keep_going = tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_FALSE( keep_going ); // sequence exhausted
}

void test_seq_advance_updates_param_on_transition(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 2 },
      { .mode = FAULT_CORRUPT_DATA, .param = 42, .count = 1 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 2,
      .current = 0,
      .sessions_in_step = 0,
   };
   // Initialize fault from first entry (as done in main tftptest.c)
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // First advance: increment, still at entry 0
   tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 0, fault.param );

   // Second advance: transition to entry 1
   tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_CORRUPT_DATA, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 42, fault.param ); // updated
}

void test_seq_advance_multi_session_entries(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 3 },
      { .mode = FAULT_RRQ_TIMEOUT, .param = 0, .count = 2 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 2,
      .current = 0,
      .sessions_in_step = 0,
   };
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Process all 3 sessions of entry 0
   tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_EQUAL_INT( 1, seq.sessions_in_step );
   TEST_ASSERT_EQUAL_INT( 0, seq.current );

   tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_EQUAL_INT( 2, seq.sessions_in_step );
   TEST_ASSERT_EQUAL_INT( 0, seq.current );

   tftptest_seq_advance(&seq, &fault); // this one causes transition
   TEST_ASSERT_EQUAL_INT( 0, seq.sessions_in_step ); // reset
   TEST_ASSERT_EQUAL_INT( 1, seq.current ); // advanced
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // Process entry 1 (count=2)
   tftptest_seq_advance(&seq, &fault);
   TEST_ASSERT_EQUAL_INT( 1, seq.sessions_in_step );

   bool keep_going = tftptest_seq_advance(&seq, &fault); // exhausted after this
   TEST_ASSERT_FALSE( keep_going );
}

void test_seq_free_zeros_struct(void)
{
   struct TFTPTest_SeqEntry entries[] = {
      { .mode = FAULT_NONE, .param = 0, .count = 1 },
   };
   struct TFTPTest_Seq seq = {
      .entries = entries,
      .n_entries = 1,
      .current = 0,
      .sessions_in_step = 0,
   };

   // Manually allocate so we can verify it's freed
   seq.entries = (struct TFTPTest_SeqEntry *)malloc(sizeof entries);
   TEST_ASSERT_NOT_NULL( seq.entries );
   memcpy(seq.entries, entries, sizeof entries);
   seq.n_entries = 1;

   tftptest_seq_free(&seq);

   TEST_ASSERT_NULL( seq.entries );
   TEST_ASSERT_EQUAL_INT( 0, seq.n_entries );
   TEST_ASSERT_EQUAL_INT( 0, seq.current );
   TEST_ASSERT_EQUAL_INT( 0, seq.sessions_in_step );
}

void test_seq_integration_real_file_good(void)
{
   // Create a realistic sequence file
   const char *path = "/tmp/tftptest_seq_integration_good.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "# Test sequence: 2 normal, 1 timeout, 2 normal\n"
      "mode=FAULT_NONE count=2\n"
      "mode=FAULT_RRQ_TIMEOUT count=1\n"
      "mode=FAULT_NONE count=2\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( 3, seq.n_entries );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Simulate running through 5 sessions
   int session_count = 0;
   while ( tftptest_seq_advance(&seq, &fault) )
      session_count++;
   session_count++; // the last advance also counts

   TEST_ASSERT_EQUAL_INT( 5, session_count );

   tftptest_seq_free(&seq);
}

void test_seq_integration_real_file_bad_mode(void)
{
   const char *path = "/tmp/tftptest_seq_integration_bad.txt";
   FILE *f = fopen(path, "w");
   TEST_ASSERT_NOT_NULL( f );
   fprintf(f,
      "mode=FAULT_NONE count=2\n"
      "mode=INVALID_FAULT_MODE count=1\n"
   );
   fclose(f);

   struct TFTPTest_Seq seq = {0};
   int rc = tftptest_seq_load(path, &seq);
   TEST_ASSERT_EQUAL_INT( -1, rc );
   TEST_ASSERT_NULL( seq.entries );
}

