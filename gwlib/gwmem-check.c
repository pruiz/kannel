/*
 * gwmem-check.h - memory managment wrapper functions, check flavor
 *
 * Lars Wirzenius
 */


#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "gwlib.h"


static int initialized = 0;

static pthread_mutex_t mutex;
static void lock(void);
static void unlock(void);
static void dump(void);
static int is_allocated(void *p);

static void remember(void *p, size_t size, const char *filename, long lineno,
	const char *function);
static void forget(void *p, const char *filename, long lineno,
	const char *function);
static void check_leaks(void);

static void fill(void *p, size_t bytes, long pattern);

void gw_check_init_mem(void) {
	pthread_mutex_init(&mutex, NULL);
	initialized = 1;
}

void *gw_check_malloc(size_t size, const char *filename, long lineno,
const char *function) {
	void *ptr;
	
	gw_assert(initialized);
	
	/* ANSI C89 says malloc(0) is implementation-defined.  Avoid it. */
	gw_assert(size > 0);
	
	ptr = malloc(size);
	if (ptr == NULL)
		panic(errno, "Memory allocation of %lu bytes failed", 
			(unsigned long) size);
	
	fill(ptr, size, 0xbabecafe);
	remember(ptr, size, filename, lineno, function);
	
	return ptr;
}


void *gw_check_realloc(void *ptr, size_t size, 
const char *filename, long lineno, const char *function) {
	void *new_ptr;
	
	gw_assert(initialized);
	gw_assert(size > 0);

	new_ptr = realloc(ptr, size);
	if (new_ptr == NULL)
		panic(errno, "Memory re-allocation of %lu bytes failed", 
			(unsigned long) size);
	
	if (new_ptr != ptr) {
		remember(new_ptr, size, filename, lineno, function);
		forget(ptr, filename, lineno, function);
	}
	
	return new_ptr;
}


void gw_check_free(void *ptr, const char *filename, long lineno,
const char *function) {
	gw_assert(initialized);
	forget(ptr, filename, lineno, function);
	free(ptr);
}


char *gw_check_strdup(const char *str, const char *filename, long lineno,
const char *function) {
	char *copy;
	
	gw_assert(initialized);
	gw_assert(str != NULL);

	copy = gw_check_malloc(strlen(str) + 1, filename, lineno, function);
	strcpy(copy, str);
	return copy;
}


void gw_check_check_leaks(void) {
	gw_assert(initialized);
	check_leaks();
}


void gw_check_assert_allocated_real(void *ptr, const char *file, long line, 
const char *function) {
	gw_assert_place(is_allocated(ptr), file, line, function);
}


/***********************************************************************
 * Local functions.
 */


#define MAX_TAB_SIZE (1024*1024L)
#define MAX_ALLOCATIONS	((long) ((MAX_TAB_SIZE)/sizeof(struct mem)))

struct mem {
	void *p;
	size_t size;
	const char *allocated_filename;
	long allocated_lineno;
	const char *allocated_function;
};
static struct mem tab[MAX_ALLOCATIONS + 1];
static long num_allocations = 0;


/*
 * Comparison function for bsearch and qsort.
 */
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

/*
 * See if a particular memory area is in the table, if so, return its
 * index, otherwise -1.
 */
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


/*
 * Is `p' a currently allocated memory area?
 */
static int is_allocated(void *p) {
	int ret;
	
	lock();
	ret = (unlocked_find(p) != -1);
	unlock();
	return ret;
}


/*
 * Add a memory area to the table.
 */
static void remember(void *p, size_t size, const char *filename, long lineno,
const char *function) {
	lock();
	if (num_allocations >= MAX_ALLOCATIONS)
		panic(0, "Too many allocations at the same time.");
	tab[num_allocations].p = p;
	tab[num_allocations].size = size;
	tab[num_allocations].allocated_filename = filename;
	tab[num_allocations].allocated_lineno = lineno;
	tab[num_allocations].allocated_function = function;
	++num_allocations;
	qsort(tab, num_allocations, sizeof(struct mem), compare_mem);
	unlock();
}


/*
 * Forget about a memory area.
 */
static void forget(void *p, const char *filename, long lineno,
const char *function) {
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
	gw_assert(p == tab[i].p);
	gw_assert(tab[i].size > 0);
	memmove(tab + i, 
	        tab + i + 1,
	        (num_allocations - i - 1) * sizeof(struct mem));
	--num_allocations;
	unlock();
}


static void check_leaks(void) {
	long i, bytes;

	lock();
	bytes = 0;
	for (i = 0; i < num_allocations; ++i)
		bytes += tab[i].size;
	debug("gwlib.gwmem", 0, 
	      "Current allocations: %ld areas, %ld bytes", 
	      num_allocations, bytes);
	dump();
	unlock();
}


static void dump(void) {
	long i;

	for (i = 0; i < num_allocations; ++i) {
		debug("gwlib.gwmem", 0, "area %ld at %p, %lu bytes, "
			"allocated at %s:%ld:%s",
			i, tab[i].p, (unsigned long) tab[i].size,
			tab[i].allocated_filename, tab[i].allocated_lineno,
			tab[i].allocated_function);
	}
}


static void lock(void) {
	if (pthread_mutex_lock(&mutex) != 0)
		panic(0, "pthread_mutex_lock failed in gwmem. Aaargh.");
}


static void unlock(void) {
	if (pthread_mutex_unlock(&mutex) != 0)
		panic(0, "pthread_mutex_unlock failed in gwmem. Aaargh.");
}


/*
 * Fill a memory area with a pattern.
 */
static void fill(void *p, size_t bytes, long pattern) 
{
	while (bytes > sizeof(long)) {
		memcpy(p, &pattern, sizeof(long));
		p += sizeof(long);
		bytes -= sizeof(long);
	}
	if (bytes > 0)
		memcpy(p, &pattern, bytes);
}
