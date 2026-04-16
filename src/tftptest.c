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
#include <getopt.h>
#include <time.h>

// Sockets
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Internal Headers
#include "tftptest_faultmode.h"
#include "tftptest_ctrl.h"
#include "tftptest_seq.h"
#include "tftp_err.h"
#include "tftp_fsm.h"
#include "tftp_log.h"
#include "tftp_parsecfg.h"
#include "tftp_pkt.h"
#include "tftp_util.h"
#include "tftptest_common.h"

/***************************** Local Declarations *****************************/

#define DEFAULT_TFTP_RQST_PORT ((unsigned short)23069)

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
static size_t   session_wrq_bytes      = 0;
static size_t   session_wrq_file_count = 0;
static bool     wrq_session_blocked    = false;
static uint32_t blocked_peer_ip        = 0;   // network byte order; 0 = none

// Per-IP abandoned session tracking (prevents per-peer DoS)
#define MAX_TRACKED_PEERS ((size_t)20)
struct PeerAbandonedCount
{
   uint32_t peer_ip;   // network byte order
   size_t   count;
};
static struct PeerAbandonedCount peer_abandoned_history[MAX_TRACKED_PEERS];
static size_t peer_history_count = 0;

// Local Function Declarations
static void handleSIGINT(int sig_num);
static enum MainRC tftptest_new_conn_sock(int * const sfd_ptr, uint16_t port);
static bool is_peer_abandoned_locked_out(uint32_t peer_ip, size_t max_abandoned_sessions);
static void record_peer_abandoned_session(uint32_t peer_ip);

/******************************* Main Function ********************************/

// Long option definitions for getopt_long()
static const struct option long_options[] =
{
   { "config",   required_argument, NULL, 'c' },
   { "port",     required_argument, NULL, 'p' },
   { "user",     required_argument, NULL, 'u' },
   { "sequence", required_argument, NULL, 't' },
   { "tid-range", required_argument, NULL, 'r' },
   { "verbose",  no_argument,       NULL, 'v' },
   { "syslog",   no_argument,       NULL, 's' },
   { "help",     no_argument,       NULL, 'h' },
   { NULL,       0,                 NULL,  0  },  // sentinel
};

static void print_usage(const char *progname)
{
   fprintf(stderr,
      "Usage: %s [OPTIONS]\n"
      "Options:\n"
      "  -c, --config <file>     Path to INI config file\n"
      "  -p, --port <port>       Override TFTP port\n"
      "  -u, --user <user>       Run as user after chroot (default: nobody)\n"
      "  -t, --sequence <file>   Test sequence file (disables control channel)\n"
      "  -r, --tid-range MIN:MAX Restrict session TID ports to this range\n"
      "  -v, --verbose           Increase verbosity (repeat for more: -vvv)\n"
      "  -s, --syslog            Enable syslog output\n"
      "  -h, --help              Show this help message\n",
      progname);
}

int main(int argc, char * argv[])
{
   int mainrc = MAINRC_FINE;
   int sysrc; // For system calls

   int sfd_newconn = 0; // socket id for new connections on the pre-configured port

   // Parse CLI arguments
   const char *config_path = NULL;
   const char *user_override = NULL;
   const char *sequence_path = NULL;
   int verbosity = 0;
   bool use_syslog = false;
   uint16_t port_override = 0;
   bool port_overridden = false;
   uint16_t tid_min_override = 0;
   uint16_t tid_max_override = 0;
   bool tid_range_overridden = false;

   int opt;
   int option_index = 0;
   while ( (opt = getopt_long(argc, argv, "c:p:u:t:r:vsh", long_options, &option_index)) != -1 )
   {
      switch ( opt )
      {
      case 'c':
         config_path = optarg;
         break;
      case 'p':
      {
         unsigned long p = strtoul(optarg, NULL, 10);
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
      case 't':
         sequence_path = optarg;
         break;
      case 'r':
      {
         // Format: MIN:MAX
         char *colon = strchr(optarg, ':');
         if ( colon == NULL )
         {
            fprintf(stderr, "Invalid --tid-range: expected MIN:MAX, got '%s'\n", optarg);
            return 1;
         }
         *colon = '\0';
         unsigned long tmin = strtoul(optarg, NULL, 10);
         unsigned long tmax = strtoul(colon + 1, NULL, 10);
         if ( tmin == 0 || tmin > 65535 || tmax == 0 || tmax > 65535 || tmin > tmax )
         {
            fprintf(stderr, "Invalid --tid-range: ports must be 1-65535 with min <= max\n");
            return 1;
         }
         tid_min_override = (uint16_t)tmin;
         tid_max_override = (uint16_t)tmax;
         tid_range_overridden = true;
         break;
      }
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

   // Map verbosity to log level (default WARN, -v = INFO, -vv = DEBUG, -vvv = TRACE)
   enum TFTP_LogLevel log_level = TFTP_LOG_WARN;
   if ( verbosity >= 3 )
      log_level = TFTP_LOG_TRACE;
   else if ( verbosity == 2 )
      log_level = TFTP_LOG_DEBUG;
   else if ( verbosity == 1 )
      log_level = TFTP_LOG_INFO;

   tftp_log_init( use_syslog, log_level );

   // Register SIGINT handler
   struct sigaction sa_cfg;
   memset(&sa_cfg, 0x00, sizeof sa_cfg);
   sigemptyset(&sa_cfg.sa_mask); // No need to mask any signals during handle
   sa_cfg.sa_handler = handleSIGINT;
   sysrc = sigaction( SIGINT,
                      &sa_cfg,
                      NULL /* old sig action */ );

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
   if ( config_path != NULL )
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
   if ( user_override != NULL )
   {
      (void)strncpy( cfg.run_as_user, user_override, sizeof cfg.run_as_user - 1 );
      cfg.run_as_user[sizeof cfg.run_as_user - 1] = '\0';
   }
   if ( log_level < cfg.log_level )
      cfg.log_level = log_level;
   if ( tid_range_overridden )
   {
      cfg.tid_port_min = tid_min_override;
      cfg.tid_port_max = tid_max_override;
   }

   // Validate TID range doesn't overlap with server ports
   if ( cfg.tid_port_min != 0 )
   {
      if ( cfg.tftp_port >= cfg.tid_port_min && cfg.tftp_port <= cfg.tid_port_max )
      {
         tftp_log( TFTP_LOG_FATAL, "TID port range %u-%u overlaps with tftp_port %u",
                   cfg.tid_port_min, cfg.tid_port_max, cfg.tftp_port );
         return 1;
      }
      if ( cfg.ctrl_port != 0 &&
           cfg.ctrl_port >= cfg.tid_port_min && cfg.ctrl_port <= cfg.tid_port_max )
      {
         tftp_log( TFTP_LOG_FATAL, "TID port range %u-%u overlaps with ctrl_port %u",
                   cfg.tid_port_min, cfg.tid_port_max, cfg.ctrl_port );
         return 1;
      }
      tftp_log( TFTP_LOG_INFO, "TID port range: %u-%u",
                cfg.tid_port_min, cfg.tid_port_max );
   }

   // Seed RNG for TID port selection
   srand((unsigned int)time(NULL));

   // Fault state
   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Sequence mode vs. control channel mode
   struct TFTPTest_Seq seq = {0};
   bool use_sequence = false;
   int ctrl_sfd = -1;

   if ( sequence_path != NULL )
   {
      if ( tftptest_seq_load(sequence_path, &seq) != 0 )
      {
         tftp_log( TFTP_LOG_FATAL, "Failed to load sequence file: %s", sequence_path );
         return EXIT_FAILURE;
      }
      use_sequence = true;
      // Set initial fault from first entry
      fault.mode  = seq.entries[0].mode;
      fault.param = seq.entries[0].param;
      tftp_log( TFTP_LOG_INFO, "Sequence step 1/%zu: %s param=%u, %zu sessions",
                seq.n_entries, tftptest_fault_mode_names[fault.mode],
                fault.param, seq.entries[0].count );
      tftp_log( TFTP_LOG_INFO, "Sequence mode: control channel disabled" );
   }
   else if ( cfg.ctrl_port != 0 )
   {
      ctrl_sfd = tftptest_ctrl_init(cfg.ctrl_port);
      if ( ctrl_sfd < 0 )
      {
         tftp_log( TFTP_LOG_WARN, "Failed to create control channel on port %u: %s",
                   (unsigned)cfg.ctrl_port, strerror(errno) );
         // Non-fatal: continue without control channel
      }
   }
   else
   {
      tftp_log( TFTP_LOG_INFO, "Fault simulation disabled (ctrl_port = 0)" );
   }

   size_t nreps = 0;

   // Set up socket for listening on for new session requests...
   // (Must bind before chroot, since socket setup needs the network stack)
   mainrc |= (int)tftptest_new_conn_sock(&sfd_newconn, cfg.tftp_port);
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
      else if ( !tftp_pkt_request_is_valid(buf, (size_t)nbytes) )
      {
         tftp_log( TFTP_LOG_WARN, "Received malformed TFTP request, dropping" );
         continue;
      }
      else if ( cfg.allowed_client_ip != 0 && cfg.allowed_client_ip != peer_addr.sin_addr.s_addr )
      {
         // Client IP does not match allowed_client_ip; silently drop the packet
         continue;
      }

      {
         char addrbuf[INET_ADDRSTRLEN];
         (void)inet_ntop( AF_INET, &peer_addr.sin_addr, addrbuf, sizeof addrbuf );
         tftp_log( TFTP_LOG_INFO, "Received request from %s:%d",
                   addrbuf, ntohs(peer_addr.sin_port) );
      }

      // Per-peer lockout: reject requests from peers that have exceeded the abandoned session limit
      if ( is_peer_abandoned_locked_out(peer_addr.sin_addr.s_addr, cfg.max_abandoned_sessions) )
      {
         char addrbuf[INET_ADDRSTRLEN];
         (void)inet_ntop( AF_INET, &peer_addr.sin_addr, addrbuf, sizeof addrbuf );
         tftp_log( TFTP_LOG_WARN, "Peer %s locked out (exceeded max_abandoned_sessions)", addrbuf );
         uint8_t errbuf[128];
         size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                              TFTP_ERRC_ACCESS_VIOLATION, "Too many abandoned sessions");
         if ( errsz > 0 )
            (void)sendto(sfd_newconn, errbuf, errsz, 0,
                         (const struct sockaddr *)&peer_addr, sizeof peer_addr);
         continue;
      }

      // Determine request type
      uint16_t opcode = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
      bool is_wrq = (opcode == TFTP_OP_WRQ);

      // WRQ pre-KickOff rejection checks
      if ( is_wrq )
      {
         // Blocked attacker IP?
         if ( blocked_peer_ip != 0 && peer_addr.sin_addr.s_addr == blocked_peer_ip )
         {
            tftp_log( TFTP_LOG_WARN, "Blocked IP attempting WRQ, rejecting" );
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 TFTP_ERRC_ACCESS_VIOLATION, "Access denied");
            if ( errsz > 0 )
               (void)sendto(sfd_newconn, errbuf, errsz, 0,
                            (const struct sockaddr *)&peer_addr, sizeof peer_addr);
            continue;
         }

         // File count limit?
         if ( cfg.max_wrq_file_count > 0 &&
              session_wrq_file_count >= cfg.max_wrq_file_count )
         {
            tftp_log( TFTP_LOG_WARN, "WRQ file count limit (%zu) reached, rejecting",
                      cfg.max_wrq_file_count );
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 TFTP_ERRC_DISK_FULL, "Upload limit exceeded");
            if ( errsz > 0 )
               (void)sendto(sfd_newconn, errbuf, errsz, 0,
                            (const struct sockaddr *)&peer_addr, sizeof peer_addr);
            // Block this IP
            blocked_peer_ip = peer_addr.sin_addr.s_addr;
            if ( cfg.allowed_client_ip != 0 && blocked_peer_ip == cfg.allowed_client_ip )
            {
               tftp_log( TFTP_LOG_ERR, "Blocked IP is the only allowed client — shutting down" );
               goto Main_CleanupTag;
            }
            continue;
         }

         // Session bytes already exceeded?
         if ( wrq_session_blocked )
         {
            tftp_log( TFTP_LOG_WARN, "WRQ session byte limit reached, rejecting" );
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 TFTP_ERRC_DISK_FULL, "Upload limit exceeded");
            if ( errsz > 0 )
               (void)sendto(sfd_newconn, errbuf, errsz, 0,
                            (const struct sockaddr *)&peer_addr, sizeof peer_addr);
            continue;
         }
      }

      // Compute WRQ session budget
      size_t budget = 0;
      if ( is_wrq && cfg.max_wrq_session_bytes > 0 )
      {
         budget = (session_wrq_bytes >= cfg.max_wrq_session_bytes)
                  ? 1  // budget exhausted, will fail immediately in FSM
                  : cfg.max_wrq_session_bytes - session_wrq_bytes;
      }

      size_t bytes_written = 0;
      enum TFTP_FSM_RC fsm_rc = tftp_fsm_kickoff(buf, (size_t)nbytes, &peer_addr, &cfg, &fault,
                                                    is_wrq ? budget : 0,
                                                    is_wrq ? &bytes_written : NULL);

      // WRQ post-KickOff accounting
      if ( is_wrq )
      {
         session_wrq_bytes      += bytes_written;
         session_wrq_file_count += 1;

         // Did a limit violation happen during transfer?
         if ( fsm_rc & TFTP_FSM_RC_WRQ_LIMIT_VIOLATION )
         {
            blocked_peer_ip = peer_addr.sin_addr.s_addr;
            char ipbuf[INET_ADDRSTRLEN];
            (void)inet_ntop(AF_INET, &peer_addr.sin_addr, ipbuf, sizeof ipbuf);
            tftp_log( TFTP_LOG_WARN, "Limit violation from %s — blocking IP for this session", ipbuf );

            // Exit if this was the only allowed client
            if ( cfg.allowed_client_ip != 0 && blocked_peer_ip == cfg.allowed_client_ip )
            {
               tftp_log( TFTP_LOG_ERR, "Blocked IP is the only allowed client — shutting down" );
               goto Main_CleanupTag;
            }
         }

         // Mark session as WRQ-blocked if session total exceeded
         if ( cfg.max_wrq_session_bytes > 0 &&
              session_wrq_bytes >= cfg.max_wrq_session_bytes )
         {
            wrq_session_blocked = true;
            tftp_log( TFTP_LOG_WARN, "WRQ session byte limit reached — no further WRQ accepted" );
         }
      }

      // Per-peer abandoned session tracking
      if ( fsm_rc & TFTP_FSM_RC_TIMEOUT )
      {
         record_peer_abandoned_session(peer_addr.sin_addr.s_addr);

         // Log if this peer just hit the limit
         if ( is_peer_abandoned_locked_out(peer_addr.sin_addr.s_addr, cfg.max_abandoned_sessions) )
         {
            char addrbuf[INET_ADDRSTRLEN];
            (void)inet_ntop( AF_INET, &peer_addr.sin_addr, addrbuf, sizeof addrbuf );
            tftp_log( TFTP_LOG_WARN,
                      "Peer %s has hit max_abandoned_sessions limit (%zu) — will reject further requests",
                      addrbuf, cfg.max_abandoned_sessions );
         }
      }

      // Advance fault mode: sequence stepper or control channel
      if ( use_sequence )
      {
         if ( !tftptest_seq_advance(&seq, &fault) )
         {
            tftp_log( TFTP_LOG_INFO, "Test sequence complete, shutting down" );
            break;
         }
      }
      else if ( ctrl_sfd >= 0 )
      {
         tftptest_ctrl_poll(ctrl_sfd, &fault, cfg.fault_whitelist);
      }
   }

   tftp_fsm_clean_exit();

   if ( bUserEndedSession )
   {
      tftp_log( TFTP_LOG_INFO, "User ended session (SIGINT)" );
   }
   else if ( use_sequence && seq.current >= seq.n_entries )
   {
      tftp_log( TFTP_LOG_INFO, "Test sequence completed successfully" );
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
   if ( use_sequence )
      tftptest_seq_free(&seq);
   else
      tftptest_ctrl_shutdown(ctrl_sfd);
   (void)close(sfd_newconn);
   tftp_log( TFTP_LOG_INFO, "Server shutting down (rc=0x%04x)", (unsigned)mainrc );
   tftp_log_shutdown();

   return mainrc;
}
 
/*********************** Local Function Implementations ***********************/

/**
 * @brief Check if a peer IP has exceeded the abandoned session limit.
 * @param[in] peer_ip        Peer's IP address (network byte order)
 * @param[in] max_abandoned  Max abandoned sessions allowed per peer (0 = unlimited)
 * @return true if peer is locked out; false otherwise
 */
static bool is_peer_abandoned_locked_out(uint32_t peer_ip, size_t max_abandoned_sessions)
{
   if ( max_abandoned_sessions == 0 )
      return false;  // Disabled

   for ( size_t i = 0; i < peer_history_count; i++ )
   {
      if ( peer_abandoned_history[i].peer_ip == peer_ip &&
           peer_abandoned_history[i].count >= max_abandoned_sessions )
         return true;
   }
   return false;
}

/**
 * @brief Record an abandoned session for a peer IP, incrementing its count.
 * @param[in] peer_ip  Peer's IP address (network byte order)
 */
static void record_peer_abandoned_session(uint32_t peer_ip)
{
   // Search for existing entry
   for ( size_t i = 0; i < peer_history_count; i++ )
   {
      if ( peer_abandoned_history[i].peer_ip == peer_ip )
      {
         peer_abandoned_history[i].count++;
         return;
      }
   }

   // Not found — add new entry if there's space
   if ( peer_history_count < MAX_TRACKED_PEERS )
   {
      peer_abandoned_history[peer_history_count].peer_ip = peer_ip;
      peer_abandoned_history[peer_history_count].count = 1;
      peer_history_count++;
   }
}

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
static enum MainRC tftptest_new_conn_sock(int * const sfd_ptr, uint16_t port)
{
   assert(sfd_ptr != NULL);

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
