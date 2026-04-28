/**
 * @file tftpqa_faultmode.c
 * @brief Fault mode name table and lookup, shared by ctrl and seq modules.
 * @date Apr 11, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include <string.h>
#include <strings.h> // for strcasecmp()
#include <assert.h>

#include "tftpqa_faultmode.h"
#include "tftpqa_common.h"

const char * const tftpqa_fault_mode_names[FAULT_MODE_COUNT] = {
   [FAULT_NONE]                      = FAULT_NAME_PREFIX "NONE",
   [FAULT_RRQ_TIMEOUT]               = FAULT_NAME_PREFIX "RRQ_TIMEOUT",
   [FAULT_WRQ_TIMEOUT]               = FAULT_NAME_PREFIX "WRQ_TIMEOUT",
   [FAULT_MID_TIMEOUT_NO_DATA]       = FAULT_NAME_PREFIX "MID_TIMEOUT_NO_DATA",
   [FAULT_MID_TIMEOUT_NO_ACK]        = FAULT_NAME_PREFIX "MID_TIMEOUT_NO_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_ACK]  = FAULT_NAME_PREFIX "MID_TIMEOUT_NO_FINAL_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_DATA] = FAULT_NAME_PREFIX "MID_TIMEOUT_NO_FINAL_DATA",
   [FAULT_FILE_NOT_FOUND]            = FAULT_NAME_PREFIX "FILE_NOT_FOUND",
   [FAULT_PERM_DENIED_READ]          = FAULT_NAME_PREFIX "PERM_DENIED_READ",
   [FAULT_PERM_DENIED_WRITE]         = FAULT_NAME_PREFIX "PERM_DENIED_WRITE",
   [FAULT_SEND_ERROR_READ]           = FAULT_NAME_PREFIX "SEND_ERROR_READ",
   [FAULT_SEND_ERROR_WRITE]          = FAULT_NAME_PREFIX "SEND_ERROR_WRITE",
   [FAULT_DUP_FINAL_DATA]            = FAULT_NAME_PREFIX "DUP_FINAL_DATA",
   [FAULT_DUP_FINAL_ACK]             = FAULT_NAME_PREFIX "DUP_FINAL_ACK",
   [FAULT_DUP_MID_DATA]              = FAULT_NAME_PREFIX "DUP_MID_DATA",
   [FAULT_DUP_MID_ACK]               = FAULT_NAME_PREFIX "DUP_MID_ACK",
   [FAULT_SKIP_ACK]                  = FAULT_NAME_PREFIX "SKIP_ACK",
   [FAULT_SKIP_DATA]                 = FAULT_NAME_PREFIX "SKIP_DATA",
   [FAULT_OOO_DATA]                  = FAULT_NAME_PREFIX "OOO_DATA",
   [FAULT_OOO_ACK]                   = FAULT_NAME_PREFIX "OOO_ACK",
   [FAULT_INVALID_BLOCK_ACK]         = FAULT_NAME_PREFIX "INVALID_BLOCK_ACK",
   [FAULT_INVALID_BLOCK_DATA]        = FAULT_NAME_PREFIX "INVALID_BLOCK_DATA",
   [FAULT_DATA_TOO_LARGE]            = FAULT_NAME_PREFIX "DATA_TOO_LARGE",
   [FAULT_DATA_LEN_MISMATCH]         = FAULT_NAME_PREFIX "DATA_LEN_MISMATCH",
   [FAULT_INVALID_OPCODE_READ]       = FAULT_NAME_PREFIX "INVALID_OPCODE_READ",
   [FAULT_INVALID_OPCODE_WRITE]      = FAULT_NAME_PREFIX "INVALID_OPCODE_WRITE",
   [FAULT_INVALID_ERR_CODE_READ]     = FAULT_NAME_PREFIX "INVALID_ERR_CODE_READ",
   [FAULT_INVALID_ERR_CODE_WRITE]    = FAULT_NAME_PREFIX "INVALID_ERR_CODE_WRITE",
   [FAULT_WRONG_TID_READ]            = FAULT_NAME_PREFIX "WRONG_TID_READ",
   [FAULT_WRONG_TID_WRITE]           = FAULT_NAME_PREFIX "WRONG_TID_WRITE",
   [FAULT_SLOW_RESPONSE]             = FAULT_NAME_PREFIX "SLOW_RESPONSE",
   [FAULT_CORRUPT_DATA]              = FAULT_NAME_PREFIX "CORRUPT_DATA",
   [FAULT_TRUNCATED_PKT]             = FAULT_NAME_PREFIX "TRUNCATED_PKT",
   [FAULT_BURST_DATA]                = FAULT_NAME_PREFIX "BURST_DATA",
};
CompileTimeAssert( ARRAY_LEN(tftpqa_fault_mode_names) == FAULT_MODE_COUNT,
                   tftpqa_fault_mode_names_size_check );

/* -----------------------------------------------------------------------------
 * Function Implementations
 * -------------------------------------------------------------------------- */ 

enum TFTPQa_FaultMode tftpqa_fault_name_lookup_mode(const char * const name)
{
   // tftpqa_seq and tftpqa_ctrl really shouldn't be calling this fcn /w
   // null pointers, so catch that for debug builds (UTs, integration tests),
   // but for release, do the if-check just to take away a DoS attack surface.
#ifndef NDEBUG
   assert( name != NULL ); 
#else
   if ( name == NULL )
      return TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND;
#endif

   // Quick invalid input checks...
   size_t len = strnlen( name, sizeof LONGEST_FAULT_MODE_NAME );

   if ( len >= sizeof(LONGEST_FAULT_MODE_NAME) )
      return TFTPTEST_FAULT_LOOKUP_NAME_TOO_LONG;
   else if ( len < (sizeof(SHORTEST_FAULT_MODE_NAME) - 1) )
      return TFTPTEST_FAULT_LOOKUP_NAME_TOO_SHORT;

   // Just linear search through - no fancy hashing
   for ( int i = 0; i < FAULT_MODE_COUNT; ++i )
   {
      // Match full name, case-insensitive (e.g., "FAULT_RRQ_TIMEOUT")
      if ( strcasecmp(name, tftpqa_fault_mode_names[i]) == 0 )
         return (enum TFTPQa_FaultMode)i;

      // Also match without the fault name prefix (e.g., "RRQ_TIMEOUT")
      const char *short_name = tftpqa_fault_mode_names[i] + FAULT_NAME_PREFIX_LEN;
      if ( strcasecmp(name, short_name) == 0 )
         return (enum TFTPQa_FaultMode)i;
   }

   return TFTPTEST_FAULT_LOOKUP_NAME_NOT_FOUND;
}
