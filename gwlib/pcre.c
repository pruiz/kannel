/*
 * pcre.c - Perl compatible regular expressions (PCREs) 
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

#include <ctype.h>

#include "gwlib/gwlib.h"
#include "pcre.h"

#ifdef HAVE_PCRE


/********************************************************************
 * Generic pcre functions.
 */

pcre *gw_pcre_comp_real(const Octstr *pattern, int cflags, const char *file, 
                        long line, const char *func)
{
    pcre *preg;
    const char *err;
    const char *pat;
    int erroffset;

    pat = pattern ? octstr_get_cstr(pattern) : NULL;
    if ((preg = pcre_compile(pat, cflags, &err, &erroffset, NULL)) == NULL) {
        error(0, "%s:%ld: %s: pcre compilation `%s' failed at offset %d: %s "
                 "(Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(pattern), 
              erroffset, err, (file), (long) (line), (func));
    }

    return preg;
}


int gw_pcre_exec_real(const pcre *preg, const Octstr *string, int start, 
                      int eflags, int *ovector, int oveccount, 
                      const char *file, long line, const char *func)
{
    int rc;
    char *sub;

    gw_assert(preg != NULL);

    sub = string ? octstr_get_cstr(string) : NULL;
    rc = pcre_exec(preg, NULL, sub,  octstr_len(string), start, eflags,
                   ovector, oveccount);

    if (rc < 0 && rc != PCRE_ERROR_NOMATCH) {
        error(0, "%s:%ld: %s: pcre execution on `%s' failed with error %d "
                 "(Called from %s:%ld:%s.)",
              __FILE__, (long) __LINE__, __func__, octstr_get_cstr(string), rc,
              (file), (long) (line), (func));
    }

    return rc;
}


/********************************************************************
 * Matching wrapper functions.
 *
 * Beware that the regex compilation takes the most significant CPU time,
 * so always try to have pre-compiled regular expressions that keep being
 * reused and re-matched on variable string patterns.
 */

int gw_pcre_match_real(const Octstr *re, const Octstr *os, const char *file, 
                       long line, const char *func)
{
    pcre *regexp;
    int rc;
    int ovector[PCRE_OVECCOUNT];

    /* compile */
    regexp = gw_pcre_comp_real(re, 0, file, line, func);
    if (regexp == NULL)
        return 0;

    /* execute and match */
    rc = gw_pcre_exec_real(regexp, os, 0, 0, ovector, PCRE_OVECCOUNT,
                           file, line, func);

    return (rc > 0) ? 1 : 0;
}


int gw_pcre_match_pre_real(const pcre *preg, const Octstr *os, const char *file, 
                           long line, const char *func)
{
    int rc;
    int ovector[PCRE_OVECCOUNT];

    gw_assert(preg != NULL);

    /* execute and match */
    rc = gw_pcre_exec_real(preg, os, 0, 0, ovector, PCRE_OVECCOUNT,
                           file, line, func);

    return (rc > 0) ? 1 : 0;
}


#endif  /* HAVE_PCRE */

