/**
 * @file tftp_fsm.c
 * @brief TFTP FSM to handle a session transaction (either read or write).
 * @date Mar 27, 2026
 * @author Abdulla Almosalmi, @memphis242
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
#include <strings.h>

// General System Headers
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include <sys/statvfs.h>

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
#include "tftptest_common.h"

/***************************** Local Declarations *****************************/

// FSM states
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

enum TFTP_TransferMode
{
   TFTP_MODE_OCTET,
   TFTP_MODE_NETASCII,
};

// Session context -- file-scope singleton (one session at a time)
static struct TFTP_FSM_Session_S
{
   int                     sfd;
   enum TFTP_FSM_State     state;
   FILE                   *fp;
   struct sockaddr_in      peer_addr;
   uint16_t                block_num;
   unsigned int            retries;
   unsigned int            max_retries;
   unsigned int            timeout_sec;
   enum TFTP_TransferMode  transfer_mode;
   bool                    netascii_pending_cr;
   uint8_t                 sendbuf[TFTP_DATA_MAX_SZ];
   size_t                  sendbuf_len;
   // WRQ DoS protection state
   size_t                  wrq_bytes_written;
   struct timespec         wrq_start_time;
   size_t                  wrq_session_budget;
   char                    wrq_filename[FILENAME_MAX_LEN + 1];
   // OOO fault state
   uint8_t                 ooo_stashed_pkt[TFTP_DATA_MAX_SZ];
   size_t                  ooo_stashed_len;
   bool                    ooo_pending;
   uint16_t                ooo_stashed_block;
} TFTP_FSM_Session;

// Internal Function Declarations
static void session_cleanup(void);
static bool tid_matches(const struct sockaddr_in *incoming);
static enum TFTP_FSM_RC send_error_to(int sfd, const struct sockaddr_in *dest,
                                       uint16_t error_code, const char *msg);
static bool fault_should_suppress_data(const struct TFTPTest_FaultState *fault,
                                        uint16_t block_num, bool is_last);
static bool fault_should_suppress_ack(const struct TFTPTest_FaultState *fault,
                                       uint16_t block_num, bool is_last);
static bool fault_should_duplicate(const struct TFTPTest_FaultState *fault,
                                    bool is_data, uint16_t block_num, bool is_last);
static void fault_modify_outgoing(const struct TFTPTest_FaultState *fault,
                                   uint8_t *pkt, size_t *pkt_len, size_t pkt_cap,
                                   bool is_data, uint16_t block_num);
static int fault_maybe_wrong_tid(const struct TFTPTest_FaultState *fault,
                                  bool is_rrq);
static void fault_maybe_delay(const struct TFTPTest_FaultState *fault);

/********************** Public Function Implementations ***********************/

enum TFTP_FSM_RC tftp_fsm_kickoff(const uint8_t *rqbuf, size_t rqsz,
                                    const struct sockaddr_in *peer_addr,
                                    const struct TFTPTest_Config *cfg,
                                    const struct TFTPTest_FaultState *fault,
                                    size_t wrq_session_budget,
                                    size_t *wrq_bytes_written)
{
   assert( rqbuf != NULL );
   assert( peer_addr != NULL );
   assert( cfg != NULL );
   assert( fault != NULL );
   assert( rqsz != 0 );

   enum TFTP_FSM_RC rc = TFTP_FSM_RC_FINE;

   // Parse the request
   uint16_t opcode = 0;
   const char *filename = NULL;
   const char *mode = NULL;
   if ( tftp_pkt_parse_request(rqbuf, rqsz, &opcode, &filename, &mode) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, "FSM: Failed to parse request packet" );
      return TFTP_FSM_RC_PROTOCOL_ERR;
   }

   tftp_log( TFTP_LOG_INFO, "FSM: %s request for '%s' (mode: %s)",
             opcode == TFTP_OP_RRQ ? "RRQ" : "WRQ", filename, mode );

   // Initialize session
   memset( &TFTP_FSM_Session, 0, sizeof TFTP_FSM_Session );
   TFTP_FSM_Session.sfd = -1;
   TFTP_FSM_Session.fp = NULL;
   TFTP_FSM_Session.peer_addr = *peer_addr;
   TFTP_FSM_Session.block_num = 0;
   TFTP_FSM_Session.retries = 0;
   TFTP_FSM_Session.max_retries = cfg->max_retransmits;
   TFTP_FSM_Session.timeout_sec = cfg->timeout_sec;

   // Determine transfer mode
   if ( strcasecmp(mode, "netascii") == 0 )
      TFTP_FSM_Session.transfer_mode = TFTP_MODE_NETASCII;
   else
      TFTP_FSM_Session.transfer_mode = TFTP_MODE_OCTET;
   TFTP_FSM_Session.netascii_pending_cr = false;

   // --- Fault simulation: complete session-level faults ---

   // Total timeout: no response at all
   if ( (opcode == TFTP_OP_RRQ && fault->mode == FAULT_RRQ_TIMEOUT) ||
        (opcode == TFTP_OP_WRQ && fault->mode == FAULT_WRQ_TIMEOUT) )
   {
      tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Simulating timeout (no response)" );
      return TFTP_FSM_RC_FINE;
   }

   // Fake error responses
   if ( fault->mode == FAULT_FILE_NOT_FOUND && opcode == TFTP_OP_RRQ )
   {
      tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Simulating file-not-found" );
      struct sockaddr_in bound = {0};
      int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
      if ( sfd >= 0 )
      {
         uint8_t errbuf[128];
         size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                              TFTP_ERRC_FILE_NOT_FOUND, "File not found");
         if ( errsz > 0 )
            (void)sendto(sfd, errbuf, errsz, 0,
                         (const struct sockaddr *)peer_addr, sizeof *peer_addr);
         (void)close(sfd);
      }
      return TFTP_FSM_RC_FINE;
   }

   if ( (fault->mode == FAULT_PERM_DENIED_READ && opcode == TFTP_OP_RRQ) ||
        (fault->mode == FAULT_PERM_DENIED_WRITE && opcode == TFTP_OP_WRQ) )
   {
      tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Simulating access violation" );
      struct sockaddr_in bound = {0};
      int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
      if ( sfd >= 0 )
      {
         uint8_t errbuf[128];
         size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                              TFTP_ERRC_ACCESS_VIOLATION, "Access denied");
         if ( errsz > 0 )
            (void)sendto(sfd, errbuf, errsz, 0,
                         (const struct sockaddr *)peer_addr, sizeof *peer_addr);
         (void)close(sfd);
      }
      return TFTP_FSM_RC_FINE;
   }

   // Open the file
   if ( opcode == TFTP_OP_RRQ )
   {
      TFTP_FSM_Session.fp = fopen(filename, "rb");
      if ( TFTP_FSM_Session.fp == NULL )
      {
         tftp_log( TFTP_LOG_WARN, "FSM: Cannot open '%s': %s", filename, strerror(errno) );
         // Send file-not-found error via temp socket
         struct sockaddr_in bound = {0};
         int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
         if ( sfd >= 0 )
         {
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 TFTP_ERRC_FILE_NOT_FOUND,
                                                 "File not found");
            if ( errsz > 0 )
               (void)sendto(sfd, errbuf, errsz, 0,
                            (const struct sockaddr *)peer_addr, sizeof *peer_addr);
            (void)close(sfd);
         }
         return TFTP_FSM_RC_FILE_ERR;
      }
   }
   else
   {
      // WRQ DoS protection: wrq_enabled check
      if ( !cfg->wrq_enabled )
      {
         tftp_log( TFTP_LOG_WARN, "FSM: WRQ disabled by config, rejecting" );
         struct sockaddr_in bound = {0};
         int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
         if ( sfd >= 0 )
         {
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 TFTP_ERRC_ACCESS_VIOLATION, "WRQ disabled");
            if ( errsz > 0 )
               (void)sendto(sfd, errbuf, errsz, 0,
                            (const struct sockaddr *)peer_addr, sizeof *peer_addr);
            (void)close(sfd);
         }
         if ( wrq_bytes_written != NULL )
            *wrq_bytes_written = 0;
         return TFTP_FSM_RC_WRQ_DISABLED;
      }

      // WRQ DoS protection: pre-flight disk space check
      if ( cfg->min_disk_free_bytes > 0 )
      {
         struct statvfs sv;
         if ( statvfs(".", &sv) == 0 )
         {
            size_t free_bytes = sv.f_bavail * sv.f_frsize;
            if ( free_bytes < cfg->min_disk_free_bytes )
            {
               tftp_log( TFTP_LOG_WARN, "FSM: Insufficient disk space (%zu < %zu), rejecting WRQ",
                         free_bytes, cfg->min_disk_free_bytes );
               struct sockaddr_in bound = {0};
               int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
               if ( sfd >= 0 )
               {
                  uint8_t errbuf[128];
                  size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                       TFTP_ERRC_DISK_FULL, "Insufficient disk space");
                  if ( errsz > 0 )
                     (void)sendto(sfd, errbuf, errsz, 0,
                                  (const struct sockaddr *)peer_addr, sizeof *peer_addr);
                  (void)close(sfd);
               }
               if ( wrq_bytes_written != NULL )
                  *wrq_bytes_written = 0;
               return TFTP_FSM_RC_WRQ_DISK_CHECK;
            }
         }
      }

      // WRQ: open file for writing (overwrites existing per RFC 1350)
      TFTP_FSM_Session.fp = fopen(filename, "wb");
      if ( TFTP_FSM_Session.fp == NULL )
      {
         tftp_log( TFTP_LOG_WARN, "FSM: Cannot create '%s': %s", filename, strerror(errno) );
         struct sockaddr_in bound = {0};
         int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
         if ( sfd >= 0 )
         {
            uint16_t ecode = (errno == EACCES) ? TFTP_ERRC_ACCESS_VIOLATION
                                               : TFTP_ERRC_NOT_DEFINED;
            uint8_t errbuf[128];
            size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf,
                                                 ecode, "Cannot create file");
            if ( errsz > 0 )
               (void)sendto(sfd, errbuf, errsz, 0,
                            (const struct sockaddr *)peer_addr, sizeof *peer_addr);
            (void)close(sfd);
         }
         return TFTP_FSM_RC_FILE_ERR;
      }

      // WRQ: record start time, session budget, and filename for limit checks
      clock_gettime(CLOCK_MONOTONIC, &TFTP_FSM_Session.wrq_start_time);
      TFTP_FSM_Session.wrq_session_budget = wrq_session_budget;
      TFTP_FSM_Session.wrq_bytes_written = 0;
      (void)strncpy(TFTP_FSM_Session.wrq_filename, filename,
                     sizeof TFTP_FSM_Session.wrq_filename - 1);
      TFTP_FSM_Session.wrq_filename[sizeof TFTP_FSM_Session.wrq_filename - 1] = '\0';
   }

   // Create ephemeral UDP socket (new TID per RFC 1350)
   struct sockaddr_in bound_addr = {0};
   TFTP_FSM_Session.sfd = tftp_util_create_ephemeral_udp_socket(&bound_addr);
   if ( TFTP_FSM_Session.sfd < 0 )
   {
      tftp_log( TFTP_LOG_ERR, "FSM: Failed to create ephemeral socket: %s",
                strerror(errno) );
      fclose(TFTP_FSM_Session.fp);
      TFTP_FSM_Session.fp = NULL;
      return TFTP_FSM_RC_SOCKET_CREATION_ERR;
   }

   // Set receive timeout
   if ( tftp_util_set_recv_timeout(TFTP_FSM_Session.sfd,
                                    TFTP_FSM_Session.timeout_sec) != 0 )
   {
      tftp_log( TFTP_LOG_ERR, "FSM: Failed to set recv timeout: %s",
                strerror(errno) );
      rc = TFTP_FSM_RC_SETSOCKOPT_ERR;
      goto fsm_cleanup;
   }

   tftp_log( TFTP_LOG_DEBUG, "FSM: Ephemeral socket on port %d",
             ntohs(bound_addr.sin_port) );

   // --- FSM loop ---
   if ( opcode == TFTP_OP_RRQ )
   {
      TFTP_FSM_Session.state = TFTP_FSM_RRQ_DATA;
   }
   else
   {
      // WRQ: send ACK block 0, then wait for DATA
      TFTP_FSM_Session.block_num = 0;
      TFTP_FSM_Session.sendbuf_len = tftp_pkt_build_ack(
         TFTP_FSM_Session.sendbuf, sizeof TFTP_FSM_Session.sendbuf, 0);
      assert( TFTP_FSM_Session.sendbuf_len > 0 );

      ssize_t sent = sendto(TFTP_FSM_Session.sfd,
                             TFTP_FSM_Session.sendbuf,
                             TFTP_FSM_Session.sendbuf_len, 0,
                             (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                             sizeof TFTP_FSM_Session.peer_addr);
      if ( sent < 0 )
      {
         tftp_log( TFTP_LOG_ERR, "FSM: sendto ACK 0 failed: %s", strerror(errno) );
         rc = TFTP_FSM_RC_SENDTO_ERR;
         goto fsm_cleanup;
      }
      tftp_log( TFTP_LOG_DEBUG, "FSM: Sent ACK block 0 for WRQ" );
      TFTP_FSM_Session.state = TFTP_FSM_WRQ_DATA;
   }

   // EAGAIN and EWOULDBLOCK may be the same value on Linux; suppress -Wlogical-op
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlogical-op"
#endif
   while ( TFTP_FSM_Session.state != TFTP_FSM_IDLE &&
           TFTP_FSM_Session.state != TFTP_FSM_ERR )
   {
      switch ( TFTP_FSM_Session.state )
      {
      case TFTP_FSM_RRQ_DATA:
      {
         uint8_t payload[TFTP_BLOCK_DATA_SZ];
         size_t payload_len = 0;
         bool is_last = false;

         if ( TFTP_FSM_Session.transfer_mode == TFTP_MODE_OCTET )
         {
            // Octet: read directly into payload
            payload_len = fread(payload, 1, sizeof payload, TFTP_FSM_Session.fp);
            if ( ferror(TFTP_FSM_Session.fp) )
            {
               tftp_log( TFTP_LOG_ERR, "FSM: File read error: %s", strerror(errno) );
               send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                             TFTP_ERRC_NOT_DEFINED, "File read error");
               rc = TFTP_FSM_RC_FILE_ERR;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }
            is_last = (payload_len < TFTP_BLOCK_DATA_SZ);
         }
         else
         {
            // Netascii: read raw bytes and translate, filling up to 512
            // Translation can expand (e.g. \n → \r\n), so read in small
            // chunks until payload is full or EOF.
            while ( payload_len < TFTP_BLOCK_DATA_SZ )
            {
               uint8_t raw[TFTP_BLOCK_DATA_SZ];
               size_t want = TFTP_BLOCK_DATA_SZ - payload_len;
               // In worst case, each byte expands to 2, so read at most
               // half the remaining space to avoid overflow
               if ( want > TFTP_BLOCK_DATA_SZ / 2 )
                  want = TFTP_BLOCK_DATA_SZ / 2;
               if ( want == 0 )
                  break;

               size_t nread = fread(raw, 1, want, TFTP_FSM_Session.fp);
               if ( ferror(TFTP_FSM_Session.fp) )
               {
                  tftp_log( TFTP_LOG_ERR, "FSM: File read error: %s", strerror(errno) );
                  send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                                TFTP_ERRC_NOT_DEFINED, "File read error");
                  rc = TFTP_FSM_RC_FILE_ERR;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
                  break;
               }

               if ( nread == 0 )
               {
                  // EOF reached
                  is_last = true;
                  break;
               }

               size_t translated = tftp_util_octet_to_netascii(
                  raw, nread,
                  payload + payload_len, TFTP_BLOCK_DATA_SZ - payload_len,
                  &TFTP_FSM_Session.netascii_pending_cr);
               payload_len += translated;
            }

            // If we broke out due to error, don't continue
            if ( TFTP_FSM_Session.state == TFTP_FSM_ERR )
               break;

            // If EOF and buffer not full, it's the last block
            if ( feof(TFTP_FSM_Session.fp) )
               is_last = true;
         }

         TFTP_FSM_Session.block_num++;
         TFTP_FSM_Session.retries = 0;

         // Build DATA packet
         TFTP_FSM_Session.sendbuf_len = tftp_pkt_build_data(
            TFTP_FSM_Session.sendbuf, sizeof TFTP_FSM_Session.sendbuf,
            TFTP_FSM_Session.block_num, payload, payload_len);
         assert( TFTP_FSM_Session.sendbuf_len > 0 );

         // Fault: OOO DATA — swap adjacent blocks
         // If we're at the stash-pending block and have a stashed packet, send current first, then stashed
         if ( TFTP_FSM_Session.ooo_pending )
         {
            // Send the current block (N+1) first
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: OOO sending block %u before stashed block %u",
                      TFTP_FSM_Session.block_num, TFTP_FSM_Session.ooo_stashed_block );
            (void)sendto(TFTP_FSM_Session.sfd,
                         TFTP_FSM_Session.sendbuf, TFTP_FSM_Session.sendbuf_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            // Then send the stashed block (N)
            (void)sendto(TFTP_FSM_Session.sfd,
                         TFTP_FSM_Session.ooo_stashed_pkt, TFTP_FSM_Session.ooo_stashed_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            TFTP_FSM_Session.ooo_pending = false;

            if ( is_last )
               TFTP_FSM_Session.state = TFTP_FSM_RRQ_FIN_DATA;
            else
               TFTP_FSM_Session.state = TFTP_FSM_RRQ_ACK;
            break;
         }

         if ( fault->mode == FAULT_OOO_DATA && !is_last )
         {
            uint16_t target = (fault->param > 0) ? (uint16_t)fault->param : 3;
            if ( TFTP_FSM_Session.block_num == target )
            {
               // Stash this packet, read next block on next iteration
               tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Stashing DATA block %u for OOO swap",
                         TFTP_FSM_Session.block_num );
               memcpy(TFTP_FSM_Session.ooo_stashed_pkt, TFTP_FSM_Session.sendbuf,
                      TFTP_FSM_Session.sendbuf_len);
               TFTP_FSM_Session.ooo_stashed_len = TFTP_FSM_Session.sendbuf_len;
               TFTP_FSM_Session.ooo_stashed_block = TFTP_FSM_Session.block_num;
               TFTP_FSM_Session.ooo_pending = true;
               // Go back to RRQ_DATA to read and send next block
               TFTP_FSM_Session.state = TFTP_FSM_RRQ_DATA;
               break;
            }
         }

         // Fault: suppress DATA send?
         if ( fault_should_suppress_data(fault, TFTP_FSM_Session.block_num, is_last) )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Suppressing DATA block %u",
                      TFTP_FSM_Session.block_num );
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
            break;
         }

         // Fault: send ERROR instead of DATA?
         if ( fault->mode == FAULT_SEND_ERROR_READ &&
              TFTP_FSM_Session.block_num > 1 )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Sending ERROR %u instead of DATA",
                      fault->param );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          (uint16_t)fault->param, "Injected error");
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
            break;
         }

         // Fault: modify outgoing DATA (invalid block#, opcode, size)
         fault_modify_outgoing(fault, TFTP_FSM_Session.sendbuf,
                               &TFTP_FSM_Session.sendbuf_len,
                               sizeof TFTP_FSM_Session.sendbuf,
                               true, TFTP_FSM_Session.block_num);

         // Fault: delayed response?
         fault_maybe_delay(fault);

         // Fault: wrong TID? Send from a different socket
         int send_sfd = fault_maybe_wrong_tid(fault, true);
         if ( send_sfd < 0 )
            send_sfd = TFTP_FSM_Session.sfd;

         // Send DATA
         ssize_t sent = sendto(send_sfd,
                               TFTP_FSM_Session.sendbuf,
                               TFTP_FSM_Session.sendbuf_len, 0,
                               (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                               sizeof TFTP_FSM_Session.peer_addr);

         if ( send_sfd != TFTP_FSM_Session.sfd )
            (void)close(send_sfd);

         if ( sent < 0 )
         {
            tftp_log( TFTP_LOG_ERR, "FSM: sendto failed: %s", strerror(errno) );
            rc = TFTP_FSM_RC_SENDTO_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Fault: duplicate DATA?
         if ( fault_should_duplicate(fault, true, TFTP_FSM_Session.block_num, is_last) )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Duplicating DATA block %u",
                      TFTP_FSM_Session.block_num );
            (void)sendto(TFTP_FSM_Session.sfd,
                         TFTP_FSM_Session.sendbuf,
                         TFTP_FSM_Session.sendbuf_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
         }

         tftp_log( TFTP_LOG_DEBUG, "FSM: Sent DATA block %u (%zu bytes)",
                   TFTP_FSM_Session.block_num, payload_len );

         // Fault: burst — send additional DATA packets without waiting for ACK
         if ( fault->mode == FAULT_BURST_DATA && !is_last &&
              TFTP_FSM_Session.block_num == 1 )
         {
            uint32_t burst_count = (fault->param > 0) ? fault->param : 3;
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Burst-sending %u additional DATA packets",
                      burst_count );
            for ( uint32_t b = 0; b < burst_count && !is_last; b++ )
            {
               uint8_t burst_payload[TFTP_BLOCK_DATA_SZ];
               size_t burst_len = fread(burst_payload, 1, sizeof burst_payload,
                                        TFTP_FSM_Session.fp);
               if ( ferror(TFTP_FSM_Session.fp) )
                  break;
               bool burst_last = (burst_len < TFTP_BLOCK_DATA_SZ);

               TFTP_FSM_Session.block_num++;
               uint8_t burst_pkt[TFTP_DATA_MAX_SZ];
               size_t burst_pkt_len = tftp_pkt_build_data(
                  burst_pkt, sizeof burst_pkt,
                  TFTP_FSM_Session.block_num, burst_payload, burst_len);

               (void)sendto(TFTP_FSM_Session.sfd,
                            burst_pkt, burst_pkt_len, 0,
                            (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                            sizeof TFTP_FSM_Session.peer_addr);
               tftp_log( TFTP_LOG_DEBUG, "FSM: FAULT: Burst DATA block %u (%zu bytes)",
                         TFTP_FSM_Session.block_num, burst_len );

               // Keep last burst packet in sendbuf for retransmit
               memcpy(TFTP_FSM_Session.sendbuf, burst_pkt, burst_pkt_len);
               TFTP_FSM_Session.sendbuf_len = burst_pkt_len;

               is_last = burst_last;
            }
         }

         if ( is_last )
            TFTP_FSM_Session.state = TFTP_FSM_RRQ_FIN_DATA;
         else
            TFTP_FSM_Session.state = TFTP_FSM_RRQ_ACK;

         break;
      }

      case TFTP_FSM_RRQ_ACK:
      case TFTP_FSM_RRQ_FIN_DATA:
      {
         // Wait for ACK
         uint8_t ackbuf[TFTP_ACK_SZ + 16]; // small extra for safety
         struct sockaddr_in recv_addr = {0};
         socklen_t addrlen = sizeof recv_addr;

         ssize_t nbytes = recvfrom(TFTP_FSM_Session.sfd,
                                    ackbuf, sizeof ackbuf, 0,
                                    (struct sockaddr *)&recv_addr, &addrlen);

         if ( nbytes < 0 )
         {
         if ( errno == EAGAIN || errno == EWOULDBLOCK )
            {
               // Timeout -- retransmit
               TFTP_FSM_Session.retries++;
               if ( TFTP_FSM_Session.retries > TFTP_FSM_Session.max_retries )
               {
                  tftp_log( TFTP_LOG_WARN,
                            "FSM: Max retransmits (%u) exceeded for block %u",
                            TFTP_FSM_Session.max_retries,
                            TFTP_FSM_Session.block_num );
                  rc = TFTP_FSM_RC_TIMEOUT;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
                  break;
               }

               tftp_log( TFTP_LOG_DEBUG, "FSM: Timeout, retransmitting block %u (attempt %u)",
                         TFTP_FSM_Session.block_num, TFTP_FSM_Session.retries );

               ssize_t sent = sendto(TFTP_FSM_Session.sfd,
                                      TFTP_FSM_Session.sendbuf,
                                      TFTP_FSM_Session.sendbuf_len, 0,
                                      (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                                      sizeof TFTP_FSM_Session.peer_addr);
               if ( sent < 0 )
               {
                  tftp_log( TFTP_LOG_ERR, "FSM: Retransmit sendto failed: %s",
                            strerror(errno) );
                  rc = TFTP_FSM_RC_SENDTO_ERR;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
               }
               // Stay in same state to wait for ACK again
               break;
            }
            else
            {
               tftp_log( TFTP_LOG_ERR, "FSM: recvfrom failed: %s", strerror(errno) );
               rc = TFTP_FSM_RC_RECVFROM_ERR;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }
         }

         // TID validation: source must match peer
         if ( !tid_matches(&recv_addr) )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: Packet from wrong TID, sending ERROR 5" );
            send_error_to(TFTP_FSM_Session.sfd, &recv_addr,
                          TFTP_ERRC_UNKNOWN_TID, "Unknown transfer ID");
            // Stay in same state, keep waiting for correct peer
            break;
         }

         // Parse ACK
         uint16_t ack_block = 0;
         if ( tftp_pkt_parse_ack(ackbuf, (size_t)nbytes, &ack_block) != 0 )
         {
            // Check if client sent an ERROR
            uint16_t err_code = 0;
            const char *err_msg = NULL;
            if ( tftp_pkt_parse_error(ackbuf, (size_t)nbytes, &err_code, &err_msg) == 0 )
            {
               tftp_log( TFTP_LOG_WARN, "FSM: Client sent ERROR %u: %s",
                         err_code, err_msg );
               rc = TFTP_FSM_RC_PROTOCOL_ERR;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }

            tftp_log( TFTP_LOG_WARN, "FSM: Expected ACK, got unexpected packet" );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_ILLEGAL_OP, "Expected ACK");
            rc = TFTP_FSM_RC_PROTOCOL_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Check block number (uint16_t arithmetic handles wrap at 65535 -> 0)
         if ( ack_block != TFTP_FSM_Session.block_num )
         {
            // Duplicate ACK for the previous block -- ignore and keep waiting.
            // Use uint16_t subtraction so the comparison is correct across wrap.
            if ( ack_block == (uint16_t)(TFTP_FSM_Session.block_num - 1) )
            {
               tftp_log( TFTP_LOG_DEBUG, "FSM: Duplicate ACK for block %u (expected %u), ignoring",
                         ack_block, TFTP_FSM_Session.block_num );
               break; // Stay in same state
            }

            // ACK for an unexpected block -- protocol error
            tftp_log( TFTP_LOG_WARN, "FSM: ACK for block %u but expected %u",
                      ack_block, TFTP_FSM_Session.block_num );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_ILLEGAL_OP, "Unexpected block number");
            rc = TFTP_FSM_RC_PROTOCOL_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         tftp_log( TFTP_LOG_DEBUG, "FSM: Received ACK for block %u", ack_block );

         // If we were in FIN_DATA state, this was the final ACK -- done
         if ( TFTP_FSM_Session.state == TFTP_FSM_RRQ_FIN_DATA )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: Transfer complete" );
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
         }
         else
         {
            // Move to next DATA block
            TFTP_FSM_Session.state = TFTP_FSM_RRQ_DATA;
         }

         break;
      }

      case TFTP_FSM_WRQ_DATA:
      {
         // WRQ DoS: duration check
         if ( cfg->max_wrq_duration_sec > 0 )
         {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            unsigned int elapsed = (unsigned int)(now.tv_sec - TFTP_FSM_Session.wrq_start_time.tv_sec);
            if ( elapsed >= cfg->max_wrq_duration_sec )
            {
               tftp_log( TFTP_LOG_WARN, "FSM: WRQ duration limit exceeded (%u >= %u sec)",
                         elapsed, cfg->max_wrq_duration_sec );
               send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                             TFTP_ERRC_DISK_FULL, "Transfer duration limit exceeded");
               // Close and delete file
               fclose(TFTP_FSM_Session.fp);
               TFTP_FSM_Session.fp = NULL;
               (void)unlink(TFTP_FSM_Session.wrq_filename);
               rc = TFTP_FSM_RC_WRQ_DURATION_LIMIT;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }
         }

         // Wait for DATA from client
         uint8_t databuf[TFTP_DATA_MAX_SZ + 16];
         struct sockaddr_in recv_addr = {0};
         socklen_t addrlen = sizeof recv_addr;

         ssize_t nbytes = recvfrom(TFTP_FSM_Session.sfd,
                                    databuf, sizeof databuf, 0,
                                    (struct sockaddr *)&recv_addr, &addrlen);

         if ( nbytes < 0 )
         {
            if ( errno == EAGAIN || errno == EWOULDBLOCK )
            {
               // Timeout -- retransmit last ACK
               TFTP_FSM_Session.retries++;
               if ( TFTP_FSM_Session.retries > TFTP_FSM_Session.max_retries )
               {
                  tftp_log( TFTP_LOG_WARN,
                            "FSM: Max retransmits (%u) exceeded waiting for DATA block %u",
                            TFTP_FSM_Session.max_retries,
                            (unsigned)(TFTP_FSM_Session.block_num + 1) );
                  rc = TFTP_FSM_RC_TIMEOUT;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
                  break;
               }

               tftp_log( TFTP_LOG_DEBUG, "FSM: Timeout, retransmitting ACK %u (attempt %u)",
                         TFTP_FSM_Session.block_num, TFTP_FSM_Session.retries );

               ssize_t sent = sendto(TFTP_FSM_Session.sfd,
                                      TFTP_FSM_Session.sendbuf,
                                      TFTP_FSM_Session.sendbuf_len, 0,
                                      (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                                      sizeof TFTP_FSM_Session.peer_addr);
               if ( sent < 0 )
               {
                  tftp_log( TFTP_LOG_ERR, "FSM: Retransmit sendto failed: %s",
                            strerror(errno) );
                  rc = TFTP_FSM_RC_SENDTO_ERR;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
               }
               break;
            }
            else
            {
               tftp_log( TFTP_LOG_ERR, "FSM: recvfrom failed: %s", strerror(errno) );
               rc = TFTP_FSM_RC_RECVFROM_ERR;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }
         }

         // TID validation
         if ( !tid_matches(&recv_addr) )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: Packet from wrong TID, sending ERROR 5" );
            send_error_to(TFTP_FSM_Session.sfd, &recv_addr,
                          TFTP_ERRC_UNKNOWN_TID, "Unknown transfer ID");
            break;
         }

         // Check if client sent an ERROR
         {
            uint16_t err_code = 0;
            const char *err_msg = NULL;
            if ( tftp_pkt_parse_error(databuf, (size_t)nbytes, &err_code, &err_msg) == 0 )
            {
               tftp_log( TFTP_LOG_WARN, "FSM: Client sent ERROR %u: %s",
                         err_code, err_msg );
               rc = TFTP_FSM_RC_PROTOCOL_ERR;
               TFTP_FSM_Session.state = TFTP_FSM_ERR;
               break;
            }
         }

         // Parse DATA
         uint16_t data_block = 0;
         const uint8_t *data_ptr = NULL;
         size_t data_len = 0;
         if ( tftp_pkt_parse_data(databuf, (size_t)nbytes, &data_block,
                                  &data_ptr, &data_len) != 0 )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: Expected DATA, got unexpected packet" );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_ILLEGAL_OP, "Expected DATA");
            rc = TFTP_FSM_RC_PROTOCOL_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Compute expected next block with uint16_t wrap (65535 -> 0)
         uint16_t expected_block = (uint16_t)(TFTP_FSM_Session.block_num + 1);

         // Check for duplicate DATA (re-send ACK, don't re-write)
         if ( data_block == TFTP_FSM_Session.block_num )
         {
            tftp_log( TFTP_LOG_DEBUG, "FSM: Duplicate DATA block %u, re-ACKing",
                      data_block );
            uint8_t dup_ack[TFTP_ACK_SZ];
            size_t ack_sz = tftp_pkt_build_ack(dup_ack, sizeof dup_ack, data_block);
            (void)sendto(TFTP_FSM_Session.sfd, dup_ack, ack_sz, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            break;
         }

         // Expect next sequential block (with uint16_t wrap)
         if ( data_block != expected_block )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: DATA block %u but expected %u",
                      data_block, (unsigned)expected_block );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_ILLEGAL_OP, "Unexpected block number");
            rc = TFTP_FSM_RC_PROTOCOL_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Write data to file
         if ( data_len > 0 )
         {
            if ( TFTP_FSM_Session.transfer_mode == TFTP_MODE_NETASCII )
            {
               // Reverse netascii translation before writing
               uint8_t raw[TFTP_BLOCK_DATA_SZ];
               size_t raw_len = tftp_util_netascii_to_octet(
                  data_ptr, data_len, raw, sizeof raw,
                  &TFTP_FSM_Session.netascii_pending_cr);

               if ( fwrite(raw, 1, raw_len, TFTP_FSM_Session.fp) != raw_len )
               {
                  tftp_log( TFTP_LOG_ERR, "FSM: File write error: %s", strerror(errno) );
                  send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                                TFTP_ERRC_DISK_FULL, "Write error");
                  rc = TFTP_FSM_RC_FILE_ERR;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
                  break;
               }
            }
            else
            {
               if ( fwrite(data_ptr, 1, data_len, TFTP_FSM_Session.fp) != data_len )
               {
                  tftp_log( TFTP_LOG_ERR, "FSM: File write error: %s", strerror(errno) );
                  send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                                TFTP_ERRC_DISK_FULL, "Write error");
                  rc = TFTP_FSM_RC_FILE_ERR;
                  TFTP_FSM_Session.state = TFTP_FSM_ERR;
                  break;
               }
            }
         }

         // Track WRQ bytes written
         TFTP_FSM_Session.wrq_bytes_written += data_len;

         // WRQ DoS: per-file size limit check
         if ( cfg->max_wrq_file_size > 0 &&
              TFTP_FSM_Session.wrq_bytes_written > cfg->max_wrq_file_size )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: Per-file WRQ size limit exceeded (%zu > %zu)",
                      TFTP_FSM_Session.wrq_bytes_written, cfg->max_wrq_file_size );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_DISK_FULL, "Upload limit exceeded");
            // Close and delete file
            fclose(TFTP_FSM_Session.fp);
            TFTP_FSM_Session.fp = NULL;
            (void)unlink(TFTP_FSM_Session.wrq_filename);
            rc = TFTP_FSM_RC_WRQ_FILE_LIMIT;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // WRQ DoS: session budget check
         if ( TFTP_FSM_Session.wrq_session_budget > 0 &&
              TFTP_FSM_Session.wrq_bytes_written > TFTP_FSM_Session.wrq_session_budget )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: WRQ session byte budget exceeded (%zu > %zu)",
                      TFTP_FSM_Session.wrq_bytes_written, TFTP_FSM_Session.wrq_session_budget );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_DISK_FULL, "Upload limit exceeded");
            // Keep partial file (per spec)
            rc = TFTP_FSM_RC_WRQ_SESSION_LIMIT;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         TFTP_FSM_Session.block_num = data_block;
         TFTP_FSM_Session.retries = 0;

         bool wrq_is_last = (data_len < TFTP_BLOCK_DATA_SZ);

         // Fault: suppress ACK?
         if ( fault_should_suppress_ack(fault, TFTP_FSM_Session.block_num, wrq_is_last) )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Suppressing ACK block %u",
                      TFTP_FSM_Session.block_num );
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
            break;
         }

         // Fault: send ERROR instead of ACK?
         if ( fault->mode == FAULT_SEND_ERROR_WRITE &&
              TFTP_FSM_Session.block_num > 0 )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Sending ERROR %u instead of ACK",
                      fault->param );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          (uint16_t)fault->param, "Injected error");
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
            break;
         }

         // Fault: OOO ACK — swap adjacent ACKs
         // If we have a stashed ACK pending, send current ACK(N+1) first, then stashed ACK(N)
         if ( TFTP_FSM_Session.ooo_pending )
         {
            // Build and send ACK(N+1) — the current block
            uint8_t ack_now[TFTP_ACK_SZ];
            size_t ack_now_len = tftp_pkt_build_ack(ack_now, sizeof ack_now,
                                                    TFTP_FSM_Session.block_num);
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: OOO sending ACK %u before stashed ACK %u",
                      TFTP_FSM_Session.block_num, TFTP_FSM_Session.ooo_stashed_block );
            (void)sendto(TFTP_FSM_Session.sfd, ack_now, ack_now_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            // Then send the stashed ACK(N)
            (void)sendto(TFTP_FSM_Session.sfd,
                         TFTP_FSM_Session.ooo_stashed_pkt, TFTP_FSM_Session.ooo_stashed_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            TFTP_FSM_Session.ooo_pending = false;

            // Copy current ACK into sendbuf for retransmit
            memcpy(TFTP_FSM_Session.sendbuf, ack_now, ack_now_len);
            TFTP_FSM_Session.sendbuf_len = ack_now_len;

            if ( wrq_is_last )
            {
               tftp_log( TFTP_LOG_INFO, "FSM: WRQ transfer complete" );
               TFTP_FSM_Session.state = TFTP_FSM_IDLE;
            }
            break;
         }

         if ( fault->mode == FAULT_OOO_ACK && !wrq_is_last )
         {
            uint16_t target = (fault->param > 0) ? (uint16_t)fault->param : 3;
            if ( TFTP_FSM_Session.block_num == target )
            {
               // Stash ACK for this block, wait for next DATA to arrive
               tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Stashing ACK block %u for OOO swap",
                         TFTP_FSM_Session.block_num );
               TFTP_FSM_Session.ooo_stashed_len = tftp_pkt_build_ack(
                  TFTP_FSM_Session.ooo_stashed_pkt, sizeof TFTP_FSM_Session.ooo_stashed_pkt,
                  TFTP_FSM_Session.block_num);
               TFTP_FSM_Session.ooo_stashed_block = TFTP_FSM_Session.block_num;
               TFTP_FSM_Session.ooo_pending = true;
               // Don't send ACK — go back to wait for next DATA
               TFTP_FSM_Session.state = TFTP_FSM_WRQ_DATA;
               break;
            }
         }

         // Send ACK
         TFTP_FSM_Session.sendbuf_len = tftp_pkt_build_ack(
            TFTP_FSM_Session.sendbuf, sizeof TFTP_FSM_Session.sendbuf,
            TFTP_FSM_Session.block_num);
         assert( TFTP_FSM_Session.sendbuf_len > 0 );

         // Fault: modify outgoing ACK (invalid block#, opcode)
         fault_modify_outgoing(fault, TFTP_FSM_Session.sendbuf,
                               &TFTP_FSM_Session.sendbuf_len,
                               sizeof TFTP_FSM_Session.sendbuf,
                               false, TFTP_FSM_Session.block_num);

         // Fault: delayed response?
         fault_maybe_delay(fault);

         // Fault: wrong TID?
         int ack_send_sfd = fault_maybe_wrong_tid(fault, false);
         if ( ack_send_sfd < 0 )
            ack_send_sfd = TFTP_FSM_Session.sfd;

         ssize_t sent = sendto(ack_send_sfd,
                               TFTP_FSM_Session.sendbuf,
                               TFTP_FSM_Session.sendbuf_len, 0,
                               (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                               sizeof TFTP_FSM_Session.peer_addr);

         if ( ack_send_sfd != TFTP_FSM_Session.sfd )
            (void)close(ack_send_sfd);

         if ( sent < 0 )
         {
            tftp_log( TFTP_LOG_ERR, "FSM: sendto ACK failed: %s", strerror(errno) );
            rc = TFTP_FSM_RC_SENDTO_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Fault: duplicate ACK?
         if ( fault_should_duplicate(fault, false, TFTP_FSM_Session.block_num, wrq_is_last) )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Duplicating ACK block %u",
                      TFTP_FSM_Session.block_num );
            (void)sendto(TFTP_FSM_Session.sfd,
                         TFTP_FSM_Session.sendbuf,
                         TFTP_FSM_Session.sendbuf_len, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
         }

         tftp_log( TFTP_LOG_DEBUG, "FSM: Sent ACK block %u", TFTP_FSM_Session.block_num );

         // If data < 512 bytes, this was the last block
         if ( wrq_is_last )
         {
            tftp_log( TFTP_LOG_INFO, "FSM: WRQ transfer complete" );
            TFTP_FSM_Session.state = TFTP_FSM_IDLE;
         }

         break;
      }

      // States that should never be entered in the FSM loop
      case TFTP_FSM_DETERMINE_RQ:
      case TFTP_FSM_RRQ_ERR:
      case TFTP_FSM_WRQ_ACK:
      case TFTP_FSM_WRQ_ERR:
      case TFTP_FSM_WRQ_FIN_ACK:
      case TFTP_FSM_IDLE:
      case TFTP_FSM_ERR:
      case TFTP_FSM_INVALID_STATE:
      default:
         tftp_log( TFTP_LOG_ERR, "FSM: Unexpected state %u", (unsigned)TFTP_FSM_Session.state );
         rc = TFTP_FSM_RC_PROTOCOL_ERR;
         TFTP_FSM_Session.state = TFTP_FSM_ERR;
         break;
      }
   }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

fsm_cleanup:
   if ( wrq_bytes_written != NULL )
      *wrq_bytes_written = TFTP_FSM_Session.wrq_bytes_written;
   session_cleanup();
   return rc;
}

void tftp_fsm_clean_exit(void)
{
   session_cleanup();
}

/*********************** Local Function Implementations ***********************/

static void session_cleanup(void)
{
   if ( TFTP_FSM_Session.fp != NULL )
   {
      (void)fclose(TFTP_FSM_Session.fp);
      TFTP_FSM_Session.fp = NULL;
   }

   if ( TFTP_FSM_Session.sfd >= 0 )
   {
      (void)close(TFTP_FSM_Session.sfd);
      TFTP_FSM_Session.sfd = -1;
   }

   TFTP_FSM_Session.state = TFTP_FSM_IDLE;
   TFTP_FSM_Session.block_num = 0;
   TFTP_FSM_Session.retries = 0;
   TFTP_FSM_Session.sendbuf_len = 0;
   TFTP_FSM_Session.ooo_pending = false;
   TFTP_FSM_Session.ooo_stashed_len = 0;
}

static bool tid_matches(const struct sockaddr_in *incoming)
{
   return incoming->sin_addr.s_addr == TFTP_FSM_Session.peer_addr.sin_addr.s_addr &&
          incoming->sin_port == TFTP_FSM_Session.peer_addr.sin_port;
}

static enum TFTP_FSM_RC send_error_to(int sfd, const struct sockaddr_in *dest,
                                       uint16_t error_code, const char *msg)
{
   uint8_t errbuf[128];
   size_t errsz = tftp_pkt_build_error(errbuf, sizeof errbuf, error_code, msg);
   if ( errsz == 0 )
      return TFTP_FSM_RC_SENDTO_ERR;

   ssize_t sent = sendto(sfd, errbuf, errsz, 0,
                          (const struct sockaddr *)dest, sizeof *dest);
   if ( sent < 0 )
   {
      tftp_log( TFTP_LOG_ERR, "FSM: Failed to send ERROR: %s", strerror(errno) );
      return TFTP_FSM_RC_SENDTO_ERR;
   }

   return TFTP_FSM_RC_FINE;
}

// These helpers handle a small subset of the large fault enum; suppress -Wswitch-enum
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"

static bool fault_should_suppress_data(const struct TFTPTest_FaultState *fault,
                                        uint16_t block_num, bool is_last)
{
   switch ( fault->mode )
   {
   case FAULT_MID_TIMEOUT_NO_DATA:
      // Suppress DATA at param block (default block 3 if param is 0)
      return block_num == (fault->param > 0 ? (uint16_t)fault->param : 3);

   case FAULT_MID_TIMEOUT_NO_FINAL_DATA:
      return is_last;

   case FAULT_SKIP_DATA:
      return block_num == (uint16_t)fault->param;

   default:
      return false;
   }
}

static bool fault_should_suppress_ack(const struct TFTPTest_FaultState *fault,
                                       uint16_t block_num, bool is_last)
{
   switch ( fault->mode )
   {
   case FAULT_MID_TIMEOUT_NO_ACK:
      return block_num == (fault->param > 0 ? (uint16_t)fault->param : 3);

   case FAULT_MID_TIMEOUT_NO_FINAL_ACK:
      return is_last;

   case FAULT_SKIP_ACK:
      return block_num == (uint16_t)fault->param;

   default:
      return false;
   }
}

static bool fault_should_duplicate(const struct TFTPTest_FaultState *fault,
                                    bool is_data, uint16_t block_num, bool is_last)
{
   switch ( fault->mode )
   {
   case FAULT_DUP_FINAL_DATA:
      return is_data && is_last;
   case FAULT_DUP_FINAL_ACK:
      return !is_data && is_last;
   case FAULT_DUP_MID_DATA:
      return is_data && block_num == (uint16_t)fault->param;
   case FAULT_DUP_MID_ACK:
      return !is_data && block_num == (uint16_t)fault->param;
   default:
      return false;
   }
}

static void fault_modify_outgoing(const struct TFTPTest_FaultState *fault,
                                   uint8_t *pkt, size_t *pkt_len, size_t pkt_cap,
                                   bool is_data, uint16_t block_num)
{
   (void)block_num;

   switch ( fault->mode )
   {
   case FAULT_INVALID_BLOCK_DATA:
      if ( is_data && *pkt_len >= 4 )
      {
         pkt[2] = (uint8_t)((fault->param >> 8) & 0xFF);
         pkt[3] = (uint8_t)(fault->param & 0xFF);
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Set DATA block# to %u", fault->param );
      }
      break;

   case FAULT_INVALID_BLOCK_ACK:
      if ( !is_data && *pkt_len >= 4 )
      {
         pkt[2] = (uint8_t)((fault->param >> 8) & 0xFF);
         pkt[3] = (uint8_t)(fault->param & 0xFF);
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Set ACK block# to %u", fault->param );
      }
      break;

   case FAULT_DATA_TOO_LARGE:
      if ( is_data )
      {
         size_t target = TFTP_DATA_HDR_SZ + TFTP_BLOCK_DATA_SZ + 8;
         if ( target <= pkt_cap )
         {
            if ( *pkt_len < target )
               memset( pkt + *pkt_len, 0, target - *pkt_len );
            *pkt_len = target;
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Padded DATA to %zu bytes", *pkt_len );
         }
      }
      break;

   case FAULT_DATA_LEN_MISMATCH:
      if ( is_data && *pkt_len > TFTP_DATA_HDR_SZ + 1 )
      {
         size_t payload = *pkt_len - TFTP_DATA_HDR_SZ;
         *pkt_len = TFTP_DATA_HDR_SZ + payload / 2;
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Truncated DATA to %zu bytes", *pkt_len );
      }
      break;

   case FAULT_INVALID_OPCODE_READ:
   case FAULT_INVALID_OPCODE_WRITE:
      if ( *pkt_len >= 2 )
      {
         pkt[0] = 0;
         pkt[1] = 9;
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Set opcode to 9 (invalid)" );
      }
      break;

   case FAULT_INVALID_ERR_CODE_READ:
   case FAULT_INVALID_ERR_CODE_WRITE:
   {
      uint16_t bad_code = (uint16_t)fault->param;
      if ( bad_code <= 7 ) bad_code = 99;
      size_t esz = tftp_pkt_build_error(pkt, pkt_cap, bad_code, "Injected bad error");
      if ( esz > 0 )
      {
         *pkt_len = esz;
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Sent ERROR with code %u", bad_code );
      }
      break;
   }

   case FAULT_CORRUPT_DATA:
      if ( is_data && *pkt_len > TFTP_DATA_HDR_SZ )
      {
         uint16_t target_block = (fault->param > 0) ? (uint16_t)fault->param : 3;
         if ( block_num == target_block )
         {
            // XOR first 4 payload bytes (or fewer if payload is shorter)
            size_t payload_start = TFTP_DATA_HDR_SZ;
            size_t corrupt_len = *pkt_len - payload_start;
            if ( corrupt_len > 4 ) corrupt_len = 4;
            for ( size_t i = 0; i < corrupt_len; i++ )
               pkt[payload_start + i] ^= 0xFF;
            tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Corrupted DATA block %u payload (%zu bytes)",
                      block_num, corrupt_len );
         }
      }
      break;

   case FAULT_TRUNCATED_PKT:
      if ( *pkt_len > 2 )
      {
         *pkt_len = 2;  // opcode only, no block# or payload
         tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Truncated packet to 2 bytes" );
      }
      break;

   default:
      break;
   }
}

#pragma GCC diagnostic pop

static int fault_maybe_wrong_tid(const struct TFTPTest_FaultState *fault,
                                  bool is_rrq)
{
   bool should = (is_rrq && fault->mode == FAULT_WRONG_TID_READ) ||
                 (!is_rrq && fault->mode == FAULT_WRONG_TID_WRITE);

   if ( !should )
      return -1;

   int sfd = tftp_util_create_ephemeral_udp_socket(NULL);
   if ( sfd >= 0 )
      tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Sending from wrong TID" );
   return sfd;
}

static void fault_maybe_delay(const struct TFTPTest_FaultState *fault)
{
   if ( fault->mode != FAULT_SLOW_RESPONSE )
      return;

   uint32_t delay_ms = fault->param > 0 ? fault->param : 4000;
   struct timespec ts = {
      .tv_sec  = delay_ms / 1000,
      .tv_nsec = (long)(delay_ms % 1000) * 1000000L,
   };

   tftp_log( TFTP_LOG_INFO, "FSM: FAULT: Delaying response by %u ms", delay_ms );
   (void)nanosleep(&ts, NULL);
}
