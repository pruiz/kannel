/*
 * test_mime_multipart.c - test MIME multipart convertion routines.
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "gwlib/gwlib.h"
#include "gwlib/mime.h"

static void help(void) 
{
    info(0, "Usage: test_mime_multipart [options] mime-encoded-file ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-n number");
    info(0, "    perform opertion n times");
}

int main(int argc, char **argv)
{
    Octstr *filename = NULL;
    unsigned long num = 1, j;
    int opt;
    Octstr *mime, *mime2;
    MIMEEntity *m;

    gwlib_init();
        
    while ((opt = getopt(argc, argv, "hv:n:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;
            case 'n':
                num = atoi(optarg);
                break;
            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }
    
    if (optind == argc) {
        help();
        exit(0);
    }

    filename = octstr_create(argv[argc-1]);
    mime = octstr_read_file(octstr_get_cstr(filename));

    for (j = 1; j <= num; j++) {

    info(0,"MIME Octstr from file `%s':", octstr_get_cstr(filename));
    octstr_dump(mime, 0);

    m = mime_octstr_to_entity(mime);

    mime_entity_dump(m);

    mime2 = mime_entity_to_octstr(m);

    info(0, "MIME Octstr after reconstruction:");
    octstr_dump(mime2, 0);

    if (octstr_compare(mime, mime2) != 0) {
        error(0, "MIME content from file `%s' and reconstruction differs!", 
              octstr_get_cstr(filename));
    } else {
        info(0, "MIME Octstr compare result has been successfull.");
    }

    octstr_destroy(mime2);
    mime_entity_destroy(m);

    } /* num times */

    octstr_destroy(filename);
 
    gwlib_shutdown();

    return 0;
}

