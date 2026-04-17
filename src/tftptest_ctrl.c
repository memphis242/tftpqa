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

#include <assert.h>
#include <errno.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "tftptest_ctrl.h"
#include "tftp_log.h"

/***************************** Local Declarations *****************************/

#define CTRL_BUF_SZ 256

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
   assert( fault != NULL );

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

      enum TFTPTest_FaultMode mode_idx = tftptest_fault_name_lookup_mode(mode_name);
      if ( (int)mode_idx < 0 )
      {
         reply_len = snprintf( reply, sizeof reply,
                        "ERR unknown mode '%s' : tftptest_fault_name_lookup_mode() returned %d\n",
                        mode_name, (int)mode_idx );
         send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      // Check whitelist (if non-zero, bit must be set)
      if ( whitelist != 0 && (int)mode_idx > FAULT_NONE &&
           !(whitelist & ((uint64_t)1 << (unsigned)((int)mode_idx - 1))) )
      {
         reply_len = snprintf(reply, sizeof reply, "ERR mode '%s' not allowed\n", mode_name);
         send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      fault->mode = mode_idx;
      fault->param = param;

      tftp_log(TFTP_LOG_INFO, "Control: fault mode set to %s (param=%u)",
               tftptest_fault_mode_names[fault->mode], fault->param);

      reply_len = snprintf(reply, sizeof reply, "OK %s %u\n",
                            tftptest_fault_mode_names[fault->mode], fault->param);
      send_reply(ctrl_sfd, &sender, reply, (size_t)reply_len);
   }
   else if ( strncasecmp(buf, "GET_FAULT", 9) == 0 )
   {
      reply_len = snprintf(reply, sizeof reply, "FAULT %s %u\n",
                            tftptest_fault_mode_names[fault->mode], fault->param);
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

static void send_reply(int sfd, const struct sockaddr_in *dest,
                        const char *msg, size_t len)
{
   (void)sendto(sfd, msg, len, 0,
                (const struct sockaddr *)dest, sizeof *dest);
}
