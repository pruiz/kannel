/*
 * check_date.c - checking of date handling functions
 */

#include <string.h>

#include "gwlib/gwlib.h"

/* Format of test_dates file:
 * Valid date strings, one per line.  If a date string is valid
 * but not in the preferred HTTP format, put the preferred version 
 * after it on the same line.  Separate them with a tab.
 */

static void check_reversible(void)
{
    Octstr *dates;
    long pos, endpos, tabpos;
    Octstr *date, *canondate;
    long timeval;

    dates = octstr_read_file("checks/test_dates");
    if (dates == NULL)
        return;

    for (pos = 0; ; pos = endpos + 1) {
        endpos = octstr_search_char(dates, '\n', pos);
        if (endpos < 0)
            break;

        tabpos = octstr_search_char(dates, '\t', pos);

        if (tabpos >= 0 && tabpos < endpos) {
            date = octstr_copy(dates, pos, tabpos - pos);
            canondate = octstr_copy(dates, tabpos + 1, endpos - tabpos - 1);
        } else {
            date = octstr_copy(dates, pos, endpos - pos);
            canondate = octstr_duplicate(date);
        }

        timeval = date_parse_http(date);
        if (timeval < 0)
            warning(0, "Could not parse date \"%s\"", octstr_get_cstr(date));
        else {
            Octstr *newdate;
            newdate = date_format_http(timeval);
            if (octstr_compare(newdate, canondate) != 0) {
                warning(0, "Date not reversible: \"%s\" becomes \"%s\"",
                        octstr_get_cstr(date), octstr_get_cstr(newdate));
            }
            octstr_destroy(newdate);
        }

        octstr_destroy(date);
        octstr_destroy(canondate);
    }

    octstr_destroy(dates);
}


int main(void)
{
    set_output_level(INFO);
    gwlib_init();
    check_reversible();
    gwlib_shutdown();
    return 0;
}
