
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "config.h"
#include "gwmem.h"
#include "log.h"
#include "thread.h"



/*
 * Start a new thread, running function func, and giving it the argument
 * `arg'. If `size' is 0, `arg' is given as is; otherwise, `arg' is copied
 * into a memory area of size `size'.
 * 
 * If `detached' is non-zero, the thread is created detached, otherwise
 * it is created detached.
 */
pthread_t start_thread(int detached, Threadfunc *func, void *arg, size_t size)
{
	void *copy;
	pthread_t id;
#if HAVE_PTHREAD_H
	pthread_attr_t attr;
	int ret;
#endif
	
	if (size == 0)
		copy = arg;
	else {
		copy = gw_malloc(size);
		memcpy(copy, arg, size);
	}
	
#if HAVE_PTHREAD_H
	pthread_attr_init(&attr);
	if (detached)
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	do {
		ret = pthread_create(&id, &attr, func, copy);
		if (ret == EAGAIN) {
			error(0, "Too many threads, waiting to create one...");
			sleep(1);
		}
	} while (ret == EAGAIN);
	pthread_attr_destroy(&attr);
	if (ret != 0) {
		error(errno, "pthread_create failed");
		goto error;
	}
#else
	id = 0;
	func(copy);
#endif

	return id;

error:
	return (pthread_t) -1;
}


Mutex *mutex_create(void) {
	Mutex *mutex;
	
	mutex = gw_malloc(sizeof(Mutex));
	pthread_mutex_init(&mutex->mutex, NULL);
	mutex->owner = (pthread_t) -1;
	return mutex;
}


void mutex_destroy(Mutex *mutex) {
	pthread_mutex_destroy(&mutex->mutex);
	gw_free(mutex);
}


void mutex_lock(Mutex *mutex)
{
    if (pthread_mutex_lock(&mutex->mutex) != 0)
	panic(errno, "mutex_lock: Mutex failure!");
    mutex->owner = pthread_self();
}


int mutex_try_lock(Mutex *mutex)
{
    int ret;
    
    /* Let's try to lock it. */
    ret = pthread_mutex_trylock(&mutex->mutex);

    if (ret == EBUSY)
	ret = -1;	/* Oops, didn't succeed, someone else locked it. */
    else if (ret != 0) {
	/* Oops, some real error occured. The only known case where this
	   happens is when a mutex object is initialized badly or not
	   at all. In that case, we're stupid and don't deserve to live. */
        panic(errno, "mutex_try_lock: Mutex failure!");
    } else if (pthread_equal(mutex->owner, pthread_self())) {
	/* The locking succeeded, but the thread system may allow the locking
	   thread to lock it a second time (Linux seems to do this, even
	   though the documentation says it shouldn't). We don't want that.
	   Thus, we keep track of the thread that locked the the mutex and
	   if we succeed in locking and the locker is us, then we unlock
	   things and pretend we never succeeded in the first place. The
	   unlocking is needed because the number of _lock or _trylock
	   operations and the number of _unlock operations must match. */
        ret = -1;
	if (pthread_mutex_unlock(&mutex->mutex) != 0)
	    panic(errno, "pthread_mutex_unlock failed");
    } else {
	/* Hey, it's ours! Let's remember that... */
        mutex->owner = pthread_self();
    }
    return ret;
}


void mutex_unlock(Mutex *mutex)
{
    mutex->owner = (pthread_t) -1;
    if (pthread_mutex_unlock(&mutex->mutex) != 0)
	panic(errno, "mutex_unlock: Mutex failure!");
}
