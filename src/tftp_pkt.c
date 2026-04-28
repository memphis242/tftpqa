/**
 * @file tftp_pkt.c
 * @brief TFTP packet encoding, decoding, and validation (RFC 1350).
 * @date Apr 02, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>
#include <arpa/inet.h>

#include "tftp_pkt.h"
#include "tftpqa_util.h"
#include "tftpqa_common.h"

/***************************** Local Declarations *****************************/

// Extract big-endian uint16 from buffer
static inline uint16_t read_u16(const uint8_t *p)
{
   return (uint16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

// Write big-endian uint16 to buffer
static inline void write_u16(uint8_t *p, uint16_t val)
{
   p[0] = (uint8_t)(val >> 8);
   p[1] = (uint8_t)(val & 0xFF);
}

/********************** Public Function Implementations ***********************/

bool tftp_pkt_request_is_valid(const uint8_t *buf, size_t sz)
{
   assert( buf != NULL );

   // Minimum size check
   if ( sz < TFTP_RQST_MIN_SZ )
      return false;

   // 1. Check opcode is RRQ or WRQ
   enum TFTPOpcode opcode = (enum TFTPOpcode)read_u16( buf );
   if ( opcode != TFTP_OP_RRQ && opcode != TFTP_OP_WRQ )
      return false;

   // 2-5. Find and validate the filename (starts at byte 2, NUL-terminated)
   const uint8_t *payload = buf + 2;
   size_t payload_len = sz - 2;

   // Scan for the NUL terminator of the filename
   const uint8_t *fname_end = memchr( payload, '\0', payload_len );
   if ( fname_end == NULL )
      return false; // No NUL terminator found

   size_t fname_len = (size_t)(fname_end - payload);

   // Filename must be at least 1 char and at most FILENAME_MAX_LEN
   if ( fname_len < FILENAME_MIN_LEN || fname_len > FILENAME_MAX_LEN )
      return false;

   // Validate each character and check constraints
   bool has_alnum = false;
   for ( size_t i = 0; i < fname_len; i++ )
   {
      char c = (char)payload[i];

      // Must be a valid filename character (printable, no path separators)
      if ( !tftpqa_util_is_valid_filename_char( c ) )
         return false;

      if ( isalnum( (unsigned char)c ) )
         has_alnum = true;
   }

   // Must have at least one alphanumeric character (reject ".", "..", etc.)
   if ( !has_alnum )
      return false;

   // 6-7. Find and validate the mode string (follows the filename NUL)
   const uint8_t *mode_start = fname_end + 1;
   size_t remaining = payload_len - fname_len - 1;

   // Scan for the NUL terminator of the mode
   const uint8_t *mode_end = memchr( mode_start, '\0', remaining );
   if ( mode_end == NULL )
      return false;

   size_t mode_len = (size_t)(mode_end - mode_start);

   if ( mode_len < TFTP_MODE_MIN_LEN || mode_len > TFTP_MODE_MAX_LEN )
      return false;

   // Mode must be "netascii" or "octet" (case-insensitive)
   if ( strncasecmp( (const char *)mode_start, "netascii", mode_len ) != 0 &&
        strncasecmp( (const char *)mode_start, "octet", mode_len ) != 0 )
      return false;

   return true;
}

int tftp_pkt_parse_request(const uint8_t *buf, size_t sz,
                           enum TFTPOpcode *opcode,
                           const char **filename,
                           const char **mode)
{
   assert( buf != NULL );
   assert( opcode != NULL );
   assert( filename != NULL );
   assert( mode != NULL );

   if ( sz < TFTP_RQST_MIN_SZ )
      return -1;

   *opcode = (enum TFTPOpcode)read_u16( buf );
   if ( *opcode != TFTP_OP_RRQ && *opcode != TFTP_OP_WRQ )
      return -1;

   // Filename starts at byte 2
   *filename = (const char *)(buf + 2);

   // Find the NUL terminator of the filename
   const uint8_t *fname_end = memchr( buf + 2, '\0', sz - 2 );
   if ( fname_end == NULL )
      return -1;

   // Mode starts after the filename NUL
   size_t fname_nul_offset = (size_t)(fname_end - buf);
   if ( fname_nul_offset + 1 >= sz )
      return -1; // No room for mode

   *mode = (const char *)(fname_end + 1);

   // Verify mode is NUL-terminated within the packet
   size_t mode_region_len = sz - fname_nul_offset - 1;
   if ( memchr( *mode, '\0', mode_region_len ) == NULL )
      return -1;

   return 0;
}

size_t tftp_pkt_build_data(uint8_t *out, size_t out_cap,
                           uint16_t block_num,
                           const uint8_t *data, size_t data_len)
{
   assert( out != NULL );
   assert( data_len <= TFTP_BLOCK_DATA_SZ );
   assert( data != NULL || data_len == 0 );

   size_t total = TFTP_DATA_HDR_SZ + data_len;
   if ( out_cap < total )
      return 0;

   write_u16( out, (uint16_t)TFTP_OP_DATA );
   write_u16( out + 2, block_num );

   if ( data_len > 0 )
      memcpy( out + TFTP_DATA_HDR_SZ, data, data_len );

   return total;
}

size_t tftp_pkt_build_ack(uint8_t *out, size_t out_cap, uint16_t block_num)
{
   assert( out != NULL );

   if ( out_cap < TFTP_ACK_SZ )
      return 0;

   write_u16( out, (uint16_t)TFTP_OP_ACK );
   write_u16( out + 2, block_num );

   return TFTP_ACK_SZ;
}

size_t tftp_pkt_build_error(uint8_t *out, size_t out_cap,
                            enum TFTPErrCode error_code, const char *errmsg)
{
   assert( out != NULL );
   assert( errmsg != NULL );

   size_t msg_len = strlen( errmsg );
   size_t total = TFTP_ERR_HDR_SZ + msg_len + 1; // +1 for NUL
   if ( out_cap < total )
      return 0;

   write_u16( out, (uint16_t)TFTP_OP_ERR );
   write_u16( out + 2, (uint16_t)error_code );
   memcpy( out + TFTP_ERR_HDR_SZ, errmsg, msg_len + 1 ); // includes NUL

   return total;
}

int tftp_pkt_parse_ack(const uint8_t *buf, size_t sz, uint16_t *block_num)
{
   assert( buf != NULL );
   assert( block_num != NULL );

   if ( sz < TFTP_ACK_SZ )
      return -1;

   if ( read_u16( buf ) != TFTP_OP_ACK )
      return -1;

   *block_num = read_u16( buf + 2 );
   return 0;
}

int tftp_pkt_parse_data(const uint8_t *buf, size_t sz,
                        uint16_t *block_num,
                        const uint8_t **data, size_t *data_len)
{
   assert( buf != NULL );
   assert( block_num != NULL );
   assert( data != NULL );
   assert( data_len != NULL );

   if ( sz < TFTP_DATA_HDR_SZ )
      return -1;

   if ( read_u16( buf ) != TFTP_OP_DATA )
      return -1;

   *block_num = read_u16( buf + 2 );
   *data = buf + TFTP_DATA_HDR_SZ;
   *data_len = sz - TFTP_DATA_HDR_SZ;

   return 0;
}

int tftp_pkt_parse_error(const uint8_t *buf, size_t sz,
                         enum TFTPErrCode *error_code, const char **errmsg)
{
   assert( buf != NULL );
   assert( error_code != NULL );
   assert( errmsg != NULL );

   // Minimum: 4-byte header + 1-byte NUL
   if ( sz < TFTP_ERR_HDR_SZ + 1 )
      return -1;

   if ( read_u16( buf ) != TFTP_OP_ERR )
      return -1;

   *error_code = (enum TFTPErrCode)read_u16( buf + 2 );
   *errmsg = (const char *)(buf + TFTP_ERR_HDR_SZ);

   // Verify the error message is NUL-terminated within the packet
   if ( memchr( *errmsg, '\0', sz - TFTP_ERR_HDR_SZ ) == NULL )
      return -1;

   return 0;
}
