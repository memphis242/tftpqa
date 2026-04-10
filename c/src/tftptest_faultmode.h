/**
 * @file tftptest_faultmode.h
 * @brief Fault injection modes for the TFTP test server.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTPTEST_FAULTMODE_H
#define TFTPTEST_FAULTMODE_H

#include <stdint.h>

// Fault injection modes
// Some modes are parameterized (e.g., block number, error code).
// The parameter is carried separately in TFTPTest_FaultState.
enum TFTPTest_FaultMode
{
   FAULT_NONE = 0,

   // Timeout faults -- server stops responding
   FAULT_RRQ_TIMEOUT,              // No response to RRQ at all
   FAULT_WRQ_TIMEOUT,              // No response to WRQ at all
   FAULT_MID_TIMEOUT_NO_DATA,      // Stop sending DATA mid-transfer (param: block#)
   FAULT_MID_TIMEOUT_NO_ACK,       // Stop sending ACK mid-transfer (param: block#)
   FAULT_MID_TIMEOUT_NO_FINAL_ACK, // Suppress final ACK only
   FAULT_MID_TIMEOUT_NO_FINAL_DATA,// Suppress final <512-byte DATA

   // Error response faults
   FAULT_FILE_NOT_FOUND,           // Always respond with file-not-found
   FAULT_PERM_DENIED_READ,         // Permission denied on RRQ
   FAULT_PERM_DENIED_WRITE,        // Permission denied on WRQ
   FAULT_SEND_ERROR_READ,          // Send ERROR after first DATA (param: error code)
   FAULT_SEND_ERROR_WRITE,         // Send ERROR after first WRQ DATA (param: error code)

   // Duplicate faults
   FAULT_DUP_FINAL_DATA,           // Duplicate the last DATA packet
   FAULT_DUP_FINAL_ACK,            // Duplicate the final ACK
   FAULT_DUP_MID_DATA,             // Duplicate DATA at block# (param: block#)
   FAULT_DUP_MID_ACK,              // Duplicate ACK at block# (param: block#)

   // Sequence / skip faults
   FAULT_SKIP_ACK,                 // Skip ACK for block# (param: block#)
   FAULT_SKIP_DATA,                // Skip DATA for block# (param: block#)
   FAULT_OOO_DATA,                 // Reorder DATA blocks (param: block# to swap)
   FAULT_OOO_ACK,                  // Reorder ACK (param: block# to swap)

   // Invalid field faults
   FAULT_INVALID_BLOCK_ACK,        // Send ACK with wrong block# (param: block#)
   FAULT_INVALID_BLOCK_DATA,       // Send DATA with wrong block# (param: block#)
   FAULT_DATA_TOO_LARGE,           // Send DATA > 512 bytes
   FAULT_DATA_LEN_MISMATCH,        // Truncate DATA payload
   FAULT_INVALID_OPCODE_READ,      // Send invalid opcode during RRQ
   FAULT_INVALID_OPCODE_WRITE,     // Send invalid opcode during WRQ
   FAULT_INVALID_ERR_CODE_READ,    // Send ERROR with invalid code (param: code)
   FAULT_INVALID_ERR_CODE_WRITE,   // Send ERROR with invalid code (param: code)

   // TID faults
   FAULT_WRONG_TID_READ,           // Send from wrong TID during RRQ
   FAULT_WRONG_TID_WRITE,          // Send from wrong TID during WRQ

   FAULT_MODE_COUNT
};

// Active fault state: mode + optional parameter
struct TFTPTest_FaultState
{
   enum TFTPTest_FaultMode mode;
   uint32_t param;  // Block number, error code, etc.
};

#endif // TFTPTEST_FAULTMODE_H
