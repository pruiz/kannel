/*
 * date.h - interface to utilities for handling date and time values
 *
 * Richard Braakman
 */

#include "gwlib.h"

/* Broken-down time structure without timezone.  The values are
 * longs because that makes them easier to use with octstr_parse_long().
 */
struct universaltime
{
    long day;      /* 1-31 */
    long month;    /* 0-11 */
    long year;     /* 1970- */
    long hour;     /* 0-23 */
    long minute;   /* 0-59 */
    long second;   /* 0-59 */
};


/* Calculate the unix time value (seconds since 1970) given a broken-down
 * date structure in GMT. */
long date_convert_universal(struct universaltime *t);


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

/*
 * attempt to read an ISO-8601 format or similar, making no assumptions on 
 * seperators and number of elements, adding 0 or 1 to missing fields
 * For example, acceptable formats :
 *  2002-05-15 13:23:44
 *  02/05/15:13:23
 * support of 2 digit years is done by assuming years 70 an over are 20th century. this will
 * have to be revised sometime in the next 50 or so years
 */
int date_parse_iso(struct universaltime *ut, Octstr *os);

/*
 * create an ISO-8601 formated time stamp
 */
Octstr* date_create_iso(time_t unixtime);
 
/*
 * Return the current date and time as a unix time value.
 */
long date_universal_now(void);
