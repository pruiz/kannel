/*
 * test_xmlrpc.c: A simple program to test XML-RPC parsing
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */

#include <stdio.h>

#include "gwlib/xmlrpc.h"

Octstr *charset = NULL;
Octstr *file_name = NULL;

static void help (void)
{
    info(0, "Usage test_xmlrpc [option] xml_source");
    info(0, "where options are");
    info(0, "-h print this text");
    info(0, "-v level set log level for stderr logging");
}

int main(int argc, char **argv)
{
    int opt, file, have_charset, ret;
    FILE *fp;
    Octstr *output, *xml_doc;
    XMLRPCMethodCall *msg;

    gwlib_init();
    file = 0;
    have_charset = 0;
    fp = NULL;

    while ((opt = getopt(argc, argv, "hv:")) != EOF) {
        switch (opt) {
            case 'h':
                help();
                exit(1);
                break;

            case 'v':
                log_set_output_level(atoi(optarg));
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

    xml_doc = octstr_read_file(argv[optind]);
    if (xml_doc == NULL)
        panic(0, "Cannot read the XML document");

    msg = xmlrpc_call_parse(xml_doc);
    output = xmlrpc_call_octstr(msg);
    octstr_dump(output, 0);

    xmlrpc_call_destroy(msg);
    octstr_destroy(xml_doc);
    octstr_destroy(output);
    gwlib_shutdown();
    return 0;
}





