/*
 * test_regex.c - test regex module
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

int main(int argc, char **argv)
{
    Octstr *re, *os, *sub;
    regex_t *regexp;
    regmatch_t pmatch[REGEX_MAX_SUB_MATCH];
    int rc;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    os = octstr_create(argv[1]);
    re = octstr_create(argv[2]);
    sub = octstr_create(argv[3]);

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
    }

    gw_regex_destroy(regexp);
    octstr_destroy(re);
    octstr_destroy(os);
    gwlib_shutdown();
    return 0;
}
