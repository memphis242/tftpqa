/**
 * @file tftp_log.c
 * @brief Logging implementation -- console output + optional syslog
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include <assert.h>
#include <errno.h>

#include <syslog.h>

#include "tftp_log.h"

/***************************** Local Declarations *****************************/

static bool               g_syslog_open = false;
static enum TFTP_LogLevel g_min_level   = TFTP_LOG_INFO;

// Map our log levels to syslog priorities
static const int s_syslog_priority[] =
{
   [TFTP_LOG_TRACE] = LOG_DEBUG,
   [TFTP_LOG_DEBUG] = LOG_DEBUG,
   [TFTP_LOG_INFO]  = LOG_INFO,
   [TFTP_LOG_WARN]  = LOG_WARNING,
   [TFTP_LOG_ERR]   = LOG_ERR,
   [TFTP_LOG_FATAL] = LOG_CRIT,
};

static const char *s_level_names[] =
{
   [TFTP_LOG_TRACE] = "TRACE",
   [TFTP_LOG_DEBUG] = "DEBUG",
   [TFTP_LOG_INFO]  = "INFO",
   [TFTP_LOG_WARN]  = "WARN",
   [TFTP_LOG_ERR]   = "ERROR",
   [TFTP_LOG_FATAL] = "FATAL",
};

/**
 * @brief Return the human-readable name for a log level.
 */
static const char *tftp_log_level_str( enum TFTP_LogLevel level );

/********************** Public Function Implementations ***********************/

void tftp_log_init( bool use_syslog, enum TFTP_LogLevel min_level )
{
   assert( min_level >= 0 && min_level < TFTP_LOG_LEVEL_COUNT );

   g_min_level = min_level;

   if ( use_syslog )
   {
      openlog( "tftptest", LOG_PID | LOG_NDELAY, LOG_DAEMON );
      g_syslog_open = true;
   }
}

void tftp_log_impl( enum TFTP_LogLevel level,
                    const char *func_name,
                    const char *fmt, ... )
{
   assert( fmt != NULL );
   assert( level >= 0 && level < TFTP_LOG_LEVEL_COUNT );

   if ( level < g_min_level )
      return;

   va_list ap;

   // Print out to console: prefix with timestamp and level
   {
      struct timespec ts;
      (void)clock_gettime( CLOCK_REALTIME, &ts );
      struct tm tm_buf;
      (void)localtime_r( &ts.tv_sec, &tm_buf );

      FILE *flog = level > TFTP_LOG_WARN ? stderr : stdout;

      (void)fprintf( flog,
               "[%-5s] %04d-%02d-%02d %02d:%02d:%02d.%03ld ",
               tftp_log_level_str(level),
               tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
               tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
               ts.tv_nsec / 1000000L );

      if ( func_name != NULL )
         (void)fprintf( flog, "%s(): ", func_name );

      va_start( ap, fmt );
      (void)vfprintf( flog, fmt, ap );
      va_end( ap );

      fputc( '\n', flog );
   }

   // syslog (if enabled)
   if ( g_syslog_open )
   {
      char syslog_fmt[1024];
      const char *use_fmt = fmt;
      if ( func_name != NULL )
      {
         snprintf( syslog_fmt, sizeof(syslog_fmt), "%s(): %s", func_name, fmt );
         use_fmt = syslog_fmt;
      }
      va_start( ap, fmt );
      vsyslog( s_syslog_priority[level], use_fmt, ap );
      va_end( ap );
   }
}

void tftp_log_shutdown( void )
{
   if ( g_syslog_open )
   {
      closelog();
      g_syslog_open = false;
   }
}

static const char *tftp_log_level_str( enum TFTP_LogLevel level )
{
   assert( level >= 0 && level < TFTP_LOG_LEVEL_COUNT );
   return s_level_names[level];
}
