/*
 * check_counter.c - Check that Counter objects work
 *
 * This is a test program for checking Counter objects. It creates some
 * threads that get many counts and check that they are increasing.
 */

#include <limits.h>

#ifndef THREADS
#define THREADS 16
#endif

#ifndef PER_THREAD
#define PER_THREAD (1000)
#endif

#include "gwlib/gwlib.h"

static void check(void *arg) {
	Counter *c;
	long i, this, prev;
	
	c = arg;
	prev = -1;
	for (i = 0; i < PER_THREAD; ++i) {
		this = counter_increase(c);
		if (this < 0)
			panic(0, "counter returned negative");
		if (this < prev)
			panic(0, "counter returned smaller than previous");
		prev = this;
	}
}


int main(void) {
	Counter *c;
	long threads[THREADS];
	long i;
	
	gwlib_init();
	log_set_output_level(GW_INFO);
	c = counter_create();
	for (i = 0; i < THREADS; ++i)
		threads[i] = gwthread_create(check, c);
	for (i = 0; i < THREADS; ++i)
		gwthread_join(threads[i]);
	
	return 0;
}
