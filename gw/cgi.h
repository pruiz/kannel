/*
 * cgi.h - declarations for CGI-BIN utility functions
 *
 * http.h declares function `httpserver_get_request', which returns the
 * arguments to a CGI-BIN as a single string. This module contains functions
 * for parsing that string.
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#ifndef CGI_H
#define CGI_H


/*
 * This defines a singly linked list with each node being one name/value pair.
 * The caller should not peek into the list.
 */
typedef struct CGIArg CGIArg;


/*
 * Decode a string containing the CGI-BIN argument part of a URL into
 * a CGIArg list. Return NULL for failure, pointer to head of list for OK.
 * `args' is modified while this is called, but it is not used after this
 * function returns, so the caller may free it or whatever.
 */
CGIArg *cgiarg_decode_to_list(char *args);


/*
 * Destroy a list of CGIArg nodes.
 */
void cgiarg_destroy_list(CGIArg *list);


/*
 * Find the value of a given argument in a list. Return -1 if the name
 * was not found, or 0 if it was (and set `*value' to point at the
 * value string).
 */
int cgiarg_get(CGIArg *list, char *name, char **value);

#endif
