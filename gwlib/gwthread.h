/*
 * gwthread.h - threads wrapper with interruptible sleep and poll operations.
 *
 * This is a (partial) encapsulation of threads.  It provides functions
 * to create new threads and to manipulate threads.  It will eventually
 * be extended to encapsulate all pthread functions we use, so that
 * non-POSIX platforms can plug in their own versions.
 *
 * Richard Braakman
 */

#ifndef GWTHREAD_H
#define GWTHREAD_H

/* gwthread_self() must return this value for the main thread. */
#define MAIN_THREAD_ID 0

typedef void gwthread_func_t(void *arg);

/* Called by the gwlib init code */
void gwthread_init(void);
void gwthread_shutdown(void);

/* Start a new thread, running func(arg).  Return the new thread ID
 * on success, or -1 on failure.  Thread IDs are unique during the lifetime
 * of the entire process, unless you use more than LONG_MAX threads. */
long gwthread_create_real(gwthread_func_t *func, const char *funcname,
			  void *arg);
#define gwthread_create(func, arg) \
	(gwthread_create_real(func, __FILE__ ":" #func, arg))

/* Wait for the other thread to terminate.  Return immediately if it
 * has already terminated. */
void gwthread_join(long thread);

/* Wait for all threads whose main function is `func' to terminate.
 * Return immediately if none are running. */
void gwthread_join_every(gwthread_func_t *func);

/* Wait for all threads to terminate.  Return immediately if none
 * are running.  This function is not intended to be called if new
 * threads are still being created, and it may not notice such threads. */
void gwthread_join_all(void);

/* Return the thread id of this thread.  Note that it may be called for
 * the main thread even before the gwthread library has been initialized
 * and after it had been shut down. */
long gwthread_self(void);

/* If the other thread is currently in gwthread_pollfd or gwthread_sleep,
 * make it return immediately.  Otherwise, make it return immediately, the
 * next time it calls one of those functions. */
void gwthread_wakeup(long thread);

/* Wake up all threads */
void gwthread_wakeup_all(void);

/* Wrapper around the poll() system call, for one file descriptor.
 * "events" is a set of the flags defined in <sys/poll.h>, usually
 * POLLIN, POLLOUT, or (POLLIN|POLLOUT).  Return when one of the
 * events is true, or when another thread calls gwthread_wakeup on us, or
 * when the timeout expires.  The timeout is specified in seconds,
 * and a negative value means do not time out.  Return the revents
 * structure filled in by poll() for this fd.  Return -1 if something
 * went wrong. */
int gwthread_pollfd(int fd, int events, double timeout);

/* Sleep until "seconds" seconds have elapsed, or until another thread
 * calls gwthread_wakeup on us.  Fractional seconds are allowed. */
void gwthread_sleep(double seconds);

#endif
