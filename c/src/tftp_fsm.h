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
};

/**
 * @brief Kicks off the TFTP FSM for the session, and returns upon completion.
 * @param[in] rqbuf      The raw RRQ/WRQ packet buffer.
 * @param[in] rqsz       Size of the packet in bytes.
 * @param[in] peer_addr  The client's address (from recvfrom on the main socket).
 * @param[in] cfg        Server configuration.
 * @param[in] fault      Active fault injection state.
 * @return Bitmask of TFTP_FSM_RC values indicating success or failure.
 */
enum TFTP_FSM_RC TFTP_FSM_KickOff(const uint8_t *rqbuf, size_t rqsz,
                                    const struct sockaddr_in *peer_addr,
                                    const struct TFTPTest_Config *cfg,
                                    const struct TFTPTest_FaultState *fault);

/**
 * @brief Pre-empts the FSM, cleans up its content, and frees resources.
 */
void TFTP_FSM_CleanExit(void);

#endif // TFTP_FSM_H
