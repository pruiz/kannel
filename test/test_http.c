/*
 * test_http.c - a simple program to test the http library
 *
 * Lars Wirzenius <liw@wapit.com>
 */


#include "gwlib/gwlib.h"

int main(int argc, char **argv) {
	int i, ret;
	Octstr *os;
	char *type, *data;
	size_t size;
	
	for (i = 1; i < argc; ++i) {
		ret = http_get(argv[i], &type, &data, &size);
		if (ret != 0)
			panic(0, "http_get failed");
		debug("", 0, "Fetched %s (%s):", argv[i], type);
		os = octstr_create_from_data(data, size);
		octstr_dump(os, 0);
	}
	
	return 0;
}
