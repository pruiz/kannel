/*
 * regex.c - POSIX regular expressions (REs) 
 *
 * This modules implements wrapper functions to regcomp(3), regexec(3),
 * et all functions from the POSIX compliance standard. Additinally
 * it provides subexpression substitution routines in order to easily
 * substitute strings arround regular expressions.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include "gwlib/gwlib.h"

void gw_regex_destroy(regex_t *preg)
{
    gw_assert(preg != NULL);

    regfree(preg);
}


regex_t *gw_regex_comp(const Octstr *pattern, int cflags)
{
    int rc;
    regex_t *preg;
    
    preg = gw_malloc(sizeof(regex_t));

    if ((rc = regcomp(preg, pattern ? octstr_get_cstr(pattern) : NULL, cflags)) != 0) {
        char buffer[512];
        regerror(rc, preg, buffer, sizeof(buffer)); 
        error(0, "RE: regex compilation <%s> failed: %s",
              octstr_get_cstr(pattern), buffer);
        return NULL;
    }

    return preg;
}


int gw_regex_exec(const regex_t *preg, const Octstr *string, size_t nmatch, 
                  regmatch_t pmatch[], int eflags)
{
    gw_assert(preg != NULL);

    return regexec(preg, string ? octstr_get_cstr(string) : NULL, 
                   nmatch, pmatch, eflags);
}


Octstr *gw_regex_error(int errcode, const regex_t *preg)
{
    char errbuf[512];
    Octstr *os;

    regerror(errcode, preg, errbuf, sizeof(errbuf));
    os = octstr_create(errbuf);

    return os;
}


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
                   size_t nmatch, regmatch_t pmatch[])
{
    const char *src = input;
    char *dest, *dst;
    char c;
    size_t no;
    int len;

    if (!source)
        return NULL;
    if (!nmatch)
        return src;

    /* First pass, find the size */
    len = 0;
    while ((c = *src++) != '\0') {
        if (c == '&')
            no = 0;
        else if (c == '$' && isdigit(*src))
            no = *src++ - '0';
        else
            no = 10;

        if (no > 9) {           /* Ordinary character. */
            if (c == '\\' && (*src == '$' || *src == '&'))
                c = *src++;
            len++;
        }
        else if (no < nmatch && pmatch[no].rm_so < pmatch[no].rm_eo) {
            len += pmatch[no].rm_eo - pmatch[no].rm_so;
        }
    }

    dest = dst = gw_malloc(len + 1);

    /* Now actually fill in the string */
    src = input;
    while ((c = *src++) != '\0') {
        if (c == '&')
            no = 0;
        else if (c == '$' && isdigit(*src))
            no = *src++ - '0';
        else
            no = 10;

        if (no > 9) {           /* Ordinary character. */
            if (c == '\\' && (*src == '$' || *src == '&'))
                c = *src++;
            *dst++ = c;
        }
        else if (no < nmatch && pmatch[no].rm_so < pmatch[no].rm_eo) {
            len = pmatch[no].rm_eo - pmatch[no].rm_so;
            memcpy(dst, source + pmatch[no].rm_so, len);
            dst += len;
        }
    }
    *dst = '\0';

    return dest;
}

