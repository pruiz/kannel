/*
 * gwmem.h - memory managment wrapper functions
 *
 * Define GWMEM_CHECK to enable memory allocation checking.
 */

#ifndef GWMEM_TRACE
#define GWMEM_TRACE 0
#endif

#ifndef GWMEM_CHECK
#define GWMEM_CHECK 0
#endif

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "gwassert.h"
#include "gwlib.h"
#include "gwmem.h"
#include "thread.h"

static int initialized = 0;

#if GWMEM_CHECK
static pthread_mutex_t mutex;
static void lock(void);
static void unlock(void);
static void dump(void);
#endif

static void remember(void *p, size_t size);
static void forget(void *p);
static void check_leaks(void);

static void fill(void *p, size_t bytes, long pattern);

void gw_init_mem(void) {
#if GWMEM_CHECK
	pthread_mutex_init(&mutex, NULL);
#endif
	initialized = 1;
}

void *gw_malloc(size_t size) {
	void *ptr;
	
	gw_assert(initialized);
	
	/* ANSI C89 says malloc(0) is implementation-defined.  Avoid it. */
	gw_assert(size > 0);
	
	ptr = malloc(size);
	if (ptr == NULL)
		panic(errno, "Memory allocation of %lu bytes failed", 
			(unsigned long) size);
	
	fill(ptr, size, 0xbabecafe);
	remember(ptr, size);
	
	return ptr;
}


void *gw_realloc(void *ptr, size_t size) {
	void *new_ptr;
	
	gw_assert(initialized);
	
	gw_assert(size > 0);
	new_ptr = realloc(ptr, size);
	if (new_ptr == NULL)
		panic(errno, "Memory re-allocation of %lu bytes failed", 
			(unsigned long) size);
	
	if (new_ptr != ptr) {
		remember(new_ptr, size);
		forget(ptr);
	}
	
	return new_ptr;
}


void  gw_free(void *ptr) {
	gw_assert(initialized);
#if 0
	forget(ptr);
	free(ptr);
#endif
}


char *gw_strdup(const char *str) {
	char *copy;
	
	gw_assert(initialized);
	
	gw_assert(str != NULL);
	copy = gw_malloc(strlen(str) + 1);
	strcpy(copy, str);
	return copy;
}


void gw_check_leaks(void) {
    gw_assert(initialized);
    check_leaks();
}


/***********************************************************************
 * Local functions.
 */


#if GWMEM_CHECK

#define MAX_TAB_SIZE (1024*1024)
#define MAX_ALLOCATIONS	((MAX_TAB_SIZE)/sizeof(struct mem))

struct mem {
	void *p;
	size_t size;
};
static struct mem tab[MAX_ALLOCATIONS + 1];
static long num_allocations = 0;
#endif


/*
 * Comparison function for bsearch and qsort.
 */
#if GWMEM_CHECK
static int compare_mem(const void *a, const void *b) {
	const struct mem *aa, *bb;
	
	aa = a;
	bb = b;
	if (aa->p < bb->p)
		return -1;
	if (aa->p > bb->p)
		return 1;
	return 0;
}
#endif

/*
 * See if a particular memory area is in the table, if so, return its
 * index, otherwise -1.
 */
#if GWMEM_CHECK
static long unlocked_find(void *p) {
	struct mem mem;
	struct mem *ptr;
	
	mem.p = p;
	ptr = bsearch(&mem, tab, num_allocations, sizeof(struct mem),
	              compare_mem);
	if (ptr == NULL)
		return -1;
	gw_assert(ptr < tab + num_allocations);
	return ptr - tab;
}

#endif


/*
 * Add a memory area to the table.
 */
static void remember(void *p, size_t size) {
#if GWMEM_CHECK
	lock();
#if GWMEM_TRACE
	debug("gwlib.gwmem", 0, "rembember %p", p);
#endif
	if (num_allocations == MAX_ALLOCATIONS)
		panic(0, "Too many allocations that haven't been freed yet.");
	tab[num_allocations].p = p;
	tab[num_allocations].size = size;
	++num_allocations;
	qsort(tab, num_allocations, sizeof(struct mem), compare_mem);
	unlock();
#endif
}


/*
 * Forget about a memory area.
 */
static void forget(void *p) {
#if GWMEM_CHECK
	long i;

	if (p == NULL)
		return;

	lock();
	i = unlocked_find(p);
	if (i == -1) {
		error(0, "Trying to free a memory area that isn't allocated.");
		error(0, "Area is %p", p);
		dump();
		panic(0, "Can't deal with memory allocation problems. DIE!");
	}
#if 1
	gw_assert(p == tab[i].p);
	gw_assert(tab[i].size > 0);
	fill(p, tab[i].size, 0xdeadbeef);
#endif
	memmove(tab + i, 
	        tab + i + 1,
	        (num_allocations - i - 1) * sizeof(struct mem));
	--num_allocations;
	unlock();
#endif
}


static void check_leaks(void) {
#if GWMEM_CHECK
	long i, bytes;

	lock();
	bytes = 0;
	for (i = 0; i < num_allocations; ++i)
		bytes += tab[i].size;
	unlock();
	debug("gwlib.gwmem", 0, 
	      "Current allocations: %ld areas, %ld bytes", 
	      num_allocations, bytes);
#endif
}


#if GWMEM_CHECK
static void dump(void) {
	long i;

	for (i = 0; i < num_allocations; ++i)
		debug("gwlib.gwmem", 0, "area %ld at %p, %lu bytes",
			i, tab[i].p, (unsigned long) tab[i].size);
}
#endif


#if GWMEM_CHECK
static void lock(void) {
	if (pthread_mutex_lock(&mutex) != 0)
		panic(0, "pthread_mutex_lock failed in gwmem. Aaargh.");
}


static void unlock(void) {
	if (pthread_mutex_unlock(&mutex) != 0)
		panic(0, "pthread_mutex_unlock failed in gwmem. Aaargh.");
}
#endif


/*
 * Fill a memory area with a pattern.
 */
static void fill(void *p, size_t bytes, long pattern) 
{
#if GWMEM_CHECK
	while (bytes > sizeof(long)) {
		memcpy(p, &pattern, sizeof(long));
		p += sizeof(long);
		bytes -= sizeof(long);
	}
	if (bytes > 0)
		memcpy(p, &pattern, bytes);
#endif
}
