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

#include "tftptest_common.h"
#include "tftp_parsecfg.h"
#include "tftp_log.h"
#include "tftptest_whitelist.h"

/***************************** Local Declarations *****************************/

#define DEFAULT_TFTP_PORT          23069
#define DEFAULT_TIMEOUT_SEC        1
#define DEFAULT_MAX_RETX           5
#define DEFAULT_MAX_REQUESTS       10000
#define MAX_LINE_LEN               512

#define DEFAULT_FILE_CREATION_MODE (mode_t)0666 // rw-rw-rw-
CompileTimeAssert( DEFAULT_FILE_CREATION_MODE < 0777,
                   invalid_default_file_creation_mode );

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
   cfg->max_wrq_file_size = 0;       // 0 = unlimited
   cfg->max_wrq_session_bytes = 0;   // 0 = unlimited
   cfg->max_wrq_duration_sec = 0;    // 0 = unlimited
   cfg->max_wrq_file_count = 0;      // 0 = unlimited
   cfg->min_disk_free_bytes = 0;     // 0 = no check
   cfg->wrq_enabled = true;
   cfg->max_abandoned_sessions = 0;  // 0 = unlimited
   cfg->tid_port_min = 0;            // 0 = OS-assigned ephemeral
   cfg->tid_port_max = 0;
   cfg->new_file_mode = DEFAULT_FILE_CREATION_MODE;

   // Default root dir: current working directory
   (void)strncpy( cfg->root_dir, ".", sizeof cfg->root_dir - 1 );
   cfg->root_dir[sizeof cfg->root_dir - 1] = '\0';

   // Default run-as user
   (void)strncpy( cfg->run_as_user, "nobody", sizeof cfg->run_as_user - 1 );
   cfg->run_as_user[sizeof cfg->run_as_user - 1] = '\0';
}

int tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg, bool whitelist_external)
{
   assert( path != NULL );
   assert( cfg != NULL );

   FILE *fp = fopen( path, "r" );
   if ( fp == NULL )
   {
      tftp_log( TFTP_LOG_ERR, __func__, "Failed to open config file '%s': %s (%d) : %s",
                path, strerrorname_np(errno), errno, strerror(errno) );
      return -1;
   }

   char line[MAX_LINE_LEN];
   int line_num = 0;
   int errors = 0;
   bool fatal = false;
   bool seen_ip_whitelist = false;

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
         tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: missing '=' delimiter", line_num );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid tftp_port '%s'", line_num, value );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid ctrl_port '%s'", line_num, value );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: root_dir empty or too long", line_num );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: run_as_user empty or too long", line_num );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: unknown log_level '%s'", line_num, value );
            errors++;
         }
      }
      else if ( strcmp( key, "timeout_sec" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         if ( v == 0 || v > 300 )
         {
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid timeout_sec '%s'", line_num, value );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid max_retransmits '%s'", line_num, value );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid max_requests '%s'", line_num, value );
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid fault_whitelist '%s'", line_num, value );
            errors++;
         }
         else
         {
            cfg->fault_whitelist = v;
         }
      }
      else if ( strcmp( key, "ip_whitelist" ) == 0 )
      {
         // Required key. Comma-separated IPv4/CIDR list.
         // Use "0.0.0.0/0" to allow any sender.
         // Empty or malformed value is fail-closed: the whole config load fails.
         seen_ip_whitelist = true;
         if ( tftp_ipwhitelist_init( value ) != 0 )
         {
            tftp_log( TFTP_LOG_ERR, __func__,
                      "Config line %d: invalid ip_whitelist '%s' (rejecting config)",
                      line_num, value );
            errors++;
            fatal = true;
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid max_wrq_duration_sec '%s' (must be 0-86400)",
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
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid wrq_enabled '%s'", line_num, value );
            errors++;
         }
      }
      else if ( strcmp( key, "max_abandoned_sessions" ) == 0 )
      {
         unsigned long v = strtoul( value, NULL, 10 );
         cfg->max_abandoned_sessions = v;
      }
      else if ( strcmp( key, "tid_port_range" ) == 0 )
      {
         // Format: MIN-MAX (e.g., "50000-50100")
         char *dash = strchr( value, '-' );
         if ( dash == NULL )
         {
            tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: invalid tid_port_range '%s' (expected MIN-MAX)",
                      line_num, value );
            errors++;
         }
         else
         {
            *dash = '\0';
            char *min_str = trim_whitespace( value );
            char *max_str = trim_whitespace( dash + 1 );
            unsigned long vmin = strtoul( min_str, NULL, 10 );
            unsigned long vmax = strtoul( max_str, NULL, 10 );
            if ( vmin == 0 || vmin > 65535 || vmax == 0 || vmax > 65535 )
            {
               tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: tid_port_range ports must be 1-65535", line_num );
               errors++;
            }
            else if ( vmin > vmax )
            {
               tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: tid_port_range min (%lu) > max (%lu)",
                         line_num, vmin, vmax );
               errors++;
            }
            else
            {
               cfg->tid_port_min = (uint16_t)vmin;
               cfg->tid_port_max = (uint16_t)vmax;
            }
         }
      }
      else if ( strcmp( key, "new_file_mode" ) == 0 )
      {
         // Parsed strictly as octal so "0666" == 0666.
         // Reject trailing junk, values above 0777 (i.e., setuid/setgid/sticky (07000) bits)
         errno = 0;
         char *endp = NULL;
         unsigned long v = strtoul( value, &endp, 8 );

         if ( errno != 0 || endp == value )
         {
            tftp_log( TFTP_LOG_WARN, __func__,
                      "Config line %d: invalid new_file_mode '%s' (expected octal)",
                      line_num, value );

            errors++;
            continue;
         }

         // Skip trailing whitespace; anything else is junk
         while ( *endp != '\0' && isspace( (unsigned char)*endp ) )
            endp++;

         if ( *endp != '\0' )
         {
            tftp_log( TFTP_LOG_WARN, __func__,
                      "Config line %d: invalid new_file_mode '%s' (trailing garbage)",
                      line_num, value );

            errors++;
            continue;
         }
         else if ( v > 0777 )
         {
            tftp_log( TFTP_LOG_ERR, __func__,
                      "Config line %d: new_file_mode 0%lo exceeds 0777 "
                      "(setuid/setgid/sticky not allowed) - defaulting to %o",
                      line_num, v,
                      DEFAULT_FILE_CREATION_MODE );

            errors++;
            continue;
         }

         cfg->new_file_mode = (mode_t)v;
      }
      else
      {
         tftp_log( TFTP_LOG_WARN, __func__, "Config line %d: unknown key '%s'", line_num, key );
      }
   }

   (void)fclose( fp );

   if ( !seen_ip_whitelist && !whitelist_external )
   {
      tftp_log( TFTP_LOG_ERR, __func__,
                "required key 'ip_whitelist' is missing from '%s'", path );
      errors++;
      fatal = true;
   }

   if ( errors > 0 )
   {
      tftp_log( TFTP_LOG_WARN, __func__, "Config file '%s': %d error(s) encountered", path, errors );
   }
   else
   {
      tftp_log( TFTP_LOG_INFO, NULL, "Config file '%s' loaded successfully", path );
   }

   if ( fatal )
      return -1;

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
