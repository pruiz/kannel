/*
 * test_counter.c - test Counter objects
 *
 * This is a test program for testing Counter objects. It creates some
 * threads that get many counts and check that they are increasing.
 *
 * Lars Wirzenius
 */

#include <limits.h>

#ifndef TRACE
#define TRACE (100*1000)
#endif

#ifndef THREADS
#define THREADS 16
#endif

#ifndef PER_THREAD
#define PER_THREAD (1000*1000)
#endif

#include "gwlib/gwlib.h"

static void *check(void *arg) {
	Counter *c;
	long i, this, prev;
	
	c = arg;
	prev = -1;
	for (i = 0; i < PER_THREAD; ++i) {
		this = counter_get(c);
#if TRACE
		if ((i % TRACE) == 0)
			info(0, "%ld returned %ld, prev is %ld", 
				i, this, prev);
#endif
		if (this < 0)
			panic(0, "counter returned negative");
		if (this < prev)
			panic(0, "counter returned smaller than previous");
		prev = this;
	}
	
	return NULL;
}


int main(void) {
	Counter *c;
	pthread_t threads[THREADS];
	long i;
	void *ret;
	
	gw_init_mem();
	info(0, "%ld threads, %ld counts each", (long) THREADS, 
		(long) PER_THREAD);
	c = counter_create();
	for (i = 0; i < THREADS; ++i)
		threads[i] = start_thread(0, check, c, 0);
	for (i = 0; i < THREADS; ++i)
		if (pthread_join(threads[i], &ret) != 0)
			panic(0, "pthread_join failed");
	
	return 0;
}
