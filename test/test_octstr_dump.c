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
    Octstr *data, *filename, *hex;

    gwlib_init();

    get_and_set_debugs(argc, argv, NULL);

    filename = octstr_create(argv[1]);
    data = octstr_read_file(octstr_get_cstr(filename));

    /* 
     * We test if this is a text/plain file with hex values in it.
     * Therefore copy the data and trail off any CR and LF from 
     * beginning and end and test if the result is only hex chars.
     * If yes, then convert to binary before dumping.
     */
    hex = octstr_duplicate(data);
    octstr_strip_crlfs(hex);
    if (octstr_is_all_hex(hex)) {
        debug("",0,"Trying to converting from hex to binary.");
        if (octstr_hex_to_binary(hex) == 0) {
            debug("",0,"Convertion was successfull.");
            octstr_destroy(data);
            data = octstr_duplicate(hex);
        } else {
            debug("",0,"Failed to convert from hex?!");
        }
    }                                      

    debug("",0,"Dumping file `%s':", octstr_get_cstr(filename));
    octstr_dump(data, 0);

    octstr_destroy(data);
    octstr_destroy(hex);
    gwlib_shutdown();
    return 0;
}
