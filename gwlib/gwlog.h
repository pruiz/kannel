/*
 * This is a simple malloc()-wrapper. It does not return NULLs but
 * instead panics. It also introduces mutex wrappers
 *
 * Kalle 'rpr' Marjola 1999
 */

#ifndef _GWLOG_H
#define _GWLOG_H

/* If we're using GCC, we can get it to check log function arguments. */
#ifdef __GNUC__
#define PRINTFLIKE __attribute__((format(printf, 2, 3)))
#else
#define PRINTFLIKE
#endif

/* Symbolic levels for output levels. */
enum output_level {
	DEBUG, INFO, WARNING, ERROR, PANIC
};

/* Print a panicky error message and terminate the program with a failure. */
void panic(int, const char *, ...) PRINTFLIKE ;

/* Print a normal error message. */
void error(int, const char *, ...) PRINTFLIKE ;

/* Print a warning message. */
void warning(int, const char *, ...) PRINTFLIKE ;

/* Print an informational message. */
void info(int, const char *, ...) PRINTFLIKE ;

/* Print a debug message. */
void debug(int, const char *, ...) PRINTFLIKE ;

/* Set minimum level for output messages to stderr. Messages with a lower 
   level are not printed to standard error, but may be printed to files
   (see below). */
void set_output_level(enum output_level level);

/* Start logging to a file as well. The file will get messages at least of
   level `level'. There is no need and no way to close the log file;
   it will be closed automatically when the program finishes. Failures
   when opening to the log file are printed to stderr. */
void open_logfile(char *filename, int level);

/* Close and re-open all logfiles */
void reopen_log_files(void);


#endif
