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

/*
 * Convert a date string as defined by the HTTP protocol (RFC 2616)
 * to a unix time value.  Return -1 if the date string was invalid.
 * Three date formats are acceptable:
 *   Sun, 06 Nov 1994 08:49:37 GMT  ; RFC 822, updated by RFC 1123
 *   Sunday, 06-Nov-94 08:49:37 GMT ; RFC 850, obsoleted by RFC 1036
 *   Sun Nov  6 08:49:37 1994       ; ANSI C's asctime() format
 * White space is significant.
 */
long date_parse_http(Octstr *date);
