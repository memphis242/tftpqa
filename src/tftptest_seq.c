/**
 * @file tftptest_seq.c
 * @brief Test sequence file parser and stepper.
 * @date Apr 11, 2026
 * @author Abdulla Almosalmi, @memphis242
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <assert.h>
#include <errno.h>

#include "tftptest_seq.h"
#include "tftptest_log.h"

#define SEQ_LINE_MAX 256

/// Count non-blank, non-comment lines in a file. Returns count, or -1 on error.
static int count_entries(FILE *fp)
{
   char line[SEQ_LINE_MAX];
   int count = 0;

   while ( fgets(line, (int)sizeof line, fp) != NULL )
   {
      // Skip leading whitespace
      const char *p = line;
      while ( isspace((unsigned char)*p) )
         p++;

      // Skip blank lines and comments
      if ( *p == '\0' || *p == '#' || *p == '\n' )
         continue;

      count++;
   }

   return count;
}

/// Parse a single key=value token. Returns 0 on success.
static int parse_token( const char *token,
                        size_t lineno,
                        enum TFTPTest_FaultMode *mode,
                        bool *mode_set,
                        uint32_t *param,
                        bool *param_set,
                        size_t *count,
                        bool *count_set )
{
   assert(token != NULL);
   assert(mode != NULL);
   assert(mode_set != NULL);
   assert(param != NULL);
   assert(param_set != NULL);
   assert(count != NULL);
   assert(count_set != NULL);

   if ( strncasecmp(token, "mode=", 5) == 0 )
   {
      const char *val = token + 5;
      enum TFTPTest_FaultMode mode_idx = tftptest_fault_name_lookup_mode(val);

      if ( (int)mode_idx < 0 )
      {
         tftptest_log( TFTP_LOG_ERR, __func__,
            "Sequence line %zu: unknown fault mode '%s' :: tftptest_fault_name_lookup_mode(): %d",
            lineno, val, (int)mode_idx );

         return -1;
      }

      *mode = mode_idx;
      *mode_set = true;
   }
   else if ( strncasecmp(token, "param=", 6) == 0 )
   {
      const char *val = token + 6;
      char *end = NULL;
      unsigned long v = strtoul(val, &end, 0);
      if ( end == val || *end != '\0' )
      {
         tftptest_log(TFTP_LOG_ERR, __func__, "Sequence line %zu: invalid param value '%s'", lineno, val);
         return -1;
      }
      *param = (uint32_t)v;
      *param_set = true;
   }
   else if ( strncasecmp(token, "count=", 6) == 0 )
   {
      const char *val = token + 6;
      char *end = NULL;
      unsigned long v = strtoul(val, &end, 0);
      if ( end == val || *end != '\0' || v == 0 )
      {
         tftptest_log(TFTP_LOG_ERR, __func__, "Sequence line %zu: invalid count '%s' (must be >= 1)", lineno, val);
         return -1;
      }
      *count = v;
      *count_set = true;
   }
   else
   {
      tftptest_log(TFTP_LOG_ERR, __func__, "Sequence line %zu: unknown key in '%s'", lineno, token);
      return -1;
   }

   return 0;
}

int tftptest_seq_load(const char *path, struct TFTPTest_Seq *seq)
{
   FILE *fp = fopen(path, "r");
   if ( fp == NULL )
   {
      tftptest_log(TFTP_LOG_ERR, __func__,
               "Cannot open sequence file '%s': %s (%d) : %s",
               path, strerrorname_np(errno), errno, strerror(errno));
      return -1;
   }

   // First pass: count entries
   int n = count_entries(fp);
   if ( n <= 0 )
   {
      tftptest_log(TFTP_LOG_ERR, __func__, "Sequence file '%s': no entries found", path);
      fclose(fp);
      return -1;
   }

   // Allocate
   struct TFTPTest_SeqEntry *entries = calloc((size_t)n, sizeof *entries);
   if ( entries == NULL )
   {
      tftptest_log(TFTP_LOG_ERR, __func__, "Sequence file: allocation failed");
      fclose(fp);
      return -1;
   }

   // Second pass: parse
   rewind(fp);
   char line[SEQ_LINE_MAX];
   size_t lineno = 0;
   size_t idx = 0;
   int errors = 0;
   size_t total_sessions = 0;

   while ( fgets(line, (int)sizeof line, fp) != NULL )
   {
      lineno++;

      // Skip leading whitespace
      char *p = line;
      while ( isspace((unsigned char)*p) )
         p++;

      // Skip blank lines and comments
      if ( *p == '\0' || *p == '#' || *p == '\n' )
         continue;

      // Strip trailing newline/whitespace
      size_t len = strlen(p);
      while ( len > 0 && isspace((unsigned char)p[len - 1]) )
         p[--len] = '\0';

      // Strip inline comments
      char *hash = strchr(p, '#');
      if ( hash != NULL )
      {
         *hash = '\0';
         // Re-strip trailing whitespace
         len = strlen(p);
         while ( len > 0 && isspace((unsigned char)p[len - 1]) )
            p[--len] = '\0';
      }

      if ( len == 0 )
         continue;

      // Parse key=value tokens
      enum TFTPTest_FaultMode mode = FAULT_NONE;
      uint32_t param = 0;
      size_t count = 1;
      bool mode_set = false, param_set = false, count_set = false;

      char *saveptr = NULL;
      char *token = strtok_r(p, " \t", &saveptr);
      while ( token != NULL )
      {
         if ( parse_token(token, lineno, &mode, &mode_set,
                          &param, &param_set, &count, &count_set) != 0 )
         {
            errors++;
            break;
         }
         token = strtok_r(NULL, " \t", &saveptr);
      }

      if ( !mode_set )
      {
         tftptest_log(TFTP_LOG_ERR, __func__, "Sequence line %zu: missing required 'mode=' field", lineno);
         errors++;
         continue;
      }

      (void)count_set;

      entries[idx].mode          = mode;
      entries[idx].param         = param;
      entries[idx].param_present = param_set;
      entries[idx].count         = count;
      total_sessions += count;
      idx++;
   }

   fclose(fp);

   if ( errors > 0 )
   {
      tftptest_log(TFTP_LOG_ERR, __func__, "Sequence file '%s': %d error(s), aborting", path, errors);
      free(entries);
      return -1;
   }

   seq->entries          = entries;
   seq->n_entries        = idx;
   seq->current          = 0;
   seq->sessions_in_step = 0;

   tftptest_log(TFTP_LOG_INFO, NULL, "Loaded test sequence: %zu entries, %zu total sessions", idx, total_sessions);

   return 0;
}

bool tftptest_seq_advance(struct TFTPTest_Seq *seq, struct TFTPTest_FaultState *fault)
{
   // FIXME: Need to handle case where this fcn is called after the last step of the last session...

   seq->sessions_in_step++;

   if ( seq->sessions_in_step >= seq->entries[seq->current].count )
   {
      // Move to next entry
      seq->current++;
      seq->sessions_in_step = 0;

      if ( seq->current >= seq->n_entries )
         return false; // Sequence exhausted

      fault->mode          = seq->entries[seq->current].mode;
      fault->param         = seq->entries[seq->current].param;
      fault->param_present = seq->entries[seq->current].param_present;

      if ( fault->param_present )
      {
         tftptest_log(TFTP_LOG_INFO, NULL, "Sequence step %zu/%zu: %s param=%u, %zu sessions",
                  seq->current + 1, seq->n_entries,
                  tftptest_fault_mode_names[fault->mode],
                  fault->param, seq->entries[seq->current].count);
      }
      else
      {
         tftptest_log(TFTP_LOG_INFO, NULL, "Sequence step %zu/%zu: %s (no param), %zu sessions",
                  seq->current + 1, seq->n_entries,
                  tftptest_fault_mode_names[fault->mode],
                  seq->entries[seq->current].count);
      }
   }

   return true;
}

void tftptest_seq_free(struct TFTPTest_Seq *seq)
{
   free(seq->entries);
   seq->entries          = NULL;
   seq->n_entries        = 0;
   seq->current          = 0;
   seq->sessions_in_step = 0;
}
