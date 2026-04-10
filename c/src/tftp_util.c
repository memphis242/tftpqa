/**
 * @file tftp_util.c
 * @brief Shared utility functions.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "tftp_util.h"

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
   if ( bound_addr != nullptr )
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

size_t tftp_util_octet_to_netascii(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_cap,
                                    bool *pending_cr)
{
   assert( in != nullptr || in_len == 0 );
   assert( out != nullptr );
   assert( pending_cr != nullptr );

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
   assert( in != nullptr || in_len == 0 );
   assert( out != nullptr );
   assert( pending_cr != nullptr );

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
