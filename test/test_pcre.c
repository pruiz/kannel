/*
 * test_pcre.c - test pcre module
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/pcre.h"

int main(int argc, char **argv)
{
    Octstr *re, *os;
    pcre *regexp;
    int ovector[PCRE_OVECCOUNT];
    int rc;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    os = octstr_create(argv[1]);
    re = octstr_create(argv[2]);

    info(0, "step 1: generic functions");

    /* compile */
    if ((regexp = gw_pcre_comp(re, 0)) == NULL)
        panic(0, "pcre compilation failed!");

    /* execute */
    rc = gw_pcre_exec(regexp, os, 0, 0, ovector, PCRE_OVECCOUNT);
    if (rc == PCRE_ERROR_NOMATCH) {
        info(0, "RE: pcre <%s> did not match on string <%s>.",
             octstr_get_cstr(re), octstr_get_cstr(os));
    } else if (rc < 0) {
        error(0, "RE: pcre <%s> execution failed with error %d.",
              octstr_get_cstr(re), rc);
    } else {
        info(0, "RE: pcre <%s> matches.", octstr_get_cstr(re));
    }
    
    info(0, "step 2: wrapper functions");

    debug("pcre",0,"RE: pcre_match <%s> on <%s> did: %s",
          octstr_get_cstr(re), octstr_get_cstr(os),
          gw_pcre_match(re, os) ? "match" : "NOT match");

    debug("pcre",0,"RE: pcre_match_pre on <%s> did: %s",
          octstr_get_cstr(os),
          gw_pcre_match_pre(regexp, os) ? "match" : "NOT match");

    octstr_destroy(re);
    octstr_destroy(os);
    gwlib_shutdown();
    return 0;
}
