/* date.c - utility functions for handling times and dates
 *
 * Richard Braakman
 */

#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include "gwlib.h"

static unsigned char *wkday[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static unsigned char *monthname[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* The starting day of each month, if there's not a leap year.
 * January 1 is day 0, December 31 is day 355. */
static int monthstart[12] = {
    0, 31, 59, 90, 120, 151,
    181, 212, 243, 273, 304, 334
};

/* Value in seconds */
#define MINUTE 60
#define HOUR (60 * MINUTE)
#define DAY (24 * HOUR)

Octstr *date_format_http(unsigned long unixtime)
{
    struct tm tm;
    unsigned char buffer[30];

    tm = gw_gmtime((time_t) unixtime);

    /* Make sure gmtime gave us a good date.  We check this to
     * protect the sprintf call below, which might overflow its
     * buffer if the field values are bad. */
    if (tm.tm_wday < 0 || tm.tm_wday > 6 ||
        tm.tm_mday < 0 || tm.tm_mday > 31 ||
        tm.tm_mon < 0 || tm.tm_mon > 11 ||
        tm.tm_year < 0 ||
        tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_min < 0 || tm.tm_min > 59 ||
        tm.tm_sec < 0 || tm.tm_sec > 61) {
        warning(0, "Bad date for timestamp %lu, cannot format.",
                unixtime);
        return NULL;
    }

    sprintf(buffer, "%s, %02d %s %04d %02d:%02d:%02d GMT",
            wkday[tm.tm_wday], tm.tm_mday, monthname[tm.tm_mon],
            tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec);

    return octstr_create(buffer);
}

long date_convert_universal(struct universaltime *t)
{
    long date;
    int leapyears;
    long year;

    date = (t->year - 1970) * (365 * DAY);

    /* If we haven't had this year's leap day yet, pretend it's
     * the previous year. */
    year = t->year;
    if (t->month <= 1)
        year--;

    /* Add leap years since 1970.  The magic number 477 is the value
     * this formula would give for 1970 itself.  Notice the extra
     * effort we make to keep it correct for the year 2100. */
    leapyears = (year / 4) - (year / 100) + (year / 400) - 477;
    date += leapyears * DAY;

    date += monthstart[t->month] * DAY;
    date += (t->day - 1) * DAY;
    date += t->hour * HOUR;
    date += t->minute * MINUTE;
    date += t->second;

    return date;
}

long date_parse_http(Octstr *date)
{
    long pos;
    struct universaltime t;
    Octstr *monthstr = NULL;

    /* First, skip the leading day-of-week string. */
    pos = octstr_search_char(date, ' ', 0);
    if (pos < 0 || pos == octstr_len(date) - 1)
        return -1;
    pos++;  /* Skip the space */

    /* Distinguish between the three acceptable formats */
    if (isdigit(octstr_get_char(date, pos)) &&
        octstr_get_char(date, pos + 2) == ' ') {
        if (octstr_len(date) - pos < (long)strlen("06 Nov 1994 08:49:37 GMT"))
            goto error;
        if (octstr_parse_long(&t.day, date, pos, 10) != pos + 2)
            goto error;
        monthstr = octstr_copy(date, pos + 3, 3);
        if (octstr_parse_long(&t.year, date, pos + 7, 10) != pos + 11)
            goto error;
        if (octstr_parse_long(&t.hour, date, pos + 12, 10) != pos + 14)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 15, 10) != pos + 17)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 18, 10) != pos + 20)
            goto error;
        /* Take the GMT part on faith. */
    } else if (isdigit(octstr_get_char(date, pos)) &&
               octstr_get_char(date, pos + 2) == '-') {
        if (octstr_len(date) - pos < (long)strlen("06-Nov-94 08:49:37 GMT"))
            goto error;
        if (octstr_parse_long(&t.day, date, pos, 10) != pos + 2)
            goto error;
        monthstr = octstr_copy(date, pos + 3, 3);
        if (octstr_parse_long(&t.year, date, pos + 7, 10) != pos + 9)
            goto error;
        if (t.year > 60)
            t.year += 1900;
        else
            t.year += 2000;
        if (octstr_parse_long(&t.hour, date, pos + 10, 10) != pos + 12)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 13, 10) != pos + 15)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 16, 10) != pos + 18)
            goto error;
        /* Take the GMT part on faith. */
    } else {
        if (octstr_len(date) - pos < (long)strlen(" 6 08:49:37 1994"))
            goto error;
        monthstr = octstr_copy(date, pos, 3);
        if (octstr_parse_long(&t.day, date, pos + 4, 10) != pos + 6)
            goto error;
        if (octstr_parse_long(&t.hour, date, pos + 7, 10) != pos + 9)
            goto error;
        if (octstr_parse_long(&t.minute, date, pos + 10, 10) != pos + 12)
            goto error;
        if (octstr_parse_long(&t.second, date, pos + 13, 10) != pos + 15)
            goto error;
        if (octstr_parse_long(&t.year, date, pos + 16, 10) != pos + 20)
            goto error;
    }

    for (t.month = 0; t.month < 12; t.month++) {
        if (octstr_str_compare(monthstr, monthname[t.month]) == 0)
            break;
    }
    if (t.month == 12)
        goto error;

    octstr_destroy(monthstr);
    return date_convert_universal(&t);

error:
    octstr_destroy(monthstr);
    return -1;
}

int date_parse_iso (struct universaltime *ut, Octstr *os)
{
    long pos = 0;
	int c;

	/* assign defaults */
	ut->month = 0;
	ut->day = 1;
	ut->hour = 0;
	ut->minute = 0;
	ut->second = 0;

    if ((pos = octstr_parse_long(&(ut->year), os, pos, 10)) < 0)
		return -1;
	if (ut->year < 70)
	    ut->year += 2000;
	else if (ut->year < 100)
		ut->year += 1900;

	while ((c = octstr_get_char(os, pos++)) != -1 && !gw_isdigit(c));
	if ((pos = octstr_parse_long(&(ut->month), os, pos, 10)) < 0)
		return 0;

	while ((c = octstr_get_char(os, pos++)) != -1 && !gw_isdigit(c));
	if ((pos = octstr_parse_long(&(ut->day), os, pos, 10)) < 0)
		return 0;

	while ((c = octstr_get_char(os, pos++)) != -1 && !gw_isdigit(c));
	if ((pos = octstr_parse_long(&(ut->hour), os, pos, 10)) < 0)
		return 0;

	while ((c = octstr_get_char(os, pos++)) != -1 && !gw_isdigit(c));
	if ((pos = octstr_parse_long(&(ut->minute), os, pos, 10)) < 0)
		return 0;

	while ((c = octstr_get_char(os, pos++)) != -1 && !gw_isdigit(c));
	if ((pos = octstr_parse_long(&(ut->second), os, pos, 10)) < 0)
		return 0;

	return 0;
}

Octstr* date_create_iso(time_t unixtime) 
{
    struct tm tm;

    tm = gw_localtime((time_t) unixtime);
    
    return octstr_format("%d-%02d-%02d %02d:%02d:%02d", 
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);    
}


/* Note that this implementation makes unportable assumptions about time_t. */
long date_universal_now(void)
{
    return (long) time(NULL);
}
