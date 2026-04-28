/**
 * @file test_tftptest_whitelist.c
 * @brief Unit tests for tftptest_whitelist module.
 * @date Apr 20, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftptest_whitelist.h"

#include <arpa/inet.h>
#include <stdio.h>

/* Forward declarations */
void test_ipwhitelist_empty_is_deny_all(void);
void test_ipwhitelist_is_deny_all_true(void);
void test_ipwhitelist_is_deny_all_false(void);
void test_ipwhitelist_single_bare_ip(void);
void test_ipwhitelist_single_cidr_24(void);
void test_ipwhitelist_host_bits_normalized(void);
void test_ipwhitelist_mixed_list_with_whitespace(void);
void test_ipwhitelist_prefix_zero_matches_all(void);
void test_ipwhitelist_prefix_32_explicit_vs_bare(void);
void test_ipwhitelist_overflow_rejected(void);
void test_ipwhitelist_malformed_results_in_deny_all(void);
void test_ipwhitelist_null_input_is_deny_all(void);
void test_ipwhitelist_malformed_trailing_comma(void);
void test_ipwhitelist_malformed_empty_middle_token(void);
void test_ipwhitelist_malformed_non_numeric_prefix(void);
void test_ipwhitelist_malformed_prefix_33(void);
void test_ipwhitelist_malformed_negative_prefix(void);
void test_ipwhitelist_malformed_bad_octet(void);
void test_ipwhitelist_malformed_trailing_garbage(void);
void test_ipwhitelist_malformed_trailing_slash(void);
void test_ipwhitelist_malformed_double_slash(void);
void test_ipwhitelist_matcher_slash_32(void);
void test_ipwhitelist_matcher_slash_24_boundary(void);
void test_ipwhitelist_matcher_multiple_entries(void);
void test_ipwhitelist_init_resets_singleton(void);

/* Blacklist & combined whitelist+blacklist */
void test_ipwhitelist_block_whitelisted_ip_excluded(void);
void test_ipwhitelist_block_non_whitelisted_ip_stays_false(void);
void test_ipwhitelist_block_duplicate_is_noop(void);
void test_ipwhitelist_block_invalid_inaddr_any(void);
void test_ipwhitelist_block_invalid_broadcast(void);
void test_ipwhitelist_block_one_from_subnet(void);
void test_ipwhitelist_block_multiple_from_subnet(void);
void test_ipwhitelist_block_subnet_boundary_ips(void);
void test_ipwhitelist_clear_resets_to_deny_all(void);
void test_ipwhitelist_clear_resets_blacklist(void);
void test_ipwhitelist_clear_twice_is_safe(void);
void test_ipwhitelist_init_does_not_reset_blacklist(void);
void test_ipwhitelist_block_forces_growth(void);
void test_ipwhitelist_block_with_allow_all_whitelist(void);

/* is_deny_all() blacklist-shadowing scenarios */
void test_ipwhitelist_is_deny_all_when_only_host_blocked(void);
void test_ipwhitelist_is_deny_all_allow_all_with_blocked_ips(void);
void test_ipwhitelist_is_deny_all_subnet_fully_shadowed(void);
void test_ipwhitelist_is_deny_all_subnet_partially_shadowed(void);
void test_ipwhitelist_is_deny_all_multi_entry_all_shadowed(void);
void test_ipwhitelist_is_deny_all_multi_entry_one_live(void);

/* Additional clear() tests */
void test_ipwhitelist_clear_allows_reblock(void);
void test_ipwhitelist_clear_then_reblock_is_deny_all(void);

/* Helper: convert "a.b.c.d" to NBO uint32 */
static uint32_t ipn(const char *s)
{
   struct in_addr a;
   TEST_ASSERT_EQUAL_INT( 1, inet_aton(s, &a) );
   return a.s_addr;
}

void test_ipwhitelist_empty_is_deny_all(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("1.2.3.4")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("255.255.255.255")) );
}

void test_ipwhitelist_is_deny_all_true(void)
{
   tftptest_ipwhitelist_init("");
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_false(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_single_bare_ip(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.20.30.40") );
   // Bare IP is /32: only exact match admitted
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.20.30.40")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.20.30.41")) );
}

void test_ipwhitelist_single_cidr_24(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("192.168.1.0/24") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.123")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.255")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.0.255")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.2.0")) );
}

void test_ipwhitelist_host_bits_normalized(void)
{
   // Host bits in CIDR are silently normalized: /24 of 192.168.1.5 should
   // behave identically to /24 of 192.168.1.0.
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("192.168.1.5/24") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.5")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.200")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.2.5")) );
}

void test_ipwhitelist_mixed_list_with_whitespace(void)
{
   int rc = tftptest_ipwhitelist_init(
      "  10.0.0.1 , 192.168.0.0/16,172.16.5.5/32 " );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.200.7")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("172.16.5.5")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_prefix_zero_matches_all(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("1.2.3.4/0") );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("0.0.0.0")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("255.255.255.255")) );
}

void test_ipwhitelist_prefix_32_explicit_vs_bare(void)
{
   // "10.0.0.1" and "10.0.0.1/32" must produce identical matching behavior.
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1/32") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_overflow_rejected(void)
{
   // Overshoot the capacity without relying on its exposed value.
   // Any reasonable ceiling (module is currently 16) is well under 100.
   char buf[1024];
   int off = 0;
   for ( int i = 0; i < 100; i++ )
   {
      int n = snprintf(buf + off, sizeof buf - (size_t)off,
                       "%s10.0.%d.%d",
                       (i == 0) ? "" : ",",
                       (i >> 8) & 0xFF, i & 0xFF);
      TEST_ASSERT_TRUE( n > 0 );
      off += n;
   }
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init(buf) );
}

void test_ipwhitelist_malformed_results_in_deny_all(void)
{
   // Install a known-good whitelist, then attempt a malformed parse.
   // On failure the singleton is reset to deny-all (fail-closed).
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/33") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_null_input_is_deny_all(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init(NULL) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_malformed_trailing_comma(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4,") );
}

void test_ipwhitelist_malformed_empty_middle_token(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4,,5.6.7.8") );
}

void test_ipwhitelist_malformed_non_numeric_prefix(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/abc") );
}

void test_ipwhitelist_malformed_prefix_33(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/33") );
}

void test_ipwhitelist_malformed_negative_prefix(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/-1") );
}

void test_ipwhitelist_malformed_bad_octet(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("999.0.0.1") );
}

void test_ipwhitelist_malformed_trailing_garbage(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4abc") );
}

void test_ipwhitelist_malformed_trailing_slash(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/") );
}

void test_ipwhitelist_malformed_double_slash(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_init("1.2.3.4/24/extra") );
}

void test_ipwhitelist_matcher_slash_32(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1/32") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_matcher_slash_24_boundary(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("192.168.1.0/24") );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("192.168.1.255")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.2.0")) );
}

void test_ipwhitelist_matcher_multiple_entries(void)
{
   TEST_ASSERT_EQUAL_INT( 0,
      tftptest_ipwhitelist_init("10.0.0.1, 192.168.0.0/16, 172.16.5.5") );

   // First entry
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   // Middle entry
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.99.99")) );
   // Last entry
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("172.16.5.5")) );
   // Not in whitelist
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("8.8.8.8")) );
}


void test_ipwhitelist_init_resets_singleton(void)
{
   // Install a whitelist, then init() should reset it to deny-all (count == 0).
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   tftptest_ipwhitelist_init("");
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

/* ===== Blacklist & combined whitelist+blacklist tests =====
 *
 * Each test begins with tftptest_ipwhitelist_clear() so the blacklist — which
 * persists across init() calls — doesn't bleed state between tests.
 */

void test_ipwhitelist_block_whitelisted_ip_excluded(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_block_non_whitelisted_ip_stays_false(void)
{
   // Blocking an IP that was never whitelisted is allowed, but contains()
   // remains false — it was already rejected by the whitelist.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.2")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
   // Original whitelisted IP is unaffected
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_block_duplicate_is_noop(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.3")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.1.2.3")) );

   // Second block of the same IP: idempotent, returns 0
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.3")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.1.2.3")) );
}

void test_ipwhitelist_block_invalid_inaddr_any(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_block(htonl(INADDR_ANY)) );
}

void test_ipwhitelist_block_invalid_broadcast(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_EQUAL_INT( -1, tftptest_ipwhitelist_block(htonl(INADDR_BROADCAST)) );
}

void test_ipwhitelist_block_one_from_subnet(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("192.168.1.0/24") );

   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.1.10")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.1.100")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.1.200")) );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("192.168.1.10")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.1.10")) );

   // Rest of subnet still accessible
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.1.100")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("192.168.1.200")) );
   // Outside the subnet, still rejected
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("192.168.2.10")) );
}

void test_ipwhitelist_block_multiple_from_subnet(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("172.16.0.0/16") );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("172.16.0.1")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("172.16.1.1")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("172.16.2.1")) );

   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("172.16.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("172.16.1.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("172.16.2.1")) );

   // Non-blocked members of the subnet remain accessible
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("172.16.0.2")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("172.16.5.5")) );
}

void test_ipwhitelist_block_subnet_boundary_ips(void)
{
   // Network address (.0) and subnet broadcast (.255) are legal to block.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.1.1.0/24") );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.1.0")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.1.255")) );

   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.1.1.0")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.1.1.255")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.1.1.1")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.1.1.128")) );
}

void test_ipwhitelist_clear_resets_to_deny_all(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   tftptest_ipwhitelist_clear();
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("0.0.0.1")) );
}

void test_ipwhitelist_clear_resets_blacklist(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );

   // After clear() + re-init the blacklist is gone: previously blocked IP is
   // accessible again.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_clear_twice_is_safe(void)
{
   // Calling clear() when the blacklist buffer is already NULL (free(NULL)) must
   // not crash.  Two consecutive clears with no block() in between exercises this.
   tftptest_ipwhitelist_clear();
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_init_does_not_reset_blacklist(void)
{
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.5")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.5")) );

   // Re-init with the same (or any) whitelist: blocked IP must stay blocked.
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.5")) );
   // Other hosts in the subnet remain accessible
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.6")) );
}

void test_ipwhitelist_block_forces_growth(void)
{
   // INITIAL_BLACKLIST_CAPACITY is 4; blocking 5 distinct IPs must trigger
   // the realloc growth path without corrupting state.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );

   char buf[16];
   uint32_t blocked[5];
   for ( int i = 1; i <= 5; i++ )
   {
      snprintf( buf, sizeof buf, "10.0.0.%d", i );
      blocked[i-1] = ipn(buf);
      TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(blocked[i-1]) );
   }

   for ( int i = 0; i < 5; i++ )
      TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(blocked[i]) );

   // An unblocked subnet member must still be reachable
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("10.0.0.6")) );
}

void test_ipwhitelist_block_with_allow_all_whitelist(void)
{
   // 0.0.0.0/0 allows every sender; the blacklist must still exclude blocked IPs.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_contains(ipn("1.1.1.1")) );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("8.8.8.8")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("1.1.1.1")) );
}

/* ===== is_deny_all() blacklist-shadowing scenarios =====
 *
 * These tests verify that is_deny_all() correctly detects when every
 * whitelisted IP has been individually blocked.  Each test begins with
 * tftptest_ipwhitelist_clear() for state isolation.
 */

void test_ipwhitelist_is_deny_all_when_only_host_blocked(void)
{
   // A /32 whitelist with its single IP blocked means no client can connect:
   // is_deny_all() must return true.  This is the primary trigger for the
   // main loop to stop accepting sessions.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_allow_all_with_blocked_ips(void)
{
   // 0.0.0.0/0 cannot be fully shadowed by individual IP blocks; is_deny_all()
   // must always return false when /0 is present, no matter how many IPs are
   // on the blacklist.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("1.2.3.4")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("5.6.7.8")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_subnet_fully_shadowed(void)
{
   // /30 covers exactly 4 IPs (.0–.3).  Blocking all four must make
   // is_deny_all() return true.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.1.2.0/30") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.0")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.1")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.2")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() ); // .3 still live
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.3")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_subnet_partially_shadowed(void)
{
   // Blocking all-but-one IP in a /30 leaves one live address: is_deny_all()
   // must return false.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.1.2.0/30") );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.0")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.1")) );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.1.2.2")) );
   // 10.1.2.3 is still live
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.1.2.3")) );
}

void test_ipwhitelist_is_deny_all_multi_entry_all_shadowed(void)
{
   // Two /32 whitelist entries, both blocked → is_deny_all() true.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1, 10.0.0.2") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() ); // 10.0.0.2 still live

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.2")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_multi_entry_one_live(void)
{
   // Two /32 entries; blocking only the first leaves the second live:
   // is_deny_all() must return false.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.1, 10.0.0.2") );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
}

/* ===== Additional clear() tests =====
 *
 * The three existing clear() tests cover: reset to deny-all, blacklist freed,
 * and double-clear safety.  These tests cover the re-use patterns.
 */

void test_ipwhitelist_clear_allows_reblock(void)
{
   // After clear(), the blacklist is freed and its lazy-init path must trigger
   // again on the next block() call, allocating a fresh buffer.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );

   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("10.0.0.0/8") );

   // block() must succeed (lazy re-alloc), and the IP must be excluded.
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_contains(ipn("10.0.0.1")) );
   // Unblocked neighbours remain accessible.
   TEST_ASSERT_TRUE(  tftptest_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_clear_then_reblock_is_deny_all(void)
{
   // Full cycle: clear → init /32 → block → is_deny_all() true.
   // Exercises that clear() resets enough state for the deny-all detection
   // to work correctly in a fresh cycle.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("172.16.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("172.16.0.1")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );

   // clear() + re-init: deny-all detection resets correctly.
   tftptest_ipwhitelist_clear();
   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_init("172.16.0.1") );
   TEST_ASSERT_FALSE( tftptest_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftptest_ipwhitelist_block(ipn("172.16.0.1")) );
   TEST_ASSERT_TRUE( tftptest_ipwhitelist_is_deny_all() );
}
