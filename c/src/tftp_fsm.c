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

/********************** Public Function Implementations ***********************/

enum TFTP_FSM_RC TFTP_FSM_KickOff(const uint8_t *rqbuf, size_t rqsz,
                                    const struct sockaddr_in *peer_addr,
                                    const struct TFTPTest_Config *cfg,
                                    const struct TFTPTest_FaultState *fault)
{
   assert( rqbuf != nullptr );
   assert( peer_addr != nullptr );
   assert( cfg != nullptr );
   assert( fault != nullptr );

   enum TFTP_FSM_RC rc = TFTP_FSM_RC_FINE;

   // Parse the request
   uint16_t opcode = 0;
   const char *filename = nullptr;
   const char *mode = nullptr;
   if ( TFTP_PKT_ParseRequest(rqbuf, rqsz, &opcode, &filename, &mode) != 0 )
   {
      tftp_log( TFTP_LOG_WARN, "FSM: Failed to parse request packet" );
      return TFTP_FSM_RC_PROTOCOL_ERR;
   }

   tftp_log( TFTP_LOG_INFO, "FSM: %s request for '%s' (mode: %s)",
             opcode == TFTP_OP_RRQ ? "RRQ" : "WRQ", filename, mode );

   // Initialize session
   memset( &TFTP_FSM_Session, 0, sizeof TFTP_FSM_Session );
   TFTP_FSM_Session.sfd = -1;
   TFTP_FSM_Session.fp = nullptr;
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

   // --- Fault injection: complete session-level faults ---

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
         size_t errsz = TFTP_PKT_BuildError(errbuf, sizeof errbuf,
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
         size_t errsz = TFTP_PKT_BuildError(errbuf, sizeof errbuf,
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
      if ( TFTP_FSM_Session.fp == nullptr )
      {
         tftp_log( TFTP_LOG_WARN, "FSM: Cannot open '%s': %s", filename, strerror(errno) );
         // Send file-not-found error via temp socket
         struct sockaddr_in bound = {0};
         int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
         if ( sfd >= 0 )
         {
            uint8_t errbuf[128];
            size_t errsz = TFTP_PKT_BuildError(errbuf, sizeof errbuf,
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
      // WRQ: open file for writing (overwrites existing per RFC 1350)
      TFTP_FSM_Session.fp = fopen(filename, "wb");
      if ( TFTP_FSM_Session.fp == nullptr )
      {
         tftp_log( TFTP_LOG_WARN, "FSM: Cannot create '%s': %s", filename, strerror(errno) );
         struct sockaddr_in bound = {0};
         int sfd = tftp_util_create_ephemeral_udp_socket(&bound);
         if ( sfd >= 0 )
         {
            uint16_t ecode = (errno == EACCES) ? TFTP_ERRC_ACCESS_VIOLATION
                                               : TFTP_ERRC_NOT_DEFINED;
            uint8_t errbuf[128];
            size_t errsz = TFTP_PKT_BuildError(errbuf, sizeof errbuf,
                                                 ecode, "Cannot create file");
            if ( errsz > 0 )
               (void)sendto(sfd, errbuf, errsz, 0,
                            (const struct sockaddr *)peer_addr, sizeof *peer_addr);
            (void)close(sfd);
         }
         return TFTP_FSM_RC_FILE_ERR;
      }
   }

   // Create ephemeral UDP socket (new TID per RFC 1350)
   struct sockaddr_in bound_addr = {0};
   TFTP_FSM_Session.sfd = tftp_util_create_ephemeral_udp_socket(&bound_addr);
   if ( TFTP_FSM_Session.sfd < 0 )
   {
      tftp_log( TFTP_LOG_ERR, "FSM: Failed to create ephemeral socket: %s",
                strerror(errno) );
      fclose(TFTP_FSM_Session.fp);
      TFTP_FSM_Session.fp = nullptr;
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
      TFTP_FSM_Session.sendbuf_len = TFTP_PKT_BuildAck(
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wlogical-op"
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
         TFTP_FSM_Session.sendbuf_len = TFTP_PKT_BuildData(
            TFTP_FSM_Session.sendbuf, sizeof TFTP_FSM_Session.sendbuf,
            TFTP_FSM_Session.block_num, payload, payload_len);
         assert( TFTP_FSM_Session.sendbuf_len > 0 );

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

         // Send DATA
         ssize_t sent = sendto(TFTP_FSM_Session.sfd,
                               TFTP_FSM_Session.sendbuf,
                               TFTP_FSM_Session.sendbuf_len, 0,
                               (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                               sizeof TFTP_FSM_Session.peer_addr);
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
         if ( TFTP_PKT_ParseAck(ackbuf, (size_t)nbytes, &ack_block) != 0 )
         {
            // Check if client sent an ERROR
            uint16_t err_code = 0;
            const char *err_msg = nullptr;
            if ( TFTP_PKT_ParseError(ackbuf, (size_t)nbytes, &err_code, &err_msg) == 0 )
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

         // Check block number
         if ( ack_block != TFTP_FSM_Session.block_num )
         {
            // Duplicate ACK for previous block -- ignore and keep waiting
            if ( ack_block < TFTP_FSM_Session.block_num )
            {
               tftp_log( TFTP_LOG_DEBUG, "FSM: Duplicate ACK for block %u (expected %u), ignoring",
                         ack_block, TFTP_FSM_Session.block_num );
               break; // Stay in same state
            }

            // ACK for future block -- protocol error
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
            const char *err_msg = nullptr;
            if ( TFTP_PKT_ParseError(databuf, (size_t)nbytes, &err_code, &err_msg) == 0 )
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
         const uint8_t *data_ptr = nullptr;
         size_t data_len = 0;
         if ( TFTP_PKT_ParseData(databuf, (size_t)nbytes, &data_block,
                                  &data_ptr, &data_len) != 0 )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: Expected DATA, got unexpected packet" );
            send_error_to(TFTP_FSM_Session.sfd, &TFTP_FSM_Session.peer_addr,
                          TFTP_ERRC_ILLEGAL_OP, "Expected DATA");
            rc = TFTP_FSM_RC_PROTOCOL_ERR;
            TFTP_FSM_Session.state = TFTP_FSM_ERR;
            break;
         }

         // Check for duplicate DATA (re-send ACK, don't re-write)
         if ( data_block <= TFTP_FSM_Session.block_num )
         {
            tftp_log( TFTP_LOG_DEBUG, "FSM: Duplicate DATA block %u, re-ACKing",
                      data_block );
            // Re-send the ACK for that block
            uint8_t dup_ack[TFTP_ACK_SZ];
            size_t ack_sz = TFTP_PKT_BuildAck(dup_ack, sizeof dup_ack, data_block);
            (void)sendto(TFTP_FSM_Session.sfd, dup_ack, ack_sz, 0,
                         (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                         sizeof TFTP_FSM_Session.peer_addr);
            break;
         }

         // Expect next sequential block
         if ( data_block != TFTP_FSM_Session.block_num + 1 )
         {
            tftp_log( TFTP_LOG_WARN, "FSM: DATA block %u but expected %u",
                      data_block, (unsigned)(TFTP_FSM_Session.block_num + 1) );
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

         // Send ACK
         TFTP_FSM_Session.sendbuf_len = TFTP_PKT_BuildAck(
            TFTP_FSM_Session.sendbuf, sizeof TFTP_FSM_Session.sendbuf,
            TFTP_FSM_Session.block_num);
         assert( TFTP_FSM_Session.sendbuf_len > 0 );

         ssize_t sent = sendto(TFTP_FSM_Session.sfd,
                               TFTP_FSM_Session.sendbuf,
                               TFTP_FSM_Session.sendbuf_len, 0,
                               (const struct sockaddr *)&TFTP_FSM_Session.peer_addr,
                               sizeof TFTP_FSM_Session.peer_addr);
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
#pragma GCC diagnostic pop

fsm_cleanup:
   session_cleanup();
   return rc;
}

void TFTP_FSM_CleanExit(void)
{
   session_cleanup();
}

/*********************** Local Function Implementations ***********************/

static void session_cleanup(void)
{
   if ( TFTP_FSM_Session.fp != nullptr )
   {
      (void)fclose(TFTP_FSM_Session.fp);
      TFTP_FSM_Session.fp = nullptr;
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
   size_t errsz = TFTP_PKT_BuildError(errbuf, sizeof errbuf, error_code, msg);
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

#pragma GCC diagnostic pop
