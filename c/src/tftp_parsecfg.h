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
   uint32_t           allowed_client_ip; // Restrict to this IP (0.0.0.0 = allow all)
   // WRQ DoS protection
   size_t             max_wrq_file_size;      // Per-file size limit in bytes (0 = unlimited)
   size_t             max_wrq_session_bytes;  // Cumulative WRQ bytes for entire server run (0 = unlimited)
   unsigned int       max_wrq_duration_sec;   // Per-WRQ wall-clock timeout in seconds (0 = unlimited)
   size_t             max_wrq_file_count;     // Max files written in this server run (0 = unlimited)
   size_t             min_disk_free_bytes;    // Reject WRQ if free disk < this (0 = no check)
   bool               wrq_enabled;            // If false, reject all WRQ with ACCESS_VIOLATION
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
 * @param[in]  path  Path to the config file.
 * @param[out] cfg   The config struct to populate.
 * @return 0 on success, -1 on error (logged via tftp_log).
 */
int tftp_parsecfg_load(const char *path, struct TFTPTest_Config *cfg);

#endif // TFTP_PARSECFG_H
