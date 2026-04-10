/**
 * @file tftptest.c
 * @brief TFTP server that you can command to simulate a variety of TFTP faults for testing purposes.
 * @date Mar 26, 2026
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
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>

// Sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Internal Headers
#include "tftptest_faultmode.h"
#include "tftptest_ctrl.h"
#include "tftp_err.h"
#include "tftp_fsm.h"
#include "tftp_log.h"
#include "tftp_parsecfg.h"
#include "tftp_pkt.h"
#include "tftp_util.h"
#include "tftptest_common.h"

/***************************** Local Declarations *****************************/

#define DEFAULT_TFTP_RQST_PORT 23069

// Types
enum MainRC
{
   MAINRC_FINE                    = 0x0000,
   MAINRC_NREP_LIM_HIT            = 0x0001,
   MAINRC_SIGINT_REGISTRATION_ERR = 0x0002,
   MAINRC_SOCKET_CREATION_ERR     = 0x0004,
   MAINRC_SETSOCKOPT_ERR          = 0x0008,
   MAINRC_SOCKBIND_ERR            = 0x0010,
   MAINRC_RECVFROM_ERR            = 0x0020,
   MAINRC_SOCK_CLOSE_ERR          = 0x0040,
   MAINRC_FAILED_CLOSE            = 0x0080,
};

// File-Scope Variables
static volatile sig_atomic_t bUserEndedSession = false;

// Local Function Declarations
static void handleSIGINT(int sig_num);
static enum MainRC TFTP_Test_SetUpNewConnSock(int * const sfd_ptr, uint16_t port);

/******************************* Main Function ********************************/
static void print_usage(const char *progname)
{
   fprintf(stderr,
      "Usage: %s [OPTIONS]\n"
      "  -c <config>  Path to INI config file\n"
      "  -p <port>    Override TFTP port\n"
      "  -u <user>    Run as user after chroot (default: nobody)\n"
      "  -v           Increase verbosity (repeat for more: -vvv)\n"
      "  -s           Enable syslog output\n"
      "  -h           Show this help\n",
      progname);
}

int main(int argc, char * argv[])
{
   int mainrc = MAINRC_FINE;
   int sysrc; // For system calls

   int sfd_newconn = 0; // socket id for new connections on the pre-configured port

   // Parse CLI arguments
   const char *config_path = nullptr;
   const char *user_override = nullptr;
   int verbosity = 0;
   bool use_syslog = false;
   uint16_t port_override = 0;
   bool port_overridden = false;

   int opt;
   while ( (opt = getopt(argc, argv, "c:p:u:vsh")) != -1 )
   {
      switch ( opt )
      {
      case 'c':
         config_path = optarg;
         break;
      case 'p':
      {
         unsigned long p = strtoul(optarg, nullptr, 10);
         if ( p == 0 || p > 65535 )
         {
            fprintf(stderr, "Invalid port: %s\n", optarg);
            return 1;
         }
         port_override = (uint16_t)p;
         port_overridden = true;
         break;
      }
      case 'u':
         user_override = optarg;
         break;
      case 'v':
         verbosity++;
         break;
      case 's':
         use_syslog = true;
         break;
      case 'h':
         print_usage(argv[0]);
         return 0;
      default:
         print_usage(argv[0]);
         return 1;
      }
   }

   // Map verbosity to log level (default INFO, -v = DEBUG, -vv = TRACE)
   enum TFTP_LogLevel log_level = TFTP_LOG_INFO;
   if ( verbosity >= 2 )
      log_level = TFTP_LOG_TRACE;
   else if ( verbosity == 1 )
      log_level = TFTP_LOG_DEBUG;

   tftp_log_init( use_syslog, log_level );

   // Register SIGINT handler
   struct sigaction sa_cfg;
   memset(&sa_cfg, 0x00, sizeof sa_cfg);
   sigemptyset(&sa_cfg.sa_mask); // No need to mask any signals during handle
   sa_cfg.sa_handler = handleSIGINT;
   sysrc = sigaction( SIGINT,
                      &sa_cfg,
                      nullptr /* old sig action */ );

   // Really shouldn't happen, but I'll check just in case.
   if ( sysrc != 0 )
   {
      tftp_log( TFTP_LOG_ERR,
               "sigaction() failed to register SIGINT handler. "
               "Returned: %d, errno: %s (%d): %s. "
               "Ctrl+C won't trigger graceful shutdown.",
               sysrc, strerrorname_np(errno), errno, strerror(errno) );

      return MAINRC_SIGINT_REGISTRATION_ERR;
   }

   // Load config
   struct TFTPTest_Config cfg;
   tftp_parsecfg_defaults(&cfg);
   if ( config_path != nullptr )
   {
      if ( tftp_parsecfg_load(config_path, &cfg) != 0 )
         tftp_log( TFTP_LOG_WARN, "Failed to load config '%s', using defaults", config_path );
   }

   // CLI overrides take precedence over config file
   if ( port_overridden )
   {
      cfg.tftp_port = port_override;
      cfg.ctrl_port = (uint16_t)(port_override + 1);
   }
   if ( user_override != nullptr )
   {
      (void)strncpy( cfg.run_as_user, user_override, sizeof cfg.run_as_user - 1 );
      cfg.run_as_user[sizeof cfg.run_as_user - 1] = '\0';
   }
   if ( log_level < cfg.log_level )
      cfg.log_level = log_level;

   // Fault state
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Set up control channel
   int ctrl_sfd = tftptest_ctrl_init(cfg.ctrl_port);
   if ( ctrl_sfd < 0 )
   {
      tftp_log( TFTP_LOG_WARN, "Failed to create control channel on port %u: %s",
                (unsigned)cfg.ctrl_port, strerror(errno) );
      // Non-fatal: continue without control channel
   }

   size_t nreps = 0;

   // Set up socket for listening on for new session requests...
   // (Must bind before chroot, since socket setup needs the network stack)
   mainrc |= (int)TFTP_Test_SetUpNewConnSock(&sfd_newconn, cfg.tftp_port);
   if ( mainrc != MAINRC_FINE )
      goto Main_CleanupTag;

   // Chroot into TFTP root directory and drop privileges
   if ( tftp_util_chroot_and_drop( cfg.root_dir, cfg.run_as_user ) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, "Failed to chroot/drop privileges" );
      mainrc |= MAINRC_FAILED_CLOSE;
      goto Main_CleanupTag;
   }

   // Primary loop
   while ( !bUserEndedSession && nreps++ < cfg.max_requests )
   {
      // Await packet
      uint8_t buf[TFTP_RQST_MAX_SZ + 1] = {0};
      struct sockaddr_in peer_addr = {0};
      socklen_t addrlen = sizeof peer_addr;

      ssize_t nbytes = recvfrom( sfd_newconn,
                                 buf, sizeof buf,
                                 0, /* flags */
                                 &peer_addr, &addrlen );

      // Validate request packet
      assert( nbytes <= (ssize_t)sizeof(buf) );
      if ( nbytes == sizeof buf )
      {
         tftp_log( TFTP_LOG_WARN, "Received oversized packet (%zd bytes), dropping", nbytes );
         continue;
      }
      else if ( nbytes < 0 )
      {
         tftp_log( TFTP_LOG_ERR, "recvfrom() failed: %s (%d)", strerror(errno), errno );
         continue;
      }
      else if ( addrlen > sizeof peer_addr )
      {
         tftp_log( TFTP_LOG_WARN, "Received request from non-IPv4 peer, dropping" );
         continue;
      }
      else if ( nbytes < (ssize_t)TFTP_RQST_MIN_SZ )
      {
         tftp_log( TFTP_LOG_WARN, "Received packet too small (%zd bytes), dropping", nbytes );
         continue;
      }
      else if ( !TFTP_PKT_RequestIsValid(buf, (size_t)nbytes) )
      {
         tftp_log( TFTP_LOG_WARN, "Received malformed TFTP request, dropping" );
         continue;
      }

      {
         char addrbuf[INET_ADDRSTRLEN];
         (void)inet_ntop( AF_INET, &peer_addr.sin_addr, addrbuf, sizeof addrbuf );
         tftp_log( TFTP_LOG_INFO, "Received request from %s:%d",
                   addrbuf, ntohs(peer_addr.sin_port) );
      }

      // Initiate FSM for session and wait for completion
      (void)TFTP_FSM_KickOff(buf, (size_t)nbytes, &peer_addr, &cfg, &fault);

      // Poll control channel for fault mode updates (non-blocking)
      if ( ctrl_sfd >= 0 )
         tftptest_ctrl_poll(ctrl_sfd, &fault, cfg.fault_whitelist);
   }

   TFTP_FSM_CleanExit();

   if ( bUserEndedSession )
   {
      tftp_log( TFTP_LOG_INFO, "User ended session (SIGINT)" );
   }
   else if ( nreps >= cfg.max_requests )
   {
      tftp_log( TFTP_LOG_WARN, "Max request limit (%zu) reached, shutting down", cfg.max_requests );
   }
   else
   {
      tftp_log( TFTP_LOG_ERR, "Main loop exited unexpectedly" );
   }

Main_CleanupTag:
   tftptest_ctrl_shutdown(ctrl_sfd);
   (void)close(sfd_newconn);
   tftp_log( TFTP_LOG_INFO, "Server shutting down (rc=0x%04x)", (unsigned)mainrc );
   tftp_log_shutdown();

   return mainrc;
}
 
/*********************** Local Function Implementations ***********************/

/**
 * @brief Handle the interrupt signal that a user would trigger /w Ctrl+C
 */
static void handleSIGINT(int sig_num)
{
   (void)sig_num; // Signal number is not necessary here

   // Abort since we didn't execute a graceful shutdown the first time.
   // Note: cannot call tftp_log() here -- not async-signal-safe.
   if ( bUserEndedSession )
   {
      abort();
   }

   bUserEndedSession = true;
}

/**
 * @brief TODO
 */
static enum MainRC TFTP_Test_SetUpNewConnSock(int * const sfd_ptr, uint16_t port)
{
   assert(sfd_ptr != nullptr);

   int sysrc = 0;
   int sfd = -1;

   tftp_log( TFTP_LOG_DEBUG, "Creating listening socket on port %u...", (unsigned)port );

   // Create socket
   sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 )
   {
      tftp_log( TFTP_LOG_ERR,
               "Failed to create listening socket. "
               "socket() returned: %d, errno: %s (%d): %s",
               sfd,
               strerrorname_np(errno), errno, strerror(errno) );

      return MAINRC_SOCKET_CREATION_ERR;
   }

   // Facilitate reuse of the consistent port in case a lingering socket from a
   // previous session hasn't been cleared away. TODO: Verify this is needed.
   sysrc = setsockopt( sfd,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &(int){1},
                       sizeof(int) );
   assert(sysrc == 0); // if setsockopt() failed, we set something up wrong

   // Bind socket to pre-configured IP/port
   const struct in_addr numerical_addr = { .s_addr = INADDR_ANY }; // TODO: Configurable
   const in_port_t net_port = htons(port);
   sysrc = bind( sfd,
                  (struct sockaddr *)
                  &(struct sockaddr_in) {
                     .sin_family = AF_INET,
                     .sin_port = net_port,
                     .sin_addr = numerical_addr
                  },
                  sizeof(struct sockaddr_in) );

   if ( sysrc != 0 )
   {
      char addrbuf[INET_ADDRSTRLEN];
      (void)inet_ntop(AF_INET, &numerical_addr, addrbuf, INET_ADDRSTRLEN);

      tftp_log( TFTP_LOG_ERR,
               "Failed to bind socket to %s:%u. "
               "bind() returned: %d, errno: %s (%d): %s",
               addrbuf, (unsigned)port, sysrc,
               strerrorname_np(errno), errno, strerror(errno) );

      (void)close(sfd);
      *sfd_ptr = -1;

      return MAINRC_SOCKBIND_ERR;
   }

   *sfd_ptr = sfd;

   return MAINRC_FINE;
}
