/*
 * protected.c - thread-safe versions of standard library functions
 *
 * Lars Wirzenius
 */


#include "gwlib.h"


enum {
	LOCALTIME,
	GMTIME,
	RAND,
	NUM_LOCKS
};


static Mutex *locks[NUM_LOCKS];


static void lock(int which) {
	mutex_lock(locks[which]);
}


static void unlock(int which) {
	mutex_unlock(locks[which]);
}


void gwlib_protected_init(void) {
	int i;

	for (i = 0; i < NUM_LOCKS; ++i)
		locks[i] = mutex_create();
}


void gwlib_protected_shutdown(void) {
	int i;

	for (i = 0; i < NUM_LOCKS; ++i)
		mutex_destroy(locks[i]);
}


struct tm gw_localtime(time_t t) {
	struct tm tm;

	lock(LOCALTIME);
	tm = *localtime(&t);
	unlock(LOCALTIME);
	return tm;
}


struct tm gw_gmtime(time_t t) {
	struct tm tm;

	lock(GMTIME);
	tm = *gmtime(&t);
	unlock(GMTIME);
	return tm;
}


int gw_rand(void) {
	int ret;
	
	lock(RAND);
	ret = rand();
	unlock(RAND);
	return ret;
}
