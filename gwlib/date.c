/* date.c - utility functions for handling times and dates
 *
 * Richard Braakman <dark@wapit.com>
 */

#include <unistd.h>

#include "gwlib.h"

unsigned char *wkday[] = {
	"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

unsigned char *monthname[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

Octstr *date_format_http(unsigned long unixtime) {
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
