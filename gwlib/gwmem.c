#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "gwlib.h"
#include "gwmem.h"
#include "thread.h"

void *gw_malloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr == NULL)
	panic(errno, "Memory allocation of %lu bytes failed", (unsigned long) size);

    return ptr;
}


void *gw_realloc(void *ptr, size_t size)
{
    void *new_ptr;

    if (size == 0)
        panic(0, "gw_realloc called with size == 0. This is an error.");
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
    
    copy = strdup(str);
    if (copy == NULL)
        panic(errno, "Memory allocation for string copy failed.");
    return copy;
}
