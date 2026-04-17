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
 * @brief Emit a log message (underlying implementation — call via the
 *        tftp_log() macro below, not directly).
 * @param[in] level  Severity of this message.
 * @param[in] func   Caller function name to prepend, or NULL to omit.
 * @param[in] fmt    printf-style format string.
 */
__attribute__(( format(printf, 3, 4) ))
void tftp_log_impl( enum TFTP_LogLevel level,
                    const char *func,
                    const char *fmt, ... );

/**
 * @brief Emit a log message.
 *
 * Automatically prepends "<funcname>(): " to the message for all levels
 * except INFO (TRACE, DEBUG get it for diagnostics; WARN/ERR/FATAL get it
 * to pinpoint the error site). This is so that when debugging, or seeing
 * warnings/errors, the function traceability is more readily available; but
 * under normal operation (INFO lvl), we don't need the extra fcn info.
 *
 * @param[in] lvl  Severity (TFTP_LOG_TRACE … TFTP_LOG_FATAL).
 * @param[in] fmt  printf-style format string, followed by optional args.
 */
#define tftp_log(lvl, fmt, ...) \
   tftp_log_impl( (lvl), \
                  ((lvl) != TFTP_LOG_INFO ? __func__ : NULL), \
                  (fmt), ##__VA_ARGS__ )

/**
 * @brief Shut down the logging subsystem (closes syslog if open).
 */
void tftp_log_shutdown(void);

#endif // TFTP_LOG_H
