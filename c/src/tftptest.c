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
static enum MainRC TFTP_Test_SetUpNewConnSock(int * const sfd_ptr);

/******************************* Main Function ********************************/
int main(int argc, char * argv[])
{
   int mainrc = MAINRC_FINE;
   int sysrc; // For system calls

   int sfd_newconn = 0; // socket id for new connections on the pre-configured port

   // Register SIGINT handler
   struct sigaction sa_cfg;
   memset(&sa_cfg, 0x00, sizeof sa_cfg);
   sigemptyset(&sa_cfg.sa_mask); // No need to mask any signals during handle
   sa_cfg.sa_handler = handleSIGINT;
   sysrc = sigaction( SIGINT,
                      &sa_cfg,
                      nullptr /* old sig action */ );

#ifndef NDEBUG
   // Really shouldn't happen, but I'll check for debug builds just in case.
   if ( sysrc != 0 )
   {
      fprintf( stderr,
               "Warning: sigaction() failed to register interrupt signal.\n"
               "Returned: %d, errno: %s (%d): %s\n"
               "You won't be able to stop the program gracefully /w Ctrl+C, \n"
               " though Ctrl+C will still terminate the program.",
               sysrc, strerrorname_np(errno), errno, strerror(errno) );

      return MAINRC_SIGINT_REGISTRATION_ERR;
   }
#endif

   // TODO: Create command UNIX Domain Socket? Use CLI arg only to set fault simulation mode?

   // Set up socket for listening on for new session requests...
   mainrc |= (int)TFTP_Test_SetUpNewConnSock(&sfd_newconn);
   if ( mainrc != MAINRC_FINE )
      goto Main_CleanupTag;

   // Primary loop
   while ( !bUserEndedSession )
   {
      // Await packet

      // Validate request packet

      // Initiate FSM for session and wait for completion

      // Await (/w timeout) for update on fault simulation mode
   }

   TFTP_FSM_CleanExit();

   if ( bUserEndedSession )
      (void)printf("\n\nUser ended session.\n");
   else
      (void)fprintf(stderr, "Error occured during main loop.");

Main_CleanupTag:
   // Cleanup
   (void)close(sfd_newconn);

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
   if ( bUserEndedSession )
      abort();

   bUserEndedSession = true;
}

/**
 * @brief TODO
 */
static enum MainRC TFTP_Test_SetUpNewConnSock(int * const sfd_ptr)
{
   assert(sfd_ptr != nullptr);

   int sysrc = 0;

   // Create socket
   *sfd_ptr = socket(AF_INET, SOCK_DGRAM, 0);
   if ( *sfd_ptr < 0 )
   {
      fprintf( stderr,
               "Error: Failed to create listening socket.\n"
               "socket() returned: %d, errno: %s (%d): %s\n",
               *sfd_ptr,
               strerrorname_np(errno), errno, strerror(errno) );

      return MAINRC_SOCKET_CREATION_ERR;
   }

   // Facilitate reuse of the consistent port in case a lingering socket from a
   // previous session hasn't been cleared away. TODO: Verify this is needed.
   sysrc = setsockopt( *sfd_ptr,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &(int){1},
                       sizeof(int) );
   assert(sysrc == 0); // if setsockopt() failed, we set something up wrong

   // Bind socket to pre-configured IP/port
   const struct in_addr numerical_addr = { .s_addr = INADDR_ANY }; // TODO: Configurable
   const unsigned short port = DEFAULT_TFTP_RQST_PORT;
   sysrc = bind( sfd,
                  (struct sockaddr *)
                  &(struct sockaddr_in) {
                     .sin_family = AF_INET,
                     .sin_port = port,
                     .sin_addr = numerical_addr
                  },
                  sizeof(struct sockaddr_in) );

   if ( sysrc != 0 )
   {
      char addrbuf[INET_ADDRSTRLEN];
      const char * rc_ptr = inet_ntop(AF_INET, &numerical_addr, addrbuf, INET_ADDRSTRLEN);
      if ( nullptr == rc_ptr )
      {
         fprintf( stderr,
                  "inet_ntop() failed to convert, errno: %s (%d): %s\n",
                  strerrorname_np(errno), errno, strerror(errno) );
         assert(rc_ptr != nullptr); // Still abort, because this indicates a design issue
      }

      fprintf( stderr,
               "Error: Failed to bind socket to specified interface:\n"
               "\tIP Address: %s\n"
               "\tPort: %d\n"
               "bind() returned: %d, errno: %s (%d): %s\n"
               "Socket will be closed. Please try again.\n",
               addrbuf, ntohs(port), sysrc,
               strerrorname_np(errno), errno, strerror(errno) );

      (void)close(*sfd_ptr);
      *sfd_ptr = -1;

      return MAINRC_SOCKBIND_ERR;
   }

   return MAINRC_FINE;
}
