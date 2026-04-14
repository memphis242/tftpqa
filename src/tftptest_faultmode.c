/**
 * @file tftptest_faultmode.c
 * @brief Fault mode name table and lookup, shared by ctrl and seq modules.
 * @date Apr 11, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include <strings.h>
#include "tftptest_faultmode.h"

const char *const tftptest_fault_mode_names[FAULT_MODE_COUNT] = {
   [FAULT_NONE]                     = "FAULT_NONE",
   [FAULT_RRQ_TIMEOUT]              = "FAULT_RRQ_TIMEOUT",
   [FAULT_WRQ_TIMEOUT]              = "FAULT_WRQ_TIMEOUT",
   [FAULT_MID_TIMEOUT_NO_DATA]      = "FAULT_MID_TIMEOUT_NO_DATA",
   [FAULT_MID_TIMEOUT_NO_ACK]       = "FAULT_MID_TIMEOUT_NO_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_ACK] = "FAULT_MID_TIMEOUT_NO_FINAL_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_DATA]= "FAULT_MID_TIMEOUT_NO_FINAL_DATA",
   [FAULT_FILE_NOT_FOUND]           = "FAULT_FILE_NOT_FOUND",
   [FAULT_PERM_DENIED_READ]         = "FAULT_PERM_DENIED_READ",
   [FAULT_PERM_DENIED_WRITE]        = "FAULT_PERM_DENIED_WRITE",
   [FAULT_SEND_ERROR_READ]          = "FAULT_SEND_ERROR_READ",
   [FAULT_SEND_ERROR_WRITE]         = "FAULT_SEND_ERROR_WRITE",
   [FAULT_DUP_FINAL_DATA]           = "FAULT_DUP_FINAL_DATA",
   [FAULT_DUP_FINAL_ACK]            = "FAULT_DUP_FINAL_ACK",
   [FAULT_DUP_MID_DATA]             = "FAULT_DUP_MID_DATA",
   [FAULT_DUP_MID_ACK]              = "FAULT_DUP_MID_ACK",
   [FAULT_SKIP_ACK]                 = "FAULT_SKIP_ACK",
   [FAULT_SKIP_DATA]                = "FAULT_SKIP_DATA",
   [FAULT_OOO_DATA]                 = "FAULT_OOO_DATA",
   [FAULT_OOO_ACK]                  = "FAULT_OOO_ACK",
   [FAULT_INVALID_BLOCK_ACK]        = "FAULT_INVALID_BLOCK_ACK",
   [FAULT_INVALID_BLOCK_DATA]       = "FAULT_INVALID_BLOCK_DATA",
   [FAULT_DATA_TOO_LARGE]           = "FAULT_DATA_TOO_LARGE",
   [FAULT_DATA_LEN_MISMATCH]        = "FAULT_DATA_LEN_MISMATCH",
   [FAULT_INVALID_OPCODE_READ]      = "FAULT_INVALID_OPCODE_READ",
   [FAULT_INVALID_OPCODE_WRITE]     = "FAULT_INVALID_OPCODE_WRITE",
   [FAULT_INVALID_ERR_CODE_READ]    = "FAULT_INVALID_ERR_CODE_READ",
   [FAULT_INVALID_ERR_CODE_WRITE]   = "FAULT_INVALID_ERR_CODE_WRITE",
   [FAULT_WRONG_TID_READ]           = "FAULT_WRONG_TID_READ",
   [FAULT_WRONG_TID_WRITE]          = "FAULT_WRONG_TID_WRITE",
   [FAULT_SLOW_RESPONSE]            = "FAULT_SLOW_RESPONSE",
   [FAULT_CORRUPT_DATA]             = "FAULT_CORRUPT_DATA",
   [FAULT_TRUNCATED_PKT]            = "FAULT_TRUNCATED_PKT",
   [FAULT_BURST_DATA]               = "FAULT_BURST_DATA",
};

// C99-compatible compile-time assertion: guards against mismatched array size
typedef char tftptest_fault_mode_names_size_check
   [(sizeof(tftptest_fault_mode_names) / sizeof(tftptest_fault_mode_names[0]) == FAULT_MODE_COUNT) ? 1 : -1];

int tftptest_fault_lookup_mode(const char *name)
{
   for ( int i = 0; i < FAULT_MODE_COUNT; i++ )
   {
      // Match full name (e.g., "FAULT_RRQ_TIMEOUT")
      if ( strcasecmp(name, tftptest_fault_mode_names[i]) == 0 )
         return i;

      // Also match without "FAULT_" prefix (e.g., "RRQ_TIMEOUT")
      const char *short_name = tftptest_fault_mode_names[i] + 6; // skip "FAULT_"
      if ( strcasecmp(name, short_name) == 0 )
         return i;
   }
   return -1;
}
