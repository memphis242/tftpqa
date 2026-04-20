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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

// Existing tests
void test_ctrl_set_fault_and_get(void);
void test_ctrl_set_fault_with_param(void);
void test_ctrl_unknown_command(void);
void test_ctrl_unknown_fault_mode(void);
void test_ctrl_whitelist_rejects_disallowed_mode(void);
void test_ctrl_set_fault_missing_mode_name(void);

// param_present semantics
void test_ctrl_param_present_false_when_no_param(void);
void test_ctrl_param_zero_is_distinct_from_no_param(void);
void test_ctrl_reset_clears_mode_and_param_present(void);

// Reply content
void test_ctrl_set_fault_reply_no_param(void);
void test_ctrl_set_fault_reply_with_param(void);
void test_ctrl_get_fault_reply_no_param(void);
void test_ctrl_get_fault_reply_with_param(void);
void test_ctrl_reset_reply(void);
void test_ctrl_unknown_cmd_reply(void);
void test_ctrl_whitelist_reject_reply(void);

// Bad input
void test_ctrl_set_fault_invalid_param_nonnumeric(void);
void test_ctrl_set_fault_invalid_param_overflow(void);
void test_ctrl_set_fault_mode_name_too_long(void);

// Protocol robustness
void test_ctrl_case_insensitive_command(void);
void test_ctrl_leading_whitespace_stripped(void);
void test_ctrl_crlf_stripped(void);

// IP allowlist
void test_ctrl_allowed_client_ip_accepts_loopback(void);
void test_ctrl_allowed_client_ip_blocks_other_sender(void);

// Init errors and poll-loop behavior
void test_ctrl_init_null_cfg(void);
void test_ctrl_init_bind_failure(void);
void test_ctrl_no_packet_poll_noop(void);
void test_ctrl_empty_packet_ignored(void);

// SET_FAULT argument validation
void test_ctrl_set_fault_bare_no_args_reply(void);
void test_ctrl_set_fault_whitespace_only_after_command(void);
void test_ctrl_set_fault_param_with_trailing_garbage(void);
void test_ctrl_set_fault_param_just_above_uint32_max(void);
void test_ctrl_set_fault_mode_name_too_long_inner(void);

/*---------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

// Send a UDP message, wait up to 1 s for a reply, then close.
// Does NOT call poll_and_handle — the packet sits in the server socket buffer.
// Use when you only need to check fault state (not reply content).
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

   struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
   (void)setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

   (void)sendto(sfd, msg, strlen(msg), 0,
                (struct sockaddr *)&dest, sizeof dest);

   ssize_t n = recv(sfd, reply, reply_cap - 1, 0);
   if ( n > 0 ) reply[n] = '\0';

   (void)close(sfd);
   return n;
}

// Send a UDP message, immediately call poll_and_handle (so the server replies
// while the client socket is still open), then receive the reply.
// Use when you need to verify the server's reply content.
static ssize_t ctrl_exchange( uint16_t port,
                              const struct TFTPTest_CtrlCfg * cfg,
                              struct TFTPTest_FaultState * fault,
                              const char * msg,
                              char * reply, size_t reply_cap )
{
   int sfd = socket(AF_INET, SOCK_DGRAM, 0);
   if ( sfd < 0 ) return -1;

   struct sockaddr_in dest = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };

   struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
   (void)setsockopt(sfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

   (void)sendto(sfd, msg, strlen(msg), 0,
                (struct sockaddr *)&dest, sizeof dest);

   // Process while client socket is open so the reply can be delivered back.
   tftptest_ctrl_poll_and_handle(cfg, fault);

   ssize_t n = recv(sfd, reply, reply_cap - 1, 0);
   if ( n > 0 ) reply[n] = '\0';

   (void)close(sfd);
   return n;
}

/*---------------------------------------------------------------------------
 * Control Channel Tests
 *---------------------------------------------------------------------------*/

void test_ctrl_set_fault_and_get(void)
{
   uint16_t port = 39999;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[256];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // GET_FAULT
   n = ctrl_send_recv(port, "GET_FAULT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   // RESET
   n = ctrl_send_recv(port, "RESET\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_set_fault_with_param(void)
{
   uint16_t port = 39998;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 5\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 5, fault.param );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_unknown_command(void)
{
   uint16_t port = 39997;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "BOGUS\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_unknown_fault_mode(void)
{
   uint16_t port = 39996;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT NONEXISTENT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_whitelist_rejects_disallowed_mode(void)
{
   uint16_t port = 39995;
   // Whitelist that allows only FAULT_RRQ_TIMEOUT (bit 0)
   uint64_t whitelist = (uint64_t)1 << 0;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, whitelist, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   // Try to set FAULT_WRQ_TIMEOUT (bit 1) -- should be rejected
   ssize_t n = ctrl_send_recv(port, "SET_FAULT WRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   // Setting RRQ_TIMEOUT should succeed with this whitelist
   n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_set_fault_missing_mode_name(void)
{
   uint16_t port = 39994;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0 };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// param_present semantics -------------------------------------------------------

// Verify that setting a fault without a parameter sets param_present=false.
// Sets a mode WITH param first so the final false value can't be confused
// with the initial state.
void test_ctrl_param_present_false_when_no_param(void)
{
   uint16_t port = 39993;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128];
   ssize_t n;

   // First, set a mode WITH a param so param_present is true.
   n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 3\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_TRUE( fault.param_present );

   // Now set a mode WITHOUT a param; param_present must be false.
   n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// param=0 with param_present=true is distinct from "no parameter supplied".
// This is the core correctness guarantee of the param_present field.
void test_ctrl_param_zero_is_distinct_from_no_param(void)
{
   uint16_t port = 39992;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128];

   ssize_t n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 0\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 0, fault.param );
   TEST_ASSERT_TRUE( fault.param_present );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// RESET must clear param and param_present, not just mode.
void test_ctrl_reset_clears_mode_and_param_present(void)
{
   uint16_t port = 39991;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128];
   ssize_t n;

   n = ctrl_send_recv(port, "SET_FAULT DUP_MID_DATA 7\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_DUP_MID_DATA, fault.mode );
   TEST_ASSERT_TRUE( fault.param_present );

   n = ctrl_send_recv(port, "RESET\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_EQUAL_UINT32( 0, fault.param );
   TEST_ASSERT_FALSE( fault.param_present );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Reply content ----------------------------------------------------------------

void test_ctrl_set_fault_reply_no_param(void)
{
   uint16_t port = 39990;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT RRQ_TIMEOUT\n",
                             reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_RRQ_TIMEOUT\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_set_fault_reply_with_param(void)
{
   uint16_t port = 39989;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT DUP_MID_DATA 5\n",
                             reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_DUP_MID_DATA 5\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_get_fault_reply_no_param(void)
{
   uint16_t port = 39988;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};
   ssize_t n;

   // Set a mode without param, then GET_FAULT.
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT RRQ_TIMEOUT\n",
                     reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );

   memset(reply, 0, sizeof reply);
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "GET_FAULT\n", reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "FAULT FAULT_RRQ_TIMEOUT\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_get_fault_reply_with_param(void)
{
   uint16_t port = 39987;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};
   ssize_t n;

   n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT DUP_MID_DATA 5\n",
                     reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );

   memset(reply, 0, sizeof reply);
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "GET_FAULT\n", reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "FAULT FAULT_DUP_MID_DATA 5\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_reset_reply(void)
{
   uint16_t port = 39986;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "RESET\n", reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_NONE\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_unknown_cmd_reply(void)
{
   uint16_t port = 39985;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[256] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "BOGUS\n", reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR unknown command 'BOGUS'\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

void test_ctrl_whitelist_reject_reply(void)
{
   uint16_t port = 39984;
   // Allow only FAULT_RRQ_TIMEOUT (bit 0)
   uint64_t whitelist = (uint64_t)1 << 0;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, whitelist, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT WRQ_TIMEOUT\n",
                             reply, sizeof reply);
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR mode 'WRQ_TIMEOUT' not allowed\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Bad input --------------------------------------------------------------------

// Non-numeric param should be rejected; fault state must not change.
void test_ctrl_set_fault_invalid_param_nonnumeric(void)
{
   uint16_t port = 39983;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT SLOW_RESPONSE abc\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR invalid param\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Param that overflows uint32_t should be rejected.
void test_ctrl_set_fault_invalid_param_overflow(void)
{
   uint16_t port = 39982;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault,
                             "SET_FAULT SLOW_RESPONSE 99999999999999999999\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR invalid param\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// A packet exceeding MAX_CTRL_CMD_SZ must be silently dropped before any
// parsing — fault state must not change and no reply is sent.
// Note: MAX_CTRL_CMD_SZ caps the packet well below the mode_name[] buffer
// limit, so the recv_ctrl_pkt size gate is the first (and effective) line of
// defense against oversized commands.
void test_ctrl_set_fault_mode_name_too_long(void)
{
   uint16_t port = 39981;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[256] = {0};

   // MAX_CTRL_CMD_SZ = sizeof("SET_FAULT")-1 + LONGEST_FAULT_MODE_NAME_LEN + 20 = 60.
   // "SET_FAULT " (10) + 51 'X's (51) + "\n" (1) = 62 bytes > 60 → dropped.
   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault,
                             "SET_FAULT "
                             "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n",
                             reply, sizeof reply);
   // Server drops it: no reply, fault unchanged.
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_EQUAL( -1, n );   // recv timed out — no reply

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Protocol robustness ----------------------------------------------------------

// Commands and mode names should be matched case-insensitively.
void test_ctrl_case_insensitive_command(void)
{
   uint16_t port = 39980;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};
   ssize_t n;

   // Fully lowercase command and mode name
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "set_fault rrq_timeout\n",
                     reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_RRQ_TIMEOUT\n", reply );

   // Mixed-case GET_FAULT
   memset(reply, 0, sizeof reply);
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "GeT_FaUlT\n", reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "FAULT FAULT_RRQ_TIMEOUT\n", reply );

   // Lowercase RESET
   memset(reply, 0, sizeof reply);
   n = ctrl_exchange(port, &ctrl_cfg, &fault, "reset\n", reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_NONE\n", reply );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Leading whitespace before the command keyword must be tolerated.
void test_ctrl_leading_whitespace_stripped(void)
{
   uint16_t port = 39979;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "   SET_FAULT RRQ_TIMEOUT\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_RRQ_TIMEOUT\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// CRLF line endings (from netcat/telnet on Windows) must be stripped correctly.
void test_ctrl_crlf_stripped(void)
{
   uint16_t port = 39978;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   // The string literal ends with \r\n; sendto sends both bytes.
   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT RRQ_TIMEOUT\r\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "OK FAULT_RRQ_TIMEOUT\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// IP allowlist -----------------------------------------------------------------

// A packet from the configured allowed IP (loopback) must be accepted.
void test_ctrl_allowed_client_ip_accepts_loopback(void)
{
   uint16_t port = 39977;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   // Only accept packets from 127.0.0.1 (network byte order)
   uint32_t loopback_nbo = htonl(INADDR_LOOPBACK);
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, loopback_nbo);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   // ctrl_exchange sends from loopback, which is the allowed IP → should work.
   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT RRQ_TIMEOUT\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_RRQ_TIMEOUT, fault.mode );
   TEST_ASSERT_GREATER_THAN( 0, n );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// A packet from a disallowed IP must be silently dropped; fault must not change.
void test_ctrl_allowed_client_ip_blocks_other_sender(void)
{
   uint16_t port = 39976;
   // Allow only 10.0.0.1; test helper sends from 127.0.0.1, which should be blocked.
   uint32_t other_ip = inet_addr("10.0.0.1");
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, other_ip);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128];

   // Send packet (from loopback), then poll. Packet is dropped; fault unchanged.
   ssize_t n = ctrl_send_recv(port, "SET_FAULT RRQ_TIMEOUT\n", reply, sizeof reply);
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   (void)n;

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Coverage gap tests -----------------------------------------------------------

// Passing NULL as cfg to tftptest_ctrl_init must return TFTPTEST_CTRL_ERR_NULL_CFG.
void test_ctrl_init_null_cfg(void)
{
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(NULL, 39974, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_ERR_NULL_CFG, rc );
}

// If the port is already in use, bind() must fail and init must return
// TFTPTEST_CTRL_ERR_BIND with cfg->sfd left at -1.
void test_ctrl_init_bind_failure(void)
{
   uint16_t port = 39973;

   // Blocker bound WITHOUT SO_REUSEADDR so the subsequent bind in init fails.
   int blocker = socket(AF_INET, SOCK_DGRAM, 0);
   TEST_ASSERT_GREATER_OR_EQUAL( 0, blocker );

   struct sockaddr_in addr = {
      .sin_family      = AF_INET,
      .sin_port        = htons(port),
      .sin_addr.s_addr = htonl(INADDR_ANY),
   };
   int bind_rc = bind(blocker, (struct sockaddr *)&addr, sizeof addr);
   TEST_ASSERT_EQUAL_INT( 0, bind_rc );

   struct TFTPTest_CtrlCfg ctrl_cfg = { .sfd = -1 };
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_ERR_BIND, rc );
   TEST_ASSERT_EQUAL_INT( -1, ctrl_cfg.sfd );

   (void)close(blocker);
}

// Calling poll_and_handle on an empty socket buffer must return immediately
// without touching the fault state (EAGAIN path in recv_ctrl_pkt).
void test_ctrl_no_packet_poll_noop(void)
{
   uint16_t port = 39972;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };

   // No packet sent — recvfrom returns -1 / EAGAIN; handler must return silently.
   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);

   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// A zero-byte UDP datagram must be silently ignored; the empty_pkt counter path
// in recv_ctrl_pkt is exercised and the fault state must not change.
void test_ctrl_empty_packet_ignored(void)
{
   uint16_t port = 39971;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };

   // Send a zero-byte payload.
   int sfd = socket(AF_INET, SOCK_DGRAM, 0);
   TEST_ASSERT_GREATER_OR_EQUAL( 0, sfd );
   struct sockaddr_in dest = {
      .sin_family      = AF_INET,
      .sin_port        = htons(port),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
   };
   (void)sendto(sfd, "", 0, 0, (struct sockaddr *)&dest, sizeof dest);
   (void)close(sfd);

   tftptest_ctrl_poll_and_handle(&ctrl_cfg, &fault);

   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// Bare "SET_FAULT" with no trailing space or mode name must be caught by the
// MIN_SET_FAULT_CMD_SZ guard and produce a specific error reply — distinct from
// the mode_len==0 path that fires when there is a space but no mode name.
void test_ctrl_set_fault_bare_no_args_reply(void)
{
   uint16_t port = 39966;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT\n", reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR missing at least mode argument for SET_FAULT\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// "SET_FAULT" followed only by whitespace must be rejected with "ERR missing
// mode name"; the mode_len == 0 branch in handle_set_fault is covered.
void test_ctrl_set_fault_whitespace_only_after_command(void)
{
   uint16_t port = 39970;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT   \n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR missing mode name\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// A param token that starts numeric but has trailing non-whitespace garbage
// (e.g. "5X") must be rejected; the *endptr != '\0' branch is covered.
void test_ctrl_set_fault_param_with_trailing_garbage(void)
{
   uint16_t port = 39969;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault, "SET_FAULT SLOW_RESPONSE 5X\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR invalid param\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// 4294967296 == 2^32 == UINT32_MAX+1. On a 64-bit host, strtoul returns the
// value without ERANGE, but the val > UINT32_MAX check must catch it.
void test_ctrl_set_fault_param_just_above_uint32_max(void)
{
   uint16_t port = 39968;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault,
                             "SET_FAULT SLOW_RESPONSE 4294967296\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR invalid param\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}

// A mode name token longer than LONGEST_FAULT_MODE_NAME_LEN chars (i.e., that
// would overflow mode_name[]) must be rejected by the mode_len >= sizeof mode_name
// guard inside handle_set_fault — distinct from the recv_ctrl_pkt size gate, which
// only drops packets that exceed MAX_CTRL_CMD_SZ entirely.
// 32 'X's: "SET_FAULT " (10) + 32 'X's (32) + "\n" (1) = 43 bytes ≤ MAX_CTRL_CMD_SZ (60),
// so the packet passes recv_ctrl_pkt but mode_len (32) >= sizeof mode_name (32) fires.
void test_ctrl_set_fault_mode_name_too_long_inner(void)
{
   uint16_t port = 39967;
   struct TFTPTest_CtrlCfg ctrl_cfg = {0};
   enum TFTPTest_CtrlResult rc = tftptest_ctrl_init(&ctrl_cfg, port, UINT64_MAX, 0);
   TEST_ASSERT_EQUAL_INT( TFTPTEST_CTRL_OK, rc );

   struct TFTPTest_FaultState fault = { .mode = FAULT_NONE, .param = 0, .param_present = false };
   char reply[128] = {0};

   ssize_t n = ctrl_exchange(port, &ctrl_cfg, &fault,
                             "SET_FAULT XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n",
                             reply, sizeof reply);
   TEST_ASSERT_EQUAL_INT( FAULT_NONE, fault.mode );
   TEST_ASSERT_FALSE( fault.param_present );
   TEST_ASSERT_GREATER_THAN( 0, n );
   TEST_ASSERT_EQUAL_STRING( "ERR mode name too long\n", reply );

   tftptest_ctrl_shutdown(&ctrl_cfg);
}
