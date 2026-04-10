/**
 * @file test_tftptest.c
 * @brief Unit tests for tftptest foundation modules.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "unity.h"

#include "tftp_err.h"
#include "tftp_log.h"
#include "tftp_parsecfg.h"
#include "tftp_pkt.h"
#include "tftp_util.h"
#include "tftptest_ctrl.h"
#include "tftptest_faultmode.h"

#include "tftptest_common.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void setUp(void) { }
void tearDown(void) { }

/*---------------------------------------------------------------------------
 * tftp_err tests
 *---------------------------------------------------------------------------*/

void test_err_str_returns_non_null_for_all_codes(void)
{
   for ( int i = 0; i < TFTP_ERR_COUNT; i++ )
   {
      const char *s = tftp_err_str( (enum TFTP_Err)i );
      TEST_ASSERT_NOT_NULL( s );
      TEST_ASSERT_TRUE( strlen(s) > 0 );
   }
}

void test_err_str_none_is_no_error(void)
{
   const char *s = tftp_err_str( TFTP_ERR_NONE );
   TEST_ASSERT_EQUAL_STRING( "No error", s );
}

/*---------------------------------------------------------------------------
 * tftp_log tests
 *---------------------------------------------------------------------------*/

void test_log_level_str_returns_expected_names(void)
{
   TEST_ASSERT_EQUAL_STRING( "TRACE", tftp_log_level_str( TFTP_LOG_TRACE ) );
   TEST_ASSERT_EQUAL_STRING( "DEBUG", tftp_log_level_str( TFTP_LOG_DEBUG ) );
   TEST_ASSERT_EQUAL_STRING( "INFO",  tftp_log_level_str( TFTP_LOG_INFO ) );
   TEST_ASSERT_EQUAL_STRING( "WARN",  tftp_log_level_str( TFTP_LOG_WARN ) );
   TEST_ASSERT_EQUAL_STRING( "ERROR", tftp_log_level_str( TFTP_LOG_ERR ) );
   TEST_ASSERT_EQUAL_STRING( "FATAL", tftp_log_level_str( TFTP_LOG_FATAL ) );
}

/*---------------------------------------------------------------------------
 * tftp_parsecfg tests
 *---------------------------------------------------------------------------*/

void test_parsecfg_defaults_produces_sane_values(void)
{
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults( &cfg );

   TEST_ASSERT_EQUAL_UINT16( 23069, cfg.tftp_port );
   TEST_ASSERT_EQUAL_UINT16( 23070, cfg.ctrl_port );
   TEST_ASSERT_EQUAL_STRING( ".", cfg.root_dir );
   TEST_ASSERT_EQUAL_INT( TFTP_LOG_INFO, cfg.log_level );
   TEST_ASSERT_EQUAL_UINT( 1, cfg.timeout_sec );
   TEST_ASSERT_EQUAL_UINT( 5, cfg.max_retransmits );
   TEST_ASSERT_EQUAL( 10000, cfg.max_requests );
   TEST_ASSERT_EQUAL_UINT64( UINT64_MAX, cfg.fault_whitelist );
}

void test_parsecfg_load_nonexistent_file_returns_error(void)
{
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults( &cfg );

   int rc = tftp_parsecfg_load( "/nonexistent/path/config.ini", &cfg );
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: RequestIsValid tests
 *---------------------------------------------------------------------------*/

void test_pkt_valid_rrq_octet(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 'o', 'o', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_TRUE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_valid_wrq_netascii(void)
{
   uint8_t buf[] = { 0x00, 0x02, 'b', 'a', 'r', 0x00,
                     'n', 'e', 't', 'a', 's', 'c', 'i', 'i', 0x00 };
   TEST_ASSERT_TRUE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_valid_rrq_netascii_case_insensitive(void)
{
   uint8_t buf[] = { 0x00, 0x01, 't', 'e', 's', 't', 0x00,
                     'N', 'E', 'T', 'A', 'S', 'C', 'I', 'I', 0x00 };
   TEST_ASSERT_TRUE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_wrong_opcode(void)
{
   uint8_t buf[] = { 0x00, 0x03, 'f', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_missing_filename_nul(void)
{
   // No NUL after filename
   uint8_t buf[] = { 0x00, 0x01, 'f', 'o', 'o', 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_empty_filename(void)
{
   // Filename is empty (NUL immediately after opcode)
   uint8_t buf[] = { 0x00, 0x01, 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_with_slash(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'a', '/', 'b', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_with_backslash(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'a', '\\', 'b', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_filename_only_dots(void)
{
   // ".." has no alphanumeric character
   uint8_t buf[] = { 0x00, 0x01, '.', '.', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_unsupported_mode_mail(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x00, 'm', 'a', 'i', 'l', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_missing_mode_nul(void)
{
   // Mode string not NUL-terminated (packet ends abruptly)
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x00, 'o', 'c', 't', 'e', 't' };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_packet_too_short(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f' };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

void test_pkt_reject_nonprintable_in_filename(void)
{
   uint8_t buf[] = { 0x00, 0x01, 'f', 0x01, 'o', 0x00, 'o', 'c', 't', 'e', 't', 0x00 };
   TEST_ASSERT_FALSE( TFTP_PKT_RequestIsValid( buf, sizeof buf ) );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: ParseRequest tests
 *---------------------------------------------------------------------------*/

void test_pkt_parse_request_rrq(void)
{
   uint8_t buf[] = { 0x00, 0x01, 't', 'e', 's', 't', 0x00,
                     'o', 'c', 't', 'e', 't', 0x00 };
   uint16_t opcode;
   const char *filename;
   const char *mode;

   int rc = TFTP_PKT_ParseRequest( buf, sizeof buf, &opcode, &filename, &mode );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( TFTP_OP_RRQ, opcode );
   TEST_ASSERT_EQUAL_STRING( "test", filename );
   TEST_ASSERT_EQUAL_STRING( "octet", mode );
}

void test_pkt_parse_request_wrq(void)
{
   uint8_t buf[] = { 0x00, 0x02, 'u', 'p', 0x00,
                     'n', 'e', 't', 'a', 's', 'c', 'i', 'i', 0x00 };
   uint16_t opcode;
   const char *filename;
   const char *mode;

   int rc = TFTP_PKT_ParseRequest( buf, sizeof buf, &opcode, &filename, &mode );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( TFTP_OP_WRQ, opcode );
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
   size_t n = TFTP_PKT_BuildData( pkt, sizeof pkt, 42, payload, sizeof payload - 1 );
   TEST_ASSERT_EQUAL( TFTP_DATA_HDR_SZ + sizeof payload - 1, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = TFTP_PKT_ParseData( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 42, block );
   TEST_ASSERT_EQUAL( sizeof payload - 1, data_len );
   TEST_ASSERT_EQUAL_MEMORY( payload, data, data_len );
}

void test_pkt_data_empty_payload(void)
{
   uint8_t pkt[TFTP_DATA_MAX_SZ];
   size_t n = TFTP_PKT_BuildData( pkt, sizeof pkt, 1, (const uint8_t *)"", 0 );
   TEST_ASSERT_EQUAL( TFTP_DATA_HDR_SZ, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = TFTP_PKT_ParseData( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 1, block );
   TEST_ASSERT_EQUAL( 0, data_len );
}

void test_pkt_data_full_512(void)
{
   uint8_t pkt[TFTP_DATA_MAX_SZ];
   uint8_t payload[TFTP_BLOCK_DATA_SZ];
   memset( payload, 0xAB, sizeof payload );

   size_t n = TFTP_PKT_BuildData( pkt, sizeof pkt, 65535, payload, sizeof payload );
   TEST_ASSERT_EQUAL( TFTP_DATA_MAX_SZ, n );

   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   int rc = TFTP_PKT_ParseData( pkt, n, &block, &data, &data_len );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 65535, block );
   TEST_ASSERT_EQUAL( TFTP_BLOCK_DATA_SZ, data_len );
}

void test_pkt_ack_round_trip(void)
{
   uint8_t pkt[TFTP_ACK_SZ];
   size_t n = TFTP_PKT_BuildAck( pkt, sizeof pkt, 7 );
   TEST_ASSERT_EQUAL( TFTP_ACK_SZ, n );

   uint16_t block;
   int rc = TFTP_PKT_ParseAck( pkt, n, &block );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 7, block );
}

void test_pkt_error_round_trip(void)
{
   uint8_t pkt[128];
   size_t n = TFTP_PKT_BuildError( pkt, sizeof pkt, TFTP_ERRC_FILE_NOT_FOUND, "File not found" );
   TEST_ASSERT_GREATER_THAN( 0, n );

   uint16_t code;
   const char *msg;
   int rc = TFTP_PKT_ParseError( pkt, n, &code, &msg );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( TFTP_ERRC_FILE_NOT_FOUND, code );
   TEST_ASSERT_EQUAL_STRING( "File not found", msg );
}

/*---------------------------------------------------------------------------
 * tftp_pkt: Parse rejection tests
 *---------------------------------------------------------------------------*/

void test_pkt_parse_ack_rejects_short(void)
{
   uint8_t buf[] = { 0x00, 0x04, 0x00 }; // Only 3 bytes
   uint16_t block;
   TEST_ASSERT_EQUAL_INT( -1, TFTP_PKT_ParseAck( buf, sizeof buf, &block ) );
}

void test_pkt_parse_ack_rejects_wrong_opcode(void)
{
   uint8_t buf[] = { 0x00, 0x03, 0x00, 0x01 }; // Opcode 3 (DATA), not ACK
   uint16_t block;
   TEST_ASSERT_EQUAL_INT( -1, TFTP_PKT_ParseAck( buf, sizeof buf, &block ) );
}

void test_pkt_parse_data_rejects_short(void)
{
   uint8_t buf[] = { 0x00, 0x03, 0x00 }; // Only 3 bytes
   uint16_t block;
   const uint8_t *data;
   size_t data_len;
   TEST_ASSERT_EQUAL_INT( -1, TFTP_PKT_ParseData( buf, sizeof buf, &block, &data, &data_len ) );
}

void test_pkt_parse_error_rejects_no_nul(void)
{
   // ERROR packet with no NUL terminator in message
   uint8_t buf[] = { 0x00, 0x05, 0x00, 0x01, 'o', 'o', 'p', 's' };
   uint16_t code;
   const char *msg;
   TEST_ASSERT_EQUAL_INT( -1, TFTP_PKT_ParseError( buf, sizeof buf, &code, &msg ) );
}

/*---------------------------------------------------------------------------
 * tftp_util tests
 *---------------------------------------------------------------------------*/

void test_util_is_valid_filename_char_accepts_alphanumeric(void)
{
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( 'a' ) );
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( 'Z' ) );
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( '0' ) );
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( '.' ) );
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( '-' ) );
   TEST_ASSERT_TRUE( tftp_util_is_valid_filename_char( '_' ) );
}

void test_util_is_valid_filename_char_rejects_separators(void)
{
   TEST_ASSERT_FALSE( tftp_util_is_valid_filename_char( '/' ) );
   TEST_ASSERT_FALSE( tftp_util_is_valid_filename_char( '\\' ) );
}

void test_util_is_valid_filename_char_rejects_control_chars(void)
{
   TEST_ASSERT_FALSE( tftp_util_is_valid_filename_char( '\0' ) );
   TEST_ASSERT_FALSE( tftp_util_is_valid_filename_char( '\n' ) );
   TEST_ASSERT_FALSE( tftp_util_is_valid_filename_char( '\x7F' ) );
}

void test_util_create_ephemeral_socket_succeeds(void)
{
   struct sockaddr_in addr;
   int sfd = tftp_util_create_ephemeral_udp_socket( &addr );
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, sfd );
   TEST_ASSERT_EQUAL_INT( AF_INET, addr.sin_family );
   // Kernel should have assigned a non-zero port
   TEST_ASSERT_NOT_EQUAL( 0, ntohs(addr.sin_port) );
   (void)close( sfd );
}

void test_util_set_recv_timeout_succeeds(void)
{
   int sfd = tftp_util_create_ephemeral_udp_socket( NULL );
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, sfd );

   int rc = tftp_util_set_recv_timeout( sfd, 2 );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   (void)close( sfd );
}

/*---------------------------------------------------------------------------
 * Netascii Translation Tests
 *---------------------------------------------------------------------------*/

void test_util_netascii_bare_lf_becomes_crlf(void)
{
   // "hello\nworld" -> "hello\r\nworld"
   uint8_t in[] = "hello\nworld";
   uint8_t out[32];
   bool pending_cr = false;
   size_t n = tftp_util_octet_to_netascii(in, sizeof(in) - 1, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 12, n ); // "hello\r\nworld" = 12 bytes
   TEST_ASSERT_EQUAL_UINT8( '\r', out[5] );
   TEST_ASSERT_EQUAL_UINT8( '\n', out[6] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_bare_cr_becomes_cr_nul(void)
{
   // "a\rb" -> "a\r\0b"
   uint8_t in[] = { 'a', '\r', 'b' };
   uint8_t out[16];
   bool pending_cr = false;
   size_t n = tftp_util_octet_to_netascii(in, sizeof in, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 4, n );
   TEST_ASSERT_EQUAL_UINT8( 'a', out[0] );
   TEST_ASSERT_EQUAL_UINT8( '\r', out[1] );
   TEST_ASSERT_EQUAL_UINT8( '\0', out[2] );
   TEST_ASSERT_EQUAL_UINT8( 'b', out[3] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_crlf_passes_through(void)
{
   // "\r\n" -> "\r\n"
   uint8_t in[] = { '\r', '\n' };
   uint8_t out[8];
   bool pending_cr = false;
   size_t n = tftp_util_octet_to_netascii(in, sizeof in, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 2, n );
   TEST_ASSERT_EQUAL_UINT8( '\r', out[0] );
   TEST_ASSERT_EQUAL_UINT8( '\n', out[1] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_pending_cr_across_boundary(void)
{
   // First chunk ends with \r -- pending_cr should be true
   uint8_t in1[] = { 'a', '\r' };
   uint8_t out[16];
   bool pending_cr = false;
   size_t n1 = tftp_util_octet_to_netascii(in1, sizeof in1, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 2, n1 ); // 'a' + '\r'
   TEST_ASSERT_TRUE( pending_cr );

   // Next chunk starts with \n -- should complete the \r\n
   uint8_t in2[] = { '\n', 'b' };
   size_t n2 = tftp_util_octet_to_netascii(in2, sizeof in2, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 2, n2 ); // '\n' + 'b'
   TEST_ASSERT_EQUAL_UINT8( '\n', out[0] );
   TEST_ASSERT_EQUAL_UINT8( 'b', out[1] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_pending_cr_followed_by_non_lf(void)
{
   // First chunk: "x\r", second chunk: "y"
   uint8_t in1[] = { 'x', '\r' };
   uint8_t out[16];
   bool pending_cr = false;
   size_t n1 = tftp_util_octet_to_netascii(in1, sizeof in1, out, sizeof out, &pending_cr);
   TEST_ASSERT_TRUE( pending_cr );
   (void)n1;

   // Next chunk: 'y' -- pending \r becomes \r\0, then 'y'
   uint8_t in2[] = { 'y' };
   size_t n2 = tftp_util_octet_to_netascii(in2, sizeof in2, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 2, n2 ); // '\0' + 'y'
   TEST_ASSERT_EQUAL_UINT8( '\0', out[0] );
   TEST_ASSERT_EQUAL_UINT8( 'y', out[1] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_no_special_chars(void)
{
   uint8_t in[] = "hello";
   uint8_t out[16];
   bool pending_cr = false;
   size_t n = tftp_util_octet_to_netascii(in, 5, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 5, n );
   TEST_ASSERT_EQUAL_MEMORY( "hello", out, 5 );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_empty_input(void)
{
   uint8_t out[8];
   bool pending_cr = false;
   size_t n = tftp_util_octet_to_netascii(nullptr, 0, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 0, n );
}

/*---------------------------------------------------------------------------
 * Reverse Netascii Translation Tests (netascii -> octet)
 *---------------------------------------------------------------------------*/

void test_util_netascii_to_octet_crlf_becomes_lf(void)
{
   uint8_t in[] = { 'a', '\r', '\n', 'b' };
   uint8_t out[8];
   bool pending_cr = false;
   size_t n = tftp_util_netascii_to_octet(in, sizeof in, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 3, n ); // "a\nb"
   TEST_ASSERT_EQUAL_UINT8( 'a', out[0] );
   TEST_ASSERT_EQUAL_UINT8( '\n', out[1] );
   TEST_ASSERT_EQUAL_UINT8( 'b', out[2] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_to_octet_cr_nul_becomes_cr(void)
{
   uint8_t in[] = { 'a', '\r', '\0', 'b' };
   uint8_t out[8];
   bool pending_cr = false;
   size_t n = tftp_util_netascii_to_octet(in, sizeof in, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 3, n ); // "a\rb"
   TEST_ASSERT_EQUAL_UINT8( 'a', out[0] );
   TEST_ASSERT_EQUAL_UINT8( '\r', out[1] );
   TEST_ASSERT_EQUAL_UINT8( 'b', out[2] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_to_octet_pending_cr_boundary(void)
{
   // First chunk ends with \r
   uint8_t in1[] = { 'x', '\r' };
   uint8_t out[8];
   bool pending_cr = false;
   size_t n1 = tftp_util_netascii_to_octet(in1, sizeof in1, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 1, n1 ); // just 'x', \r is pending
   TEST_ASSERT_TRUE( pending_cr );

   // Second chunk: \n completes the \r\n -> \n
   uint8_t in2[] = { '\n', 'y' };
   size_t n2 = tftp_util_netascii_to_octet(in2, sizeof in2, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 2, n2 ); // '\n' + 'y'
   TEST_ASSERT_EQUAL_UINT8( '\n', out[0] );
   TEST_ASSERT_EQUAL_UINT8( 'y', out[1] );
   TEST_ASSERT_FALSE( pending_cr );
}

void test_util_netascii_to_octet_no_special(void)
{
   uint8_t in[] = "hello";
   uint8_t out[8];
   bool pending_cr = false;
   size_t n = tftp_util_netascii_to_octet(in, 5, out, sizeof out, &pending_cr);
   TEST_ASSERT_EQUAL_size_t( 5, n );
   TEST_ASSERT_EQUAL_MEMORY( "hello", out, 5 );
   TEST_ASSERT_FALSE( pending_cr );
}

/*---------------------------------------------------------------------------
 * Control Channel Tests
 *---------------------------------------------------------------------------*/

// Helper: send a UDP message and receive the reply
static ssize_t ctrl_send_recv(uint16_t port, const char *msg,
                               char *reply, size_t reply_cap)
{
   int sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 ) return -1;

   struct sockaddr_in dest = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };

   // Set recv timeout so test doesn't hang
   struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
   (void)setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

   (void)sendto(sfd, msg, strlen(msg), 0,
                (struct sockaddr *)&dest, sizeof dest);

   ssize_t n = recv(sfd, reply, reply_cap - 1, 0);
   if ( n > 0 ) reply[n] = '\0';

   (void)close(sfd);
   return n;
}

void test_ctrl_set_fault_and_get(void)
{
   // Use a high port unlikely to conflict
   uint16_t port = 39999;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Send SET_FAULT
   char reply[128];
   (void)sendto(socket(AF_INET, SOCK_DGRAM, 0), "SET_FAULT RRQ_TIMEOUT\n", 22, 0,
                (struct sockaddr *)&(struct sockaddr_in){
                   .sin_family = AF_INET,
                   .sin_port = htons(port),
                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                }, sizeof(struct sockaddr_in));

   // Simplified: use ctrl_send_recv helper
   ssize_t n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);

   // Poll to process the first sendto (the one without recv)
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);

   // Poll again for the ctrl_send_recv message
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);

   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // GET_FAULT
   n = ctrl_send_recv(port, "GET_FAULT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   // Reply was sent to the ctrl_send_recv socket which already closed,
   // but fault state should remain
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // RESET
   n = ctrl_send_recv(port, "RESET\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_set_fault_with_param(void)
{
   uint16_t port = 39998;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 5\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 5, fault.param );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_unknown_command(void)
{
   uint16_t port = 39997;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "BOGUS\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   // Fault should remain unchanged
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_unknown_fault_mode(void)
{
   uint16_t port = 39996;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT NONEXISTENT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode ); // unchanged
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

/*---------------------------------------------------------------------------
 * Test Runner
 *---------------------------------------------------------------------------*/

int main(void)
{
   UNITY_BEGIN();

   // tftp_err
   RUN_TEST( test_err_str_returns_non_null_for_all_codes );
   RUN_TEST( test_err_str_none_is_no_error );

   // tftp_log
   RUN_TEST( test_log_level_str_returns_expected_names );

   // tftp_parsecfg
   RUN_TEST( test_parsecfg_defaults_produces_sane_values );
   RUN_TEST( test_parsecfg_load_nonexistent_file_returns_error );

   // tftp_pkt: validation
   RUN_TEST( test_pkt_valid_rrq_octet );
   RUN_TEST( test_pkt_valid_wrq_netascii );
   RUN_TEST( test_pkt_valid_rrq_netascii_case_insensitive );
   RUN_TEST( test_pkt_reject_wrong_opcode );
   RUN_TEST( test_pkt_reject_missing_filename_nul );
   RUN_TEST( test_pkt_reject_empty_filename );
   RUN_TEST( test_pkt_reject_filename_with_slash );
   RUN_TEST( test_pkt_reject_filename_with_backslash );
   RUN_TEST( test_pkt_reject_filename_only_dots );
   RUN_TEST( test_pkt_reject_unsupported_mode_mail );
   RUN_TEST( test_pkt_reject_missing_mode_nul );
   RUN_TEST( test_pkt_reject_packet_too_short );
   RUN_TEST( test_pkt_reject_nonprintable_in_filename );

   // tftp_pkt: parsing
   RUN_TEST( test_pkt_parse_request_rrq );
   RUN_TEST( test_pkt_parse_request_wrq );

   // tftp_pkt: build/parse round-trips
   RUN_TEST( test_pkt_data_round_trip );
   RUN_TEST( test_pkt_data_empty_payload );
   RUN_TEST( test_pkt_data_full_512 );
   RUN_TEST( test_pkt_ack_round_trip );
   RUN_TEST( test_pkt_error_round_trip );

   // tftp_pkt: parse rejection
   RUN_TEST( test_pkt_parse_ack_rejects_short );
   RUN_TEST( test_pkt_parse_ack_rejects_wrong_opcode );
   RUN_TEST( test_pkt_parse_data_rejects_short );
   RUN_TEST( test_pkt_parse_error_rejects_no_nul );

   // tftp_util
   RUN_TEST( test_util_is_valid_filename_char_accepts_alphanumeric );
   RUN_TEST( test_util_is_valid_filename_char_rejects_separators );
   RUN_TEST( test_util_is_valid_filename_char_rejects_control_chars );
   RUN_TEST( test_util_create_ephemeral_socket_succeeds );
   RUN_TEST( test_util_set_recv_timeout_succeeds );

   // netascii translation
   RUN_TEST( test_util_netascii_bare_lf_becomes_crlf );
   RUN_TEST( test_util_netascii_bare_cr_becomes_cr_nul );
   RUN_TEST( test_util_netascii_crlf_passes_through );
   RUN_TEST( test_util_netascii_pending_cr_across_boundary );
   RUN_TEST( test_util_netascii_pending_cr_followed_by_non_lf );
   RUN_TEST( test_util_netascii_no_special_chars );
   RUN_TEST( test_util_netascii_empty_input );

   // reverse netascii (netascii -> octet)
   RUN_TEST( test_util_netascii_to_octet_crlf_becomes_lf );
   RUN_TEST( test_util_netascii_to_octet_cr_nul_becomes_cr );
   RUN_TEST( test_util_netascii_to_octet_pending_cr_boundary );
   RUN_TEST( test_util_netascii_to_octet_no_special );

   // control channel
   RUN_TEST( test_ctrl_set_fault_and_get );
   RUN_TEST( test_ctrl_set_fault_with_param );
   RUN_TEST( test_ctrl_unknown_command );
   RUN_TEST( test_ctrl_unknown_fault_mode );

   return UNITY_END();
}
