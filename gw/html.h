/*
 * html.h - declarations for functions that manipulate HTML
 *
 * Lars Wirzenius for WapIT Ltd.
 */

#ifndef HTML_H
#define HTML_H

#include <stddef.h>

#include "gwlib/gwlib.h"

/*
 * Remove HTML tags and decode HTML entities in `html'. Return the new
 * string.
 * 
 * Do something intelligent even with broken HTML so that the
 * function never fails.
 */
Octstr *html_to_sms(Octstr *html);

#endif
