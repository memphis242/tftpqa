/**
 * @file tftp_fsm.h
 * @brief TFTP FSM to handle a session transaction (either read or write).
 * @date Mar 27, 2026
 * @author Abdulla Almosalami, @memphis242
 */

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
};

/**
 * @brief Kicks off the TFTP FSM for the session, and returns upon completion.
 * @param[in]
 * @param[in]
 * @return
 */
enum TFTP_FSM_RC TFTP_FSM_KickOff(uint8_t *rqbuf, size_t rqsz);

/**
 * @brief Pre-empts the FSM, cleans up its content, and frees resources.
 * @return None
 */
void TFTP_FSM_CleanExit(void);
