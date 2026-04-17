/**
 * @file test_tftp_util.c
 * @brief Unit tests for tftp_util module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftp_util.h"
#include "tftp_pkt.h"
#include "tftptest_common.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

void test_util_is_valid_filename_char_accepts_alphanumeric(void);
void test_util_is_valid_filename_char_rejects_separators(void);
void test_util_is_valid_filename_char_rejects_control_chars(void);
void test_util_create_ephemeral_socket_succeeds(void);
void test_util_set_recv_timeout_succeeds(void);
void test_util_netascii_bare_lf_becomes_crlf(void);
void test_util_netascii_bare_cr_becomes_cr_nul(void);
void test_util_netascii_crlf_passes_through(void);
void test_util_netascii_pending_cr_across_boundary(void);
void test_util_netascii_pending_cr_followed_by_non_lf(void);
void test_util_netascii_no_special_chars(void);
void test_util_netascii_empty_input(void);
void test_util_netascii_to_octet_crlf_becomes_lf(void);
void test_util_netascii_to_octet_cr_nul_becomes_cr(void);
void test_util_netascii_to_octet_pending_cr_boundary(void);
void test_util_netascii_to_octet_no_special(void);
void test_chroot_and_drop_non_root_succeeds(void);
void test_chroot_and_drop_bad_dir_fails(void);
void test_pkt_ack_block_zero(void);
void test_util_create_udp_socket_in_range_succeeds(void);
void test_util_create_udp_socket_in_range_single_port(void);
void test_util_create_udp_socket_in_range_all_busy(void);

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
   size_t n = tftp_util_octet_to_netascii(NULL, 0, out, sizeof out, &pending_cr);
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
 * chroot_and_drop tests (non-root only)
 *---------------------------------------------------------------------------*/

void test_chroot_and_drop_non_root_succeeds(void)
{
   // When not running as root, chroot_and_drop should chdir and return 0
   // (skipping actual chroot and privilege drop)
   if ( getuid() == 0 )
   {
      TEST_IGNORE_MESSAGE("Test skipped when running as root");
      return;
   }

   // Use /tmp as the directory (always exists and is writable)
   int rc = tftp_util_chroot_and_drop("/tmp", "nobody");
   TEST_ASSERT_EQUAL_INT( 0, rc );

   // Verify we're now in /tmp
   char cwd[PATH_MAX];
   TEST_ASSERT_NOT_NULL( getcwd(cwd, sizeof cwd) );
   TEST_ASSERT_EQUAL_STRING( "/tmp", cwd );
}

void test_chroot_and_drop_bad_dir_fails(void)
{
   int rc = tftp_util_chroot_and_drop("/nonexistent_dir_12345", "nobody");
   TEST_ASSERT_EQUAL_INT( -1, rc );
}

void test_pkt_ack_block_zero(void)
{
   uint8_t pkt[TFTP_ACK_SZ];
   size_t n = tftp_pkt_build_ack(pkt, sizeof pkt, 0);
   TEST_ASSERT_EQUAL( TFTP_ACK_SZ, n );

   uint16_t block;
   int rc = tftp_pkt_parse_ack(pkt, n, &block);
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_EQUAL_UINT16( 0, block );
}

void test_util_create_udp_socket_in_range_succeeds(void)
{
   struct sockaddr_in bound_addr = {0};
   int sfd = tftp_util_create_udp_socket_in_range(49200, 49210, &bound_addr);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, sfd );

   uint16_t port = ntohs(bound_addr.sin_port);
   TEST_ASSERT_GREATER_OR_EQUAL( 49200, port );
   TEST_ASSERT_LESS_OR_EQUAL( 49210, port );

   close(sfd);
}

void test_util_create_udp_socket_in_range_single_port(void)
{
   struct sockaddr_in bound_addr = {0};
   int sfd = tftp_util_create_udp_socket_in_range(49220, 49220, &bound_addr);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, sfd );
   TEST_ASSERT_EQUAL_UINT16( 49220, ntohs(bound_addr.sin_port) );
   close(sfd);
}

void test_util_create_udp_socket_in_range_all_busy(void)
{
   // Occupy a small range of 3 ports, then verify the function returns -1
   uint16_t base = 49230;
   int blockers[3];
   for ( int i = 0; i < 3; i++ )
   {
      blockers[i] = socket(AF_INET, SOCK_DGRAM, 0);
      TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, blockers[i] );

      struct sockaddr_in addr = {0};
      addr.sin_family      = AF_INET;
      addr.sin_port        = htons((uint16_t)(base + i));
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
      int brc = bind(blockers[i], (struct sockaddr *)&addr, sizeof addr);
      TEST_ASSERT_EQUAL_INT( 0, brc );
   }

   // Now try to create a socket in the fully occupied range
   int sfd = tftp_util_create_udp_socket_in_range(base, (uint16_t)(base + 2), NULL);
   TEST_ASSERT_EQUAL_INT( -1, sfd );

   for ( int i = 0; i < 3; i++ )
      close(blockers[i]);
}

/*---------------------------------------------------------------------------
 * suspicious text byte detection
 *---------------------------------------------------------------------------*/

void test_util_suspicious_text_clean_ascii(void)
{
   const uint8_t data[] = "Hello, world!\r\n";
   TEST_ASSERT_FALSE( tftp_util_has_suspicious_text_bytes(data, sizeof(data) - 1) );
}

void test_util_suspicious_text_allowed_controls(void)
{
   // HT, LF, VT, FF, CR, ESC — all allowed
   const uint8_t data[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1B };
   TEST_ASSERT_FALSE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_cr_nul_allowed(void)
{
   // CR+NUL is legitimate netascii for bare CR
   const uint8_t data[] = { 'A', '\r', '\0', 'B' };
   TEST_ASSERT_FALSE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_standalone_nul(void)
{
   // NUL not preceded by CR is suspicious
   const uint8_t data[] = { 'A', '\0', 'B' };
   TEST_ASSERT_TRUE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_leading_nul(void)
{
   // NUL at start of buffer — no preceding CR
   const uint8_t data[] = { '\0', 'A' };
   TEST_ASSERT_TRUE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_bell_char(void)
{
   // BEL (0x07) — not in the allowed set
   const uint8_t data[] = { 'A', 0x07, 'B' };
   TEST_ASSERT_TRUE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_del_char(void)
{
   // DEL (0x7F) — suspicious
   const uint8_t data[] = { 'A', 0x7F };
   TEST_ASSERT_TRUE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_high_byte(void)
{
   // Non-ASCII (0x80+) — suspicious
   const uint8_t data[] = { 'H', 'i', 0xC0 };
   TEST_ASSERT_TRUE( tftp_util_has_suspicious_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_empty_buffer(void)
{
   TEST_ASSERT_FALSE( tftp_util_has_suspicious_text_bytes(NULL, 0) );
}
