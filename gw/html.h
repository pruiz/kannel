/*
 * html.h - declarations for functions that manipulate HTML
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef HTML_H
#define HTML_H

#include <stddef.h>

/* Remove HTML tags and decode HTML entities in `html', take up to `smsmax'
   characters that remain and put them into `sms'. Do not fail. */
void html_to_sms(char *sms, size_t smsmax, char *html);

/* The `html' argument contains a HTML page. This function will return a
   new HTML page, which is the same as `html', but if `html' contains
   the string given `prefix', everything up to and including that will
   be removed. Similarly, if `html' contains `suffix' after `prefix',
   everything from the start of `suffix' will be removed from `html'.
   The resulting page will be stored as a dynamically alloctead string,
   which the caller will free. The page pointed by `html' will not be
   modified. Return NULL for error. */
char *html_strip_prefix_and_suffix(char *html, char *prefix, char *suffix);

#endif
