/*
 * gwlib.c - definition of the gwlib_init and gwlib_shutdown functions
 *
 * Lars Wirzenius
 */

#include "gwlib.h"


void gwlib_init(void) {
	gw_init_mem();
	http2_init();
}

void gwlib_shutdown(void) {
	http2_shutdown();
}
