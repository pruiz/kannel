/*
 * test_http.c - a simple program to test the http library
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"

static void help(void) {
	info(0, "Usage: test_http [-r repeats] url ...\n"
		"where -r means the number of times the fetches should be\n"
		"repeated.");
}

int main(int argc, char **argv) {
	int i, opt, ret, source;
	Octstr *os;
	char *type, *data;
	size_t size;
	long repeats;
	
	gw_init_mem();

	repeats = 1;
	source = 0;

	while ((opt = getopt(argc, argv, "hr:s")) != EOF) {
		switch (opt) {
		case 'r':
			repeats = atoi(optarg);
			break;

		case 's':
			source = 1;
			break;

		case 'h':
			help();
			exit(0);
		
		case '?':
		default:
			error(0, "Invalid option %c", opt);
			help();
			panic(0, "Stopping.");
		}
	}
	
	if (source)
		set_output_level(PANIC);

	while (repeats-- > 0) {
		for (i = optind; i < argc; ++i) {
			ret = http_get(argv[i], &type, &data, &size);
			if (ret != 0)
				panic(0, "http_get failed");
			if (source) {
				printf("%.*s", (int) size, data);
			} else {
				debug("", 0, "Fetched %s (%s):", 
					argv[i], type);
				os = octstr_create_from_data(data, size);
				octstr_dump(os, 0);
				octstr_destroy(os);
			}
			gw_free(data);
			gw_free(type);
		}
	}
	
	gw_check_leaks();
	
	return 0;
}
