/*
 * test_urltrans.c - a simple program to test the URL translation module
 *
 * Lars Wirzenius <liw@wapit.com>
 */

#include <stdlib.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "gw/urltrans.h"

static void help(void) {
	info(0, "Usage: test_urltrans [-r repeats] foo.smsconf pattern ...\n"
		"where -r means the number of times the test should be\n"
		"repeated.");
}

int main(int argc, char **argv) {
	int i, opt;
	Octstr *url;
	long repeats;
	URLTranslationList *list;
	URLTranslation *t;
	Config *cfg;
	
	gw_init_mem();

	repeats = 1;

	while ((opt = getopt(argc, argv, "hr:")) != EOF) {
		switch (opt) {
		case 'r':
			repeats = atoi(optarg);
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

	if (optind + 1 >= argc) {
		error(0, "Missing arguments.");
		help();
		panic(0, "Stopping.");
	}
	cfg = config_create(argv[optind]);
	if (config_read(cfg) == -1)
		panic(0, "Couldn't read configuration file.");
	
	list = urltrans_create();
	if (urltrans_add_cfg(list, cfg) == -1)
		panic(0, "Error parsing configuration.");

	while (repeats-- > 0) {
		for (i = optind + 1; i < argc; ++i) {
			url = octstr_create(argv[i]);
			t = urltrans_find(list, url);
			info(0, "type = %d", urltrans_type(t));
			octstr_destroy(url);
		}
	}
	urltrans_destroy(list);
	config_destroy(cfg);
	
	gw_check_leaks();
	
	return 0;
}
