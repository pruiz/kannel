/*
 * html.h - declarations for functions that manipulate HTML
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef HTML_H
#define HTML_H

#include <stddef.h>

#include "gwlib/octstr.h"

/*
 * Remove HTML tags and decode HTML entities in `html'. Return the
 * result as an Octstr.
 * 
 * Do something intelligent even with broken HTML so that the
 * function never fails.
 */
Octstr *html_to_sms(Octstr *html);

/*
 * The `html' argument contains a HTML page. This function will return a
 *  new HTML page, which is the same as `html', but if `html' contains
 * the string given `prefix', everything up to and including that will
 * be removed. Similarly, if `html' contains `suffix' after `prefix',
 * everything from the start of `suffix' will be removed from `html'.
 * The resulting page will be stored as a dynamically alloctead string,
 * which the caller will free. The page pointed by `html' will not be
 * modified.
 */
Octstr *html_strip_prefix_and_suffix(Octstr *html, char *prefix, 
    	    	    	    	     char *suffix);

#endif
