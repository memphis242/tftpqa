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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
void test_util_check_read_perms_world_readable(void);
void test_util_check_read_perms_not_world_readable(void);
void test_util_check_read_perms_setuid_rejected(void);
void test_util_check_read_perms_directory_rejected(void);
void test_util_check_write_perms_world_writable(void);
void test_util_check_write_perms_not_world_writable(void);
void test_util_open_for_read_rejects_symlink(void);
void test_util_open_for_write_creates_with_mode(void);
void test_util_open_for_write_overwrites_existing(void);
void test_util_open_for_write_create_mode_stripped_by_umask(void);

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
 * text byte check (suspicious / UTF-8 / clean)
 *---------------------------------------------------------------------------*/

void test_util_suspicious_text_clean_ascii(void)
{
   const uint8_t data[] = "Hello, world!\r\n";
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_CLEAN, tftp_util_check_text_bytes(data, sizeof(data) - 1) );
}

void test_util_suspicious_text_allowed_controls(void)
{
   // HT, LF, VT, FF, CR, ESC — all allowed
   const uint8_t data[] = { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x1B };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_CLEAN, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_cr_nul_allowed(void)
{
   // CR+NUL is legitimate netascii for bare CR
   const uint8_t data[] = { 'A', '\r', '\0', 'B' };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_CLEAN, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_standalone_nul(void)
{
   // NUL not preceded by CR is suspicious
   const uint8_t data[] = { 'A', '\0', 'B' };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_leading_nul(void)
{
   // NUL at start of buffer — no preceding CR
   const uint8_t data[] = { '\0', 'A' };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_bell_char(void)
{
   // BEL (0x07) — not in the allowed set
   const uint8_t data[] = { 'A', 0x07, 'B' };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_del_char(void)
{
   // DEL (0x7F) — suspicious
   const uint8_t data[] = { 'A', 0x7F };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_high_byte(void)
{
   // 0xC0 is an invalid UTF-8 lead byte (overlong) — suspicious
   const uint8_t data[] = { 'H', 'i', 0xC0 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_suspicious_text_empty_buffer(void)
{
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_CLEAN, tftp_util_check_text_bytes(NULL, 0) );
}

/*---------------------------------------------------------------------------
 * UTF-8 awareness tests
 *---------------------------------------------------------------------------*/

void test_util_text_check_valid_utf8_2byte(void)
{
   // "café" — 0xC3 0xA9 is é (U+00E9)
   const uint8_t data[] = { 'c', 'a', 'f', 0xC3, 0xA9 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_HAS_UTF8, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_valid_utf8_3byte(void)
{
   // € (U+20AC) = 0xE2 0x82 0xAC
   const uint8_t data[] = { 0xE2, 0x82, 0xAC };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_HAS_UTF8, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_valid_utf8_4byte(void)
{
   // 🎉 (U+1F389) = 0xF0 0x9F 0x8E 0x89
   const uint8_t data[] = { 0xF0, 0x9F, 0x8E, 0x89 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_HAS_UTF8, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_utf8_mixed_with_ascii(void)
{
   // "Hello café\n" — mixed ASCII + UTF-8
   const uint8_t data[] = { 'H', 'e', 'l', 'l', 'o', ' ',
                             'c', 'a', 'f', 0xC3, 0xA9, '\n' };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_HAS_UTF8, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_lone_continuation_byte(void)
{
   // 0x80 alone — continuation byte without lead
   const uint8_t data[] = { 0x80 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_overlong_2byte(void)
{
   // 0xC0 0x80 — overlong encoding of U+0000
   const uint8_t data[] = { 0xC0, 0x80 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_truncated_sequence(void)
{
   // 0xC3 at end of buffer — truncated 2-byte sequence
   const uint8_t data[] = { 0xC3 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_overlong_3byte(void)
{
   // 0xE0 0x80 0x80 — overlong encoding (second byte < 0xA0)
   const uint8_t data[] = { 0xE0, 0x80, 0x80 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

void test_util_text_check_above_max_codepoint(void)
{
   // 0xF4 0x90 0x80 0x80 — above U+10FFFF
   const uint8_t data[] = { 0xF4, 0x90, 0x80, 0x80 };
   TEST_ASSERT_EQUAL_INT( TFTP_TEXT_SUSPICIOUS, tftp_util_check_text_bytes(data, sizeof(data)) );
}

/*---------------------------------------------------------------------------
 * File-Permission Helper Tests
 *---------------------------------------------------------------------------*/

// Small helper: create a tempfile with given contents and mode, return path.
// The returned pointer is the same buffer passed in. chmod() is explicit so
// test outcomes do not depend on the host umask.
static int perm_test_make_tmpfile(char *template_buf, mode_t mode)
{
   int fd = mkstemp(template_buf);
   if ( fd < 0 )
      return -1;
   if ( fchmod(fd, mode) != 0 )
   {
      (void)close(fd);
      (void)unlink(template_buf);
      return -1;
   }
   return fd;
}

void test_util_check_read_perms_world_readable(void)
{
   char path[] = "/tmp/tftptest_perm_rd_XXXXXX";
   int fd = perm_test_make_tmpfile(path, 0644);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   mode_t observed = 0;
   enum TFTPUtil_PermCheck rc = tftp_util_check_read_perms(fd, &observed);
   TEST_ASSERT_EQUAL_INT( TFTP_PERM_OK, rc );
   TEST_ASSERT_EQUAL_UINT( 0644, observed & 0777 );

   (void)close(fd);
   (void)unlink(path);
}

void test_util_check_read_perms_not_world_readable(void)
{
   char path[] = "/tmp/tftptest_perm_nr_XXXXXX";
   int fd = perm_test_make_tmpfile(path, 0640);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   mode_t observed = 0;
   enum TFTPUtil_PermCheck rc = tftp_util_check_read_perms(fd, &observed);
   TEST_ASSERT_EQUAL_INT( TFTP_PERM_NOT_WORLD_READABLE, rc );
   TEST_ASSERT_EQUAL_UINT( 0640, observed & 0777 );

   (void)close(fd);
   (void)unlink(path);
}

void test_util_check_read_perms_setuid_rejected(void)
{
   char path[] = "/tmp/tftptest_perm_su_XXXXXX";
   // 04644 = setuid + world-readable. Setuid check must win over world-read.
   int fd = perm_test_make_tmpfile(path, 04644);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   // Some systems silently strip S_ISUID on non-root chmod. Only run the
   // assertion if the bit actually stuck.
   struct stat st;
   TEST_ASSERT_EQUAL_INT( 0, fstat(fd, &st) );
   if ( st.st_mode & S_ISUID )
   {
      mode_t observed = 0;
      enum TFTPUtil_PermCheck rc = tftp_util_check_read_perms(fd, &observed);
      TEST_ASSERT_EQUAL_INT( TFTP_PERM_SETUID_SETGID, rc );
   }

   (void)close(fd);
   (void)unlink(path);
}

void test_util_check_read_perms_directory_rejected(void)
{
   // /tmp itself is a directory that the test user can open O_RDONLY.
   int fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   mode_t observed = 0;
   enum TFTPUtil_PermCheck rc = tftp_util_check_read_perms(fd, &observed);
   TEST_ASSERT_EQUAL_INT( TFTP_PERM_NOT_REGULAR, rc );

   (void)close(fd);
}

void test_util_check_write_perms_world_writable(void)
{
   char path[] = "/tmp/tftptest_perm_ww_XXXXXX";
   int fd = perm_test_make_tmpfile(path, 0666);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   mode_t observed = 0;
   enum TFTPUtil_PermCheck rc = tftp_util_check_write_perms(fd, &observed);
   TEST_ASSERT_EQUAL_INT( TFTP_PERM_OK, rc );
   TEST_ASSERT_EQUAL_UINT( 0666, observed & 0777 );

   (void)close(fd);
   (void)unlink(path);
}

void test_util_check_write_perms_not_world_writable(void)
{
   char path[] = "/tmp/tftptest_perm_nw_XXXXXX";
   int fd = perm_test_make_tmpfile(path, 0664);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );

   mode_t observed = 0;
   enum TFTPUtil_PermCheck rc = tftp_util_check_write_perms(fd, &observed);
   TEST_ASSERT_EQUAL_INT( TFTP_PERM_NOT_WORLD_WRITABLE, rc );
   TEST_ASSERT_EQUAL_UINT( 0664, observed & 0777 );

   (void)close(fd);
   (void)unlink(path);
}

void test_util_open_for_read_rejects_symlink(void)
{
   // Create a target file, then a symlink to it. open_for_read must refuse
   // the symlink (final path component) with ELOOP.
   char target[] = "/tmp/tftptest_sym_tgt_XXXXXX";
   int tfd = perm_test_make_tmpfile(target, 0644);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, tfd );
   (void)close(tfd);

   char linkpath[] = "/tmp/tftptest_sym_lnk_XXXXXX";
   int dummyfd = mkstemp(linkpath);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, dummyfd );
   (void)close(dummyfd);
   TEST_ASSERT_EQUAL_INT( 0, unlink(linkpath) );  // make room for symlink
   TEST_ASSERT_EQUAL_INT( 0, symlink(target, linkpath) );

   errno = 0;
   int fd = tftp_util_open_for_read(linkpath);
   int saved_errno = errno;
   TEST_ASSERT_EQUAL_INT( -1, fd );
   TEST_ASSERT_EQUAL_INT( ELOOP, saved_errno );

   (void)unlink(linkpath);
   (void)unlink(target);
}

void test_util_open_for_write_creates_with_mode(void)
{
   char path[] = "/tmp/tftptest_open_cr_XXXXXX";
   int dummy = mkstemp(path);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, dummy );
   (void)close(dummy);
   TEST_ASSERT_EQUAL_INT( 0, unlink(path) );

   mode_t saved = umask(0);

   bool created = false;
   int fd = tftp_util_open_for_write(path, 0666, &created);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );
   TEST_ASSERT_TRUE( created );

   struct stat st;
   TEST_ASSERT_EQUAL_INT( 0, fstat(fd, &st) );
   TEST_ASSERT_EQUAL_UINT( 0666, st.st_mode & 0777 );

   (void)umask(saved);
   (void)close(fd);
   (void)unlink(path);
}

void test_util_open_for_write_overwrites_existing(void)
{
   char path[] = "/tmp/tftptest_open_ov_XXXXXX";
   int fd = perm_test_make_tmpfile(path, 0666);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );
   // Write some data so we can verify truncation
   const char *junk = "stale contents";
   TEST_ASSERT_EQUAL_INT( (int)strlen(junk), (int)write(fd, junk, strlen(junk)) );
   (void)close(fd);

   bool created = true;  // should be flipped to false
   int wfd = tftp_util_open_for_write(path, 0666, &created);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, wfd );
   TEST_ASSERT_FALSE( created );

   struct stat st;
   TEST_ASSERT_EQUAL_INT( 0, fstat(wfd, &st) );
   TEST_ASSERT_EQUAL_INT( 0, (int)st.st_size );  // O_TRUNC

   (void)close(wfd);
   (void)unlink(path);
}

void test_util_open_for_write_create_mode_stripped_by_umask(void)
{
   char path[] = "/tmp/tftptest_open_um_XXXXXX";
   int dummy = mkstemp(path);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, dummy );
   (void)close(dummy);
   TEST_ASSERT_EQUAL_INT( 0, unlink(path) );

   mode_t saved = umask(0022);

   bool created = false;
   int fd = tftp_util_open_for_write(path, 0666, &created);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, fd );
   TEST_ASSERT_TRUE( created );

   struct stat st;
   TEST_ASSERT_EQUAL_INT( 0, fstat(fd, &st) );
   // Umask 0022 strips group/other write bits -> 0644
   TEST_ASSERT_EQUAL_UINT( 0644, st.st_mode & 0777 );

   (void)umask(saved);
   (void)close(fd);
   (void)unlink(path);
}
