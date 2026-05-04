/**
 * @file tftpqa_log.h
 * @brief Logging interface -- writes to stderr and optionally syslog.
 * @date Apr 10, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#ifndef TFTPTEST_LOG_H
#define TFTPTEST_LOG_H

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

enum TFTPQA_UseSyslog
{
   DONT_USE_SYSLOG,
   USE_SYSLOG
};

/**
 * @brief Initialize the logging subsystem.
 * @param[in] use_syslog  If USE_SYSLOG, also log to syslog (opens with "tftpqa" id)
 * @param[in] min_level   Messages below this level are suppressed.
 */
void tftpqa_log_init( enum TFTPQA_UseSyslog use_syslog, enum TFTP_LogLevel min_level );

/**
 * @brief Emit a log message.
 * @param[in] level  Severity of this message.
 * @param[in] func   Caller function name to prepend before the message body,
 *                   or NULL to omit. Pass __func__ for TRACE/DEBUG/WARN/ERR/FATAL;
 *                   pass NULL for INFO.
 * @param[in] fmt    printf-style format string.
 */
__attribute__(( format(printf, 3, 4) ))
void tftpqa_log( enum TFTP_LogLevel level,
                   const char *func,
                   const char *fmt, ... );

/**
 * @brief Shut down the logging subsystem (closes syslog if open).
 */
void tftpqa_log_shutdown(void);

#endif // TFTPTEST_LOG_H
