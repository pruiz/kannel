/*
 * test_boxc.c - test boxc connection module of bearerbox
 *
 * Stipe Tolj <tolj@wapme-systems.de>
 */
             
#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "gw/shared.h"

static void help(void)
{
    info(0, "Usage: test_boxc [options] ...");
    info(0, "where options are:");
    info(0, "-v number");
    info(0, "    set log level for stderr logging");
    info(0, "-h hostname");
    info(0, "    hostname where bearerbox is running (default: localhost)");
    info(0, "-p number");
    info(0, "    port for smsbox connections on bearerbox host (default: 13001)");
    info(0, "-c number");
    info(0, "    numer of sequential connections that are made and closed (default: 1)");
}

/* global variables */
static unsigned long port = 13001;
static  unsigned int no_conn = 1;
static Octstr *host;

static void run_connects(void)
{
    unsigned int i;
    Msg *msg;

    for (i = 1; i <= no_conn; i++) {

        /* connect to Kannel's bearerbox */
        connect_to_bearerbox(host, port, 0, NULL);

        /* identify ourself to bearerbox */
        msg = msg_create(admin);
        msg->admin.command = cmd_identify;
        msg->admin.boxc_id = octstr_create("test-smsbox");
        write_to_bearerbox(msg);

        /* do something, like passing MT messages */

        /* close connection and shutdown */
        close_connection_to_bearerbox();
    }
}

int main(int argc, char **argv)
{
    int opt;

    gwlib_init();

    host = octstr_create("localhost");

    while ((opt = getopt(argc, argv, "v:h:p:c:")) != EOF) {
        switch (opt) {
            case 'v':
                log_set_output_level(atoi(optarg));
                break;

            case 'h':
                octstr_destroy(host);
                host = octstr_create(optarg);
                break;

            case 'p':
                port = atoi(optarg);
                break;

            case 'c':
                no_conn = atoi(optarg);
                break;

            case '?':
            default:
                error(0, "Invalid option %c", opt);
                help();
                panic(0, "Stopping.");
        }
    }

    if (!optind) {
        help();
        exit(0);
    }

    run_connects();

    octstr_destroy(host);

    gwlib_shutdown();

    return 0;
}

