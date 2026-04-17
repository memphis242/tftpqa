/**
 * @file tftptest_faultmode.c
 * @brief Fault mode name table and lookup, shared by ctrl and seq modules.
 * @date Apr 11, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include <string.h>
#include <assert.h>

#include "tftptest_faultmode.h"
#include "tftptest_common.h"

static const char SHORTEST_FAULT_MODE_NAME[] = "NONE";
static const char LONGEST_FAULT_MODE_NAME[]  = "FAULT_MID_TIMEOUT_NO_FINAL_DATA";

const char *const tftptest_fault_mode_names[FAULT_MODE_COUNT] = {
   [FAULT_NONE]                      = "FAULT_NONE",
   [FAULT_RRQ_TIMEOUT]               = "FAULT_RRQ_TIMEOUT",
   [FAULT_WRQ_TIMEOUT]               = "FAULT_WRQ_TIMEOUT",
   [FAULT_MID_TIMEOUT_NO_DATA]       = "FAULT_MID_TIMEOUT_NO_DATA",
   [FAULT_MID_TIMEOUT_NO_ACK]        = "FAULT_MID_TIMEOUT_NO_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_ACK]  = "FAULT_MID_TIMEOUT_NO_FINAL_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_DATA] = "FAULT_MID_TIMEOUT_NO_FINAL_DATA",
   [FAULT_FILE_NOT_FOUND]            = "FAULT_FILE_NOT_FOUND",
   [FAULT_PERM_DENIED_READ]          = "FAULT_PERM_DENIED_READ",
   [FAULT_PERM_DENIED_WRITE]         = "FAULT_PERM_DENIED_WRITE",
   [FAULT_SEND_ERROR_READ]           = "FAULT_SEND_ERROR_READ",
   [FAULT_SEND_ERROR_WRITE]          = "FAULT_SEND_ERROR_WRITE",
   [FAULT_DUP_FINAL_DATA]            = "FAULT_DUP_FINAL_DATA",
   [FAULT_DUP_FINAL_ACK]             = "FAULT_DUP_FINAL_ACK",
   [FAULT_DUP_MID_DATA]              = "FAULT_DUP_MID_DATA",
   [FAULT_DUP_MID_ACK]               = "FAULT_DUP_MID_ACK",
   [FAULT_SKIP_ACK]                  = "FAULT_SKIP_ACK",
   [FAULT_SKIP_DATA]                 = "FAULT_SKIP_DATA",
   [FAULT_OOO_DATA]                  = "FAULT_OOO_DATA",
   [FAULT_OOO_ACK]                   = "FAULT_OOO_ACK",
   [FAULT_INVALID_BLOCK_ACK]         = "FAULT_INVALID_BLOCK_ACK",
   [FAULT_INVALID_BLOCK_DATA]        = "FAULT_INVALID_BLOCK_DATA",
   [FAULT_DATA_TOO_LARGE]            = "FAULT_DATA_TOO_LARGE",
   [FAULT_DATA_LEN_MISMATCH]         = "FAULT_DATA_LEN_MISMATCH",
   [FAULT_INVALID_OPCODE_READ]       = "FAULT_INVALID_OPCODE_READ",
   [FAULT_INVALID_OPCODE_WRITE]      = "FAULT_INVALID_OPCODE_WRITE",
   [FAULT_INVALID_ERR_CODE_READ]     = "FAULT_INVALID_ERR_CODE_READ",
   [FAULT_INVALID_ERR_CODE_WRITE]    = "FAULT_INVALID_ERR_CODE_WRITE",
   [FAULT_WRONG_TID_READ]            = "FAULT_WRONG_TID_READ",
   [FAULT_WRONG_TID_WRITE]           = "FAULT_WRONG_TID_WRITE",
   [FAULT_SLOW_RESPONSE]             = "FAULT_SLOW_RESPONSE",
   [FAULT_CORRUPT_DATA]              = "FAULT_CORRUPT_DATA",
   [FAULT_TRUNCATED_PKT]             = "FAULT_TRUNCATED_PKT",
   [FAULT_BURST_DATA]                = "FAULT_BURST_DATA",
};
CompileTimeAssert( ARRAY_SZ(tftptest_fault_mode_names) == FAULT_MODE_COUNT,
                   tftptest_fault_mode_names_size_check );

/* -----------------------------------------------------------------------------
 * Function Implementations
 * -------------------------------------------------------------------------- */ 

int tftptest_fault_name_lookup_mode(const char *name)
{
   // Since we can internally control what calls this fcn, definitely shouldn't
   // pass in NULL...
   assert( name != NULL);

   // Quick invalid input checks...
   size_t len = strnlen( name, sizeof LONGEST_FAULT_MODE_NAME );
   if ( len >= sizeof(LONGEST_FAULT_MODE_NAME) )
      return -1;
   else if ( len < (sizeof(SHORTEST_FAULT_MODE_NAME) - 1) )
      return -2;

   // Just linear search through - no fancy hashing
   for ( int i = 0; i < FAULT_MODE_COUNT; ++i )
   {
      // Match full name, case-insensitive (e.g., "FAULT_RRQ_TIMEOUT")
      if ( strcasecmp(name, tftptest_fault_mode_names[i]) == 0 )
         return i;

      // Also match without "FAULT_" prefix (e.g., "RRQ_TIMEOUT")
      const char *short_name = tftptest_fault_mode_names[i] + 6; // skip "FAULT_"
      if ( strcasecmp(name, short_name) == 0 )
         return i;
   }

   return -3;
}
