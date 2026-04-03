/**
 * @file tftp_fsm.c
 * @brief TFTP FSM to handle a session transaction (either read or write).
 * @date Mar 27, 2026
 * @author Abdulla Almosalami, @memphis242
 */

/*************************** File Header Inclusions ***************************/

// Standard C Headers
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>

// General System Headers
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

// Sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Internal Headers
#include "tftp_err.h"
#include "tftp_fsm.h"
#include "tftp_log.h"
#include "tftp_pkt.h"
#include "tftp_util.h"

/***************************** Local Declarations *****************************/

constexpr size_t MAX_FNAME_LEN = 1024; // FIXME: Determine system limit at runtime?

// Internal Types
enum TFTP_FSM_State
{
   TFTP_FSM_DETERMINE_RQ,
   TFTP_FSM_MIN_STATE = TFTP_FSM_DETERMINE_RQ,

   TFTP_FSM_RRQ_DATA,
   TFTP_FSM_RRQ_ACK,
   TFTP_FSM_RRQ_ERR,
   TFTP_FSM_RRQ_FIN_DATA,

   TFTP_FSM_WRQ_DATA,
   TFTP_FSM_WRQ_ACK,
   TFTP_FSM_WRQ_ERR,
   TFTP_FSM_WRQ_FIN_ACK,

   TFTP_FSM_IDLE,
   TFTP_FSM_ERR,

   TFTP_FSM_MAX_STATE = TFTP_FSM_ERR,
   TFTP_FSM_INVALID_STATE,
};

struct TFTP_FSM_Session_S
{
   int sfd;
   enum TFTP_FSM_State state;
   FILE *fp;
   char fname[MAX_FNAME_LEN];
   size_t fname_len;
};

// Internal Variables
struct TFTP_FSM_Session_S TFTP_FSM_Session;

// Internal Function Declarations

/********************** Public Function Implementations ***********************/

enum TFTP_FSM_RC TFTP_FSM_KickOff(uint8_t *rqbuf, size_t rqsz)
{
   enum TFTP_FSM_RC rc = TFTP_FSM_RC_FINE;

TFTP_FSM_Engine_ExitTag:
   return rc;
}

void TFTP_FSM_CleanExit(void)
{
   // TODO
   TFTP_FSM_Session.state = TFTP_FSM_IDLE;
   TFTP_FSM_Session.fname_len = 0;
   memset(TFTP_FSM_Session.fname, 0x00, sizeof TFTP_FSM_Session.fname);

   if ( TFTP_FSM_Session.fp != nullptr )
   {
      (void)fclose(TFTP_FSM_Session.fp);
      TFTP_FSM_Session.fp = nullptr;
   }

   (void)close(TFTP_FSM_Session.sfd);
   TFTP_FSM_Session.sfd = -1;

   return;
}
 
/*********************** Local Function Implementations ***********************/
