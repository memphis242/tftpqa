/**
 * @file tftp_err.h
 * @brief Internal application error codes and string table.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 *
 * These are *application-level* errors (socket failures, config problems, etc.),
 * not TFTP protocol error codes (those live in tftp_pkt.h).
 */

#ifndef TFTP_ERR_H
#define TFTP_ERR_H

// Internal application error codes
enum TFTP_Err
{
   TFTP_ERR_NONE = 0,
   TFTP_ERR_SOCKET,
   TFTP_ERR_BIND,
   TFTP_ERR_FILE_OPEN,
   TFTP_ERR_FILE_READ,
   TFTP_ERR_FILE_WRITE,
   TFTP_ERR_TIMEOUT,
   TFTP_ERR_PROTOCOL,
   TFTP_ERR_CONFIG,
   TFTP_ERR_ALLOC,
   TFTP_ERR_CHROOT,
   TFTP_ERR_PRIVDROP,

   TFTP_ERR_COUNT,
};

/**
 * @brief Return a human-readable string for an application error code.
 * @param[in] err  The error code.
 * @return A static string describing the error. Never returns NULL.
 */
const char *tftp_err_str(enum TFTP_Err err);

#endif // TFTP_ERR_H
