/*
 * wapitlib.h - generally useful, non-application specific functions for WapIT
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef WAPITLIB_H
#define WAPITLIB_H

#include <stddef.h>
#include <stdio.h>

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

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
    

/*
 * this function checks if the given 'ip' is in 'accept_string', which
 * has multiple ips separated with ';', like "100.0.0.0;20.0.0.30"
 * The 'accept_stting' can have '*' instead of number in any location,
 * so complete 'anything matches' is "*.*.*.*". Functionalibility is
 * non-determined if either of the IP-addresses is invalid.
 *
 * If 'match_buffer' is not NULL, copies the matching IP to it. Caller
 * must provide buffer large enough, is used.
 *
 * Return 0 if no match found, in which case match_buffer is untouched,
 * and 1 if match found, in which case match buffer is modified, if provided
 */
int check_ip(char *accept_string, char *ip, char *match_buffer);

#endif
