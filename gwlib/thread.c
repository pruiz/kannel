
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

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
#if HAVE_THREADS
	pthread_attr_t attr;
	int ret;
#endif
	
	if (size == 0)
		copy = arg;
	else {
		copy = malloc(size);
		if (copy == NULL) {
			error(errno, "malloc failed");
			goto error;
		}
		memcpy(copy, arg, size);
	}
	
#if HAVE_THREADS
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
	return -1;
}


void mutex_lock(pthread_mutex_t *mutex)
{
    if (pthread_mutex_lock(mutex) != 0)
	panic(errno, "Mutex failure!");
}


int mutex_try_lock(pthread_mutex_t *mutex)
{
    int ret;
    
    ret = pthread_mutex_trylock(mutex);
    if (ret == EBUSY)
	ret = -1;
    else if (ret != 0)
        panic(errno, "Mutex failure!");
    return ret;
}


void mutex_unlock(pthread_mutex_t *mutex)
{
    if (pthread_mutex_unlock(mutex) != 0)
	panic(errno, "Mutex failure!");
}


void mutex_destroy(pthread_mutex_t *mutex)
{
     if (pthread_mutex_destroy != 0)
        panic(errno, "Trying to destroy a locked mutex");
}





