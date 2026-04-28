/**
 * @file tftpqa_seq.h
 * @brief Test sequence file parser and stepper.
 * @date Apr 11, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTPTEST_SEQ_H
#define TFTPTEST_SEQ_H

#include <stdbool.h>
#include <stddef.h>
#include "tftpqa_faultmode.h"

struct TFTPQa_SeqEntry
{
   enum TFTPQa_FaultMode mode;
   uint32_t                param;
   bool                    param_present;
   size_t                  count;  // number of sessions this entry covers
};

struct TFTPQa_Seq
{
   struct TFTPQa_SeqEntry *entries;
   size_t                    n_entries;
   size_t                    current;          // index into entries[]
   size_t                    sessions_in_step; // sessions consumed in current entry
};

/// Parse a test sequence file. Returns 0 on success, -1 on error (logs diagnostics).
/// Caller must call tftpqa_seq_free() when done.
int tftpqa_seq_load(const char *path, struct TFTPQa_Seq *seq);

/// Advance the sequence after a session completes. Updates fault in place.
/// Returns true if sequence is still active, false if exhausted (server should shut down).
bool tftpqa_seq_advance(struct TFTPQa_Seq *seq, struct TFTPQa_FaultState *fault);

/// Free allocated entries.
void tftpqa_seq_free(struct TFTPQa_Seq *seq);

#endif // TFTPTEST_SEQ_H
