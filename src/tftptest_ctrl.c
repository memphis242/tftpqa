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

#include "tftptest_common.h"
#include "tftptest_ctrl.h"
#include "tftp_log.h"

/***************************** Local Declarations *****************************/

#define CTRL_CMD_BUF_SZ ((size_t)256)
#define MAX_CTRL_CMD_SZ (LONGEST_FAULT_MODE_NAME_LEN + UINT32_WIDTH + 4) // 4 for spaces and nul terminations
CompileTimeAssert( CTRL_CMD_BUF_SZ >= MAX_CTRL_CMD_SZ, tftptest_ctrl_buf_too_small );

#define CTRL_RSP_BUF_SZ (CTRL_CMD_BUF_SZ * 2) // twice as large in case we're sending back what we got in

static void send_reply( int sfd,
                        const struct sockaddr_in *dest,
                        const char *msg,
                        size_t len );

/********************** Public Function Implementations ***********************/

enum TFTPTest_CtrlResult tftptest_ctrl_init( struct TFTPTest_CtrlCfg * const cfg,
                                             uint16_t port,
                                             uint64_t whitelist )
{
   if ( cfg == NULL )
   {
      tftp_log(TFTP_LOG_ERR, __func__, "NULL cfg pointer");
      return TFTPTEST_CTRL_ERR_NULL_CFG;
   }

   // Initialize early so shutdown() is safe on any failure path, and so
   // diagnostics can see port/whitelist even if init fails.
   cfg->sfd       = -1;
   cfg->port      = port;
   cfg->whitelist = whitelist;

   enum TFTPTest_CtrlResult rc = TFTPTEST_CTRL_OK;
   int sfd = -1;
   int flags = 0;
   struct sockaddr_in addr = {
      .sin_family      = AF_INET,
      .sin_port        = htons(port),
      .sin_addr.s_addr = htonl(INADDR_ANY),
   };

   sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "socket() failed: %s (%d) : %s",
                strerrorname_np(errno), errno, strerror(errno) );
      return TFTPTEST_CTRL_ERR_SOCKET;
   }

   flags = fcntl(sfd, F_GETFL, 0);
   if ( flags < 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "fcntl(F_GETFL) failed: %s (%d) : %s",
                strerrorname_np(errno), errno, strerror(errno) );
      rc = TFTPTEST_CTRL_ERR_FCNTL_GETFL;
      goto cleanup;
   }

   if ( fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "fcntl(F_SETFL, O_NONBLOCK) failed: %s (%d) : %s",
                strerrorname_np(errno), errno, strerror(errno) );
      rc = TFTPTEST_CTRL_ERR_FCNTL_SETFL;
      goto cleanup;
   }

   if ( setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "setsockopt(SO_REUSEADDR) failed: %s (%d) : %s",
                strerrorname_np(errno), errno, strerror(errno) );
      rc = TFTPTEST_CTRL_ERR_SETSOCKOPT;
      goto cleanup;
   }

   if ( bind(sfd, (struct sockaddr *)&addr, sizeof addr) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "bind(port=%u) failed: %s (%d) : %s",
                (unsigned)port, strerrorname_np(errno), errno, strerror(errno) );
      rc = TFTPTEST_CTRL_ERR_BIND;
      goto cleanup;
   }

   cfg->sfd = sfd;
   tftp_log(TFTP_LOG_INFO, NULL, "Control channel listening on port %u", (unsigned)port);

   return TFTPTEST_CTRL_OK;

cleanup:
   if ( sfd >= 0 && close(sfd) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__,
                "close(sfd=%d) failed during init cleanup: %s (%d) : %s",
                sfd, strerrorname_np(errno), errno, strerror(errno) );
   }
   return rc;
}

void tftptest_ctrl_poll_and_handle( const struct TFTPTest_CtrlCfg * const cfg,
                                    struct TFTPTest_FaultState * const fault )
{
   assert( cfg != NULL );
   assert( fault != NULL );

   char buf[CTRL_CMD_BUF_SZ];
   struct sockaddr_in sender = {0};
   socklen_t addrlen = sizeof sender;

   ssize_t nbytes = recvfrom( cfg->sfd,
                              buf, sizeof(buf) - 1,
                              0,
                              (struct sockaddr *)&sender, &addrlen );
   if ( nbytes <= 0 )
   {
#if ( EAGAIN == EWOULDBLOCK )
      if ( errno != EAGAIN )
#else
      if ( errno != EAGAIN && errno != EWOULDBLOCK )
#endif
      {
         tftp_log( TFTP_LOG_WARN, __func__,
                   "recvfrom() got unexpected error, returned %zd :: %s (%d) : %s",
                   nbytes, strerrorname_np(errno), errno, strerror(errno) );
      }

      return;
   }
   else if ( (size_t)nbytes > MAX_CTRL_CMD_SZ )
   {
      tftp_log( TFTP_LOG_INFO, NULL,
                "Ctrl cmd larger than expected max sz (%zu), received %zu bytes",
                MAX_CTRL_CMD_SZ, (size_t)nbytes );
   }

   assert( nbytes >  0 );
   assert( (size_t)nbytes <= sizeof buf );

   buf[nbytes] = '\0';

   // Strip trailing newline
   if ( nbytes > 0 && buf[nbytes - 1] == '\n' )
      buf[--nbytes] = '\0';

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = 0;

   if ( strcasecmp(buf, "SET_FAULT") == 0 )
   {
      // Parse: SET_FAULT <mode> [param]
      char mode_name[64] = {0};
      uint32_t param = 0;
      int nfields = sscanf(buf + sizeof("SET_FAULT"), " %63s %u", mode_name, &param);

      if ( nfields < 1 )
      {
         tftp_log( TFTP_LOG_INFO, NULL, "Too few args for SET_FAULT cmd: '%s'", buf );

         reply_len = snprintf(reply, sizeof reply, "ERR missing mode name\n");
         send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      enum TFTPTest_FaultMode mode_idx = tftptest_fault_name_lookup_mode(mode_name);
      if ( (int)mode_idx < 0 )
      {
         tftp_log( TFTP_LOG_INFO, NULL, "Ctrl port cmd mode unknown: '%s'", mode_name );

         reply_len = snprintf( reply, sizeof reply,
                        "ERR unknown mode '%s' : error code %d\n",
                        mode_name, (int)mode_idx );
         send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
         return;
      }

      // Check whitelist
      assert( mode_idx >= 0 );
      if ( (int)mode_idx > FAULT_NONE )
      {
         uint64_t mode_bit = (uint64_t)1 << (unsigned)((int)mode_idx - 1);
         if ( !(cfg->whitelist & mode_bit) )
         {
            tftp_log( TFTP_LOG_INFO, NULL, "Ctrl port cmd not on whitelist: '%s'", mode_name );

            reply_len = snprintf(reply, sizeof reply, "ERR mode '%s' not allowed\n", mode_name);
            send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
            return;
         }
      }

      fault->mode          = mode_idx;
      fault->param         = param;
      fault->param_present = (nfields >= 2);

      if ( fault->param_present )
      {
         tftp_log( TFTP_LOG_INFO, NULL,
                   "Control: fault mode set to %s (param=%u)",
                   tftptest_fault_mode_names[fault->mode], fault->param );
         reply_len = snprintf( reply, sizeof reply,
                               "OK %s %u\n",
                               tftptest_fault_mode_names[fault->mode], fault->param );
      }
      else
      {
         tftp_log( TFTP_LOG_INFO, NULL,
                   "Control: fault mode set to %s (no param)",
                   tftptest_fault_mode_names[fault->mode] );
         reply_len = snprintf( reply, sizeof reply,
                               "OK %s\n",
                               tftptest_fault_mode_names[fault->mode] );
      }

      send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
   }
   else if ( strcasecmp(buf, "GET_FAULT") == 0 )
   {
      tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd GET_FAULT cmd'%s'", buf );

      if ( fault->param_present )
      {
         reply_len = snprintf( reply, sizeof reply,
                               "FAULT %s %u\n",
                               tftptest_fault_mode_names[fault->mode], fault->param );
      }
      else
      {
         reply_len = snprintf( reply, sizeof reply,
                               "FAULT %s\n",
                               tftptest_fault_mode_names[fault->mode] );
      }
      send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
   }
   else if ( strcasecmp(buf, "RESET") == 0 )
   {
      tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd RESET cmd'%s'", buf );

      fault->mode          = FAULT_NONE;
      fault->param         = 0;
      fault->param_present = false;
      tftp_log( TFTP_LOG_INFO, NULL, "Control: fault mode reset" );

      reply_len = snprintf(reply, sizeof reply, "OK 0\n");
      send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
   }
   else
   {
      tftp_log( TFTP_LOG_INFO, NULL, "Unknown ctrl port cmd rcvd '%s'", buf );

      reply_len = snprintf(reply, sizeof reply, "ERR unknown command '%s'\n", buf);
      send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
   }
}

void tftptest_ctrl_shutdown( const struct TFTPTest_CtrlCfg * cfg )
{

#ifndef NDEBUG
   assert( cfg != NULL );
#else
   if ( cfg == NULL || cfg->sfd < 0 )
      return;
#endif

   if ( close(cfg->sfd) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__,
                "close(sfd=%d) failed: %s (%d) : %s",
                cfg->sfd, strerrorname_np(errno), errno, strerror(errno) );
   }

   tftp_log( TFTP_LOG_INFO, NULL,
             "Closed ctrl port socket %d listening on port %u",
             cfg->sfd, cfg->port );
}

/*********************** Local Function Implementations ***********************/

static void send_reply( int sfd,
                        const struct sockaddr_in *dest,
                        const char *msg,
                        size_t len )
{
   ssize_t sysrc = sendto( sfd,
                           msg, len,
                           0,
                           (const struct sockaddr *)dest,
                           sizeof *dest );

   if ( sysrc < 0 )
      tftp_log( TFTP_LOG_WARN, __func__,
                "Failed to send reply '%s' to UDP ctrl port cmd :: "
                "sendto() returned %zd :: %s (%d) : %s",
                msg,
                sysrc, strerrorname_np(errno), errno, strerror(errno) );
}
