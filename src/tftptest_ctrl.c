/**
 * @file tftptest_ctrl.c
 * @brief UDP control channel for setting fault simulation mode.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <assert.h>

#include "tftptest_ctrl.h"
#include "tftp_log.h"

/***************************** Local Declarations *****************************/

#define CTRL_BUF_SZ 256

// Fault mode name table -- must match enum TFTPTest_FaultMode order
static const char *fault_mode_names[] = {
   [FAULT_NONE]                     = "NONE",
   [FAULT_RRQ_TIMEOUT]              = "RRQ_TIMEOUT",
   [FAULT_WRQ_TIMEOUT]              = "WRQ_TIMEOUT",
   [FAULT_MID_TIMEOUT_NO_DATA]      = "MID_TIMEOUT_NO_DATA",
   [FAULT_MID_TIMEOUT_NO_ACK]       = "MID_TIMEOUT_NO_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_ACK] = "MID_TIMEOUT_NO_FINAL_ACK",
   [FAULT_MID_TIMEOUT_NO_FINAL_DATA]= "MID_TIMEOUT_NO_FINAL_DATA",
   [FAULT_FILE_NOT_FOUND]           = "FILE_NOT_FOUND",
   [FAULT_PERM_DENIED_READ]         = "PERM_DENIED_READ",
   [FAULT_PERM_DENIED_WRITE]        = "PERM_DENIED_WRITE",
   [FAULT_SEND_ERROR_READ]          = "SEND_ERROR_READ",
   [FAULT_SEND_ERROR_WRITE]         = "SEND_ERROR_WRITE",
   [FAULT_DUP_FINAL_DATA]           = "DUP_FINAL_DATA",
   [FAULT_DUP_FINAL_ACK]            = "DUP_FINAL_ACK",
   [FAULT_DUP_MID_DATA]             = "DUP_MID_DATA",
   [FAULT_DUP_MID_ACK]              = "DUP_MID_ACK",
   [FAULT_SKIP_ACK]                 = "SKIP_ACK",
   [FAULT_SKIP_DATA]                = "SKIP_DATA",
   [FAULT_OOO_DATA]                 = "OOO_DATA",
   [FAULT_OOO_ACK]                  = "OOO_ACK",
   [FAULT_INVALID_BLOCK_ACK]        = "INVALID_BLOCK_ACK",
   [FAULT_INVALID_BLOCK_DATA]       = "INVALID_BLOCK_DATA",
   [FAULT_DATA_TOO_LARGE]           = "DATA_TOO_LARGE",
   [FAULT_DATA_LEN_MISMATCH]        = "DATA_LEN_MISMATCH",
   [FAULT_INVALID_OPCODE_READ]      = "INVALID_OPCODE_READ",
   [FAULT_INVALID_OPCODE_WRITE]     = "INVALID_OPCODE_WRITE",
   [FAULT_INVALID_ERR_CODE_READ]    = "INVALID_ERR_CODE_READ",
   [FAULT_INVALID_ERR_CODE_WRITE]   = "INVALID_ERR_CODE_WRITE",
   [FAULT_WRONG_TID_READ]           = "WRONG_TID_READ",
   [FAULT_WRONG_TID_WRITE]          = "WRONG_TID_WRITE",
   [FAULT_SLOW_RESPONSE]            = "SLOW_RESPONSE",
   [FAULT_CORRUPT_DATA]             = "CORRUPT_DATA",
   [FAULT_TRUNCATED_PKT]            = "TRUNCATED_PKT",
   [FAULT_BURST_DATA]               = "BURST_DATA",
};

_Static_assert(sizeof(fault_mode_names) / sizeof(fault_mode_names[0]) == FAULT_MODE_COUNT,
               "fault_mode_names must have FAULT_MODE_COUNT entries");

static int lookup_fault_mode(const char *name);
static void send_reply(int sfd, const struct sockaddr_in *dest,
                        const char *msg, size_t len);

/********************** Public Function Implementations ***********************/

int tftptest_ctrl_init(uint16_t port)
{
   int sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 )
      return -1;

   // Set non-blocking
   int flags = fcntl(sfd, F_GETFL, 0);
   if ( flags < 0 || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0 )
   {
      (void)close(sfd);
      return -1;
   }

   // Allow reuse
   (void)setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));

   struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_ANY),
   };

   if ( bind(sfd, (struct sockaddr *)&addr, sizeof addr) != 0 )
   {
      (void)close(sfd);
      return -1;
   }

   tftp_log(TFTP_LOG_INFO, "Control channel listening on port %u", (unsigned)port);
   return sfd;
}

void tftptest_ctrl_poll(int ctrl_sfd, struct TFTPTest_FaultState *fault,
                         uint64_t whitelist)
{
   assert( fault != nullptr );

   char buf[CTRL_BUF_SZ];
   struct sockaddr_in sender = {0};
   socklen_t addrlen = sizeof sender;

   ssize_t nbytes = recvfrom(ctrl_sfd, buf, sizeof(buf) - 1, 0,
                              (struct sockaddr *)&sender, &addrlen);
   if ( nbytes <= 0 )
      return; // No message (EAGAIN) or error

   buf[nbytes] = '\0';

   // Strip trailing newline
   if ( nbytes > 0 && buf[nbytes - 1] == '\n' )
      buf[--nbytes] = '\0';

   char reply[CTRL_BUF_SZ];
   int reply_len = 0;

   if ( strncasecmp(buf, "SET_FAULT", 9) == 0 )
   {
      // Parse: SET_FAULT <mode> [param]
      char mode_name[64] = {0};
      uint32_t param = 0;
      int nfields = sscanf(buf + 9, " %63s %u", mode_name, &param);

      if ( nfields < 1 )
      {
         reply_len = snprintf(reply, sizeof reply, "ERR missing mode name\n");
         send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      int mode_idx = lookup_fault_mode(mode_name);
      if ( mode_idx < 0 )
      {
         reply_len = snprintf(reply, sizeof reply, "ERR unknown mode '%s'\n", mode_name);
         send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      // Check whitelist (if non-zero, bit must be set)
      if ( whitelist != 0 && mode_idx > 0 &&
           !(whitelist & ((uint64_t)1 << (unsigned)(mode_idx - 1))) )
      {
         reply_len = snprintf(reply, sizeof reply, "ERR mode '%s' not allowed\n", mode_name);
         send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      fault->mode = (enum TFTPTest_FaultMode)mode_idx;
      fault->param = param;

      tftp_log(TFTP_LOG_INFO, "Control: fault mode set to %s (param=%u)",
               fault_mode_names[fault->mode], fault->param);

      reply_len = snprintf(reply, sizeof reply, "OK %s %u\n",
                            fault_mode_names[fault->mode], fault->param);
      send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
   }
   else if ( strncasecmp(buf, "GET_FAULT", 9) == 0 )
   {
      reply_len = snprintf(reply, sizeof reply, "FAULT %s %u\n",
                            fault_mode_names[fault->mode], fault->param);
      send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
   }
   else if ( strncasecmp(buf, "RESET", 5) == 0 )
   {
      fault->mode = FAULT_NONE;
      fault->param = 0;
      tftp_log(TFTP_LOG_INFO, "Control: fault mode reset");
      reply_len = snprintf(reply, sizeof reply, "OK 0\n");
      send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
   }
   else
   {
      reply_len = snprintf(reply, sizeof reply, "ERR unknown command\n");
      send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
   }
}

void tftptest_ctrl_shutdown(int ctrl_sfd)
{
   if ( ctrl_sfd >= 0 )
      (void)close(ctrl_sfd);
}

/*********************** Local Function Implementations ***********************/

static int lookup_fault_mode(const char *name)
{
   for ( int i = 0; i < FAULT_MODE_COUNT; i++ )
   {
      if ( strcasecmp(name, fault_mode_names[i]) == 0 )
         return i;
   }
   return -1;
}

static void send_reply(int sfd, const struct sockaddr_in *dest,
                        const char *msg, size_t len)
{
   (void)sendto(sfd, msg, len, 0,
                (const struct sockaddr *)dest, sizeof *dest);
}
