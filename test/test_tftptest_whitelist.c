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
void test_ipwhitelist_is_only_this_host_true(void);
void test_ipwhitelist_is_only_this_host_wrong_ip(void);
void test_ipwhitelist_is_only_this_host_multiple_entries(void);
void test_ipwhitelist_is_only_this_host_empty(void);
void test_ipwhitelist_init_resets_singleton(void);

/* Helper: convert "a.b.c.d" to NBO uint32 */
static uint32_t ipn(const char *s)
{
   struct in_addr a;
   TEST_ASSERT_EQUAL_INT( 1, inet_aton(s, &a) );
   return a.s_addr;
}

void test_ipwhitelist_empty_is_deny_all(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("1.2.3.4")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("255.255.255.255")) );
}

void test_ipwhitelist_is_deny_all_true(void)
{
   tftp_ipwhitelist_init("");
   TEST_ASSERT_TRUE( tftp_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_is_deny_all_false(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_deny_all() );

   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("0.0.0.0/0") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_deny_all() );
}

void test_ipwhitelist_single_bare_ip(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.20.30.40") );
   // Bare IP is /32: only exact match admitted; is_only_this_host confirms /32.
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("10.20.30.40")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.20.30.41")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_only_this_host(ipn("10.20.30.40")) );
}

void test_ipwhitelist_single_cidr_24(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("192.168.1.0/24") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.123")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.255")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("192.168.0.255")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("192.168.2.0")) );
}

void test_ipwhitelist_host_bits_normalized(void)
{
   // Host bits in CIDR are silently normalized: /24 of 192.168.1.5 should
   // behave identically to /24 of 192.168.1.0.
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("192.168.1.5/24") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.5")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.200")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("192.168.2.5")) );
}

void test_ipwhitelist_mixed_list_with_whitespace(void)
{
   int rc = tftp_ipwhitelist_init(
      "  10.0.0.1 , 192.168.0.0/16,172.16.5.5/32 " );
   TEST_ASSERT_EQUAL_INT( 0, rc );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.200.7")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("172.16.5.5")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_prefix_zero_matches_all(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("1.2.3.4/0") );
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("0.0.0.0")) );
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("255.255.255.255")) );
}

void test_ipwhitelist_prefix_32_explicit_vs_bare(void)
{
   // "10.0.0.1" and "10.0.0.1/32" must produce identical matching behavior.
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.2")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );

   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1/32") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.2")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );
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
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init(buf) );
}

void test_ipwhitelist_malformed_results_in_deny_all(void)
{
   // Install a known-good whitelist, then attempt a malformed parse.
   // On failure the singleton is reset to deny-all (fail-closed).
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/33") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_null_input_is_deny_all(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init(NULL) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
}

void test_ipwhitelist_malformed_trailing_comma(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4,") );
}

void test_ipwhitelist_malformed_empty_middle_token(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4,,5.6.7.8") );
}

void test_ipwhitelist_malformed_non_numeric_prefix(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/abc") );
}

void test_ipwhitelist_malformed_prefix_33(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/33") );
}

void test_ipwhitelist_malformed_negative_prefix(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/-1") );
}

void test_ipwhitelist_malformed_bad_octet(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("999.0.0.1") );
}

void test_ipwhitelist_malformed_trailing_garbage(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4abc") );
}

void test_ipwhitelist_malformed_trailing_slash(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/") );
}

void test_ipwhitelist_malformed_double_slash(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("1.2.3.4/24/extra") );
}

void test_ipwhitelist_matcher_slash_32(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1/32") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.2")) );
}

void test_ipwhitelist_matcher_slash_24_boundary(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("192.168.1.0/24") );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.0")) );
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_contains(ipn("192.168.1.255")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("192.168.2.0")) );
}

void test_ipwhitelist_matcher_multiple_entries(void)
{
   TEST_ASSERT_EQUAL_INT( 0,
      tftp_ipwhitelist_init("10.0.0.1, 192.168.0.0/16, 172.16.5.5") );

   // First entry
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   // Middle entry
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("192.168.99.99")) );
   // Last entry
   TEST_ASSERT_TRUE( tftp_ipwhitelist_contains(ipn("172.16.5.5")) );
   // Not in whitelist
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("8.8.8.8")) );
}

void test_ipwhitelist_is_only_this_host_true(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_TRUE( tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );
}

void test_ipwhitelist_is_only_this_host_wrong_ip(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.2")) );
}

void test_ipwhitelist_is_only_this_host_multiple_entries(void)
{
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1, 10.0.0.2") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );
}

void test_ipwhitelist_is_only_this_host_empty(void)
{
   TEST_ASSERT_EQUAL_INT( -1, tftp_ipwhitelist_init("") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );
}

void test_ipwhitelist_init_resets_singleton(void)
{
   // Install a whitelist, then init() should reset it to deny-all (count == 0).
   TEST_ASSERT_EQUAL_INT( 0, tftp_ipwhitelist_init("10.0.0.1") );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_deny_all() );

   tftp_ipwhitelist_init("");
   TEST_ASSERT_TRUE(  tftp_ipwhitelist_is_deny_all() );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("8.8.8.8")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_contains(ipn("10.0.0.1")) );
   TEST_ASSERT_FALSE( tftp_ipwhitelist_is_only_this_host(ipn("10.0.0.1")) );
}
