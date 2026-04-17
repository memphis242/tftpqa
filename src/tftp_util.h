/**
 * @file tftp_util.h
 * @brief Shared utility functions.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_UTIL_H
#define TFTP_UTIL_H

#include <stdbool.h>
#include <netinet/in.h>

/**
 * @brief Create a UDP socket bound to an ephemeral port on INADDR_ANY.
 * @param[out] bound_addr  Filled with the address the socket was bound to
 *                         (including the kernel-assigned port). May be NULL.
 * @return The socket file descriptor on success, or -1 on error (errno set).
 */
int tftp_util_create_ephemeral_udp_socket(struct sockaddr_in *bound_addr);

/**
 * @brief Create a UDP socket bound to a random port within [port_min, port_max].
 *
 * Picks a random port in the range and retries with other ports on EADDRINUSE.
 * Tries every port in the range exactly once (random order) before giving up.
 *
 * @param[in]  port_min   Minimum port number (inclusive, must be >= 1).
 * @param[in]  port_max   Maximum port number (inclusive, must be >= port_min).
 * @param[out] bound_addr Filled with the address the socket was bound to. May be NULL.
 * @return The socket file descriptor on success, or -1 if no port in range is available.
 */
int tftp_util_create_udp_socket_in_range(uint16_t port_min, uint16_t port_max,
                                         struct sockaddr_in *bound_addr);

/**
 * @brief Set the receive timeout on a socket.
 * @param[in] sfd          Socket file descriptor.
 * @param[in] timeout_sec  Timeout in seconds. 0 means no timeout.
 * @return 0 on success, -1 on error (errno set).
 */
int tftp_util_set_recv_timeout(int sfd, unsigned int timeout_sec);

/**
 * @brief Check whether a character is valid in a TFTP filename.
 *
 * Allows printable ASCII excluding path separators ('/' and '\\').
 */
bool tftp_util_is_valid_filename_char(char c);

/**
 * @brief Check whether a byte buffer contains bytes suspicious for a text
 *        (netascii) transfer.
 *
 * Scans for non-printable, non-standard-whitespace bytes that would not
 * normally appear in a text file.  Allowed bytes:
 *   - Printable ASCII (0x20–0x7E)
 *   - HT (0x09), LF (0x0A), VT (0x0B), FF (0x0C), CR (0x0D), ESC (0x1B)
 *   - NUL (0x00) only when immediately preceded by CR (CR+NUL = bare CR
 *     in the netascii encoding)
 *
 * @param[in] data  Buffer to scan. \nonnull
 * @param[in] len   Number of bytes to scan.
 * @return true if at least one suspicious byte was found; false otherwise.
 */
bool tftp_util_has_suspicious_text_bytes(const uint8_t *data, size_t len);

/**
 * @brief Convert octet (raw) data to netascii for sending.
 *
 * Applies RFC 764 NVT translation:
 *   - Bare LF (\n not preceded by \r) becomes \r\n
 *   - Bare CR (\r not followed by \n) becomes \r\0
 *   - \r\n passes through unchanged
 *
 * @param[in]     in          Input buffer (raw file bytes).
 * @param[in]     in_len      Number of input bytes.
 * @param[out]    out         Output buffer (must have room for up to 2*in_len).
 * @param[in]     out_cap     Capacity of the output buffer.
 * @param[in,out] pending_cr  Carries state across calls: set to true if the
 *                            last byte of the previous call was \r. Must be
 *                            initialized to false before the first call.
 * @return Number of bytes written to out.
 */
size_t tftp_util_octet_to_netascii(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_cap,
                                    bool *pending_cr);

/**
 * @brief Convert netascii data back to octet (raw) for writing to file.
 *
 * Reverses the RFC 764 NVT translation:
 *   - \r\n becomes \n
 *   - \r\0 becomes \r
 *   - Other \r followed by anything else passes through as-is
 *
 * @param[in]     in          Input buffer (netascii bytes from network).
 * @param[in]     in_len      Number of input bytes.
 * @param[out]    out         Output buffer.
 * @param[in]     out_cap     Capacity of the output buffer.
 * @param[in,out] pending_cr  Carries state across calls: set to true if the
 *                            last byte of the previous call was \r. Must be
 *                            initialized to false before the first call.
 * @return Number of bytes written to out.
 */
size_t tftp_util_netascii_to_octet(const uint8_t *in, size_t in_len,
                                    uint8_t *out, size_t out_cap,
                                    bool *pending_cr);

/**
 * @brief Change into the TFTP root directory, chroot into it, and drop
 *        privileges to the specified unprivileged user.
 *
 * Sequence: chdir(dir) → chroot(".") → chdir("/") → setgid → setuid.
 * After this call, the process is jailed in @p dir and running as @p user.
 *
 * If the process is not running as root (uid != 0), the chroot and
 * privilege-drop steps are skipped, but chdir is still performed.
 *
 * @param[in] dir   Absolute path to the TFTP root directory.
 * @param[in] user  Username to drop privileges to (e.g. "nobody").
 * @return 0 on success, -1 on error (details logged via tftp_log).
 */
int tftp_util_chroot_and_drop(const char *dir, const char *user);

#endif // TFTP_UTIL_H
