/**
 * @file test_tftptest_ctrl.c
 * @brief Unit tests for tftptest_ctrl module.
 * @date Apr 12, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include "test_common.h"
#include "tftptest_ctrl.h"
#include "tftptest_faultmode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*---------------------------------------------------------------------------
 * Control Channel Tests
 *---------------------------------------------------------------------------*/

// Helper: send a UDP message and receive the reply
static ssize_t ctrl_send_recv(uint16_t port, const char *msg,
                               char *reply, size_t reply_cap)
{
   int sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 ) return -1;

   struct sockaddr_in dest = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };

   // Set recv timeout so test doesn't hang
   struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
   (void)setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

   (void)sendto(sfd, msg, strlen(msg), 0,
                (struct sockaddr *)&dest, sizeof dest);

   ssize_t n = recv(sfd, reply, reply_cap - 1, 0);
   if ( n > 0 ) reply[n] = '\0';

   (void)close(sfd);
   return n;
}

void test_ctrl_set_fault_and_get(void)
{
   // Use a high port unlikely to conflict
   uint16_t port = 39999;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };

   // Send SET_FAULT
   char reply[128];
   (void)sendto(socket(AF_INET, SOCK_DGRAM, 0), "SET_FAULT RRQ_TIMEOUT\n", 22, 0,
                (struct sockaddr *)&(struct sockaddr_in){
                   .sin_family = AF_INET,
                   .sin_port = htons(port),
                   .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                }, sizeof(struct sockaddr_in));

   // Simplified: use ctrl_send_recv helper
   ssize_t n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);

   // Poll to process the first sendto (the one without recv)
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);

   // Poll again for the ctrl_send_recv message
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);

   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // GET_FAULT
   n = ctrl_send_recv(port, "GET_FAULT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   // Reply was sent to the ctrl_send_recv socket which already closed,
   // but fault state should remain
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // RESET
   n = ctrl_send_recv(port, "RESET\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_set_fault_with_param(void)
{
   uint16_t port = 39998;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 5\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 5, fault.param );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_unknown_command(void)
{
   uint16_t port = 39997;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "BOGUS\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   // Fault should remain unchanged
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_unknown_fault_mode(void)
{
   uint16_t port = 39996;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT NONEXISTENT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode ); // unchanged
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_whitelist_rejects_disallowed_mode(void)
{
   uint16_t port = 39995;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   // Whitelist that allows only FAULT_RRQ_TIMEOUT (bit 0)
   uint64_t whitelist = (uint64_t)1 << 0;

   // Try to set FAULT_WRQ_TIMEOUT (bit 1) -- should be rejected
   ssize_t n = ctrl_send_recv(port, "SET_FAULT WRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, whitelist);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode ); // unchanged
   (void)n;

   // Setting RRQ_TIMEOUT should succeed with this whitelist
   n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, whitelist);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   tftptest_ctrl_shutdown(ctrl_sfd);
}

void test_ctrl_set_fault_missing_mode_name(void)
{
   uint16_t port = 39994;
   int ctrl_sfd = tftptest_ctrl_init(port);
   TEST_ASSERT_GREATER_OR_EQUAL_INT( 0, ctrl_sfd );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   // SET_FAULT with no mode name
   ssize_t n = ctrl_send_recv(port, "SET_FAULT\n", reply, sizeof reply);
   tftptest_ctrl_poll(ctrl_sfd, &fault, 0);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode ); // unchanged
   (void)n;

   tftptest_ctrl_shutdown(ctrl_sfd);
}

