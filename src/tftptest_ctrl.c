/**
 * @file tftptest_ctrl.c
 * @brief UDP control channel for setting fault simulation mode.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>

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
// Max cmd string len + 10 digits max for parameter arg + 10 for extra spaces and nul
#define MAX_CTRL_CMD_SZ (sizeof("SET_FAULT") - 1 + LONGEST_FAULT_MODE_NAME_LEN + 10 + 10)
CompileTimeAssert( CTRL_CMD_BUF_SZ >= MAX_CTRL_CMD_SZ, tftptest_ctrl_buf_too_small );

// Make response buffer twice as large in case we're sending back what we got in
#define CTRL_RSP_BUF_SZ (CTRL_CMD_BUF_SZ * 2)
// Reasonable upper bound on response buffer size too...
CompileTimeAssert( CTRL_RSP_BUF_SZ < INT_MAX, tftptest_ctrl_response_buf_too_big );

static void send_reply( int sfd,
                        const struct sockaddr_in *dest,
                        const char *msg,
                        size_t len );

/********************** Public Function Implementations ***********************/

enum TFTPTest_CtrlResult tftptest_ctrl_init( struct TFTPTest_CtrlCfg * const cfg,
                                             uint16_t port,
                                             uint64_t whitelist,
                                             uint32_t allowed_client_ip )
{
   if ( cfg == NULL )
   {
      tftp_log(TFTP_LOG_ERR, __func__, "NULL cfg pointer");
      return TFTPTEST_CTRL_ERR_NULL_CFG;
   }

   // Initialize early so shutdown() is safe on any failure path, and so
   // diagnostics can see port/whitelist even if init fails.
   cfg->sfd               = -1;
   cfg->port              = port;
   cfg->whitelist         = whitelist;
   cfg->allowed_client_ip = allowed_client_ip;

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
   if ( nbytes < 0 )
   {
#if ( EAGAIN == EWOULDBLOCK )
      if ( errno != EAGAIN )
#else
      if ( errno != EAGAIN && errno != EWOULDBLOCK )
#endif
         tftp_log( TFTP_LOG_WARN, __func__,
                   "recvfrom() got unexpected error, returned %zd :: %s (%d) : %s",
                   nbytes, strerrorname_np(errno), errno, strerror(errno) );
      return;
   }
   else if ( nbytes == 0 )
   {
      static size_t empty_pkt_rcv_counter = 0;
      tftp_log( TFTP_LOG_INFO, NULL,
                "Empty UDP packet received on ctrl port... %zu of those rcvd now",
                ++empty_pkt_rcv_counter );
      return;
   }
   else if ( (size_t)nbytes < (sizeof("RESET")-1) )
   {
      static size_t undersized_pkt_rcv_counter = 0;
      tftp_log( TFTP_LOG_INFO, NULL,
                "Undersized payload received... %zu of those rcvd now",
                ++undersized_pkt_rcv_counter );
      return;
   }
   else if ( (size_t)nbytes > MAX_CTRL_CMD_SZ )
   {
      tftp_log( TFTP_LOG_INFO, NULL,
                "Ctrl cmd larger than expected max sz (%zu), received %zu bytes",
                MAX_CTRL_CMD_SZ, (size_t)nbytes );
      return;
   }
   else if ( sender.sin_family != AF_INET )
   {
      static size_t unsupported_af_counter = 0;
      tftp_log( TFTP_LOG_INFO, NULL,
                "Msg from unsupported socket address family received (not IPv4)"
                " sender.sin_family: %u... %zu unsupported address family pkts rcvd now",
                sender.sin_family, ++unsupported_af_counter );
      return;
   }
   // TODO: Need to rate-limit all these bad cases...

   // Enforce allowed_client_ip: 0 = accept any sender; otherwise must match exactly.
   if ( cfg->allowed_client_ip != 0 &&
        cfg->allowed_client_ip != sender.sin_addr.s_addr )
   {
      char sender_ip[INET_ADDRSTRLEN] = {0};
      const char * rcptr = inet_ntop(AF_INET, &sender.sin_addr, sender_ip, sizeof sender_ip);

      if ( rcptr == NULL )
         tftp_log( TFTP_LOG_INFO, NULL,
                   "inet_ntop failed :: sender.sin_addr.s_addr: %lu :: %s (%d) : %s",
                   (unsigned long)ntohl(sender.sin_addr.s_addr), strerrorname_np(errno), errno, strerror(errno) );
      else
         tftp_log( TFTP_LOG_INFO, NULL,
                   "Dropping ctrl pkt from disallowed sender %s:%u",
                   sender_ip, (unsigned)ntohs(sender.sin_port) );

      return;
   }

   assert( (size_t)nbytes > (sizeof("RESET")-1) );
   assert( (size_t)nbytes <= sizeof buf );

   buf[nbytes] = '\0';

   // Strip trailing newline
   size_t cmdlen = (size_t)nbytes;
   if ( cmdlen > 0 && (buf[cmdlen - 1] == '\n' || buf[cmdlen - 1] == '\r') )
      buf[--cmdlen] = '\0';

   // Strip leading whitespace
   const char *cmd = buf;
   while ( isspace((unsigned char)(*cmd)) && (cmdlen > 0) ) { cmd++; cmdlen--; }

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = 0;

   // SET_FAULT ----------------------------------------------------------------
   if ( cmdlen > (sizeof("SET_FAULT")-1)
        && strncasecmp(cmd, "SET_FAULT", sizeof("SET_FAULT")-1) == 0
        && (cmd[sizeof("SET_FAULT")-1] == ' ' || cmd[sizeof("SET_FAULT")-1] == '\t') )
   {
      tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd SET_FAULT cmd '%s'", cmd );

      // Parse: SET_FAULT <mode> [param]
      char mode_name[64] = {0};
      uint32_t param = 0;
      bool param_present = false;

      // Skip past "SET_FAULT" and any leading whitespace
      const char *p = cmd + sizeof("SET_FAULT") - 1;
      while ( *p == ' ' || *p == '\t' )
         p++;

      // Extract mode name token (up to next whitespace or end)
      const char *mode_start = p;
      while ( *p != '\0' && *p != ' ' && *p != '\t' )
         p++;

      size_t mode_len = (size_t)(p - mode_start);

      if ( mode_len == 0 )
      {
         tftp_log( TFTP_LOG_INFO, NULL, "Too few args for SET_FAULT cmd: '%s'", cmd );

         reply_len = snprintf(reply, sizeof reply, "ERR missing mode name\n");
         assert( reply_len < (int)(sizeof reply) );

         if ( reply_len > 0 )
            send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
         else
            tftp_log( TFTP_LOG_ERR, __func__,
                      "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                      __LINE__, strerrorname_np(errno), errno, strerror(errno) );

         return;
      }
      else if ( mode_len >= sizeof mode_name )
      {
         tftp_log( TFTP_LOG_INFO, NULL,
                   "SET_FAULT mode name too long (%zu bytes)", mode_len );

         reply_len = snprintf(reply, sizeof reply, "ERR mode name too long\n");
         assert( reply_len < (int)(sizeof reply) );

         if ( reply_len > 0 )
            send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
         else
            tftp_log( TFTP_LOG_ERR, __func__,
                      "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                      __LINE__, strerrorname_np(errno), errno, strerror(errno) );

         return;
      }
      else
      {
         memcpy(mode_name, mode_start, mode_len);
         mode_name[mode_len] = '\0';
      }

      // Optional second token: parse as uint32_t via strtoul
      while ( *p == ' ' || *p == '\t' )
         p++;

      if ( *p != '\0' )
      {
         char *endptr = NULL;
         errno = 0;
         unsigned long val = strtoul(p, &endptr, 10);

         if ( endptr == p
              || (*endptr != '\0' && *endptr != ' ' && *endptr != '\t')
              || errno == ERANGE
              || val > UINT32_MAX )
         {
            tftp_log( TFTP_LOG_INFO, NULL,
                      "SET_FAULT param invalid or out of range: '%s'",
                      p );

            reply_len = snprintf(reply, sizeof reply, "ERR invalid param\n");
            assert( reply_len < (int)(sizeof reply) );

            if ( reply_len > 0 )
               send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
            else
               tftp_log( TFTP_LOG_ERR, __func__,
                         "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                         __LINE__, strerrorname_np(errno), errno, strerror(errno) );

            return;
         }

         param = (uint32_t)val;
         param_present = true;
      }

      enum TFTPTest_FaultMode mode_idx = tftptest_fault_name_lookup_mode(mode_name);
      if ( (int)mode_idx < 0 )
      {
         tftp_log( TFTP_LOG_INFO, NULL, "Ctrl port cmd mode unknown: '%s'", mode_name );

         reply_len = snprintf( reply, sizeof reply,
                        "ERR unknown mode '%s' : error code %d\n",
                        mode_name, (int)mode_idx );
         assert( reply_len < (int)(sizeof reply) );

         if ( reply_len > 0 )
            send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
         else
            tftp_log( TFTP_LOG_ERR, __func__,
                      "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                      __LINE__, strerrorname_np(errno), errno, strerror(errno) );

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
            assert( reply_len < (int)(sizeof reply) );

            if ( reply_len > 0 )
               send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
            else
               tftp_log( TFTP_LOG_ERR, __func__,
                         "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                         __LINE__, strerrorname_np(errno), errno, strerror(errno) );

            return;
         }
      }

      fault->mode          = mode_idx;
      fault->param         = param;
      fault->param_present = param_present;

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
      assert( reply_len < (int)(sizeof reply) );

      if ( reply_len > 0 )
         send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
      else
         tftp_log( TFTP_LOG_ERR, __func__,
                   "%d :: Unable to send SET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                   __LINE__, strerrorname_np(errno), errno, strerror(errno) );
   }
   // GET_FAULT ----------------------------------------------------------------
   else if ( cmdlen >= (sizeof("GET_FAULT")-1)
             && strncasecmp(cmd, "GET_FAULT", sizeof("GET_FAULT")-1) == 0
             && (cmdlen == (sizeof("GET_FAULT")-1) || isspace((unsigned char)cmd[ sizeof("GET_FAULT")-1 ])) )
   {
      tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd GET_FAULT cmd '%s'", cmd );

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
      assert( reply_len < (int)(sizeof reply) );

      if ( reply_len > 0 )
         send_reply( cfg->sfd, &sender, reply, (size_t)reply_len );
      else
         tftp_log( TFTP_LOG_ERR, __func__,
                   "%d :: Unable to send GET_FAULT reply :: snprintf() failed, %s (%d) : %s",
                   __LINE__, strerrorname_np(errno), errno, strerror(errno) );
   }
   // RESET --------------------------------------------------------------------
   else if ( cmdlen >= (sizeof("RESET")-1)
             && strncasecmp(cmd, "RESET", sizeof("RESET")-1) == 0
             && (cmdlen == (sizeof("RESET")-1) || isspace((unsigned char)cmd[ sizeof("RESET")-1 ])) )
   {
      tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd RESET cmd '%s'", cmd );

      fault->mode          = FAULT_NONE;
      fault->param         = 0;
      fault->param_present = false;
      tftp_log( TFTP_LOG_INFO, NULL, "Control: fault mode reset" );

      reply_len = snprintf(reply, sizeof reply, "OK FAULT_NONE\n");
      assert( reply_len < (int)(sizeof reply) );

      if ( reply_len > 0 )
         send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
      else
         tftp_log( TFTP_LOG_ERR, __func__,
                   "%d :: Unable to send RESET reply :: snprintf() failed, %s (%d) : %s",
                   __LINE__, strerrorname_np(errno), errno, strerror(errno) );
   }
   // (unknown cmd) ------------------------------------------------------------
   else
   {
      tftp_log( TFTP_LOG_INFO, NULL, "Unknown ctrl port cmd rcvd '%s'", cmd );

      reply_len = snprintf(reply, sizeof reply, "ERR unknown command\n");
      assert( reply_len < (int)(sizeof reply) );

      if ( reply_len > 0 )
         send_reply(cfg->sfd, &sender, reply, (size_t)reply_len);
      else
         tftp_log( TFTP_LOG_ERR, __func__,
                   "%d :: Unable to send reply for unknown cmd :: snprintf() failed, %s (%d) : %s",
                   __LINE__, strerrorname_np(errno), errno, strerror(errno) );
   }
}

void tftptest_ctrl_shutdown( struct TFTPTest_CtrlCfg * const cfg )
{

#ifndef NDEBUG
   assert( cfg != NULL );
   assert( cfg->sfd >= 0 );
#else
   if ( cfg == NULL || cfg->sfd < 0 )
      return;
#endif

   tftp_log( TFTP_LOG_INFO, NULL,
             "Closing ctrl port socket %d listening on port %u...",
             cfg->sfd, cfg->port );

   if ( close(cfg->sfd) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__,
                "close(sfd=%d) failed: %s (%d) : %s",
                cfg->sfd, strerrorname_np(errno), errno, strerror(errno) );
   }

   cfg->sfd = -1;
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
