/*
 * log.h - logging functions
 *
 * Please note that opening and closing of log files are not thread safe.
 * Don't do it unless you're in single-thread mode.
 */

#ifndef GWLOG_H
#define GWLOG_H

/* If we're using GCC, we can get it to check log function arguments. */
#ifdef __GNUC__
#define PRINTFLIKE __attribute__((format(printf, 2, 3)))
#define PRINTFLIKE2 __attribute__((format(printf, 3, 4)))
#else
#define PRINTFLIKE
#define PRINTFLIKE2
#endif

/* Symbolic levels for output levels. */
enum output_level {
	DEBUG, INFO, WARNING, ERROR, PANIC, LOG
};

/* Print an 'always printed' (forced) message (LOG:)
 * This log level is meant for messages that are _always_ printed. They
 * are not errors, warnings or similar things but data that always should
 * be printed. Like billing information.
 */
void forced(int, const char *, ...) PRINTFLIKE ;

/* Print a panicky error message and terminate the program with a failure.
 * So, this function is called when there is no other choice than to exit
 * immediately, with given reason
 */
void panic(int, const char *, ...) PRINTFLIKE ;

/* Print a normal error message. Used when something which should be
 * investigated and possibly fixed, happens. The error might be fatal, too,
 * but we have time to put system down peacefully.
 */
void error(int, const char *, ...) PRINTFLIKE ;

/* Print a warning message. 'Warning' is a message that should be told and
 * distinguished from normal information (info), but does not necessary require
 * any further investigations. Like 'warning, no sender number set'
 */
void warning(int, const char *, ...) PRINTFLIKE ;

/* Print an informational message. This information should be limited to
 * one or two rows per request, if real debugging information is needed,
 * use debug
 */
void info(int, const char *, ...) PRINTFLIKE ;

/*
 * Print a debug message. Most of the log messages should be of this level when
 * the system is under development. The first argument gives the `place'
 * where the function is called from; see function set_debug_places.
 */
void debug(const char *, int, const char *, ...) PRINTFLIKE2 ;


/*
 * Set the places from which debug messages are actually printed. This
 * allows run-time configuration of what is and is not logged when debug
 * is called. `places' is a string of tokens, separated by whitespace and/or
 * commas, with trailing asterisks (`*') matching anything. For instance,
 * if `places' is "wap.wsp.* wap.wtp.* wapbox", then all places that begin with
 * "wap.wsp." or "wap.wtp." (including the dots) are logged, and so is the place
 * called "wapbox". Nothing else is logged at debug level, however.
 * The 'places' string can also have negations, marked with '-' at the start,
 * so that nothing in that place is outputted. So if the string is
 * "wap.wsp.* -wap.wap.http", only wap.wsp is logged, but not http-parts on it
 */
void set_debug_places(const char *places);


/* Set minimum level for output messages to stderr. Messages with a lower 
   level are not printed to standard error, but may be printed to files
   (see below). */
void set_output_level(enum output_level level);

/*
 * Set syslog usage. If `ident' is NULL, syslog is not used.
 */
void set_syslog(const char *ident, int syslog_level);

/* Start logging to a file as well. The file will get messages at least of
   level `level'. There is no need and no way to close the log file;
   it will be closed automatically when the program finishes. Failures
   when opening to the log file are printed to stderr. */
void open_logfile(char *filename, int level);

/* Close and re-open all logfiles */
void reopen_log_files(void);

/*
 * Close all log files.
 */
void close_all_logfiles(void);

#endif
