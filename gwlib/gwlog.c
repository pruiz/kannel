#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>

#include "gwlog.h"
#include "thread.h"


/*
 * List of currently open log files.
 */
#define MAX_LOGFILES 8
static struct {
	FILE *file;
	int minimum_output_level;
        char *filename;		/* to allow re-open */
} logfiles[MAX_LOGFILES];
static int num_logfiles = 0;


/* Make sure stderr is included in the list. */
static void add_stderr(void) {
	int i;
	
	for (i = 0; i < num_logfiles; ++i)
		if (logfiles[i].file == stderr)
			return;
	logfiles[num_logfiles].file = stderr;
	logfiles[num_logfiles].minimum_output_level = DEBUG;
	++num_logfiles;
}


void set_output_level(enum output_level level) {
	int i;
	
	add_stderr();
	for (i = 0; i < num_logfiles; ++i) {
		if (logfiles[i].file == stderr) {
			logfiles[i].minimum_output_level = level;
			break;
		}
	}
}

void reopen_log_files(void) {
	int i;
	
	for (i = 0; i < num_logfiles; ++i)
	    if (logfiles[i].file != stderr) {
		fclose(logfiles[i].file);
		logfiles[i].file = fopen(logfiles[i].filename, "a");
		if (logfiles[i].file == NULL) {
		    error(errno, "Couldn't re-open logfile `%s'.", logfiles[i].filename);
		}
	    }		
}

void open_logfile(char *filename, int level) {
	FILE *f;
	
	add_stderr();
	if (num_logfiles == MAX_LOGFILES) {
		error(0, "Too many log files already open, not adding `%s'",
			filename);
		return;
	}
	
	f = fopen(filename, "a");
	if (f == NULL) {
		error(errno, "Couldn't open logfile `%s'.", filename);
		return;
	}
	
	logfiles[num_logfiles].file = f;
	logfiles[num_logfiles].minimum_output_level = level;
	logfiles[num_logfiles].filename = strdup(filename);
	if (logfiles[num_logfiles].filename == NULL) {
	    error(errno, "Couldn't strdup filename");
	    fclose(f);
	}
	else {
	    ++num_logfiles;
	    info(0, "Added logfile `%s' with level `%d'.", filename, level);
	}
}


#define FORMAT_SIZE (10*1024)
static void format(char *buf, int level, int e, const char *fmt)
{
	static char *tab[] = {
		"DEBUG: ",
		"INFO: ",
		"WARNING: ",
		"ERROR: ",
		"PANIC: "
	};
	static int tab_size = sizeof(tab) / sizeof(tab[0]);
	time_t t;
	struct tm *tm;
	char *p, prefix[1024];
	
	p = prefix;
	time(&t);
	tm = gmtime(&t);
	strftime(p, sizeof(prefix), "%Y-%m-%d %H:%M:%S ", tm);

	p = strchr(p, '\0');
	sprintf(p, "[%d] ", (int) pthread_self());

	p = strchr(p, '\0');
	if (level < 0 || level >= tab_size)
		sprintf(p, "UNKNOWN: ");
	else
		sprintf(p, "%s", tab[level]);

	if (strlen(prefix) + strlen(fmt) > FORMAT_SIZE / 2) {
		sprintf(buf, "%s <OUTPUT message too long>\n", prefix);
		return;
	}

	if (e == 0)
		sprintf(buf, "%s%s\n", prefix, fmt);
	else
		sprintf(buf, "%s%s\n%sSystem error %d: %s\n",
			prefix, fmt, prefix, e, strerror(e));
}


static void output(FILE *f, char *buf, va_list args) {
	vfprintf(f, buf, args);
	fflush(f);
}


/* Almost all of the message printing functions are identical, except for
   the output level they use. This macro contains the identical parts of
   the functions so that the code needs to exist only once. It's a bit
   more awkward to edit, but that can't be helped. The "do {} while (0)"
   construct is a gimmick to be more like a function call in all syntactic
   situation. */
#define FUNCTION_GUTS(level) \
	do { \
		int i; \
		char buf[FORMAT_SIZE]; \
		va_list args; \
		add_stderr(); \
		format(buf, level, e, fmt); \
		for (i = 0; i < num_logfiles; ++i) { \
			if (level >= logfiles[i].minimum_output_level) { \
				va_start(args, fmt); \
				output(logfiles[i].file, buf, args); \
				va_end(args); \
			} \
		} \
	} while (0)

void panic(int e, const char *fmt, ...) {
	FUNCTION_GUTS(PANIC);
	exit(EXIT_FAILURE);
}


void error(int e, const char *fmt, ...) {
	FUNCTION_GUTS(ERROR);
}


void warning(int e, const char *fmt, ...) {
	FUNCTION_GUTS(WARNING);
}


void info(int e, const char *fmt, ...) {
	FUNCTION_GUTS(INFO);
}


void debug(int e, const char *fmt, ...) {
	FUNCTION_GUTS(DEBUG);
}


