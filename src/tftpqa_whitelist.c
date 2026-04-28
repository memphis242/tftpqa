/**
 * @file tftpqa_whitelist.c
 * @brief IPv4 whitelist supporting single-host and CIDR subnet entries.
 * @date Apr 20, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>

#include <assert.h>
#include <errno.h>

#include <arpa/inet.h>

#include "tftpqa_whitelist.h"
#include "tftpqa_log.h"

/***************************** Local Declarations *****************************/

#define TFTP_IPWHITELIST_MAX        ((size_t)16)
#define TFTP_BLACKLIST_MAX_CAPACITY ((size_t)65536)
#define INITIAL_BLACKLIST_CAPACITY  ((size_t)4)

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

// We need a separate black-list because individual IPs may be blocked out of a
// given subnet that is on the whitelist, so it is not sufficient to block an IP
// by removing an entry from the whitelist.
// Also, for now, the blacklist will be a list of individual IPs, not subnets.
// On the performance pass-through, we'll try and add subnet functionality to
// optimize.
struct TFTP_IPBlackList
{
   uint32_t * addrs_nbo;
   size_t len;
   size_t cap;
};
static struct TFTP_IPBlackList s_blacklist;

static const char *skip_ws(const char *p);
static int parse_list( const char *s, struct TFTP_IPWhitelist *out );
static int parse_one_token( const char *tok,
                            size_t tok_len,
                            uint32_t *out_addr_nbo,
                            uint32_t *out_mask_nbo );

static bool on_blacklist(uint32_t ip_nbo);

/********************** Public Function Implementations ***********************/

int tftpqa_ipwhitelist_init(const char *s)
{
   struct TFTP_IPWhitelist tmp;
   if ( s == NULL || parse_list(s, &tmp) != 0 )
   {
      if ( s == NULL )
         tftpqa_log( TFTP_LOG_ERR, __func__, "NULL whitelist string — deny-all installed" );

      // Reset whitelist to deny-all on any failure
      s_whitelist.count = 0;
      memset(s_whitelist.addr_nbo, 0x00, sizeof s_whitelist.addr_nbo);
      memset(s_whitelist.mask_nbo, 0xFF, sizeof s_whitelist.mask_nbo);

      return -1;
   }

   s_whitelist = tmp;

   return 0;
}

bool tftpqa_ipwhitelist_is_deny_all(void)
{
   if ( s_whitelist.count == 0 )
      return true;

   // Check if the blacklist completely cancels out the whitelist
   // I'll do a basic linear at first, and then optimize on the performance
   // run through the codebase. TODO: Strong candidate for optimization!
   // Iterate through each whitelist item and try to find one that _isn't_ covered
   // by the blacklist
   for ( size_t i=0; i < s_whitelist.count; ++i )
   {
      uint32_t ip   = s_whitelist.addr_nbo[i];
      uint32_t mask = s_whitelist.mask_nbo[i];
      bool is_blocked = false;

      assert( (mask == 0 && ip == 0) || (mask != 0) );

      // 0.0.0.0/0 means allow-all, so there's no blacklisting that full range
      // at the moment, realistically.
      if ( mask == 0 )
         return false;

      uint32_t base_ip_of_subnet = ip & mask; // redundant, because whitelist IP
                                              // already stored at base of subnet,
                                              // but just to be defensive...
      uint32_t subnet_range = ~ntohl(mask) + 1;
      assert(subnet_range > 0);

      // Have to check every IP in subnet against blacklist...
      if ( subnet_range <= TFTP_BLACKLIST_MAX_CAPACITY )
      {
         for ( uint32_t j=0, candidate_ip = ntohl(base_ip_of_subnet);
               j < subnet_range; 
               ++j, ++candidate_ip )
         {
            if ( !( is_blocked = on_blacklist(htonl(candidate_ip)) ) )
               break;
         }
      }

      if ( !is_blocked ) // found whitelist IP that isn't blocked?
         return false;
   }

   return true;
}

bool tftpqa_ipwhitelist_contains(uint32_t ip_nbo)
{
   if ( s_whitelist.count == 0 || on_blacklist(ip_nbo) )
      return false;

   for ( size_t i = 0; i < s_whitelist.count; i++ )
   {
      if ( (ip_nbo & s_whitelist.mask_nbo[i]) == s_whitelist.addr_nbo[i] )
         return true;
   }

   return false;
}

int tftpqa_ipwhitelist_block(uint32_t ip_nbo)
{
   if ( ip_nbo == INADDR_ANY || ip_nbo == INADDR_BROADCAST )
   {
      tftpqa_log( TFTP_LOG_INFO, __func__,
                "Attempted to block an invalid IP address: %08X :: Not allowed",
                ntohl(ip_nbo) );
      return -1;
   }

   // Check if entry already exists on the blacklist...
   if ( on_blacklist(ip_nbo) )
      return 0;

   // Check if this is the first time
   assert( (s_blacklist.addrs_nbo == NULL && s_blacklist.cap == 0)
           || (s_blacklist.addrs_nbo != NULL && s_blacklist.cap > 0) );

   if ( s_blacklist.addrs_nbo == NULL )
   {
      // Allocate a new blacklist
      s_blacklist.addrs_nbo = malloc( INITIAL_BLACKLIST_CAPACITY * sizeof(uint32_t) );

      if ( s_blacklist.addrs_nbo == NULL )
      {
         tftpqa_log( TFTP_LOG_ERR, __func__,
                   "malloc() failed at blacklist creation. %s (%d) : %s"
                   " Unable to block %08X",
                   strerrorname_np(errno), errno, strerror(errno),
                   ntohl(ip_nbo) );
         s_blacklist.len = 0;
         s_blacklist.cap = 0;
         return -2;
      }
      else
      {
         s_blacklist.len = 0;
         s_blacklist.cap = INITIAL_BLACKLIST_CAPACITY;
      }
   }

   // Add to the blacklist
   // Expand blacklist vector if needed
   assert( s_blacklist.cap > 0 );

   if ( s_blacklist.len >= s_blacklist.cap )
   {
      size_t new_cap = s_blacklist.cap * 2;

      if ( new_cap > TFTP_BLACKLIST_MAX_CAPACITY )
      {
         tftpqa_log( TFTP_LOG_ERR, __func__,
                   "Blacklist is at maximum capacity! Unable to block %08X",
                   ntohl(ip_nbo) );
         return 1;
      }

      uint32_t * tmp = realloc( s_blacklist.addrs_nbo, new_cap * sizeof(uint32_t) );
      if ( tmp == NULL )
      {
         tftpqa_log( TFTP_LOG_ERR, __func__,
                   "realloc() failed at blacklist expansion!"
                   " %s (%d) : %s :: Unable to block %08X",
                   strerrorname_np(errno), errno, strerror(errno),
                   ntohl(ip_nbo) );
         return 2;
      }

      s_blacklist.addrs_nbo = tmp;
      s_blacklist.cap = new_cap;
   }

   assert( s_blacklist.cap <= TFTP_BLACKLIST_MAX_CAPACITY );
   assert( s_blacklist.len <  s_blacklist.cap );

   s_blacklist.addrs_nbo[s_blacklist.len++] = ip_nbo;

   char addrbuf[INET_ADDRSTRLEN];
   (void)inet_ntop( AF_INET, &(struct in_addr){ .s_addr = ip_nbo }, addrbuf, sizeof addrbuf );
   tftpqa_log( TFTP_LOG_INFO, NULL, "Blocking IP address: %s", addrbuf );

   return 0;
}

void tftpqa_ipwhitelist_clear(void)
{
   s_whitelist.count = 0;
   memset(s_whitelist.addr_nbo, 0x00, sizeof s_whitelist.addr_nbo);
   memset(s_whitelist.mask_nbo, 0xFF, sizeof s_whitelist.mask_nbo);

   free(s_blacklist.addrs_nbo);
   s_blacklist.addrs_nbo = NULL;
   s_blacklist.len = 0;
   s_blacklist.cap = 0;
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
         tftpqa_log( TFTP_LOG_ERR, __func__,
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
         tftpqa_log( TFTP_LOG_ERR, __func__,
                   "Empty entry (double comma, leading comma, or trailing comma) "
                   "— deny-all installed" );
         return -3;
      }

      uint32_t addr_nbo = 0;
      uint32_t mask_nbo = 0;
      if ( parse_one_token( tok_start, tok_len, &addr_nbo, &mask_nbo ) != 0 )
      {
         tftpqa_log( TFTP_LOG_ERR, __func__,
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
            tftpqa_log( TFTP_LOG_ERR, __func__,
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

static bool on_blacklist(uint32_t ip_nbo)
{
   assert( s_blacklist.len <= s_blacklist.cap);

   // TODO: Strong candidate for optimization!
   // For now, we will do a linear scan through; later on, I'll optimize this on
   // the performance pass through the codebase.
   for ( size_t i=0; i < s_blacklist.len; ++i )
      if ( ip_nbo == s_blacklist.addrs_nbo[i] )
         return true;

   return false;
}
