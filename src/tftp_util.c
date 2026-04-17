/**
 * @file tftp_util.c
 * @brief Shared utility functions.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "tftp_util.h"
#include "tftp_log.h"

/********************** Public Function Implementations ***********************/

int tftp_util_create_ephemeral_udp_socket(struct sockaddr_in *bound_addr)
{
   int sfd = socket( AF_INET, SOCK_DGRAM, 0 );
   if ( sfd < 0 )
      return -1;

   struct sockaddr_in addr;
   memset( &addr, 0, sizeof addr );
   addr.sin_family      = AF_INET;
   addr.sin_port        = htons( 0 ); // Let the kernel assign a port
   addr.sin_addr.s_addr = htonl( INADDR_ANY );

   if ( bind( sfd, (struct sockaddr *)&addr, sizeof addr ) != 0 )
   {
      (void)close( sfd );
      return -1;
   }

   // Retrieve the assigned address if caller wants it
   if ( bound_addr != NULL )
   {
      socklen_t addrlen = sizeof *bound_addr;
      if ( getsockname( sfd, (struct sockaddr *)bound_addr, &addrlen ) != 0 )
      {
         (void)close( sfd );
         return -1;
      }
   }

   return sfd;
}

int tftp_util_create_udp_socket_in_range(uint16_t port_min, uint16_t port_max,
                                         struct sockaddr_in *bound_addr)
{
   assert( port_min >= 1 );
   assert( port_max >= port_min );

   unsigned int range_size = (unsigned int)(port_max - port_min) + 1;
   unsigned int start = (unsigned int)rand() % range_size;

   for ( unsigned int i = 0; i < range_size; i++ )
   {
      uint16_t port = (uint16_t)(port_min + ((start + i) % range_size));

      int sfd = socket( AF_INET, SOCK_DGRAM, 0 );
      if ( sfd < 0 )
         return -1;

      struct sockaddr_in addr;
      memset( &addr, 0, sizeof addr );
      addr.sin_family      = AF_INET;
      addr.sin_port        = htons( port );
      addr.sin_addr.s_addr = htonl( INADDR_ANY );

      if ( bind( sfd, (struct sockaddr *)&addr, sizeof addr ) != 0 )
      {
         (void)close( sfd );
         if ( errno == EADDRINUSE )
            continue;
         return -1;  // Unexpected error (e.g., EACCES)
      }

      // Bound successfully
      if ( bound_addr != NULL )
      {
         socklen_t addrlen = sizeof *bound_addr;
         if ( getsockname( sfd, (struct sockaddr *)bound_addr, &addrlen ) != 0 )
         {
            (void)close( sfd );
            return -1;
         }
      }

      return sfd;
   }

   // All ports in range exhausted
   errno = EADDRINUSE;
   return -1;
}

int tftp_util_set_recv_timeout(int sfd, unsigned int timeout_sec)
{
   assert( sfd >= 0 );

   struct timeval tv = {
      .tv_sec  = (time_t)timeout_sec,
      .tv_usec = 0,
   };

   return setsockopt( sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv );
}

bool tftp_util_is_valid_filename_char(char c)
{
   // Allow printable ASCII (0x20..0x7E) excluding path separators
   if ( c < 0x20 || c > 0x7E )
      return false;
   if ( c == '/' || c == '\\' )
      return false;
   return true;
}

enum TFTPUtil_TextCheck tftp_util_check_text_bytes(const uint8_t *data, size_t len)
{
   assert(data != NULL || len == 0);

   bool prev_cr  = false;
   bool saw_utf8 = false;

   for ( size_t i = 0; i < len; i++ )
   {
      uint8_t byte = data[i];

      // CR+NUL is legitimate in netascii (encodes bare CR)
      if ( byte == 0x00 )
      {
         if ( prev_cr )
         {
            prev_cr = false;
            continue;
         }
         return TFTP_TEXT_SUSPICIOUS; // standalone NUL
      }

      prev_cr = (byte == '\r');

      // Printable ASCII
      if ( byte >= 0x20 && byte <= 0x7E )
         continue;

      // Standard text control characters
      switch ( byte )
      {
         case 0x09: // HT  (horizontal tab)
         case 0x0A: // LF  (line feed)
         case 0x0B: // VT  (vertical tab)
         case 0x0C: // FF  (form feed)
         case 0x0D: // CR  (carriage return)
         case 0x1B: // ESC (ANSI escape sequences)
            continue;
         default:
            break;
      }

      // Check for valid UTF-8 multi-byte sequences
      if ( byte >= 0x80 )
      {
         int expected;
         if      ( byte >= 0xC2 && byte <= 0xDF ) expected = 1;
         else if ( byte >= 0xE0 && byte <= 0xEF ) expected = 2;
         else if ( byte >= 0xF0 && byte <= 0xF4 ) expected = 3;
         else return TFTP_TEXT_SUSPICIOUS;  // 0x80-0xBF, 0xC0-0xC1, 0xF5+

         // Check for truncated sequence
         if ( i + (size_t)expected >= len )
            return TFTP_TEXT_SUSPICIOUS;

         // Reject overlong / out-of-range encodings
         if ( byte == 0xE0 && data[i + 1] < 0xA0 )
            return TFTP_TEXT_SUSPICIOUS;
         if ( byte == 0xF0 && data[i + 1] < 0x90 )
            return TFTP_TEXT_SUSPICIOUS;
         if ( byte == 0xF4 && data[i + 1] > 0x8F )
            return TFTP_TEXT_SUSPICIOUS;

         // Verify all continuation bytes are 0x80-0xBF
         for ( int j = 1; j <= expected; j++ )
         {
            if ( (data[i + (size_t)j] & 0xC0) != 0x80 )
               return TFTP_TEXT_SUSPICIOUS;
         }

         i += (size_t)expected;
         saw_utf8 = true;
         continue;
      }

      // Everything else is suspicious: other C0 controls (0x01-0x08,
      // 0x0E-0x1A, 0x1C-0x1F), DEL (0x7F)
      return TFTP_TEXT_SUSPICIOUS;
   }

   return saw_utf8 ? TFTP_TEXT_HAS_UTF8 : TFTP_TEXT_CLEAN;
}

size_t tftp_util_octet_to_netascii(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_cap,
                                    bool *pending_cr)
{
   assert( in != NULL || in_len == 0 );
   assert( out != NULL );
   assert( pending_cr != NULL );

   size_t o = 0;

   for ( size_t i = 0; i < in_len && o < out_cap; i++ )
   {
      uint8_t c = in[i];

      if ( *pending_cr )
      {
         *pending_cr = false;
         if ( c == '\n' )
         {
            // Previous \r + this \n = \r\n (already wrote \r)
            if ( o < out_cap )
               out[o++] = '\n';
            continue;
         }
         else
         {
            // Previous \r was bare -- emit \0 after it, then process this byte
            if ( o < out_cap )
               out[o++] = '\0';
            // Fall through to process current byte
         }
      }

      if ( c == '\r' )
      {
         // Write the \r now; next byte determines if it's \r\n or \r\0
         if ( o < out_cap )
            out[o++] = '\r';
         *pending_cr = true;
      }
      else if ( c == '\n' )
      {
         // Bare \n -- convert to \r\n
         if ( o + 1 < out_cap )
         {
            out[o++] = '\r';
            out[o++] = '\n';
         }
         else if ( o < out_cap )
         {
            // Only room for \r -- we lose the \n (caller should size buffer)
            out[o++] = '\r';
         }
      }
      else
      {
         out[o++] = c;
      }
   }

   return o;
}

size_t tftp_util_netascii_to_octet(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_cap,
                                    bool *pending_cr)
{
   assert( in != NULL || in_len == 0 );
   assert( out != NULL );
   assert( pending_cr != NULL );

   size_t o = 0;

   for ( size_t i = 0; i < in_len && o < out_cap; i++ )
   {
      uint8_t c = in[i];

      if ( *pending_cr )
      {
         *pending_cr = false;
         if ( c == '\n' )
         {
            // \r\n -> \n
            if ( o < out_cap )
               out[o++] = '\n';
            continue;
         }
         else if ( c == '\0' )
         {
            // \r\0 -> \r
            if ( o < out_cap )
               out[o++] = '\r';
            continue;
         }
         else
         {
            // Bare \r followed by something else -- emit \r, then process c
            if ( o < out_cap )
               out[o++] = '\r';
            // Fall through to process current byte
         }
      }

      if ( c == '\r' )
      {
         *pending_cr = true;
      }
      else
      {
         out[o++] = c;
      }
   }

   return o;
}

int tftp_util_chroot_and_drop(const char *dir, const char *user)
{
   assert( dir != NULL );
   assert( user != NULL );

   // Step 1: chdir into the target directory
   if ( chdir( dir ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "chdir('%s') failed: %s", dir, strerror( errno ) );
      return -1;
   }

   // If not running as root, skip chroot and privilege drop
   if ( getuid() != 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__,
                "Not running as root (uid=%ju); skipping chroot and privilege drop",
                (uintmax_t)getuid() );
      return 0;
   }

   // Step 2: chroot into the current directory
   if ( chroot( "." ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "chroot('.') failed: %s", strerror( errno ) );
      return -1;
   }

   // Step 3: chdir to new root so relative paths work
   if ( chdir( "/" ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "chdir('/') after chroot failed: %s", strerror( errno ) );
      return -1;
   }

   // Step 4: Look up the target user
   errno = 0;
   struct passwd *pw = getpwnam( user );
   if ( pw == NULL )
   {
      if ( errno != 0 )
         tftp_log( TFTP_LOG_ERR, __func__, "getpwnam('%s') failed: %s", user, strerror( errno ) );
      else
         tftp_log( TFTP_LOG_ERR, __func__, "User '%s' not found", user );
      return -1;
   }

   // Step 5: Drop group privileges first (must do before setuid)
   if ( setgid( pw->pw_gid ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "setgid(%ju) failed: %s",
                (uintmax_t)pw->pw_gid, strerror( errno ) );
      return -1;
   }

   // Drop supplementary groups
   if ( setgroups( 0, NULL ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "setgroups(0, NULL) failed: %s", strerror( errno ) );
      return -1;
   }

   // Step 6: Drop user privileges (point of no return)
   if ( setuid( pw->pw_uid ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "setuid(%ju) failed: %s",
                (uintmax_t)pw->pw_uid, strerror( errno ) );
      return -1;
   }

   // Step 7: Verify we actually dropped privileges
   assert( getuid() != 0 );
   assert( geteuid() != 0 );

   tftp_log( TFTP_LOG_INFO, NULL, "Chrooted to '%s', running as user '%s' (uid=%ju, gid=%ju)",
             dir, user, (uintmax_t)pw->pw_uid, (uintmax_t)pw->pw_gid );

   return 0;
}
