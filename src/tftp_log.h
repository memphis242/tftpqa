/**
 * @file tftp_log.h
 * @brief Logging interface -- writes to stderr and optionally syslog.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTP_LOG_H
#define TFTP_LOG_H

#include <stdbool.h>

// Log levels, ordered by increasing severity
enum TFTP_LogLevel
{
   TFTP_LOG_TRACE = 0,
   TFTP_LOG_DEBUG,
   TFTP_LOG_INFO,
   TFTP_LOG_WARN,
   TFTP_LOG_ERR,
   TFTP_LOG_FATAL,

   TFTP_LOG_LEVEL_COUNT,
};

/**
 * @brief Initialize the logging subsystem.
 * @param[in] use_syslog  If true, also log to syslog (opens with "tftptest" ident).
 * @param[in] min_level   Messages below this level are suppressed.
 */
void tftp_log_init(bool use_syslog, enum TFTP_LogLevel min_level);

/**
 * @brief Emit a log message.
 * @param[in] level  Severity of this message.
 * @param[in] fmt    printf-style format string.
 */
__attribute__(( format(printf, 2, 3) ))
void tftp_log(enum TFTP_LogLevel level, const char *fmt, ...);

/**
 * @brief Shut down the logging subsystem (closes syslog if open).
 */
void tftp_log_shutdown(void);

/**
 * @brief Return the human-readable name for a log level.
 */
const char *tftp_log_level_str(enum TFTP_LogLevel level);

#endif // TFTP_LOG_H
