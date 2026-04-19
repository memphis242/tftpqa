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
 * @brief Control-channel configuration + live state.
 *
 * Populated by tftptest_ctrl_init(). Subsequent tftptest_ctrl_* calls take a
 * pointer-to-const to this struct — fields are not meant to change after init.
 */
struct TFTPTest_CtrlCfg
{
   int      sfd;                // Socket fd (-1 if uninitialized / after shutdown)
   uint16_t port;               // Port the control channel is bound to
   uint64_t whitelist;          // Bitmask of allowed fault modes (bit (mode-1) gates
                                // each mode; FAULT_NONE is always allowed).
                                // 0 = lock out all fault modes.
                                // UINT64_MAX = allow all.
   uint32_t allowed_client_ip;  // Allowed sender IPv4 (network byte order).
                                // 0 = accept from any sender.
};

/**
 * @brief Return codes for tftptest_ctrl_init(). One code per failure mode so
 *        the caller can distinguish which syscall went wrong; each failure is
 *        also logged inside tftptest_ctrl_init() with errno details.
 */
enum TFTPTest_CtrlResult
{
   TFTPTEST_CTRL_OK                = 0,
   TFTPTEST_CTRL_ERR_NULL_CFG      = 1,  // cfg pointer was NULL
   TFTPTEST_CTRL_ERR_SOCKET        = 2,  // socket() failed
   TFTPTEST_CTRL_ERR_FCNTL_GETFL   = 3,  // fcntl(F_GETFL) failed
   TFTPTEST_CTRL_ERR_FCNTL_SETFL   = 4,  // fcntl(F_SETFL, O_NONBLOCK) failed
   TFTPTEST_CTRL_ERR_SETSOCKOPT    = 5,  // setsockopt(SO_REUSEADDR) failed
   TFTPTEST_CTRL_ERR_BIND          = 6,  // bind() failed
};

/**
 * @brief Create and bind the control channel UDP socket (non-blocking).
 *
 * On success, cfg->sfd is set to a valid, non-blocking, bound UDP socket, and
 * cfg->port / cfg->whitelist are set from the arguments. On failure, cfg->sfd
 * is left at -1 (any partially-acquired socket is closed internally), the
 * failing syscall is logged with errno, and a specific error code is returned.
 *
 * @param[out] cfg                Populated on success.
 * @param[in]  port               UDP port to bind to.
 * @param[in]  whitelist          Bitmask of allowed fault modes.
 *                                0 = lock out all; UINT64_MAX = allow all.
 * @param[in]  allowed_client_ip  Allowed sender IPv4 (network byte order).
 *                                0 = accept from any sender.
 * @return TFTPTEST_CTRL_OK on success; a TFTPTEST_CTRL_ERR_* code otherwise.
 */
enum TFTPTest_CtrlResult tftptest_ctrl_init( struct TFTPTest_CtrlCfg * const cfg,
                                              uint16_t port,
                                              uint64_t whitelist,
                                              uint32_t allowed_client_ip );

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
 *   (unknown)                        -> ERR unknown command\n
 *
 * @param[in]     cfg    Control channel config (must have been initialized).
 * @param[in,out] fault  Current fault state (read and updated).
 */
void tftptest_ctrl_poll_and_handle( const struct TFTPTest_CtrlCfg * const cfg,
                                    struct TFTPTest_FaultState * const fault );

/**
 * @brief Close the control channel socket.
 * @param[in] cfg  Control channel config.
 */
void tftptest_ctrl_shutdown( struct TFTPTest_CtrlCfg * const cfg );

#endif // TFTPTEST_CTRL_H
