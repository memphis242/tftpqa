/**
 * @file tftp_parsecfg.h
 * @brief INI-style configuration file parser.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_PARSECFG_H
#define TFTP_PARSECFG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include <sys/types.h>

#include "tftp_log.h"

// Server configuration
struct TFTPTest_Config
{
   uint16_t           tftp_port;        // TFTP listening port (default 23069)
   uint16_t           ctrl_port;        // Control channel port (default tftp_port + 1, 0 = disable faults)
   char               root_dir[PATH_MAX]; // TFTP root directory
   char               run_as_user[64];   // Drop privileges to this user (default "nobody")
   enum TFTP_LogLevel log_level;        // Minimum log level
   unsigned int       timeout_sec;      // Per-packet timeout in seconds (default 1)
   unsigned int       max_retransmits;  // Max retransmit attempts (default 5)
   size_t             max_requests;     // Max TFTP requests before restart (default 10000)
   uint64_t           fault_whitelist;  // Bitmask of allowed fault modes
   // WRQ DoS protection
   size_t             max_wrq_file_size;      // Per-file size limit in bytes (0 = unlimited)
   size_t             max_wrq_session_bytes;  // Cumulative WRQ bytes for entire server run (0 = unlimited)
   unsigned int       max_wrq_duration_sec;   // Per-WRQ wall-clock timeout in seconds (0 = unlimited)
   size_t             max_wrq_file_count;     // Max files written in this server run (0 = unlimited)
   size_t             min_disk_free_bytes;    // Reject WRQ if free disk < this (0 = no check)
   bool               wrq_enabled;            // If false, reject all WRQ with ACCESS_VIOLATION
   // Session abandonment protection
   size_t             max_abandoned_sessions;  // Lock out all requests after this many timed-out sessions (0 = unlimited)
   // TID port range (0/0 = OS-assigned ephemeral, current default)
   uint16_t           tid_port_min;
   uint16_t           tid_port_max;
   // File-permission policy for newly created WRQ files (default 0666).
   // Parsed as octal in the config file. Setuid/setgid/sticky bits (07000) are rejected.
   mode_t             new_file_mode;
};

/**
 * @brief Fill a config struct with sane defaults.
 * @param[out] cfg  The config struct to populate.
 */
void tftp_parsecfg_defaults(struct TFTPTest_Config *cfg);

/**
 * @brief Parse an INI-style config file into a config struct.
 *
 * The struct should be pre-filled with defaults (via tftp_parsecfg_defaults)
 * before calling this function; only keys present in the file are overwritten.
 *
 * @param[in]  path               Path to the config file.
 * @param[out] cfg                The config struct to populate.
 * @param[in]  whitelist_external When true, a missing ip_whitelist key is not an
 *                                error (the caller will supply the whitelist via
 *                                another means, e.g. a CLI flag). When false, the
 *                                key is required and its absence is fatal.
 * @return 0 on success, -1 on error (logged via tftp_log).
 */
int tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg, bool whitelist_external);

#endif // TFTP_PARSECFG_H
