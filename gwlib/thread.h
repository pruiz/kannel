/*
 * thread.h - thread manipulation
 */

#ifndef _GW_THREAD_H
#define _GW_THREAD_H



#if HAVE_PTHREAD_H
#include <pthread.h>
#else
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_attr_t;
#define pthread_self() (0)
#endif

/*
 * Type of function for threads. See pthread.h.
 */
typedef void *Threadfunc(void *arg);


/*
 * Wrapper around pthread_mutex_t to avoid problems with recursive calls
 * to pthread_mutex_trylock on Linux (at least).
 */
typedef struct {
	pthread_mutex_t mutex;
	pthread_t owner;
} Mutex;


/*
 * Start a new thread, running function func, and giving it the argument
 * `arg'. If `size' is 0, `arg' is given as is; otherwise, `arg' is copied
 * into a memory area of size `size'.
 * 
 * If `detached' is non-zero, the thread is created detached, otherwise
 * it is created detached.
 */
pthread_t start_thread(int detached, Threadfunc *func, void *arg, size_t size);


/*
 * Create a Mutex.
 */
Mutex *mutex_create(void);


/*
 * Destroy a Mutex.
 */
void mutex_destroy(Mutex *mutex);


/* lock given mutex. PANICS if fails (non-initialized mutex or other
 *  coding error) */ 
void mutex_lock(Mutex *mutex);


/* lock given mutex. PANICS if fails (non-initialized mutex or other
 * coding error). Same as mutex_lock, except returns 0 if the lock was
 * made and -1 if not. */ 
int mutex_try_lock(Mutex *mutex);


/* unlock given mutex, PANICX if fails (so do not call for non-locked)
 */
void mutex_unlock(Mutex *mutex);


/* delete mutex data structures (in case if Linux, just check whether it is 
 * locked).
 */
void mutex_destroy(Mutex *mutex);

#endif


