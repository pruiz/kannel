/*
 * fdset.h - module for managing a large collection of file descriptors
 */

typedef struct FDSet FDSet;

/*
 * A function of this type will be called to indicate that a file descriptor
 * has shown activity.  The revents parameter is in the same format as
 * returned by poll().  The data pointer was supplied by the caller who
 * registered the fd with us in the first place.
 * NOTE: Beware of concurrency issues.  The callback function will run
 * in the fdset's private thread, not in the caller's thread.
 * This also means that if the callback does a lot of work it will slow
 * down the polling process.  This may be good or bad.
 */
typedef void fdset_callback_t(int fd, int revents, void *data);

/*
 * Create a new, empty file descriptor set and start its thread.
 */
FDSet *fdset_create(void);

/*
 * Destroy a file descriptor set.  Will emit a warning if any file
 * descriptors are still registered with it.
 */
void fdset_destroy(FDSet *set);

/* 
 * Register a file descriptor with this set, and listen for the specified
 * events (see fdset_listen() for details on that).  Record the callback
 * function which will be used to notify the caller about events.
 */
void fdset_register(FDSet *set, int fd, int events,
                    fdset_callback_t callback, void *data);

/*
 * Change the set of events to listen for for this file descriptor.
 * Events is in the same format as the events field in poll() -- in
 * practice, use POLLIN for "input available", POLLOUT for "ready to
 * accept more output", and POLLIN|POLLOUT for both.
 *
 * The mask field indicates which event flags can be affected.  For
 * example, if events is POLLIN and mask is POLLIN, then the POLLOUT
 * setting will not be changed by this.  If mask were POLLIN|POLLOUT,
 * then the POLLOUT setting would be turned off.
 *
 * The fd must first have been registered.  Locks are used to
 * guarantee that the callback function will not be called for
 * the old events after this function returns.
 */
void fdset_listen(FDSet *set, int fd, int mask, int events);

/*
 * Forget about this fd.  Locks are used to guarantee that the callback
 * function will not be called for this fd after this function returns.
 */
void fdset_unregister(FDSet *set, int fd);
