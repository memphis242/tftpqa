/**
 * @file test_tftp_pkt.c
 * @brief Unit tests for tftp_pkt module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_pkt.h"
#include "tftpqa_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_pkt_valid_rrq_octet(void);
void test_pkt_valid_wrq_netascii(void);
void test_pkt_valid_rrq_netascii_case_insensitive(void);
void test_pkt_reject_wrong_opcode(void);
void test_pkt_reject_missing_filename_nul(void);
void test_pkt_reject_empty_filename(void);
void test_pkt_reject_filename_with_slash(void);
void test_pkt_reject_filename_with_backslash(void);
void test_pkt_reject_filename_only_dots(void);
void test_pkt_reject_unsupported_mode_mail(void);
void test_pkt_reject_missing_mode_nul(void);
void test_pkt_reject_packet_too_short(void);
void test_pkt_reject_nonprintable_in_filename(void);
void test_pkt_parse_request_rrq(void);
void test_pkt_parse_request_wrq(void);
void test_pkt_data_round_trip(void);
void test_pkt_data_empty_payload(void);
void test_pkt_data_full_512(void);
void test_pkt_ack_round_trip(void);
void test_pkt_error_round_trip(void);
void test_pkt_parse_ack_rejects_short(void);
void test_pkt_parse_ack_rejects_wrong_opcode(void);
void test_pkt_parse_data_rejects_short(void);
void test_pkt_parse_error_rejects_no_nul(void);
void test_pkt_reject_filename_too_long(void);
void test_pkt_parse_data_rejects_wrong_opcode(void);
void test_pkt_build_error_returns_zero_when_buffer_too_small(void);
void test_pkt_build_error_succeeds_with_adequate_buffer(void);
void test_pkt_valid_rrq_octet_mixed_case(void);

/*---------------------------------------------------------------------------
 * tftp_pkt: RequestIsValid tests
 *---------------------------------------------------------------------------*/

void test_pkt_valid_rrq_octet(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 'o', 'o', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_TRUE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_valid_wrq_netascii(void)
{
   uint8_t buf[] = { 0x00, 0x02, 'b', 'a', 'r', 0x00,
                     'n', 'e', 't', 'a', 's', 'c', 'i', 'i', 0x00 };
   TEST_ASSERT_TRUE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_valid_rrq_netascii_case_insensitive(void)
{
   uint8_t buf[] = { 0x00, 0x01, 't', 'e', 's', 't', 0x00,
                     'N', 'E', 'T', 'A', 'S', 'C', 'I', 'I', 0x00 };
   TEST_ASSERT_TRUE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_wrong_opcode(void)
{
   uint8_t buf[] = { 0x00, 0x03, 'f', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_missing_filename_nul(void)
{
   // No NUL after filename
   uint8_t buf[] = { 0x00, 0x01, 'f', 'o', 'o', 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_empty_filename(void)
{
   // Filename is empty (NUL immediately after opcode)
   uint8_t buf[] = { 0x00, 0x01, 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_with_slash(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'a', '/', 'b', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_with_backslash(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'a', '\\', 'b', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_only_dots(void)
{
   // ".." has no alphanumeric character
   uint8_t buf[] = { 0x00, 0x01, '.', '.', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_unsupported_mode_mail(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x00, 'm', 'a', 'i', 'l', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_missing_mode_nul(void)
{
   // Mode string not NUL-terminated (packet ends abruptly)
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x00, 'o', 'c', 't', 'e', 't' };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_packet_too_short(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f' };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

void test_pkt_reject_nonprintable_in_filename(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x01, 'o', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid( buf, sizeof buf ) );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: ParseRequest tests
 *---------------------------------------------------------------------------*/

void test_pkt_parse_request_rrq(void)
{
   uint8_t buf[] = { 0x00, 0x01, 't', 'e', 's', 't', 0x00,
                     'o', 'c', 't', 'e', 't', 0x00 };
   enum TFTPOpcode opcode;
   const char *filename;
   const char *mode;

   int rc = tftp_pkt_parse_request( buf, sizeof buf, &opcode, &filename, &mode );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( TFTP_OP_RRQ, opcode );
   TEST_ASSERT_EQUAL_STRING( "test", filename );
   TEST_ASSERT_EQUAL_STRING( "octet", mode );
}

void test_pkt_parse_request_wrq(void)
{
   uint8_t buf[] = { 0x00, 0x02, 'u', 'p', 0x00,
                     'n', 'e', 't', 'a', 's', 'c', 'i', 'i', 0x00 };
   enum TFTPOpcode opcode;
   const char *filename;
   const char *mode;

   int rc = tftp_pkt_parse_request( buf, sizeof buf, &opcode, &filename, &mode );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( TFTP_OP_WRQ, opcode );
   TEST_ASSERT_EQUAL_STRING( "up", filename );
   TEST_ASSERT_EQUAL_STRING( "netascii", mode );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: Build/Parse round-trip tests
 *---------------------------------------------------------------------------*/

void test_pkt_data_round_trip(void)
{
   uint8_t pkt[TFTP_DATA_MAX_SZ];
   uint8_t payload[] = "Hello, TFTP!";
   size_t n = tftp_pkt_build_data( pkt, sizeof pkt, 42, payload, sizeof payload - 1 );
   TEST_ASSERT_EQUAL( TFTP_DATA_HDR_SZ + sizeof payload - 1, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = tftp_pkt_parse_data( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 42, block );
   TEST_ASSERT_EQUAL( sizeof payload - 1, data_len );
   TEST_ASSERT_EQUAL_MEMORY( payload, data, data_len );
}

void test_pkt_data_empty_payload(void)
{
   uint8_t pkt[TFTP_DATA_MAX_SZ];
   size_t n = tftp_pkt_build_data( pkt, sizeof pkt, 1, (const uint8_t *)"", 0 );
   TEST_ASSERT_EQUAL( TFTP_DATA_HDR_SZ, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = tftp_pkt_parse_data( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 1, block );
   TEST_ASSERT_EQUAL( 0, data_len );
}

void test_pkt_data_full_512(void)
{
   uint8_t pkt[TFTP_DATA_MAX_SZ];
   uint8_t payload[TFTP_BLOCK_DATA_SZ];
   memset( payload, 0xAB, sizeof payload );

   size_t n = tftp_pkt_build_data( pkt, sizeof pkt, 65535, payload, sizeof payload );
   TEST_ASSERT_EQUAL( TFTP_DATA_MAX_SZ, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = tftp_pkt_parse_data( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 65535, block );
   TEST_ASSERT_EQUAL( TFTP_BLOCK_DATA_SZ, data_len );
}

void test_pkt_ack_round_trip(void)
{
   uint8_t pkt[TFTP_ACK_SZ];
   size_t n = tftp_pkt_build_ack( pkt, sizeof pkt, 7 );
   TEST_ASSERT_EQUAL( TFTP_ACK_SZ, n );

   uint16_t block;
   int rc = tftp_pkt_parse_ack( pkt, n, &block );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 7, block );
}

void test_pkt_error_round_trip(void)
{
   uint8_t pkt[128];
   size_t n = tftp_pkt_build_error( pkt, sizeof pkt, TFTP_ERRC_FILE_NOT_FOUND, "File not found" );
   TEST_ASSERT_GREATER_THAN( 0, n );

   enum TFTPErrCode code;
   const char *msg;
   int rc = tftp_pkt_parse_error( pkt, n, &code, &msg );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_INT( TFTP_ERRC_FILE_NOT_FOUND, code );
   TEST_ASSERT_EQUAL_STRING( "File not found", msg );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: Parse rejection tests
 *---------------------------------------------------------------------------*/

void test_pkt_parse_ack_rejects_short(void)
{
   uint8_t buf[] = { 0x00, 0x04, 0x00 }; // Only 3 bytes
   uint16_t block;
   TEST_ASSERT_EQUAL_INT( -1, tftp_pkt_parse_ack( buf, sizeof buf, &block ) );
}

void test_pkt_parse_ack_rejects_wrong_opcode(void)
{
   uint8_t buf[] = { 0x00, 0x03, 0x00, 0x01 }; // Opcode 3 (DATA), not ACK
   uint16_t block;
   TEST_ASSERT_EQUAL_INT( -1, tftp_pkt_parse_ack( buf, sizeof buf, &block ) );
}

void test_pkt_parse_data_rejects_short(void)
{
   uint8_t buf[] = { 0x00, 0x03, 0x00 }; // Only 3 bytes
   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   TEST_ASSERT_EQUAL_INT( -1, tftp_pkt_parse_data( buf, sizeof buf, &block, &data, &data_len ) );
}

void test_pkt_parse_error_rejects_no_nul(void)
{
   // ERROR packet with no NUL terminator in message
   uint8_t buf[] = { 0x00, 0x05, 0x00, 0x01, 'o', 'o', 'p', 's' };
   enum TFTPErrCode code;
   const char *msg;
   TEST_ASSERT_EQUAL_INT( -1, tftp_pkt_parse_error( buf, sizeof buf, &code, &msg ) );
}

/*---------------------------------------------------------------------------
 * Packet edge case tests
 *---------------------------------------------------------------------------*/

void test_pkt_reject_filename_too_long(void)
{
   // Build a request with filename exceeding FILENAME_MAX_LEN
   uint8_t buf[512];
   buf[0] = 0x00;
   buf[1] = 0x01; // RRQ
   // Fill filename with 'a' up to FILENAME_MAX_LEN + 1
   size_t fname_len = FILENAME_MAX_LEN + 1;
   memset(buf + 2, 'a', fname_len);
   buf[2 + fname_len] = 0x00; // filename NUL
   memcpy(buf + 2 + fname_len + 1, "octet", 5);
   buf[2 + fname_len + 1 + 5] = 0x00; // mode NUL
   size_t total = 2 + fname_len + 1 + 6;

   TEST_ASSERT_FALSE( tftp_pkt_request_is_valid(buf, total) );
}

void test_pkt_parse_data_rejects_wrong_opcode(void)
{
   // ACK opcode (4) instead of DATA (3)
   uint8_t buf[] = { 0x00, 0x04, 0x00, 0x01, 'x' };
   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   TEST_ASSERT_EQUAL_INT( -1, tftp_pkt_parse_data(buf, sizeof buf, &block, &data, &data_len) );
}

void test_pkt_build_error_returns_zero_when_buffer_too_small(void)
{
   // BuildError returns 0 when message doesn't fit in buffer
   uint8_t pkt[8]; // too small for header + message + NUL
   size_t n = tftp_pkt_build_error(pkt, sizeof pkt, 0, "This message is way too long to fit");
   TEST_ASSERT_EQUAL( 0, n );
}

void test_pkt_build_error_succeeds_with_adequate_buffer(void)
{
   uint8_t pkt[128];
   size_t n = tftp_pkt_build_error(pkt, sizeof pkt, 3, "Disk full");
   // 4 (header) + 9 (msg) + 1 (NUL) = 14
   TEST_ASSERT_EQUAL( 14, n );
}

void test_pkt_valid_rrq_octet_mixed_case(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'F', 'i', 'L', 'e', 0x00,
                     'O', 'C', 'T', 'E', 'T', 0x00 };
   TEST_ASSERT_TRUE( tftp_pkt_request_is_valid(buf, sizeof buf) );
}

