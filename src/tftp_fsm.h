/**
 * @file tftp_fsm.h
 * @brief TFTP FSM to handle a session transaction (either read or write).
 * @date Mar 27, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_FSM_H
#define TFTP_FSM_H

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#include "tftp_parsecfg.h"
#include "tftptest_faultmode.h"

// Types
enum TFTP_FSM_RC
{
   TFTP_FSM_RC_FINE                    = 0x0000,
   TFTP_FSM_RC_NREP_LIM_HIT            = 0x0001,
   TFTP_FSM_RC_SIGINT_REGISTRATION_ERR = 0x0002,
   TFTP_FSM_RC_SOCKET_CREATION_ERR     = 0x0004,
   TFTP_FSM_RC_SETSOCKOPT_ERR          = 0x0008,
   TFTP_FSM_RC_RECVFROM_ERR            = 0x0010,
   TFTP_FSM_RC_SENDTO_ERR              = 0x0020,
   TFTP_FSM_RC_FAILED_CLOSE            = 0x0040,
   TFTP_FSM_RC_FILE_ERR                = 0x0080,
   TFTP_FSM_RC_TIMEOUT                 = 0x0100,
   TFTP_FSM_RC_PROTOCOL_ERR            = 0x0200,
   TFTP_FSM_RC_WRQ_FILE_LIMIT          = 0x0400,  // Per-file size exceeded; file deleted
   TFTP_FSM_RC_WRQ_SESSION_LIMIT       = 0x0800,  // Session-total exceeded; file kept
   TFTP_FSM_RC_WRQ_DURATION_LIMIT      = 0x1000,  // Duration exceeded; file deleted
   TFTP_FSM_RC_WRQ_DISK_CHECK          = 0x2000,  // Pre-flight disk space check failed
   TFTP_FSM_RC_WRQ_DISABLED            = 0x4000,  // WRQ disabled by config
};

// Convenience mask: limit violations during transfer that trigger IP block
#define TFTP_FSM_RC_WRQ_LIMIT_VIOLATION \
    (TFTP_FSM_RC_WRQ_FILE_LIMIT | TFTP_FSM_RC_WRQ_SESSION_LIMIT | TFTP_FSM_RC_WRQ_DURATION_LIMIT)

/**
 * @brief Kicks off the TFTP FSM for the session, and returns upon completion.
 * @param[in]  rqbuf              The raw RRQ/WRQ packet buffer.
 * @param[in]  rqsz               Size of the packet in bytes.
 * @param[in]  peer_addr          The client's address (from recvfrom on the main socket).
 * @param[in]  cfg                Server configuration.
 * @param[in]  fault              Active fault simulation state.
 * @param[in]  wrq_session_budget Remaining session byte budget for WRQ (0 = unlimited).
 * @param[out] wrq_bytes_written  Bytes written by this WRQ session (may be NULL for RRQ).
 * @return Bitmask of TFTP_FSM_RC values indicating success or failure.
 */
enum TFTP_FSM_RC tftp_fsm_kickoff(const uint8_t *rqbuf, size_t rqsz,
                                    const struct sockaddr_in *peer_addr,
                                    const struct TFTPTest_Config *cfg,
                                    const struct TFTPTest_FaultState *fault,
                                    size_t wrq_session_budget,
                                    size_t *wrq_bytes_written);

/**
 * @brief Pre-empts the FSM, cleans up its content, and frees resources.
 */
void tftp_fsm_clean_exit(void);

#endif // TFTP_FSM_H
