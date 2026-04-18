/**
 * @file tftp_util.h
 * @brief Shared utility functions.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_UTIL_H
#define TFTP_UTIL_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
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
 * @brief Result of scanning a buffer for text-mode compatibility.
 */
enum TFTPUtil_TextCheck
{
   TFTP_TEXT_CLEAN,       /**< Pure 7-bit ASCII text, nothing to report. */
   TFTP_TEXT_HAS_UTF8,    /**< Valid UTF-8 multi-byte characters found (INFO-worthy). */
   TFTP_TEXT_SUSPICIOUS,  /**< Invalid/non-text bytes found (WARN-worthy). */
};

/**
 * @brief Check whether a byte buffer is compatible with a text (netascii)
 *        transfer, with UTF-8 awareness.
 *
 * Scans for non-printable, non-standard-whitespace bytes that would not
 * normally appear in a text file.  Allowed bytes:
 *   - Printable ASCII (0x20–0x7E)
 *   - HT (0x09), LF (0x0A), VT (0x0B), FF (0x0C), CR (0x0D), ESC (0x1B)
 *   - NUL (0x00) only when immediately preceded by CR (CR+NUL = bare CR
 *     in the netascii encoding)
 *   - Valid UTF-8 multi-byte sequences (0xC2–0xF4 lead bytes with proper
 *     continuation bytes) — allowed but noted as not strictly RFC 764
 *
 * Malformed UTF-8 (overlong encodings, truncated sequences, lone continuation
 * bytes, codepoints above U+10FFFF) is treated as suspicious.
 *
 * @param[in] data  Buffer to scan. May be NULL only if @p len is 0.
 * @param[in] len   Number of bytes to scan.
 * @return TFTP_TEXT_CLEAN if all bytes are 7-bit ASCII text,
 *         TFTP_TEXT_HAS_UTF8 if valid UTF-8 multi-byte characters were found,
 *         TFTP_TEXT_SUSPICIOUS if any invalid/non-text bytes were found.
 *         SUSPICIOUS takes priority over HAS_UTF8.
 */
enum TFTPUtil_TextCheck tftp_util_check_text_bytes(const uint8_t *data, size_t len);

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

/**
 * @brief Result of a file-permission check on a tftp-opened file.
 */
enum TFTPUtil_PermCheck
{
   TFTP_PERM_OK,                  /**< File passes all permission checks. */
   TFTP_PERM_NOT_REGULAR,         /**< Not a regular file (dir, block, char, fifo, socket). */
   TFTP_PERM_SETUID_SETGID,       /**< Setuid and/or setgid bit is set. */
   TFTP_PERM_NOT_WORLD_READABLE,  /**< Missing S_IROTH (RRQ policy). */
   TFTP_PERM_NOT_WORLD_WRITABLE,  /**< Missing S_IWOTH (WRQ overwrite policy). */
   TFTP_PERM_FSTAT_FAILED,        /**< fstat() on the fd failed (errno set). */
};

/**
 * @brief Open a file for reading with careful consideration of file attributes.
 *
 * Uses O_RDONLY | O_NOFOLLOW | O_CLOEXEC. A symlink as the final path component
 * yields ELOOP. The caller is expected to run subsequent permission checks on
 * the returned fd via tftp_util_check_read_perms().
 *
 * @param[in] filename  Path (within the chroot) to open.
 * @return fd on success, -1 on error with errno set (ENOENT, EACCES, ELOOP, ...).
 */
int tftp_util_open_for_read(const char *filename); // FIXME: Should have fname len arg or internal check to prevent out-of-bounds access

/**
 * @brief Open a file for writing, creating it if it does not already exist.
 *
 * Three-phass attempt:
 *   1. Try O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC on the existing path.
 *   2. On ENOENT, fall back to O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW |
 *      O_CLOEXEC with @p create_mode.
 *   3. On EEXIST from (2) (race), retry (1) once more.
 *
 * A symlink as the final path component yields ELOOP (O_NOFOLLOW).
 * When a new file is created, it is opened with exactly @p create_mode (further
 * reduced by the process umask).
 *
 * @param[in]  filename     Path (within the chroot) to open or create.
 * @param[in]  create_mode  Mode to apply on creation (e.g. 0666).
 * @param[out] out_created  Set to true iff this call created the file;
 *                          false if it opened a pre-existing file. Required.
 * @return fd on success, -1 on error with errno set.
 */
int tftp_util_open_for_write( const char *filename,
                              mode_t create_mode,
                              bool *out_created );

/**
 * @brief Verify a fd refers to a regular, non-setuid, world-readable file.
 *
 * Single fstat() on @p fd; check order: is regular → setuid/setgid → S_IROTH
 *
 * @param[in]  fd        File descriptor to inspect.
 * @param[out] out_mode  Receives st.st_mode & 07777 on success or failure.
 *                       May be NULL.
 * @return TFTP_PERM_OK or the first violated check.
 */
enum TFTPUtil_PermCheck tftp_util_check_read_perms(int fd, mode_t *out_mode);

/**
 * @brief Verify a fd refers to a regular, non-setuid, world-writable file.
 *
 * Single fstat() on @p fd; check order: is regular → setuid/setgid → S_IWOTH.
 *
 * @param[in]  fd        File descriptor to inspect.
 * @param[out] out_mode  Receives st.st_mode & 07777 on success or failure.
 *                       May be NULL.
 * @return TFTP_PERM_OK or the first violated check.
 */
enum TFTPUtil_PermCheck tftp_util_check_write_perms(int fd, mode_t *out_mode);

#endif // TFTP_UTIL_H
