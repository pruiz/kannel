/*
 * gwmem.h - memory managment wrapper functions
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "gwassert.h"
#include "gwlib.h"
#include "gwmem.h"
#include "thread.h"

static void fill(void *p, size_t bytes, long pattern);

void *gw_malloc(size_t size)
{
    void *ptr;

    /* ANSI C89 says malloc(0) is implementation-defined.  Avoid it. */
    gw_assert(size > 0);

    ptr = malloc(size);
    if (ptr == NULL)
	panic(errno, "Memory allocation of %lu bytes failed", (unsigned long) size);

    fill(ptr, size, 0xbabecafe);
    return ptr;
}


void *gw_realloc(void *ptr, size_t size)
{
    void *new_ptr;

    gw_assert(size > 0);
    new_ptr = realloc(ptr, size);
    if (new_ptr == NULL)
	panic(errno, "Memory re-allocation of %lu bytes failed", (unsigned long) size);
    
    return new_ptr;
}


void  gw_free(void *ptr)
{
    free(ptr);
}


char *gw_strdup(const char *str)
{
    char *copy;
    
    gw_assert(str != NULL);
    copy = strdup(str);
    if (copy == NULL)
        panic(errno, "Memory allocation for string copy failed.");
    return copy;
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
    memcpy(p, &pattern, bytes);
}
