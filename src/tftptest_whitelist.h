/**
 * @file tftptest_whitelist.h
 * @brief IPv4 whitelist supporting single-host and CIDR subnet entries.
 * @date Apr 20, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTPTEST_WHITELIST_H
#define TFTPTEST_WHITELIST_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Parse a comma-separated list of IPv4 entries and install it as the
 *        module whitelist of clients.
 *
 * - Each entry is either a
 *    - bare IPv4 address (treated as /32), or
 *    - CIDR form "a.b.c.d/N" with N in 0..32
 * - Use "0.0.0.0/0" to allow any sender
 * - An empty string installs a deny-all whitelist
 * - Whitespace around commas is fine
 *
 * CIDR == Classless Inter-Domain Routing
 *
 * On failure (malformed entries, capacity overflow) deny-all is the fallback.
 *
 * @param[in] s  Input string comma-separated list
 * @return 0 on success, -1 on any parse error or malformed input
 */
int tftp_ipwhitelist_init(const char *s);

/**
 * @brief Check if the whitelist is deny-all (empty).
 * @return true iff whitelist is empty (deny-all), false otherwise
 */
bool tftp_ipwhitelist_is_deny_all(void);

/**
 * @brief Check whether an IPv4 address is on the whitelist.
 * @param[in] ip_nbo  Candidate IPv4 address in network byte order
 * @return true if ip_nbo is whitelisted, false otherwise
 */
bool tftp_ipwhitelist_contains(uint32_t ip_nbo);

/**
 * @brief Block a particular IP address
 * @param[in] ip_nbo IPv4 address to block in network byte order
 * @return 0 on successful add to blacklist, non-zero otherwise
 */
int tftp_ipwhitelist_block(uint32_t ip_nbo);

/**
 * @brief Check if the whitelist contains exclusively ip_nbo.
 *
 * Used to detect the "blocking the only whitelisted client" scenario so the
 * server can shut down rather than lock itself out permanently.
 *
 * @param[in] ip_nbo  Candidate IPv4 address in network byte order
 * @return true if ip_nbo is the only whitelisted address, false otherwise
 */
bool tftp_ipwhitelist_is_only_this_host(uint32_t ip_nbo);

/**
 * @brief Clean up internal resources used by this module (use at exit)
 */
void tftp_ipwhitelist_clear(void);

#endif // TFTPTEST_WHITELIST_H
