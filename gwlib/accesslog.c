/*
 * accesslog.c - implement access logging functions
 *
 * see accesslog.h.
 *
 * Kalle Marjola 2000 for Project Kannel
 */


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include "gwlib.h"

static FILE *file = NULL;
static char filename[FILENAME_MAX + 1]; /* to allow re-open */

static int use_localtime;

/*
 * Reopen/rotate lock.
 */
static List *writers = NULL;

void alog_reopen(void)
{
    if (file == NULL)
	return;

    alog("Log ends");

    list_lock(writers);
    /* wait for writers to complete */
    list_consume(writers);

    fclose(file);
    file = fopen(filename, "a");

    list_unlock(writers);

    if (file == NULL) {
	error(errno, "Couldn't re-open access logfile `%s'.",
	      filename);
    } else
	alog("Log begins");
}


void alog_close(void)
{

    if (file != NULL) {
	alog("Log ends");
        list_lock(writers);
        /* wait for writers to complete */
        list_consume(writers);
	fclose(file);
	file = NULL;
        list_unlock(writers);
        list_destroy(writers, NULL);
        writers = NULL;
    }
}


void alog_open(char *fname, int use_localtm)
{
    FILE *f;
    
    if (file != NULL) {
	warning(0, "Opening an already opened access log");
	alog_close();
    }
    if (strlen(fname) > FILENAME_MAX) {
	error(0, "Access Log filename too long: `%s', cannot open.", fname);
	return;
    }

    if (writers == NULL)
        writers = list_create();

    f = fopen(fname, "a");
    if (f == NULL) {
	error(errno, "Couldn't open logfile `%s'.", fname);
	return;
    }
    file = f;
    strcpy(filename, fname);
    info(0, "Started access logfile `%s'.", filename);
    use_localtime = use_localtm;
    alog("Log begins");
}


void alog_use_localtime(void)
{
    use_localtime = 1;
}

void alog_use_gmtime(void)
{
    use_localtime = 0;
}

#define FORMAT_SIZE (10*1024)
static void format(char *buf, const char *fmt)
{
    time_t t;
    struct tm tm;
    char *p, prefix[1024];
	
    p = prefix;
    time(&t);
    if (use_localtime)
	tm = gw_localtime(t);
    else
	tm = gw_gmtime(t);

    sprintf(p, "%04d-%02d-%02d %02d:%02d:%02d ",
	    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);

    if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
	sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
	return;
    }
    sprintf(buf, "%s%s\n", prefix, fmt);
}

/* XXX should we also log automatically into main log, too? */

void alog(const char *fmt, ...)
{
    char *buf;
    va_list args;

    if (file == NULL)
	return;

    buf = gw_malloc(FORMAT_SIZE + 1);
    format(buf, fmt);
    va_start(args, fmt);

    list_lock(writers);
    list_add_producer(writers);
    list_unlock(writers);

    vfprintf(file, buf, args);
    fflush(file);

    list_remove_producer(writers);

    va_end(args);
    gw_free(buf);
}
