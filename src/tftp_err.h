/**
 * @file tftp_err.h
 * @brief Internal application error codes.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 *
 * These are *application-level* errors (syscall failures, config problems, etc.),
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

#endif // TFTP_ERR_H
