/*
 * utils.c - generally useful, non-application specific functions for Gateway
 *
 * Kalle Marjola <rpr@wapit.com>
 */

#ifndef _GW_UTILS_H
#define _GW_UTILS_H

#include <stddef.h>
#include <stdio.h>
#include "octstr.h"
#include <termios.h>

/*
 * Octet and MultibyteInteger (variable length) functions
 */

typedef unsigned char Octet;		/* 8-bit basic data */
typedef unsigned long MultibyteInt;	/* limited to 32 bits, not 35 */

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

/* parse command line arguments and set options '-v', '-D', '-F' and '-V'
 * (or --verbosity, --debug, --logfile, --fileverbosity, respectively)
 *
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


/*
 * return 0 if 'ip' is denied by deny_ip and not allowed by allow_ip
 * return 1 otherwise (deny_ip is NULL or 'ip' is in allow_ip or is not
 *  in deny_ip)
 * return -1 on error ('ip' is NULL)
 */
int is_allowed_ip(char *allow_ip, char *deny_ip, Octstr *ip);


/*
 * Normalize 'number', like change number "040500" to "0035840500" if
 * the dial-prefix is like "0035840,040;0035850,050"
 *
 * return -1 on error, 0 if no match in dial_prefixes and 1 if match found
 * If the 'number' needs normalization, it is done.
 */
int normalize_number(char *dial_prefixes, Octstr **number);

/*
 * Convert a standard "network long" (32 bits in 4 octets, most significant
 * octet first) to the host representation.
 */
long decode_network_long(unsigned char *data);


/*
 * Convert a long to the standard network representation (32 bits in 4
 * octets, most significant octet first).
 */
void encode_network_long(unsigned char *data, unsigned long value);


/* kannel implementation of cfmakeraw*/
#if !HAVE_CFMAKERAW
void kannel_cfmakeraw (struct termios *tio);
#else
/* If it does exist, call cfmakeraw rather than the internal one*/
#define kannel_cfmakeraw cfmakeraw
#endif


#endif
