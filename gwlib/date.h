/*
 * date.h - interface to utilities for handling date and time values
 *
 * Richard Braakman
 */

#include "gwlib.h"

/*
 * Convert a unix time value to a value of the form
 * Sun, 06 Nov 1994 08:49:37 GMT
 * This is the format required by the HTTP protocol (RFC 2616),
 * and it is defined in RFC 822 as updated by RFC 1123.
 */
Octstr *date_format_http(unsigned long unixtime);
