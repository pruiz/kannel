/*
 * accesslog.c - implement access logging functions
 *
 * this module is somewhat similar to general logging module log.c,
 * but is far more simplified and is meant for access logs;
 * i.e. no multiple 'debug levels' nor multiple files, just one
 * file to save access information
 *
 * This way the Kannel adminstration can destroy all standard log files
 * when extra room is needed and only store these access logs for
 * statistics/billing information
 *
 */


#ifndef ACCESSLOG_H
#define ACCESSLOG_H

/* open access log with filename fname. if use_localtime != 0 then
 * all events are logged with localtime, not GMT
 */
void alog_open(char *fname, int use_localtime);

/* close access log. Do nothing if no open file */
void alog_close(void);

/* close and reopen access log. Do nothing if no open file */
void alog_reopen(void);

/* set access log to use localtimer in timestamps */
void alog_use_localtime(void);

/* set access log to use GMT in timestamps */
void alog_use_gmtime(void);

/* log given message with arguments (normal printf) into access log,
 * along with timestamp */
void alog(const char *fmt, ...);

#endif

