/* udpfeed.c - blindly send UDP packets to a certain port
 *
 * This little tool reads a bunch of files and sends each of them
 * to a given port as a single UDP packets.  It's useful for running
 * sets of test packets to see if any of them will crash the gateway.
 * By default, it sends them at one-second intervals.
 */

#include <unistd.h>

#include "gwlib/gwlib.h"

#define UDP_MAXIMUM (65535 - 40)

static unsigned char usage[] = "\
Usage: udpfeed [options] files...\n\
\n\
where options are:\n\
\n\
-h		help\n\
-g hostname	name of IP number of host to send to (default: localhost)\n\
-p port		port number to send to (default: 9200)\n\
-i interval	delay between packers (default: 1.0 seconds)\n\
\n\
Each file will be sent as a single packet.\n\
";

static Octstr *hostname;
static int port = 9200;  /* By default, the sessionless WSP port */
static double interval = 1.0;  /* Default interval (seconds) between packets */
static long maxsize = UDP_MAXIMUM;  /* Maximum packet size in octets */

static void help(void) {
	info(0, "\n%s", usage);
}

static void send_file(int udpsock, char *filename, Octstr *address) {
	Octstr *contents;

	contents = octstr_read_file(filename);
	if (contents == NULL) {
		info(0, "Skipping \"%s\".", filename);
		return;
	}

	info(0, "Sending \"%s\", %ld octets.", filename, octstr_len(contents));

	if (octstr_len(contents) > maxsize) {
		octstr_truncate(contents, maxsize);
		warning(0, "Truncating to %ld octets.", maxsize);
	}

	udp_sendto(udpsock, contents, address);

	octstr_destroy(contents);
}

int main(int argc, char **argv) {
	int opt;
	Octstr *address;
	int udpsock;

	gwlib_init();

	/* Set defaults that can't be set statically */
	hostname = octstr_create("localhost");

	while ((opt = getopt(argc, argv, "hg:p:i:m:")) != EOF) {
		switch(opt) {
		case 'g':
			octstr_destroy(hostname);
			hostname = octstr_create(optarg);
			break;

		case 'p':
			port = atoi(optarg);
			break;

		case 'i':
			interval = atof(optarg);
			break;

		case 'm':
			maxsize = atol(optarg);
			if (maxsize > UDP_MAXIMUM) {
				maxsize = UDP_MAXIMUM;
				warning(0, "-m: truncated to UDP maximum of"
					"%ld bytes.", maxsize);
			}
			break;

		case 'h':
			help();
			exit(0);
			break;

		case '?':
		default:
			error(0, "Unknown option '%c'", opt);
			help();
			exit(1);
			break;
		}
	}

	address = udp_create_address(hostname, port);
	udpsock = udp_client_socket();
	if (udpsock < 0)
		exit(1);

	for ( ; optind < argc; optind++) {
		send_file(udpsock, argv[optind], address);
		if (interval > 0 && optind + 1 < argc)
			gwthread_sleep(interval);
	}

	octstr_destroy(address);
	octstr_destroy(hostname);
	gwlib_shutdown();
    	return 0;
}
