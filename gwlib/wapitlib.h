/*
 * wapitlib.h - generally useful, non-application specific functions for WapIT
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef WAPITLIB_H
#define WAPITLIB_H

#include <stddef.h>
#include <stdio.h>
#if HAVE_THREADS
#include <pthread.h>
#else
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_attr_t;
#define pthread_self() (0)
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

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

/* Open a server socket. Return -1 for error, >= 0 socket number for OK.*/
int make_server_socket(int port);

/* Open a client socket. */
int tcpip_connect_to_server(char *hostname, int port);

/* Write string to socket. */
int write_to_socket(int socket, char *str);

/* Read a line from a socket. Return -1 for error, 0 for EOF, or 1 for OK.
   Remove CRLF and LF from end of line. */
int read_line(int fd, char *line, int max);

/* Read the rest of the input file into dynamically allocated memory. */
int read_to_eof(int fd, char **data, size_t *len);

/* Check if there is something to be read in 'fd'. Return 1 if there
 * is data, 0 otherwise, -1 on error */
int read_available(int fd);

/* Split string `buf' up to `max' words at space characters. Return number
   of words found. If there are more than `max' words, all the remaining
   words are the last word, even if it may contain spaces. */
int split_words(char *buf, int max, char **words);


/* Remove leading and trailing whitespace. */
char *trim_ends(char *str);


/* Count the number of times `pat' occurs on `str'. */
int count_occurences(char *str, char *pat);


/* Make a dynamically allocated copy of first `n' characters of `str'. */
char *strndup(char *str, size_t n);


/*
 * Type of function for threads. See pthread.h.
 */
typedef void *Threadfunc(void *arg);

/*
 * Start a new thread, running function func, and giving it the argument
 * `arg'. If `size' is 0, `arg' is given as is; otherwise, `arg' is copied
 * into a memory area of size `size'.
 * 
 * If `detached' is non-zero, the thread is created detached, otherwise
 * it is created detached.
 */
pthread_t start_thread(int detached, Threadfunc *func, void *arg, size_t size);



/*
 * Octet and MultibyteInteger (variable length) functions
 * (Kalle Marjola 1999)
 */

typedef unsigned char Octet;		/* 8-bit basic data */
typedef unsigned int MultibyteInt;	/* limited to 32 bits, not 35 */

/* get value of a multibyte ineteger. Note that it MUST be a valid
 * numbers, otherwise an overflow may occur as the function keeps
 * on reading the number until continue-bit (high bit) is not set.
 * Does not fail, always returns some number, but may overflow. */
MultibyteInt get_variable_value(Octet *source, int *len);

/* write given multibyte integer into given destination string, which
 * must be large enough to handle the number (5 bytes is always sufficient)
 * returns the total length of the written number, in bytes. Does not fail */
int write_variable_value(MultibyteInt value, Octet *dest);

/* reverse the value of an octet */
Octet reverse_octet(Octet source);

/* url-decode given string, doing the appropriate conversion in place.
 * Any corrupted codings ('%pr' for example) are left in place.
 * If the end of the string is malformed ('%n\0' or '%\0') returns -1,
 * 0 otherwise. The string so-far is modified. */
int url_decode(char *string);


/*
 * like strstr, but ignore case
 */
char *str_case_str(char *str, char *pat);


/*
 * seek string 's' backward from offset 'start_offset'. Return offset of
 * the first occurance of any character in 'accept' string, or -1 if not
 * found  */
int str_reverse_seek(const char *s, int start_offset, const char *accept);

/* as above but ignoring case */
int str_reverse_case_seek(const char *s, int start_offset, const char *accept);


/* parse command line arguments and set options '-v', '-F' and '-V'
 * (output-level, logfile name, logfile output-level)
 * Any other argument starting with '-' calls 'find_own' function,
 * which is provided by the user. If set to NULL, these are ignored
 * (but error message is put into stderr)
 *
 * Returns index of next argument after any parsing 
 *
 * Function 'find_own' has following parameters:
 *   index is the current index in argv
 *   argc and argv are command line parameters, directly transfered 
 *
 *   the function returns any extra number of parameters needed to be
 *   skipped. It must personally deal with any malformed arguments.
 *   It return -1 if it cannot find match for the argument
 *
 * sample simple function is like:
 *   int find_is_there_X(int i, int argc, char **argv)
 *      {  if (strcmp(argv[i], "-X")==0) return 0; else return -1; } 
 */
int get_and_set_debugs(int argc, char **argv,
		       int (*find_own) (int index, int argc, char **argv));


/* print usage of all standard arguments (parsed in get_and_set_debugs)
 * to given stream */
void print_std_args_usage(FILE *stream);
    

/* lock given mutex. PANICS if fails (non-initialized mutex or other
 *  coding error) */ 
void mutex_lock(pthread_mutex_t *mutex);


/* unlock given mutex, PANICX if fails (so do not call for non-locked)
 */
void mutex_unlock(pthread_mutex_t *mutex);
    
#endif
