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
 * @brief Create and bind the control channel UDP socket.
 * @param[in] port  The port to bind to.
 * @return Socket fd on success, -1 on error.
 */
int tftptest_ctrl_init(uint16_t port);

/**
 * @brief Non-blocking poll of the control channel.
 *
 * Checks if a control message has arrived. If so, parses it, updates the
 * fault state, and sends a reply.
 *
 * Protocol (text-based, newline-terminated):
 *   SET_FAULT <mode_name> [param]\n  -> OK <mode_name> [param]\n
 *   GET_FAULT\n                      -> FAULT <mode_name> [param]\n
 *   RESET\n                          -> OK 0\n
 *   (unknown)                        -> ERR unknown command\n
 *
 * @param[in]     ctrl_sfd  Control socket fd.
 * @param[in,out] fault     Current fault state (read and updated).
 * @param[in]     whitelist Bitmask of allowed fault modes. 0 = allow all.
 */
void tftptest_ctrl_poll(int ctrl_sfd, struct TFTPTest_FaultState *fault,
                         uint64_t whitelist);

/**
 * @brief Close the control channel socket.
 * @param[in] ctrl_sfd  Control socket fd.
 */
void tftptest_ctrl_shutdown(int ctrl_sfd);

#endif // TFTPTEST_CTRL_H
