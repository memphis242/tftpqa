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

/***************************** Local Declarations *****************************/

// Types
enum MainRC
{
   MAINRC_FINE                    = 0x0000,
   MAINRC_NREP_LIM_HIT            = 0x0001,
   MAINRC_SIGINT_REGISTRATION_ERR = 0x0002,
   MAINRC_SOCKET_CREATION_ERR     = 0x0004,
   MAINRC_SETSOCKOPT_ERR          = 0x0008,
   MAINRC_RECVFROM_ERR            = 0x0010,
   MAINRC_SENDTO_ERR              = 0x0020,
   MAINRC_FAILED_CLOSE            = 0x0040,
};

// File-Scope Variables
static volatile sig_atomic_t bUserEndedSession = false;

// Local Function Declarations
static void handleSIGINT(int sig_num);

/******************************* Main Function ********************************/
int main(int argc, char * argv[])
{
   int mainrc = MAINRC_FINE;
   int sysrc; // For system calls

   // Register SIGINT handler
   struct sigaction sa_cfg;
   memset(&sa_cfg, 0x00, sizeof sa_cfg);
   sigemptyset(&sa_cfg.sa_mask); // No need to mask any signals during handle
   sa_cfg.sa_handler = handleSIGINT;
   sysrc = sigaction( SIGINT,
                      &sa_cfg,
                      nullptr /* old sig action */ );

#ifndef NDEBUG
   if ( sysrc != 0 )
   {
      fprintf( stderr,
               "Warning: sigaction() failed to register interrupt signal.\n"
               "Returned: %d, errno: %s (%d): %s\n"
               "You won't be able to stop the program gracefully /w Ctrl+C, \n"
               " though Ctrl+C will still terminate the program.",
               sysrc, strerrorname_np(errno), errno, strerror(errno) );

      mainrc |= MAINRC_SIGINT_REGISTRATION_ERR;
   }
#endif

   // TODO: Create command UNIX Domain Socket? Use CLI arg only to set fault simulation mode?

   // Create persistent listening socket on port 21069

   // While loop
      // Await packet

      // Validate request packet

      // Initiate FSM for session and wait for completion

      // Await (/w timeout) for update on fault simulation mode

   // Cleanup

Main_ExitTag:
   return mainrc;
}
 
/*********************** Local Function Implementations ***********************/

/**
 * @brief Handle the interrupt signal that a user would trigger /w Ctrl+C
 */
static void handleSIGINT(int sig_num)
{
   (void)sig_num; // Signal number is not necessary here

   if ( bUserEndedSession )
      abort();

   bUserEndedSession = true;
}
