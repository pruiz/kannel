/*
 * gwthread-pthread.c - implementation of gwthread.h using POSIX threads.
 *
 * Richard Braakman <dark@wapit.com>
 */

#include <unistd.h>
#include <errno.h>
#include <sys/poll.h>
#include <pthread.h>

#include <gwlib/gwlib.h>

/* Maximum number of live threads we can support at once.  Increasing
 * this will increase the size of the threadtable.  Use powers of two
 * for efficiency. */
#define THREADTABLE_SIZE 1024

struct threadinfo {
	pthread_t self;
	const char *name;
	Threadfunc *func;
	long number;
	int wakefd_send;
	int wakefd_recv;
	pthread_cond_t exiting;
};

struct new_thread_args {
	Threadfunc *func;
	void *arg;
	struct threadinfo *ti;
};

/* The index is the external thread number modulo the table size; the
 * thread number allocation code makes sure that there are no collisions. */
static struct threadinfo *threadtable[THREADTABLE_SIZE];
#define THREAD(t) (threadtable[(t) % THREADTABLE_SIZE])

/* Number of threads currently in the thread table. */
static long active_threads = 0;

/* Number to use for the next thread created.  The actual number used
 * may be higher than this, in order to avoid collisions in the threadtable.
 * Specifically, (threadnumber % THREADTABLE_SIZE) must be unique for all
 * live threads. */
static long next_threadnumber;

/* Our key for accessing the (struct gwthread *) we stash in the
 * thread-specific-data area.  This is much more efficient than
 * accessing a global table, which we would have to lock. */
pthread_key_t tsd_key;

pthread_mutex_t threadtable_lock;

static void lock(void) {
	int ret;

	ret = pthread_mutex_lock(&threadtable_lock);
	if (ret != 0) {
		panic(ret, "gwthread-pthread: could not lock thread table");
	}
}

static void unlock(void) {
	int ret;

	ret = pthread_mutex_unlock(&threadtable_lock);
	if (ret != 0) {
		panic(ret, "gwthread-pthread: could not unlock thread table");
	}
}

static void flushpipe(int fd) {
	unsigned char buf[128];
	ssize_t bytes;

	do {
		bytes = read(fd, buf, sizeof(buf));
	} while (bytes > 0);
}

/* Allocate and fill a threadinfo structure for a new thread, and store
 * it in a free slot in the thread table.  The thread table must already
 * be locked by the caller.  Return the thread number chosen for this
 * thread.  The caller must make sure that there is room in the table. */
static long fill_threadinfo(pthread_t id, const char *name, Threadfunc *func,
struct threadinfo *ti) {
	int pipefds[2];
	long first_try;
	int ret;

	gw_assert(active_threads < THREADTABLE_SIZE);

	ti->self = id;
	ti->name = name;
	ti->func = func;

	if (pipe(pipefds) < 0) {
		panic(errno, "cannot allocate wakeup pipe for new thread");
	}
	ti->wakefd_send = pipefds[0];
	ti->wakefd_recv = pipefds[1];
	socket_set_blocking(ti->wakefd_send, 0);
	socket_set_blocking(ti->wakefd_recv, 0);

	ret = pthread_cond_init(&ti->exiting, NULL);
	if (ret != 0) {
		panic(ret, "cannot create condition variable for new thread");
	}

	/* Find a free table entry and claim it. */
	first_try = next_threadnumber;
	do {
		ti->number = next_threadnumber++;
		/* Check if we looped all the way around the thread table. */
		if (ti->number == first_try + THREADTABLE_SIZE) {
			panic(0, "Cannot have more than %d active threads",
				THREADTABLE_SIZE);
		}
	} while (THREAD(ti->number) != NULL);
	THREAD(ti->number) = ti;

	active_threads++;

	return ti->number;
}

/* Look up the threadinfo pointer for the current thread */
static struct threadinfo *getthreadinfo(void) {
	struct threadinfo *threadinfo;

	threadinfo = pthread_getspecific(tsd_key);
	if (threadinfo == NULL) {
		panic(0, "gwthread-pthread: pthread_getspecific failed");
	} else {
		gw_assert(pthread_equal(threadinfo->self, pthread_self()));
	}
	return threadinfo;
}

static void delete_threadinfo(void) {
	struct threadinfo *threadinfo;

	threadinfo = getthreadinfo();
	pthread_cond_broadcast(&threadinfo->exiting);
	pthread_cond_destroy(&threadinfo->exiting);
	close(threadinfo->wakefd_send);
	close(threadinfo->wakefd_recv);
	/* The main thread may still try call gwthread_self, when
	 * logging stuff.  So we need to set this to a safe value. */
#if 0 /* XXX for some reason, this makes shutdown hang --liw */
	pthread_setspecific(tsd_key, NULL);
#endif
	THREAD(threadinfo->number) = NULL;
	active_threads--;
	gw_free(threadinfo);
}

static void create_threadinfo_main(void) {
	struct threadinfo *ti;
	int ret;

	ti = gw_malloc(sizeof(*ti));
	fill_threadinfo(pthread_self(), "main", NULL, ti);
	ret = pthread_setspecific(tsd_key, ti);
	if (ret != 0) {
		panic(ret, "gwthread-pthread: pthread_setspecific failed");
	}
}
	
void gwthread_init(void) {
	int ret;
	int i;

	pthread_mutex_init(&threadtable_lock, NULL);

	ret = pthread_key_create(&tsd_key, NULL);
	if (ret != 0) {
		panic(ret, "gwthread-pthread: pthread_key_create failed");
	}

	for (i = 0; i < THREADTABLE_SIZE; i++) {
		threadtable[i] = NULL;
	}
	active_threads = 0;

	create_threadinfo_main();
}

void gwthread_shutdown(void) {
	int ret;
	int running;
	int i;

	/* Main thread must not have disappeared */
	gw_assert(threadtable[0] != NULL);
	lock();
	delete_threadinfo();
	
	running = 0;
	for (i = 0; i < THREADTABLE_SIZE; i++) {
		if (threadtable[i] != NULL) {
			debug("gwlib", 0, "Thread %ld (%s) still running", 
				threadtable[i]->number,
				threadtable[i]->name);
		}
		running++;
	}
	unlock();

	/* We can't do a full cleanup this way */
	if (running)
		return;

	ret = pthread_mutex_destroy(&threadtable_lock);
	if (ret != 0) {
		warning(ret, "cannot destroy threadtable lock");
	}

	ret = pthread_key_delete(tsd_key);
	if (ret != 0) {
		warning(ret, "cannot delete TSD key");
	}
}

static void *new_thread(void *arg) {
	int ret;
	struct new_thread_args *p = arg;

	/* Make sure we don't start until our parent has entered
	 * our thread info in the thread table. */
	lock();
	unlock();

	/* This has to be done here, because pthread_setspecific cannot
	 * be called by our parent on our behalf.  That's why the ti
	 * pointer is passed in the new_thread_args structure. */
	/* Synchronization is not a problem, because the only thread
	 * that relies on this call having been made is this one --
	 * no other thread can access our TSD anyway. */
	ret = pthread_setspecific(tsd_key, p->ti);
	if (ret != 0) {
		panic(ret, "gwthread-pthread: pthread_setspecific failed");
	}

	(p->func)(p->arg);

	lock();
	debug("gwlib.gwthread", 0, "Thread %ld (%s) terminates.",
		p->ti->number, p->ti->name);
	/* Must free p before signaling our exit, otherwise there is
	 * a race with gw_check_leaks at shutdown. */
	gw_free(p);
	delete_threadinfo();
	unlock();

	return NULL;
}

long gwthread_create_real(Threadfunc *func, const char *name, void *arg) {
	int ret;
	pthread_t id;
	struct new_thread_args *p;
	long number;

	/* We want to pass both these arguments to our wrapper function
	 * new_thread, but the pthread_create interface will only let
	 * us pass one pointer.  So we wrap them in a little struct. */
	p = gw_malloc(sizeof(*p));
	p->func = func;
	p->arg = arg;
	p->ti = gw_malloc(sizeof(*(p->ti)));

	/* Lock the thread table here, so that new_thread can block
	 * on that lock.  That way, the new thread won't start until
	 * we have entered it in the thread table. */
	lock();

	if (active_threads >= THREADTABLE_SIZE) {
		unlock();
		warning(0, "Too many threads, could not create new thread.");
		gw_free(p);
		return -1;
	}

	ret = pthread_create(&id, NULL, &new_thread, p);
	if (ret != 0) {
		unlock();
		error(ret, "Could not create new thread.");
		gw_free(p);
		return -1;
	}
	ret = pthread_detach(id);
	if (ret != 0) {
		warning(ret, "Could not detach new thread.");
	}

	number = fill_threadinfo(id, name, func, p->ti);
	unlock();
	
	debug("gwlib.gwthread", 0, "Started thread %ld (%s)", 
		number, name);

	return number;
}

void gwthread_join(long thread) {
	struct threadinfo *threadinfo;
	int ret;

	lock();
	threadinfo = THREAD(thread);
	if (threadinfo == NULL || threadinfo->number != thread) {
		unlock();
		return;
	}

	/* The wait immediately releases the lock, and reacquires it
	 * when the condition is satisfied.  So don't worry, we're not
	 * blocking while keeping the table locked. */
	ret = pthread_cond_wait(&threadinfo->exiting, &threadtable_lock);
	unlock();

	if (ret != 0) {
		warning(ret, "gwthread_join: error in pthread_cond_wait");
	}
}

void gwthread_join_all(Threadfunc *func) {
	long i;
	struct threadinfo *ti;
	int ret;

	lock();
	for (i = 0; i < THREADTABLE_SIZE; ++i) {
		ti = THREAD(i);
		if (ti == NULL || ti->func != func)
			continue;
		debug("gwlib.gwthread", 0, "Waiting for %ld (%s) to terminate",
			ti->number, ti->name);
		ret = pthread_cond_wait(&ti->exiting, &threadtable_lock);
		if (ret != 0) {
			warning(ret, "gwthread_join_all: error in "
					"pthread_cond_wait");
		}
	}
	unlock();
}

/* Return the thread id of this thread. */
long gwthread_self(void) {
	struct threadinfo *threadinfo;
	threadinfo = pthread_getspecific(tsd_key);
	if (threadinfo)
		return threadinfo->number;
	else
		return -1;
}

void gwthread_wakeup(long thread) {
	unsigned char c = 0;
	struct threadinfo *threadinfo;
	int fd;

	lock();

	threadinfo = THREAD(thread);
	if (threadinfo == NULL || threadinfo->number != thread) {
		unlock();
		return;
	}

	fd = threadinfo->wakefd_send;
	unlock();

	write(fd, &c, 1);
}

int gwthread_pollfd(int fd, int events, double timeout) {
	struct pollfd pollfd[2];
	struct threadinfo *threadinfo;
	int milliseconds;
	int ret;

	threadinfo = getthreadinfo();

	pollfd[0].fd = threadinfo->wakefd_recv;
	pollfd[0].events = POLLIN;

	pollfd[1].fd = fd;
	pollfd[1].events = events;

	milliseconds = timeout * 1000;

	ret = poll(pollfd, 2, milliseconds);
	if (ret < 0 && (errno == EINTR || errno == EAGAIN)) {
		return 0;
	}
	if (ret < 0) {
		error(errno, "gwthread_pollfd: error in poll");
		return -1;
	}
	if (pollfd[0].revents) {
		flushpipe(pollfd[0].fd);
	}
	return pollfd[1].revents;
}

void gwthread_sleep(double seconds) {
	struct pollfd pollfd;
	struct threadinfo *threadinfo;
	int milliseconds;
	int ret;

	threadinfo = getthreadinfo();

	pollfd.fd = threadinfo->wakefd_recv;
	pollfd.events = POLLIN;

	milliseconds = seconds * 1000;
	ret = poll(&pollfd, 1, milliseconds);
	if (ret < 0) {
		if (errno != EINTR && errno != EAGAIN) {
			warning(errno, "gwthread_sleep: error in poll");
		}
	}
	if (ret == 1) {
		flushpipe(pollfd.fd);
	}
}
