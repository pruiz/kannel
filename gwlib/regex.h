/*
 * regex.h - POSIX regular expressions (REs) 
 *
 * This modules implements wrapper functions to regcomp(3), regexec(3),
 * et all functions from the POSIX compliance standard. Additinally
 * it provides subexpression substitution routines in order to easily
 * substitute strings arround regular expressions.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#ifndef REGEX_H
#define REGEX_H

#include <regex.h>


/*
 * We handle a maximum of 10 subexpression matches and 
 * substitution escape codes $0 to $9.
 */
#define REGEX_MAX_SUB_MATCH 10


/*
 * Destroy a previously compiled regular expression.
 */
void gw_regex_destroy(regex_t *preg);


/*
 * Compile a regular expression provided by pattern and return
 * the regular expression type as function result.
 * If the compilation fails, return NULL.
 */
regex_t *gw_regex_comp(const Octstr *pattern, int cflags);


/*
 * Execute a previously compile regular expression on a given
 * string and provide the matches via nmatch and pmatch[].
 */
int gw_regex_exec(const regex_t *preg, const Octstr *string, size_t nmatch, 
                  regmatch_t pmatch[], int eflags);


Octstr *gw_regex_error(int errcode, const regex_t *preg);

/* This function substitutes for $0-$9, filling in regular expression
 * submatches. Pass it the same nmatch and pmatch arguments that you
 * passed gw_regexec(). pmatch should not be greater than the maximum number
 * of subexpressions - i.e. one more than the re_nsub member of regex_t.
 *
 * input should be the string with the $-expressions, source should be the
 * string that was matched against.
 *
 * It returns the substituted string, or NULL on error.
 *
 * Parts of this code are based on Henry Spencer's regsub(), from his
 * AT&T V8 regexp package. Function borrowed by apache-1.3/src/main/util.c
 */
char *gw_regex_sub(const char *input, const char *source,
                   size_t nmatch, regmatch_t pmatch[]);


#endif

