/*
 * test_pap.c: A simple program to test pap compiler
 *
 * By Aarno Syvänen for Wiral Ltd
 */

#include <stdio.h>

#include "gwlib/gwlib.h"
#include "gw/wap_push_pap_compiler.h"
#include "wap/wap_events.h"

static void help (void)
{
    info(0, "Usage test_pap [option] pap_source");
    info(0, "where options are");
    info(0, "-h print this text");
    info(0, "-v level set log level for stderr logging");
    info(0, "-l log wap event to this file");
}

int main(int argc, char **argv)
{
    int opt,
        ret;
    Octstr *pap_doc,
           *log_file;
    WAPEvent *e;

    log_file = NULL;
    gwlib_init();
    
    while ((opt = getopt(argc, argv, "h:v:l:")) != EOF) {
        switch (opt) {
        case 'h':
	    help();
            exit(1);
	break;

        case 'v':
	    log_set_output_level(atoi(optarg));
	break;

        case 'l':
	    octstr_destroy(log_file);
	    log_file = octstr_create(optarg);
	break;

        case '?':
        default:
	    error(0, "Invalid option %c", opt);
            help();
            panic(0, "Stopping");
	break;
        }
    }

    if (optind >= argc) {
        error(0, "Missing arguments");
        help();
        panic(0, "Stopping");
    }

    if (log_file != NULL) {
    	log_open(octstr_get_cstr(log_file), GW_DEBUG, GW_NON_EXCL);
	octstr_destroy(log_file);
    }

    pap_doc = octstr_read_file(argv[optind]);
    if (pap_doc == NULL)
        panic(0, "Cannot read the pap document");

    e = NULL;
    ret = pap_compile(pap_doc, &e);
    
    if (ret < 0) {
        debug("test.pap", 0, "Unable to compile the pap document"); 
        return 1;           
    } 

    debug("test.pap", 0, "Compiling successfull, wap event being:\n");
    wap_event_dump(e);

    wap_event_destroy(e);
    octstr_destroy(pap_doc);
    gwlib_shutdown();
    return 0;
}





