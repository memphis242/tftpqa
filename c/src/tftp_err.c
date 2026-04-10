/**
 * @file tftp_err.c
 * @brief Application error code string table.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <assert.h>

#include "tftp_err.h"

/***************************** Local Declarations *****************************/

static const char *s_err_strings[] =
{
   [TFTP_ERR_NONE]       = "No error",
   [TFTP_ERR_SOCKET]     = "Socket creation failed",
   [TFTP_ERR_BIND]       = "Socket bind failed",
   [TFTP_ERR_FILE_OPEN]  = "File open failed",
   [TFTP_ERR_FILE_READ]  = "File read failed",
   [TFTP_ERR_FILE_WRITE] = "File write failed",
   [TFTP_ERR_TIMEOUT]    = "Operation timed out",
   [TFTP_ERR_PROTOCOL]   = "Protocol violation",
   [TFTP_ERR_CONFIG]     = "Configuration error",
   [TFTP_ERR_ALLOC]      = "Memory allocation failed",
   [TFTP_ERR_CHROOT]     = "chroot() failed",
   [TFTP_ERR_PRIVDROP]   = "Privilege drop failed",
};

/********************** Public Function Implementations ***********************/

const char *tftp_err_str(enum TFTP_Err err)
{
   assert( err >= 0 && err < TFTP_ERR_COUNT );
   assert( s_err_strings[err] != nullptr );
   return s_err_strings[err];
}
