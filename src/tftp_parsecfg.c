/**
 * @file tftp_parsecfg.c
 * @brief INI-style configuration file parser.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

/*************************** File Header Inclusions ***************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>

#include <arpa/inet.h>

#include "tftp_parsecfg.h"
#include "tftp_log.h"

/***************************** Local Declarations *****************************/

#define DEFAULT_TFTP_PORT       23069
#define DEFAULT_TIMEOUT_SEC 1
#define DEFAULT_MAX_RETX    5
#define DEFAULT_MAX_REQUESTS      10000
#define MAX_LINE_LEN              512

// Local Function Declarations
static char *trim_whitespace(char *str);
static int parse_log_level(const char *str, enum TFTP_LogLevel *out);

/********************** Public Function Implementations ***********************/

void tftp_parsecfg_defaults(struct TFTPTest_Config *cfg)
{
   assert( cfg != NULL );

   memset( cfg, 0, sizeof *cfg );

   cfg->tftp_port       = DEFAULT_TFTP_PORT;
   cfg->ctrl_port       = DEFAULT_TFTP_PORT + 1;  // 0 = disable fault simulation
   cfg->log_level       = TFTP_LOG_INFO;
   cfg->timeout_sec     = DEFAULT_TIMEOUT_SEC;
   cfg->max_retransmits = DEFAULT_MAX_RETX;
   cfg->max_requests    = DEFAULT_MAX_REQUESTS;
   cfg->fault_whitelist = UINT64_MAX; // All faults allowed by default
   cfg->allowed_client_ip = 0; // 0.0.0.0 = allow all clients
   cfg->max_wrq_file_size = 0;       // 0 = unlimited
   cfg->max_wrq_session_bytes = 0;   // 0 = unlimited
   cfg->max_wrq_duration_sec = 0;    // 0 = unlimited
   cfg->max_wrq_file_count = 0;      // 0 = unlimited
   cfg->min_disk_free_bytes = 0;     // 0 = no check
   cfg->wrq_enabled = true;
   cfg->max_abandoned_sessions = 0;  // 0 = unlimited

   // Default root dir: current working directory
   (void)strncpy( cfg->root_dir, ".", sizeof cfg->root_dir - 1 );
   cfg->root_dir[sizeof cfg->root_dir - 1] = '\0';

   // Default run-as user
   (void)strncpy( cfg->run_as_user, "nobody", sizeof cfg->run_as_user - 1 );
   cfg->run_as_user[sizeof cfg->run_as_user - 1] = '\0';
}

int tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg)
{
   assert( path != NULL );
   assert( cfg != NULL );

   FILE *fp = fopen( path, "r" );
   if ( fp == NULL )
   {
      tftp_log( TFTP_LOG_ERR, "Failed to open config file '%s': %s",
                path, strerror( errno ) );
      return -1;
   }

   char line[MAX_LINE_LEN];
   int line_num = 0;
   int errors = 0;

   while ( fgets( line, (int)sizeof line, fp ) != NULL )
   {
      line_num++;

      // Strip trailing newline
      size_t len = strlen( line );
      if ( len > 0 && line[len - 1] == '\n' )
         line[len - 1] = '\0';

      // Trim leading/trailing whitespace
      char *trimmed = trim_whitespace( line );

      // Skip blank lines and comments
      if ( trimmed[0] == '\0' || trimmed[0] == '#' )
         continue;

      // Split on first '='
      char *eq = strchr( trimmed, '=' );
      if ( eq == NULL )
      {
         tftp_log( TFTP_LOG_WARN, "Config line %d: missing '=' delimiter", line_num );
         errors++;
         continue;
      }

      *eq = '\0';
      char *key   = trim_whitespace( trimmed );
      char *value = trim_whitespace( eq + 1 );

      // Strip inline comments (everything after #)
      char *hash = strchr( value, '#' );
      if ( hash != NULL )
      {
         *hash = '\0';
         value = trim_whitespace( value );
      }

      // Match keys
      if ( strcmp( key, "tftp_port" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v == 0 || v > 65535 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid tftp_port '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->tftp_port = (uint16_t)v;
         }
      }
      else if ( strcmp( key, "ctrl_port" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v > 65535 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid ctrl_port '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->ctrl_port = (uint16_t)v;  // 0 = disable fault simulation
         }
      }
      else if ( strcmp( key, "root_dir" ) == 0 )
      {
         if ( strlen( value ) == 0 || strlen( value ) >= sizeof cfg->root_dir )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: root_dir empty or too long", line_num );
            errors++;
         }
         else
         {
            (void)strncpy( cfg->root_dir, value, sizeof cfg->root_dir - 1 );
            cfg->root_dir[sizeof cfg->root_dir - 1] = '\0';
         }
      }
      else if ( strcmp( key, "run_as_user" ) == 0 )
      {
         if ( strlen( value ) == 0 || strlen( value ) >= sizeof cfg->run_as_user )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: run_as_user empty or too long", line_num );
            errors++;
         }
         else
         {
            (void)strncpy( cfg->run_as_user, value, sizeof cfg->run_as_user - 1 );
            cfg->run_as_user[sizeof cfg->run_as_user - 1] = '\0';
         }
      }
      else if ( strcmp( key, "log_level" ) == 0 )
      {
         if ( parse_log_level( value, &cfg->log_level ) != 0 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: unknown log_level '%s'", line_num, value );
            errors++;
         }
      }
      else if ( strcmp( key, "timeout_sec" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v == 0 || v > 300 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid timeout_sec '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->timeout_sec = (unsigned int)v;
         }
      }
      else if ( strcmp( key, "max_retransmits" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v == 0 || v > 100 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid max_retransmits '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->max_retransmits = (unsigned int)v;
         }
      }
      else if ( strcmp( key, "max_requests" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v == 0 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid max_requests '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->max_requests = v;
         }
      }
      else if ( strcmp( key, "fault_whitelist" ) == 0 )
      {
         // Accept hex (0x...) or decimal
         char *endptr = NULL;
         uint64_t v = strtoull( value, &endptr, 0 );
         if ( endptr == value )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid fault_whitelist '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->fault_whitelist = v;
         }
      }
      else if ( strcmp( key, "allowed_client_ip" ) == 0 )
      {
         // Parse IP address (0.0.0.0 means allow all)
         if ( strlen( value ) == 0 || strcmp( value, "0.0.0.0" ) == 0 )
         {
            cfg->allowed_client_ip = 0; // Allow all clients
         }
         else
         {
            struct in_addr addr;
            if ( inet_aton( value, &addr ) == 0 )
            {
               tftp_log( TFTP_LOG_WARN, "Config line %d: invalid allowed_client_ip '%s'", line_num, value );
               errors++;
            }
            else
            {
               cfg->allowed_client_ip = addr.s_addr; // Store in network byte order
            }
         }
      }
      else if ( strcmp( key, "max_wrq_file_size" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->max_wrq_file_size = v;
      }
      else if ( strcmp( key, "max_wrq_session_bytes" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->max_wrq_session_bytes = v;
      }
      else if ( strcmp( key, "max_wrq_duration_sec" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v > 86400 )
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid max_wrq_duration_sec '%s' (must be 0-86400)",
                      line_num, value );
            errors++;
         }
         else
         {
            cfg->max_wrq_duration_sec = (unsigned int)v;
         }
      }
      else if ( strcmp( key, "max_wrq_file_count" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->max_wrq_file_count = v;
      }
      else if ( strcmp( key, "min_disk_free_bytes" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->min_disk_free_bytes = v;
      }
      else if ( strcmp( key, "wrq_enabled" ) == 0 )
      {
         if ( strcasecmp( value, "true" ) == 0 || strcasecmp( value, "yes" ) == 0 ||
              strcasecmp( value, "1" ) == 0 )
         {
            cfg->wrq_enabled = true;
         }
         else if ( strcasecmp( value, "false" ) == 0 || strcasecmp( value, "no" ) == 0 ||
                   strcasecmp( value, "0" ) == 0 )
         {
            cfg->wrq_enabled = false;
         }
         else
         {
            tftp_log( TFTP_LOG_WARN, "Config line %d: invalid wrq_enabled '%s'", line_num, value );
            errors++;
         }
      }
      else if ( strcmp( key, "max_abandoned_sessions" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->max_abandoned_sessions = v;
      }
      else
      {
         tftp_log( TFTP_LOG_WARN, "Config line %d: unknown key '%s'", line_num, key );
      }
   }

   (void)fclose( fp );

   if ( errors > 0 )
   {
      tftp_log( TFTP_LOG_WARN, "Config file '%s': %d error(s) encountered", path, errors );
   }
   else
   {
      tftp_log( TFTP_LOG_INFO, "Config file '%s' loaded successfully", path );
   }

   return 0;
}

/*********************** Local Function Implementations ***********************/

static char *trim_whitespace(char *str)
{
   assert( str != NULL );

   // Leading
   while ( isspace( (unsigned char)*str ) )
      str++;

   // Trailing
   char *end = str + strlen( str ) - 1;
   while ( end > str && isspace( (unsigned char)*end ) )
      end--;
   end[1] = '\0';

   return str;
}

static int parse_log_level(const char *str, enum TFTP_LogLevel *out)
{
   assert( str != NULL );
   assert( out != NULL );

   // Case-insensitive comparison
   if ( strcasecmp( str, "trace" ) == 0 )      *out = TFTP_LOG_TRACE;
   else if ( strcasecmp( str, "debug" ) == 0 )  *out = TFTP_LOG_DEBUG;
   else if ( strcasecmp( str, "info" ) == 0 )   *out = TFTP_LOG_INFO;
   else if ( strcasecmp( str, "warn" ) == 0 )   *out = TFTP_LOG_WARN;
   else if ( strcasecmp( str, "error" ) == 0 )  *out = TFTP_LOG_ERR;
   else if ( strcasecmp( str, "fatal" ) == 0 )  *out = TFTP_LOG_FATAL;
   else return -1;

   return 0;
}
