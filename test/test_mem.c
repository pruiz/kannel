/*
 * test_mem.c - test memory allocation functions.
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"

int main(void) {
	void *p;
	long i;
	
	gw_init_mem();

	p = gw_malloc(100);
	gw_free(p);
	gw_check_leaks();

	p = gw_malloc(100);
	gw_check_leaks();
	gw_free(p);
	gw_check_leaks();

	for (i = 0; i < 1000; ++i) {
		p = gw_malloc(100);
		debug("", 0, "i = %ld", i);
	}

	return 0;
}
