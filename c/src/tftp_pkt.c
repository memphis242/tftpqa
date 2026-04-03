/**
 * @file tftp_pkt.c
 * @brief Functions that handle packet identification, verification, and parsing.
 * @date Apr 02, 2026
 * @author Abdulla Almosalami, @memphis242
 */

/*************************** File Header Inclusions ***************************/

// Standard C Headers
#include <string.h>
// Internal Headers
#include "tftp_pkt.h"

/***************************** Local Declarations *****************************/

/************************** Function Implementations **************************/

bool TFTP_PKT_RequestIsValid(uint8_t *buf, size_t sz)
{
   // TODO
   // Check that the first two bytes are the RRQ (1) or WRQ (2) opcodes.

   // Check that the filename string is nul-terminated within max filename len

   // Check that the filename has valid filename characters

   // Check that the mode is "netascii" or "octet" ("mail" will not be supported)

   // Check that the mode string is nul-terminated

   return false;
}
