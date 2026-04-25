/**
 * @file tftptest_ctrl.h
 * @brief UDP control channel for setting fault simulation mode.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTPTEST_CTRL_H
#define TFTPTEST_CTRL_H

#include <stdint.h>

#include "tftptest_faultmode.h"

/**
 * @brief Return codes for tftptest_ctrl_init(). One code per failure mode so
 *        the caller can distinguish which syscall went wrong; each failure is
 *        also logged inside tftptest_ctrl_init() with errno details.
 */
enum TFTPTest_CtrlResult
{
   TFTPTEST_CTRL_OK              = 0,
   TFTPTEST_CTRL_ERR_SOCKET      = 1,  // socket() failed
   TFTPTEST_CTRL_ERR_FCNTL_GETFL = 2,  // fcntl(F_GETFL) failed
   TFTPTEST_CTRL_ERR_FCNTL_SETFL = 3,  // fcntl(F_SETFL, O_NONBLOCK) failed
   TFTPTEST_CTRL_ERR_SETSOCKOPT  = 4,  // setsockopt(SO_REUSEADDR) failed
   TFTPTEST_CTRL_ERR_BIND        = 5,  // bind() failed
};

/**
 * @brief Create and bind the control channel UDP socket (non-blocking).
 *
 * The module holds all state internally as a singleton. On failure any
 * partially-acquired socket is closed and a specific error code is returned.
 *
 * @param[in]  port       UDP port to bind to.
 * @param[in]  whitelist  Bitmask of allowed fault modes.
 *                        0 = lock out all; UINT64_MAX = allow all.
 * @return TFTPTEST_CTRL_OK on success; a TFTPTEST_CTRL_ERR_* code otherwise.
 */
enum TFTPTest_CtrlResult tftptest_ctrl_init( uint16_t port, uint64_t whitelist );

/**
 * @brief Non-blocking poll of the control channel and handle of available data.
 *
 * Checks if a control message has arrived. If so, parses it, updates the
 * fault state, and sends a reply.
 *
 * Protocol (text-based, newline _or_ nul-terminated):
 *   SET_FAULT <mode_name> [param]\n  -> OK <mode_name> [param]\n
 *   GET_FAULT\n                      -> FAULT <mode_name> [param]\n
 *   RESET\n                          -> OK 0\n
 *   (unknown)                        -> ERR unknown/malformed command\n
 *
 * @param[in,out] fault  Current fault state (read and updated).
 */
void tftptest_ctrl_poll_and_handle( struct TFTPTest_FaultState * const fault );

/**
 * @brief Close the control channel socket.
 */
void tftptest_ctrl_shutdown( void );

#endif // TFTPTEST_CTRL_H
