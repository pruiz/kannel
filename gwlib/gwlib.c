/*
 * gwlib.c - definition of the gwlib_init and gwlib_shutdown functions
 *
 * Lars Wirzenius
 */

#include "gwlib.h"


/*
 * Has gwlib been initialized?
 */
static int init = 0;


void gwlib_assert_init(void) {
	gw_assert(init != 0);
}


void gwlib_init(void) {
	gw_assert(!init);
	gw_init_mem();
	gwthread_init();
	http2_init();
	socket_init();
	init = 1;
}

void gwlib_shutdown(void) {
	gwlib_assert_init();
	http2_shutdown();
	socket_shutdown();
	gwthread_shutdown();
	gw_check_leaks();
	init = 0;
}
