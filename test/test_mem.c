/*
 * test_mem.c - test memory allocation functions.
 *
 * Lars Wirzenius
 */


#include "gwlib/gwlib.h"

int main(void) {
	void *p;
	
	gw_init_mem();

	p = gw_malloc(100);
	gw_free(p);
	gw_check_leaks();

	p = gw_malloc(100);
	gw_check_leaks();

	return 0;
}
