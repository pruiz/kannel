/*
 * test_octstr_dump.c - reads a file and performs dumping.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"

int main(int argc, char **argv)
{
    Octstr *data, *filename;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    filename = octstr_create(argv[1]);
    data = octstr_read_file(octstr_get_cstr(filename));

    debug("",0,"Dumping file `%s':", octstr_get_cstr(filename));
    octstr_dump(data, 0);

    octstr_destroy(data);
    gwlib_shutdown();
    return 0;
}
