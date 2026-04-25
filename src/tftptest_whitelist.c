/**
 * @file tftptest_whitelist.c
 * @brief IPv4 whitelist supporting single-host and CIDR subnet entries.
 * @date Apr 20, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>

#include <arpa/inet.h>

#include "tftptest_whitelist.h"
#include "tftp_log.h"

/***************************** Local Declarations *****************************/

#define TFTP_IPWHITELIST_MAX ((size_t)16)

// Longest possible token: "255.255.255.255/32" = 18 chars + NUL = 19
#define MAX_TOKEN_LEN ((size_t)19)

struct TFTP_IPWhitelist
{
   uint32_t addr_nbo[TFTP_IPWHITELIST_MAX]; // pre-masked network address, NBO (Network Byte Order)
   uint32_t mask_nbo[TFTP_IPWHITELIST_MAX]; // NBO; 0xFFFFFFFF for /32, 0 for /0
   size_t   count;                          // 0 = deny all
};

// Module singleton
static struct TFTP_IPWhitelist s_whitelist;

static const char *skip_ws(const char *p);
static int parse_list( const char *s, struct TFTP_IPWhitelist *out );
static int parse_one_token( const char *tok,
                            size_t tok_len,
                            uint32_t *out_addr_nbo,
                            uint32_t *out_mask_nbo );

/********************** Public Function Implementations ***********************/

int tftp_ipwhitelist_init(const char *s)
{
   struct TFTP_IPWhitelist tmp;
   if ( s == NULL || parse_list(s, &tmp) != 0 )
   {
      if ( s == NULL )
         tftp_log( TFTP_LOG_ERR, __func__, "NULL whitelist string — deny-all installed" );

      // Reset to deny-all on any failure
      s_whitelist.count = 0;
      memset(s_whitelist.addr_nbo, 0x00, sizeof s_whitelist.addr_nbo);
      memset(s_whitelist.mask_nbo, 0xFF, sizeof s_whitelist.mask_nbo);
      return -1;
   }

   s_whitelist = tmp;
   return 0;
}

bool tftp_ipwhitelist_is_deny_all(void)
{
   return s_whitelist.count == 0;
}

bool tftp_ipwhitelist_contains(uint32_t ip_nbo)
{
   if ( s_whitelist.count == 0 )
      return false;

   for ( size_t i = 0; i < s_whitelist.count; i++ )
   {
      if ( (ip_nbo & s_whitelist.mask_nbo[i]) == s_whitelist.addr_nbo[i] )
         return true;
   }

   return false;
}

bool tftp_ipwhitelist_is_only_this_host(uint32_t ip_nbo)
{
   return s_whitelist.count == 1
       && s_whitelist.mask_nbo[0] == 0xFFFFFFFFu
       && s_whitelist.addr_nbo[0] == ip_nbo;
}

/*********************** Local Function Implementations ***********************/

static const char *skip_ws(const char *p)
{
   assert(p != NULL);
   while ( *p != '\0' && isspace( (unsigned char)*p ) )
      p++;
   return p;
}

static int parse_list(const char *s, struct TFTP_IPWhitelist *out)
{
   assert( s != NULL );
   assert( out != NULL );

   out->count = 0;

   const char *p = skip_ws( s );
   if ( *p == '\0' )
      return -1; // empty cfg

   while ( *p != '\0' )
   {
      if ( out->count >= TFTP_IPWHITELIST_MAX )
      {
         tftp_log( TFTP_LOG_ERR, __func__,
                   "Too many entries (max %zu) — deny-all installed", TFTP_IPWHITELIST_MAX );
         return -2; // overflow
      }

      // Find end of current token (next comma or end of string)
      const char *tok_start = p;
      while ( *p != '\0' && *p != ',' )
         p++;
      const char *tok_end = p;

      // Trim trailing whitespace within the token
      while ( tok_end > tok_start && isspace( (unsigned char)*(tok_end - 1) ) )
         tok_end--;

      size_t tok_len = (size_t)(tok_end - tok_start);
      if ( tok_len == 0 )
      {
         tftp_log( TFTP_LOG_ERR, __func__,
                   "Empty entry (double comma, leading comma, or trailing comma) "
                   "— deny-all installed" );
         return -3;
      }

      uint32_t addr_nbo = 0;
      uint32_t mask_nbo = 0;
      if ( parse_one_token( tok_start, tok_len, &addr_nbo, &mask_nbo ) != 0 )
      {
         tftp_log( TFTP_LOG_ERR, __func__,
                   "Malformed entry '%.*s' — deny-all installed",
                   (int)tok_len, tok_start );
         return -4;
      }

      out->addr_nbo[out->count] = addr_nbo;
      out->mask_nbo[out->count] = mask_nbo;
      out->count++;

      if ( *p == ',' )
      {
         p++; // consume comma
         p = skip_ws( p );
         if ( *p == '\0' ) // trailing comma
         {
            tftp_log( TFTP_LOG_ERR, __func__,
                      "Trailing comma in whitelist — deny-all installed" );
            return -5;
         }
      }
   }

   return 0;
}

// Parse a single trimmed token of form "a.b.c.d" or "a.b.c.d/N".
// Returns 0 on success, -1 on any malformed input.
static int parse_one_token( const char *tok,
                            size_t tok_len,
                            uint32_t *out_addr_nbo,
                            uint32_t *out_mask_nbo )
{
   if ( tok_len == 0 || tok_len >= MAX_TOKEN_LEN )
      return -1;

   // Copy into a NUL-terminated stack buffer for inet_aton / strtoul.
   char buf[MAX_TOKEN_LEN];
   memcpy( buf, tok, tok_len );
   buf[tok_len] = '\0';

   // Find optional '/' prefix separator
   char *slash = strchr( buf, '/' );
   unsigned int prefix = 32;

   if ( slash != NULL )
   {
      *slash = '\0';
      const char *pfx_str = slash + 1;

      if ( *pfx_str == '\0' )
         return -1; // trailing slash is not approved, e.g., "1.2.3.4/"

      // Must be all digits, no sign, no trailing junk
      for ( const char *q = pfx_str; *q != '\0'; q++ )
      {
         if ( !isdigit( (unsigned char)*q ) )
            return -2;
      }

      char *endptr = NULL;
      unsigned long v = strtoul( pfx_str, &endptr, 10 );
      if ( endptr == pfx_str || *endptr != '\0' || v > 32 )
         return -3;

      prefix = (unsigned int)v;
   }

   // Validate IP portion has no trailing garbage after inet_aton.
   // inet_aton accepts multiple forms (hex, octal, fewer-than-4 dotted
   // parts); reject those here to keep the spec tight: require exactly
   // four **decimal** octets, each 0..255, no leading '+'/'-'.
   {
      const char *q = buf;
      assert(q != NULL);

      for ( int i = 0; i < 4; i++ )
      {
         if ( !isdigit( (unsigned char)*q ) )
            return -4;

         unsigned int octet = 0;
         int digit_count = 0;
         while ( isdigit( (unsigned char)*q ) )
         {
            octet *= 10;
            octet += (unsigned int)( *q - '0' );
            q++;
            digit_count++;
            if ( digit_count > 3 || octet > 255 )
               return -5;
         }

         if ( i < 3 )
         {
            if ( *q != '.' )
               return -6;
            q++;  // skip dot, advance to next octet
         }
         else if ( *q != '\0' )
               return -7;  // trailing garbage after last octet
      }
   }

   // Using inet_aton() for now because we're IPv4-only
   // inet_aton() should be guaranteed to pass at this point, but we'll check
   // the return value anyways because I'm paranoid.
   struct in_addr addr;
   if ( inet_aton( buf, &addr ) == 0 )
      return -8;

   // Compute mask (host-order then to NBO). Special-case /0 to avoid UB.
   uint32_t mask_hbo = (prefix == 0) ? 0u : (0xFFFFFFFFu << (32 - prefix));
   uint32_t mask_nbo = htonl( mask_hbo );

   *out_addr_nbo = addr.s_addr & mask_nbo;
   *out_mask_nbo = mask_nbo;

   return 0;
}
