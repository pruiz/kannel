/*
 * timestamp.c - Convert a textual timestamps to seconds since epoch
 *
 * Read textual timestamps, one per line, from the standard input, and 
 * convert them to integers giving the corresponding number of seconds
 * since the beginning of the epoch (beginning of 1970). Both the input
 * and the results should be in UTC.
 *
 * Lars Wirzenius
 */


#include <stdio.h>

#include "gwlib/gwlib.h"


static Octstr *read_line(FILE *f, Octstr *buf)
{
    Octstr *os;
    char cbuf[8*1024];
    size_t n;
    long pos;
    
    pos = octstr_search_char(buf, '\n', 0);
    while (pos == -1 && (n = fread(cbuf, 1, sizeof(cbuf), f)) > 0) {
	octstr_append_data(buf, cbuf, n);
	pos = octstr_search_char(buf, '\n', 0);
    }

    if (pos == -1) {
    	pos = octstr_len(buf);
	if (pos == 0)
	    return NULL;
    }
    os = octstr_copy(buf, 0, pos);
    octstr_delete(buf, 0, pos + 1);

    return os;
}


static int remove_long(long *p, Octstr *os)
{
    long pos;
    
    pos = octstr_parse_long(p, os, 0, 10);
    if (pos == -1)
    	return -1;
    octstr_delete(os, 0, pos);
    return 0;
}


static int remove_prefix(Octstr *os, Octstr *prefix)
{
    if (octstr_ncompare(os, prefix, octstr_len(prefix)) != 0)
    	return -1;
    octstr_delete(os, 0, octstr_len(prefix));
    return 0;
}


static int parse_date(struct universaltime *ut, Octstr *os)
{
    long pos;

    pos = 0;
    
    if (remove_long(&ut->year, os) == -1 ||
        remove_prefix(os, octstr_imm("-")) == -1 ||
	remove_long(&ut->month, os) == -1 ||
        remove_prefix(os, octstr_imm("-")) == -1 ||
	remove_long(&ut->day, os) == -1 ||
        remove_prefix(os, octstr_imm(" ")) == -1 ||
	remove_long(&ut->hour, os) == -1 ||
        remove_prefix(os, octstr_imm(":")) == -1 ||
	remove_long(&ut->minute, os) == -1 ||
        remove_prefix(os, octstr_imm(":")) == -1 ||
	remove_long(&ut->second, os) == -1 ||
        remove_prefix(os, octstr_imm(" ")) == -1)
    	return -1;
    return 0;
}


int main(void)
{
    struct universaltime ut;
    Octstr *os;
    Octstr *buf;
    
    gwlib_init();
    buf = octstr_create("");
    while ((os = read_line(stdin, buf)) != NULL) {
	if (parse_date(&ut, os) == -1)
	    panic(0, "Bad line: %s", octstr_get_cstr(os));
	printf("%ld %s\n", date_convert_universal(&ut), octstr_get_cstr(os));
	octstr_destroy(os);
    }

    log_set_output_level(GW_PANIC);
    gwlib_shutdown();

    return 0;
}
