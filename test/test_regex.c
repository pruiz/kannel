/*
 * test_regex.c - test regex module
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/regex.h"

#if defined(HAVE_REGEX) || defined(HAVE_PCRE)

int main(int argc, char **argv)
{
    Octstr *re, *os, *sub;
    Octstr *tmp;
    regex_t *regexp;
    regmatch_t pmatch[REGEX_MAX_SUB_MATCH];
    int rc;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    os = octstr_create(argv[1]);
    re = octstr_create(argv[2]);
    sub = octstr_create(argv[3]);

    info(0, "step 1: generic functions");

    /* compile */
    if ((regexp = gw_regex_comp(re, REG_EXTENDED|REG_ICASE)) == NULL)
        panic(0, "regex compilation failed!");

    debug("regex",0,"RE: regex <%s> has %d subexpressions.",
          octstr_get_cstr(re), regexp->re_nsub);

    /* execute */
    rc = gw_regex_exec(regexp, os, REGEX_MAX_SUB_MATCH, &pmatch[0], 0);
    if (rc == REG_NOMATCH) {
        info(0, "RE: regex <%s> did not match on string <%s>.",
             octstr_get_cstr(re), octstr_get_cstr(os));
    } else if (rc != 0) {
        Octstr *err = gw_regex_error(rc, regexp);
        error(0, "RE: regex <%s> execution failed: %s",
              octstr_get_cstr(re), octstr_get_cstr(err));
        octstr_destroy(err);
    } else {
        char *rsub;
        debug("regex",0,"RE: regex <%s> matches.", octstr_get_cstr(re));
        rsub = gw_regex_sub(octstr_get_cstr(sub), octstr_get_cstr(os),
                            REGEX_MAX_SUB_MATCH, &pmatch[0]);
        debug("regex",0,"RE: substituted string is <%s>.", rsub);
        gw_free(rsub);
    }
    
    info(0, "step 2: wrapper functions");

    debug("regex",0,"RE: regex_match <%s> on <%s> did: %s",
          octstr_get_cstr(re), octstr_get_cstr(os),
          gw_regex_match(re, os) ? "match" : "NOT match");

    debug("regex",0,"RE: regex_match_pre on <%s> did: %s",
          octstr_get_cstr(os),
          gw_regex_match_pre(regexp, os) ? "match" : "NOT match");

    tmp = gw_regex_subst(re, os, sub);
    debug("regex",0,"RE: regex_subst <%s> on <%s> rule <%s>: %s",
          octstr_get_cstr(re), octstr_get_cstr(os), octstr_get_cstr(sub),
          octstr_get_cstr(tmp));
    octstr_destroy(tmp);

    tmp = gw_regex_subst_pre(regexp, os, sub);
    debug("regex",0,"RE: regex_subst_pre on <%s> rule <%s>: %s",
          octstr_get_cstr(os), octstr_get_cstr(sub), octstr_get_cstr(tmp));

    gw_regex_destroy(regexp);
    octstr_destroy(tmp);
    octstr_destroy(re);
    octstr_destroy(os);
    gwlib_shutdown();
    return 0;
}

#endif
