/*
 * test_md5.c - test MD5 routine.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

int main(int argc, char **argv)
{
    Octstr *data, *enc;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    data = octstr_create(argv[1]);
    enc = md5(data);

    debug("",0,"MD5 <%s>", octstr_get_cstr(enc));

    octstr_destroy(data);
    octstr_destroy(enc);
    gwlib_shutdown();
    return 0;
}
