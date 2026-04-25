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
#include "tftptest_whitelist.h"
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

#define MIN_SET_FAULT_CMD_SZ (sizeof("SET_FAULT x")-1)

// Module-private config struct type
struct TFTPTest_CtrlCfg
{
   int      sfd;       // Socket fd (-1 if uninitialized / after shutdown)
   uint16_t port;      // Port the control channel is bound to
   uint64_t whitelist; // Bitmask of allowed fault modes
};

// The one instance — sfd=-1 means uninitialised / shut down.
static struct TFTPTest_CtrlCfg s_ctrl_cfg = { .sfd = -1 };

// Stage helpers for the dispatcher
static ssize_t recv_ctrl_pkt( char * buf, size_t buf_sz,
                              struct sockaddr_in * sender );
static bool sender_allowed( const struct sockaddr_in * sender );

// Per-command handlers
static void handle_set_fault( struct TFTPTest_FaultState * fault,
                              const struct sockaddr_in * sender,
                              const char * cmd,
                              size_t cmdlen );
static void handle_get_fault( const struct TFTPTest_FaultState * fault,
                              const struct sockaddr_in * sender );
static void handle_reset( struct TFTPTest_FaultState * fault,
                          const struct sockaddr_in * sender );
static void handle_unknown( const struct sockaddr_in * sender,
                            const char * cmd );

static void send_reply( int sfd,
                        const struct sockaddr_in * dest,
                        const char * msg,
                        size_t len );

/** (Desired) Inline Functions **/

// True when `cmd` (length cmdlen) starts with `name` (length name_len),
// case-insensitive, followed by either end-of-string or whitespace.
static inline bool cmd_matches( const char * cmd, size_t cmdlen,
                                const char * name, size_t name_len )
{
   return cmdlen >= name_len
       && strncasecmp(cmd, name, name_len) == 0
       && ( cmdlen == name_len
            || isspace((unsigned char)cmd[name_len]) );
}

// Centralizes the "send reply unless snprintf failed" boilerplate. Truncation
// (snprintf returning >= cap) is the caller's responsibility to assert against;
// this helper only handles the snprintf-returned-negative failure mode.
static inline void send_reply_or_log_fail( int sfd,
                                           const struct sockaddr_in * sender,
                                           const char * reply,
                                           int reply_len,
                                           const char * caller_func )
{
   if ( reply_len >= 0 )
   {
      send_reply(sfd, sender, reply, (size_t)reply_len);
   }
   else
   {
      tftp_log( TFTP_LOG_ERR, caller_func,
                "Unable to send reply :: snprintf() failed, %s (%d) : %s",
                strerrorname_np(errno), errno, strerror(errno) );
   }
}

/********************** Public Function Implementations ***********************/

enum TFTPTest_CtrlResult tftptest_ctrl_init( uint16_t port, uint64_t whitelist )
{
   // Initialize early so shutdown() is safe on any failure path.
   s_ctrl_cfg.sfd       = -1;
   s_ctrl_cfg.port      = port;
   s_ctrl_cfg.whitelist = whitelist;

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

   s_ctrl_cfg.sfd = sfd;
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

void tftptest_ctrl_poll_and_handle( struct TFTPTest_FaultState * const fault )
{
   assert( fault != NULL );

   char buf[CTRL_CMD_BUF_SZ];
   struct sockaddr_in sender = {0};

   ssize_t nbytes = recv_ctrl_pkt(buf, sizeof buf, &sender);
   if ( nbytes <= 0 )
      return;

   if ( !sender_allowed(&sender) )
      return;

   assert( nbytes >  0 );
   assert( (size_t)nbytes <= sizeof buf );

   buf[nbytes] = '\0';
   size_t cmdlen = (size_t)nbytes;

   // Strip all trailing CR/LF (handles bare \n, bare \r, and Windows \r\n)
   while ( cmdlen > 0 && (buf[cmdlen - 1] == '\n' || buf[cmdlen - 1] == '\r') )
      buf[--cmdlen] = '\0';

   // Strip leading whitespace. Bounded by `cmdlen > 0` for defense in depth;
   // isspace('\0') is 0 so the loop also self-terminates at the trailing nul.
   const char *cmd = buf;
   while ( cmdlen > 0 && isspace((unsigned char)(*cmd)) )
   {
      cmd++;
      cmdlen--;
   }

   if ( cmd_matches(cmd, cmdlen, "SET_FAULT", sizeof("SET_FAULT")-1) )
   {
      handle_set_fault(fault, &sender, cmd, cmdlen);
   }
   else if ( cmd_matches(cmd, cmdlen, "GET_FAULT", sizeof("GET_FAULT")-1) )
   {
      handle_get_fault(fault, &sender);
   }
   else if ( cmd_matches(cmd, cmdlen, "RESET", sizeof("RESET")-1) )
   {
      handle_reset(fault, &sender);
   }
   else
   {
      handle_unknown(&sender, cmd);
   }
}

void tftptest_ctrl_shutdown( void )
{
   if ( s_ctrl_cfg.sfd < 0 )
      return;

   tftp_log( TFTP_LOG_INFO, NULL,
             "Closing ctrl port socket %d listening on port %u...",
             s_ctrl_cfg.sfd, s_ctrl_cfg.port );

   if ( close(s_ctrl_cfg.sfd) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__,
                "close(sfd=%d) failed: %s (%d) : %s",
                s_ctrl_cfg.sfd, strerrorname_np(errno), errno, strerror(errno) );
   }

   s_ctrl_cfg.sfd = -1;
}

/*********************** Local Function Implementations ***********************/

// Receive one ctrl-port packet, validating size and address-length sanity.
// Returns the byte count on a usable packet; returns 0 (and the caller should
// silently move on) for empty / oversized / wrong-address-family / no-data
// cases. Returns -1 only on an unexpected recvfrom() error (already logged).
static ssize_t recv_ctrl_pkt( char * buf, size_t buf_sz,
                              struct sockaddr_in * sender )
{
   assert( buf != NULL );
   assert( sender != NULL );
   assert( buf_sz > 0 );

   socklen_t addrlen = sizeof *sender;

   ssize_t nbytes = recvfrom( s_ctrl_cfg.sfd,
                              buf, buf_sz - 1,
                              0,
                              (struct sockaddr *)sender, &addrlen );
   if ( nbytes < 0 )
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
         return -1;
      }

      return 0;
   }
   else if ( nbytes == 0 )
   {
      static size_t empty_pkt_rcv_counter = 0;

      tftp_log( TFTP_LOG_INFO, NULL,
                "Empty UDP packet received on ctrl port... %zu of those rcvd now",
                ++empty_pkt_rcv_counter );
      return 0;
   }
   else if ( (size_t)nbytes > MAX_CTRL_CMD_SZ )
   {
      tftp_log( TFTP_LOG_INFO, NULL,
                "Ctrl cmd larger than expected max sz (%zu), received %zu bytes",
                MAX_CTRL_CMD_SZ, (size_t)nbytes );
      return 0;
   }
   // Presently, this else clause is dead code, but it is meant to be forward
   // defensive, for if/when other address families come into play.
   else if ( addrlen != sizeof(struct sockaddr_in)
             || sender->sin_family != AF_INET )
   {
      static size_t unsupported_af_counter = 0;

      tftp_log( TFTP_LOG_INFO, NULL,
                "Msg from non-IPv4 sender on ctrl port: addrlen=%u family=%u"
                " ... %zu non-IPv4 pkts rcvd now",
                addrlen, (unsigned)sender->sin_family,
                ++unsupported_af_counter );
      return 0;
   }
   // TODO: rate-limit all these bad cases.

   return nbytes;
}

// Returns true if the sender is permitted to talk to this control channel.
// Logs the drop at INFO when rejecting.
static bool sender_allowed( const struct sockaddr_in * sender )
{
   assert( sender != NULL );

   if ( tftp_ipwhitelist_contains( sender->sin_addr.s_addr ) )
   {
      return true;
   }

   char sender_ip[INET_ADDRSTRLEN] = {0};
   const char * rcptr = inet_ntop(AF_INET, &sender->sin_addr,
                                  sender_ip, sizeof sender_ip);
   if ( rcptr == NULL )
      tftp_log( TFTP_LOG_INFO, NULL,
                "inet_ntop failed :: sender.sin_addr.s_addr: %lu :: %s (%d) : %s",
                (unsigned long)sender->sin_addr.s_addr,
                strerrorname_np(errno), errno, strerror(errno) );
   else
      tftp_log( TFTP_LOG_INFO, NULL,
                "Dropping ctrl pkt from disallowed sender %s:%u",
                sender_ip, (unsigned)ntohs(sender->sin_port) );

   return false;
}

// Handle SET_FAULT <mode> [param]. Caller has already verified that `cmd`
// begins with "SET_FAULT" + whitespace and contains at least one byte after.
static void handle_set_fault( struct TFTPTest_FaultState * fault,
                              const struct sockaddr_in * sender,
                              const char * cmd,
                              size_t cmdlen )
{
   assert( fault != NULL );
   assert( sender != NULL );
   assert( cmd != NULL );

   tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd SET_FAULT cmd '%s'", cmd );

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = 0;

   if ( cmdlen < MIN_SET_FAULT_CMD_SZ )
   {
      tftp_log( TFTP_LOG_INFO, NULL, "Bare SET_FAULT cmd received (no args)" );

      reply_len = snprintf(reply, sizeof reply, "ERR missing at least mode argument for SET_FAULT\n");
      assert( reply_len < (int)(sizeof reply) );
      send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

      return;
   }

   char mode_name[LONGEST_FAULT_MODE_NAME_LEN + 1] = {0};
   uint32_t param = 0;
   bool param_present = false;

   // Skip past "SET_FAULT" and any inter-token whitespace
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
      send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

      return;
   }
   else if ( mode_len >= sizeof mode_name )
   {
      tftp_log( TFTP_LOG_INFO, NULL,
                "SET_FAULT mode name too long (%zu bytes)", mode_len );

      reply_len = snprintf(reply, sizeof reply, "ERR mode name too long\n");
      assert( reply_len < (int)(sizeof reply) );
      send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

      return;
   }

   memcpy(mode_name, mode_start, mode_len);
   mode_name[mode_len] = '\0';

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
                   "SET_FAULT param invalid or out of range: '%s'", p );

         reply_len = snprintf(reply, sizeof reply, "ERR invalid param\n");
         assert( reply_len < (int)(sizeof reply) );
         send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

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
      send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

      return;
   }

   // Whitelist gate
   assert( mode_idx >= 0 );
   if ( (int)mode_idx > FAULT_NONE )
   {
      uint64_t mode_bit = (uint64_t)1 << (unsigned)((int)mode_idx - 1);
      if ( !(s_ctrl_cfg.whitelist & mode_bit) )
      {
         tftp_log( TFTP_LOG_INFO, NULL, "Ctrl port cmd not on whitelist: '%s'", mode_name );

         reply_len = snprintf(reply, sizeof reply, "ERR mode '%s' not allowed\n", mode_name);
         assert( reply_len < (int)(sizeof reply) );
         send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);

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
   send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);
}

static void handle_get_fault( const struct TFTPTest_FaultState * fault,
                              const struct sockaddr_in * sender )
{
   assert( fault != NULL );
   assert( sender != NULL );

   tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd GET_FAULT cmd" );

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = 0;

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
   send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);
}

static void handle_reset( struct TFTPTest_FaultState * fault,
                          const struct sockaddr_in * sender )
{
   assert( fault != NULL );
   assert( sender != NULL );

   tftp_log( TFTP_LOG_DEBUG, __func__, "Rcvd RESET cmd" );

   fault->mode          = FAULT_NONE;
   fault->param         = 0;
   fault->param_present = false;
   tftp_log( TFTP_LOG_INFO, NULL, "Control: fault mode reset" );

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = snprintf(reply, sizeof reply, "OK FAULT_NONE\n");
   assert( reply_len < (int)(sizeof reply) );
   send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);
}

static void handle_unknown( const struct sockaddr_in * sender,
                            const char * cmd )
{
   assert( sender != NULL );
   assert( cmd != NULL );

   tftp_log( TFTP_LOG_INFO, NULL, "Unknown (or malformed) ctrl port cmd rcvd '%s'", cmd );

   char reply[CTRL_RSP_BUF_SZ];
   int reply_len = snprintf(reply, sizeof reply, "ERR unknown/malformed command '%s'\n", cmd);
   assert( reply_len < (int)(sizeof reply) );
   send_reply_or_log_fail(s_ctrl_cfg.sfd, sender, reply, reply_len, __func__);
}

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
