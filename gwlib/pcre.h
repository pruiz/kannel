/*
 * pcre.h - Perl compatible regular expressions (PCREs) 
 *
 * This modules implements wrapper functions to the pcre_foobar() et all
 * functions implemented in the libpcre.a library.
 * PCRE is a library of functions to support regular expressions whose syntax
 * and semantics are as close as possible to those of the Perl 5 language.
 *
 * See http://www.pcre.org/ for more details on PCRE regular expressions.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#ifndef PCRE_H
#define PCRE_H

#ifdef HAVE_PCRE

#include <pcre.h>


#define PCRE_OVECCOUNT 30    /* should be a multiple of 3 */

/*
 * Compile a regular expression provided by pattern and return
 * the regular expression type as function result.
 * If the compilation fails, return NULL.
 */
pcre *gw_pcre_comp_real(const Octstr *pattern, int cflags, const char *file, 
                        long line, const char *func);
#define gw_pcre_comp(pattern, cflags) \
    gw_pcre_comp_real(pattern, cflags, __FILE__, __LINE__, __func__)


/*
 * Execute a previously compile regular expression on a given
 * string and provide the matches via nmatch and pmatch[].
 */
int gw_pcre_exec_real(const pcre *preg, const Octstr *string, int start, 
                      int eflags, int *ovector, int oveccount, 
                      const char *file, long line, const char *func);
#define gw_pcre_exec(preg, string, start, eflags, ovector, oveccount) \
    gw_pcre_exec_real(preg, string, start, eflags, ovector, oveccount, \
                      __FILE__, __LINE__, __func__)


/*
 * Match directly a given regular expression and a source string. This assumes
 * that the RE has not been pre-compiled and hence perform the compile and 
 * exec step in this matching step.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_pcre_match_real(const Octstr *re, const Octstr *os, const char *file, 
                       long line, const char *func);
#define gw_pcre_match(re, os) \
    gw_pcre_match_real(re, os, __FILE__, __LINE__, __func__)


/*
 * Match directly a given source string against a previously pre-compiled
 * regular expression.
 * Return 1 if the regular expression is successfully matching, 0 otherwise.
 */
int gw_pcre_match_pre_real(const pcre *preg, const Octstr *os, const char *file, 
                           long line, const char *func);
#define gw_pcre_match_pre(preg, os) \
    gw_pcre_match_pre_real(preg, os, __FILE__, __LINE__, __func__)


#endif  /* HAVE_PCRE */
#endif  /* PCRE_H */


